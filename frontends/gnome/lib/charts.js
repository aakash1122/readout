// Cairo drawings in the Stats style (docs/visual-spec.md sections 1.5 and 3).
//
// Every drawing is a Canvas (St.DrawingArea) that repaints only when its data changed and
// it is mapped. Sizes are proportional to the canvas or to Theme.unit (one Stats point), so
// they follow the text scale.

import Cairo from 'cairo';
import GObject from 'gi://GObject';
import Pango from 'gi://Pango';
import PangoCairo from 'gi://PangoCairo';
import St from 'gi://St';

import {Theme, withAlpha} from './theme.js';

/**
 * A St.DrawingArea that draws through a callback with a Theme.
 */
export const Canvas = GObject.registerClass(
class ReadoutCanvas extends St.DrawingArea {
    /**
     * @param {object} params - St.DrawingArea properties, plus "well": null, "normal" or "drive"
     * @param {Function} draw - draw(cr, width, height, theme)
     */
    _init(params = {}, draw = null) {
        const {style_class: styleClass = '', well = null, ...rest} = params;
        super._init({style_class: `readout-canvas ${styleClass}`.trim(), ...rest});
        this._draw = draw;
        this._well = well;
        this._theme = null;
        this._dirty = true;
        this.connect('notify::mapped', () => {
            if (this.mapped && this._dirty)
                this.queue_repaint();
        });
        this.connect('notify::resource-scale', () => {
            this._theme = null;
            this.invalidate();
        });
    }

    /** Marks the drawing as outdated; it repaints now if visible, else when it is shown. */
    invalidate() {
        this._dirty = true;
        if (this.mapped)
            this.queue_repaint();
    }

    vfunc_style_changed() {
        super.vfunc_style_changed();
        this._theme = null;
        this.invalidate();
    }

    vfunc_repaint() {
        this._dirty = false;
        const cr = this.get_context();
        const [width, height] = this.get_surface_size();
        try {
            this._theme ??= new Theme(this);
            if (width > 0 && height > 0) {
                if (this._well)
                    drawWell(cr, this._theme, width, height, this._well);
                this._draw?.(cr, width, height, this._theme);
            }
        } catch (error) {
            logError(error, 'Readout: drawing failed');
        } finally {
            cr.$dispose();
        }
    }
});

/**
 * Chart well: lightGray at 10%, radius 3 (the drive chart is white in light mode).
 *
 * @param {Cairo.Context} cr - context
 * @param {Theme} theme - theme
 * @param {number} width - width
 * @param {number} height - height
 * @param {string} kind - "normal" or "drive"
 */
function drawWell(cr, theme, width, height, kind) {
    roundedRect(cr, 0, 0, width, height, 3 * theme.unit);
    setColor(cr, kind === 'drive' && !theme.dark ? [1, 1, 1, 1] : theme.color('lightGray', 0.1));
    cr.fill();
}

/**
 * @param {Cairo.Context} cr - context
 * @param {number[]} color - [r, g, b, a]
 */
export function setColor(cr, color) {
    cr.setSourceRGBA(color[0], color[1], color[2], color[3]);
}

/**
 * Adds a rounded rectangle path.
 *
 * @param {Cairo.Context} cr - context
 * @param {number} x - left
 * @param {number} y - top
 * @param {number} width - width
 * @param {number} height - height
 * @param {number} radius - corner radius
 */
export function roundedRect(cr, x, y, width, height, radius) {
    const r = Math.max(0, Math.min(radius, width / 2, height / 2));
    cr.newSubPath();
    cr.arc(x + width - r, y + r, r, -Math.PI / 2, 0);
    cr.arc(x + width - r, y + height - r, r, 0, Math.PI / 2);
    cr.arc(x + r, y + height - r, r, Math.PI / 2, Math.PI);
    cr.arc(x + r, y + r, r, Math.PI, Math.PI * 1.5);
    cr.closePath();
}

/**
 * Draws text with Pango.
 *
 * @param {Cairo.Context} cr - context
 * @param {Theme} theme - theme (for the font family)
 * @param {string} text - text
 * @param {object} options - {x, y, width, height, size (px), weight, color, align, tabular}
 * @returns {number} text width in pixels
 */
export function drawText(cr, theme, text, {
    x = 0, y = 0, width = 0, height = 0, size = 10, weight = Pango.Weight.NORMAL,
    color = theme.text, align = 'left', tabular = true,
} = {}) {
    // The actor's Pango context carries the shell's font options (grayscale antialiasing);
    // a plain Cairo layout would pick subpixel rendering, which fringes on a transparent canvas.
    const layout = theme.pangoContext ? Pango.Layout.new(theme.pangoContext) : PangoCairo.create_layout(cr);
    const font = Pango.FontDescription.from_string(theme.fontFamily);
    font.set_absolute_size(size * Pango.SCALE);
    font.set_weight(weight);
    layout.set_font_description(font);
    if (tabular) {
        const attributes = new Pango.AttrList();
        attributes.insert(Pango.attr_font_features_new('tnum=1'));
        layout.set_attributes(attributes);
    }
    layout.set_text(text, -1);
    const [, logical] = layout.get_pixel_extents();
    let left = x;
    if (align === 'center')
        left = x + (width - logical.width) / 2;
    else if (align === 'right')
        left = x + width - logical.width;
    const top = y + (height - logical.height) / 2;
    setColor(cr, color);
    cr.moveTo(left, top);
    PangoCairo.show_layout(cr, layout);
    return logical.width;
}

/**
 * Draws one Stats line series: a hairline polyline with a gradient fill down (or up) to the baseline.
 *
 * @param {Cairo.Context} cr - context
 * @param {Theme} theme - theme
 * @param {object} options - {values, capacity, x, y, width, height, maximum, color, flip, flatFill, fillAlphas}
 */
export function drawSeries(cr, theme, {
    values, capacity, x, y, width, height, maximum, color, flip = false, flatFill = false, fillAlphas = [0.25, 0.5],
}) {
    if (values.length === 0 || height <= 0)
        return;
    const lineWidth = theme.hairline;
    const usable = height - lineWidth;
    const step = width / Math.max(1, capacity - 1);
    const startX = x + width - (values.length - 1) * step;
    const scale = maximum > 0 ? maximum : 1;
    // AppKit y grows upward; "flip" draws downward from the top edge (mirrored halves).
    const yOf = value => {
        const fraction = Math.max(0, Math.min(1, (Number.isFinite(value) ? value : 0) / scale));
        return flip ? y + lineWidth / 2 + fraction * usable : y + height - lineWidth / 2 - fraction * usable;
    };
    const baseline = flip ? y : y + height;
    const points = values.map((value, index) => [startX + index * step, yOf(value)]);

    // Fill first so the line stays crisp on top.
    cr.moveTo(points[0][0], baseline);
    for (const [px, py] of points)
        cr.lineTo(px, py);
    cr.lineTo(points[points.length - 1][0], baseline);
    cr.closePath();
    if (flatFill) {
        setColor(cr, withAlpha(color, 0.5));
    } else {
        let peak = baseline;
        for (const [, py] of points)
            peak = flip ? Math.max(peak, py) : Math.min(peak, py);
        const gradient = new Cairo.LinearGradient(0, baseline, 0, peak === baseline ? baseline - 1 : peak);
        gradient.addColorStopRGBA(0, color[0], color[1], color[2], color[3] * fillAlphas[0]);
        gradient.addColorStopRGBA(1, color[0], color[1], color[2], color[3] * fillAlphas[1]);
        cr.setSource(gradient);
    }
    cr.fill();

    cr.setLineWidth(lineWidth);
    cr.setLineJoin(Cairo.LineJoin.ROUND);
    cr.moveTo(points[0][0], points[0][1]);
    for (let index = 1; index < points.length; index++)
        cr.lineTo(points[index][0], points[index][1]);
    setColor(cr, color);
    cr.stroke();
}

/**
 * Ring or filled pie: segments clockwise from 12 o'clock, remainder in the inactive color.
 *
 * @param {Cairo.Context} cr - context
 * @param {Theme} theme - theme
 * @param {object} options - {segments: [{value 0..1, color}], cx, cy, size, stroke, filled, inactive}
 */
export function drawRing(cr, theme, {segments, cx, cy, size, stroke, filled = false, inactive = theme.inactive}) {
    const radius = filled ? size / 2 : (size - stroke) / 2;
    let angle = -Math.PI / 2;
    let total = 0;
    const draw = (fraction, color) => {
        if (fraction <= 0)
            return;
        const end = angle + fraction * 2 * Math.PI;
        if (filled) {
            cr.moveTo(cx, cy);
            cr.arc(cx, cy, radius, angle, end);
            cr.closePath();
            setColor(cr, color);
            cr.fill();
        } else {
            cr.setLineWidth(stroke);
            cr.setLineCap(Cairo.LineCap.BUTT);
            cr.arc(cx, cy, radius, angle, end);
            setColor(cr, color);
            cr.stroke();
        }
        angle = end;
    };
    for (const segment of segments) {
        const fraction = Math.max(0, Math.min(1 - total, segment.value || 0));
        draw(fraction, segment.color);
        total += fraction;
    }
    draw(1 - total, inactive);
}

/**
 * 270 degree gauge with round caps, open at the bottom.
 *
 * @param {Cairo.Context} cr - context
 * @param {Theme} theme - theme
 * @param {object} options - {fraction, cx, cy, size, stroke, color}
 */
export function drawGauge(cr, theme, {fraction, cx, cy, size, stroke, color}) {
    const radius = (size - stroke) / 2;
    const start = Math.PI * 0.75;
    const sweep = Math.PI * 1.5;
    cr.setLineWidth(stroke);
    cr.setLineCap(Cairo.LineCap.ROUND);
    cr.arc(cx, cy, radius, start, start + sweep);
    setColor(cr, theme.inactive);
    cr.stroke();
    const value = Math.max(0, Math.min(1, fraction || 0));
    if (value > 0) {
        cr.arc(cx, cy, radius, start, start + sweep * value);
        setColor(cr, color);
        cr.stroke();
    }
}

/**
 * Upper half disk (Stats TachometerGraphView): segments left to right.
 *
 * @param {Cairo.Context} cr - context
 * @param {Theme} theme - theme
 * @param {object} options - {segments, x, y, size, inactive}
 */
export function drawTachometer(cr, theme, {segments, x, y, size, inactive = theme.inactive}) {
    const radius = size / 2;
    const cx = x + radius;
    // The half disk sits in the lower part of the square (Stats shifts it down 4 of 18).
    const cy = y + radius + size * 4 / 18;
    let angle = Math.PI;
    let total = 0;
    const draw = (fraction, color) => {
        if (fraction <= 0)
            return;
        const end = angle + fraction * Math.PI;
        cr.moveTo(cx, cy);
        cr.arc(cx, cy, radius, angle, end);
        cr.closePath();
        setColor(cr, color);
        cr.fill();
        angle = end;
    };
    for (const segment of segments) {
        const fraction = Math.max(0, Math.min(1 - total, segment.value || 0));
        draw(fraction, segment.color);
        total += fraction;
    }
    draw(1 - total, inactive);
}

/**
 * Stats "no data" mark: a circle with a diagonal slash.
 *
 * @param {Cairo.Context} cr - context
 * @param {Theme} theme - theme
 * @param {number} width - area width
 * @param {number} height - area height
 * @param {number} size - circle size
 */
export function drawEmpty(cr, theme, width, height, size) {
    const radius = size / 2;
    const cx = width / 2;
    const cy = height / 2;
    cr.setLineWidth(theme.hairline);
    cr.arc(cx, cy, radius, 0, 2 * Math.PI);
    cr.moveTo(cx - radius * 0.7, cy + radius * 0.7);
    cr.lineTo(cx + radius * 0.7, cy - radius * 0.7);
    setColor(cr, theme.text);
    cr.stroke();
}

/**
 * Fixed-size history of numbers.
 */
export class Series {
    /** @param {number} capacity - points kept */
    constructor(capacity) {
        this.capacity = capacity;
        this.values = [];
    }

    /** @param {number} value - new point */
    push(value) {
        this.values.push(Number.isFinite(value) ? value : 0);
        if (this.values.length > this.capacity)
            this.values.splice(0, this.values.length - this.capacity);
    }

    /** @param {Array} points - [[timestamp, value], ...] from GetHistory() */
    setPoints(points) {
        this.values = (points ?? []).slice(-this.capacity).map(point => point[1]);
    }

    /** @param {number[]} values - plain values */
    setValues(values) {
        this.values = values.slice(-this.capacity);
    }

    get maximum() {
        let maximum = 0;
        for (const value of this.values)
            maximum = Math.max(maximum, value);
        return maximum;
    }
}

/**
 * Popup line chart (Stats LineChartView) in a well.
 */
export class LineChart {
    /**
     * @param {object} options - {capacity, maximum (null = auto), color(theme) -> color, styleClass, well}
     */
    constructor({capacity = 180, maximum = 100, color = theme => theme.accent, styleClass = '', well = 'normal'} = {}) {
        this.series = new Series(capacity);
        this._maximum = maximum;
        this._color = color;
        this.actor = new Canvas({style_class: styleClass, x_expand: true, well},
            (cr, width, height, theme) => this._draw(cr, width, height, theme));
    }

    push(value) {
        this.series.push(value);
        this.actor.invalidate();
    }

    setPoints(points) {
        this.series.setPoints(points);
        this.actor.invalidate();
    }

    setValues(values) {
        this.series.setValues(values);
        this.actor.invalidate();
    }

    _draw(cr, width, height, theme) {
        const maximum = this._maximum ?? (this.series.maximum > 0 ? this.series.maximum : 1);
        const inset = theme.unit;
        drawSeries(cr, theme, {
            values: this.series.values, capacity: this.series.capacity,
            x: inset, y: 0, width: width - 2 * inset, height,
            maximum, color: this._color(theme),
        });
    }
}

/**
 * Mirrored chart (Stats NetworkChartView): the first series grows up from the center line,
 * the second grows down. Each half scales to its own maximum.
 */
export class MirroredChart {
    /**
     * @param {object} options - {capacity, colors: [top, bottom] palette names, minMax, styleClass, format, well}
     */
    constructor({
        capacity = 180, colors = ['red', 'blue'], minMax = false, format = null, styleClass = '', well = 'normal',
    } = {}) {
        this.top = new Series(capacity);
        this.bottom = new Series(capacity);
        this._colors = colors;
        this._minMax = minMax;
        this._format = format;
        this.actor = new Canvas({style_class: styleClass, x_expand: true, well},
            (cr, width, height, theme) => this._draw(cr, width, height, theme));
    }

    push(top, bottom) {
        this.top.push(top);
        this.bottom.push(bottom);
        this.actor.invalidate();
    }

    setPoints(topPoints, bottomPoints) {
        this.top.setPoints(topPoints);
        this.bottom.setPoints(bottomPoints);
        this.actor.invalidate();
    }

    _draw(cr, width, height, theme) {
        const half = height / 2;
        const inset = theme.unit;
        const topMax = this.top.maximum;
        const bottomMax = this.bottom.maximum;
        const common = {capacity: this.top.capacity, x: inset, width: width - 2 * inset};
        drawSeries(cr, theme, {
            ...common, values: this.top.values, y: 0, height: half,
            maximum: topMax || 1, color: theme.color(this._colors[0]),
        });
        drawSeries(cr, theme, {
            ...common, values: this.bottom.values, y: half, height: half,
            maximum: bottomMax || 1, color: theme.color(this._colors[1]), flip: true,
        });
        if (this._minMax && this._format) {
            const size = 9 * theme.unit;
            const options = {x: 2 * theme.unit, width: width / 2, height: size * 1.4, size, weight: Pango.Weight.LIGHT};
            drawText(cr, theme, this._format(topMax), {...options, y: 0});
            drawText(cr, theme, this._format(bottomMax), {...options, y: height - size * 1.4});
        }
    }
}

/**
 * Ring with segments (Stats PieChartView), centered in its canvas.
 */
export class Ring {
    /** @param {object} options - {styleClass} */
    constructor({styleClass = 'readout-ring'} = {}) {
        this._segments = [];
        this._key = '';
        this.actor = new Canvas({style_class: styleClass}, (cr, width, height, theme) => {
            const size = Math.min(width, height);
            drawRing(cr, theme, {
                segments: this._segments.map(segment => ({value: segment.value, color: theme.color(segment.color)})),
                cx: width / 2, cy: height / 2, size, stroke: size * 7 / 70,
            });
        });
    }

    /** @param {Array} segments - [{value 0..1, color palette name}] */
    setSegments(segments) {
        const key = segments.map(segment => `${segment.color}:${Math.round((segment.value || 0) * 400)}`).join(',');
        if (key === this._key)
            return;
        this._key = key;
        this._segments = segments;
        this.actor.invalidate();
    }
}

/**
 * 270 degree gauge (Stats HalfCircleGraphView).
 */
export class Gauge {
    /** @param {object} options - {styleClass, color palette name or "accent"} */
    constructor({styleClass = 'readout-gauge', color = 'blue'} = {}) {
        this._fraction = 0;
        this._color = color;
        this.actor = new Canvas({style_class: styleClass}, (cr, width, height, theme) => {
            const size = Math.min(width, height);
            drawGauge(cr, theme, {
                fraction: this._fraction, cx: width / 2, cy: height / 2, size,
                stroke: size * 7 / 50, color: theme.color(this._color),
            });
        });
    }

    /** @param {number} fraction - 0..1 */
    setFraction(fraction) {
        const value = Number.isFinite(fraction) ? Math.max(0, Math.min(1, fraction)) : 0;
        if (Math.abs(value - this._fraction) < 0.002)
            return;
        this._fraction = value;
        this.actor.invalidate();
    }
}

/**
 * RAM pressure gauge: a green / yellow / red arc with a needle.
 */
export class PressureGauge {
    constructor() {
        this._level = 'normal';
        this.actor = new Canvas({style_class: 'readout-pressure-gauge'}, (cr, width, height, theme) => {
            const size = Math.min(width, height);
            const stroke = size * 7 / 50;
            const radius = (size - stroke) / 2;
            const cx = width / 2;
            const cy = height / 2;
            const start = Math.PI * 0.75;
            const third = Math.PI * 0.5;
            cr.setLineWidth(stroke);
            ['green', 'yellow', 'red'].forEach((name, index) => {
                // Butt caps between thirds, round at both ends.
                cr.setLineCap(Cairo.LineCap.BUTT);
                cr.arc(cx, cy, radius, start + index * third, start + (index + 1) * third);
                setColor(cr, theme.color(name));
                cr.stroke();
            });
            for (const [angle, name] of [[start, 'green'], [start + 3 * third, 'red']]) {
                cr.arc(cx + Math.cos(angle) * radius, cy + Math.sin(angle) * radius, stroke / 2, 0, 2 * Math.PI);
                setColor(cr, theme.color(name));
                cr.fill();
            }
            // Needle: normal points into the green third, warning up, critical into the red third.
            const angle = {normal: Math.PI * 1.05, warning: Math.PI * 1.5, critical: Math.PI * 1.95}[this._level] ??
                Math.PI * 1.05;
            const length = radius - stroke;
            const base = 2 * theme.unit;
            const tipX = cx + Math.cos(angle) * length;
            const tipY = cy + Math.sin(angle) * length;
            const normalX = Math.cos(angle + Math.PI / 2) * base;
            const normalY = Math.sin(angle + Math.PI / 2) * base;
            cr.moveTo(cx + normalX, cy + normalY);
            cr.lineTo(tipX, tipY);
            cr.lineTo(cx - normalX, cy - normalY);
            cr.closePath();
            setColor(cr, theme.color('blue'));
            cr.fill();
            cr.arc(cx, cy, 2 * theme.unit, 0, 2 * Math.PI);
            cr.fill();
            const number = {normal: '1', warning: '2', critical: '4'}[this._level] ?? '1';
            drawText(cr, theme, number, {
                x: cx - size / 4, y: cy + 3 * theme.unit, width: size / 2, height: 11 * theme.unit,
                size: 9 * theme.unit, align: 'center',
            });
        });
    }

    /** @param {string} level - normal, warning, critical */
    setLevel(level) {
        if (level === this._level)
            return;
        this._level = level;
        this.actor.invalidate();
    }
}

/**
 * Per-core LED meters (Stats BarChartView).
 */
export class CoreBars {
    constructor() {
        this._values = [];
        this._colors = [];
        this.actor = new Canvas({style_class: 'readout-core-bars', x_expand: true, well: 'normal'},
            (cr, width, height, theme) => this._draw(cr, width, height, theme));
    }

    /**
     * @param {number[]} values - percent per core
     * @param {string[]} colors - palette name per core
     */
    setValues(values, colors) {
        this._values = values;
        this._colors = colors;
        this.actor.invalidate();
    }

    _draw(cr, width, height, theme) {
        const count = this._values.length;
        if (count === 0)
            return;
        const inset = 2 * theme.unit;
        const gap = 2 * theme.unit;
        const innerWidth = width - 2 * inset;
        const innerHeight = height - 2 * inset;
        const columnWidth = (innerWidth - gap * (count - 1)) / count;
        const blocks = 16;
        const blockGap = theme.unit;
        const blockHeight = (innerHeight - 2 * theme.unit - blockGap) / blocks - blockGap;
        const led = innerHeight >= 30 * theme.unit && blockHeight > 0.5;
        this._values.forEach((value, index) => {
            const x = inset + index * (columnWidth + gap);
            roundedRect(cr, x, inset, columnWidth, innerHeight, 3 * theme.unit);
            setColor(cr, theme.color('underPage', 0.5));
            cr.fill();
            const fraction = Math.max(0, Math.min(1, (value || 0) / 100));
            const color = theme.color(this._colors[index] ?? 'blue');
            if (!led) {
                const fill = innerHeight * fraction;
                roundedRect(cr, x, inset + innerHeight - fill, columnWidth, fill, 3 * theme.unit);
                setColor(cr, color);
                cr.fill();
                return;
            }
            const active = Math.round(fraction * blocks);
            const blockWidth = Math.max(1, columnWidth - 4 * theme.unit);
            for (let block = 0; block < blocks; block++) {
                const y = inset + innerHeight - theme.unit - (block + 1) * (blockHeight + blockGap);
                roundedRect(cr, x + (columnWidth - blockWidth) / 2, y, blockWidth, blockHeight, theme.unit);
                setColor(cr, block < active ? color : theme.color('controlBackground', 0.4));
                cr.fill();
            }
        });
    }
}

/**
 * Connectivity grid (Stats GridChartView): newest cell bottom-right.
 */
export class GridChart {
    /** @param {object} options - {columns, rows} */
    constructor({columns = 30, rows = 3} = {}) {
        this._columns = columns;
        this._rows = rows;
        this._values = [];
        this.actor = new Canvas({style_class: 'readout-grid-chart', x_expand: true}, (cr, width, height, theme) => {
            const gap = 2 * theme.unit;
            const cellWidth = (width - gap * (this._columns - 1)) / this._columns;
            const cellHeight = (height - gap * (this._rows - 1)) / this._rows;
            const cells = this._columns * this._rows;
            const offset = cells - this._values.length;
            for (let index = 0; index < cells; index++) {
                const column = index % this._columns;
                const row = Math.floor(index / this._columns);
                const left = column * (cellWidth + gap);
                roundedRect(cr, left, row * (cellHeight + gap), cellWidth, cellHeight, theme.unit);
                const value = index >= offset ? this._values[index - offset] : null;
                if (value === null)
                    setColor(cr, theme.color('underPage', 0.4));
                else
                    setColor(cr, theme.color(value ? 'green' : 'red'));
                cr.fill();
            }
        });
    }

    /** @param {boolean[]} values - oldest first */
    setValues(values) {
        this._values = values.slice(-this._columns * this._rows);
        this.actor.invalidate();
    }
}

/**
 * Horizontal capacity bar (Stats Disk BarView).
 */
export class CapacityBar {
    constructor() {
        this._fraction = 0;
        this.actor = new Canvas({style_class: 'readout-capacity-bar', x_expand: true}, (cr, width, height, theme) => {
            const inset = theme.unit;
            const radius = 3 * theme.unit;
            const border = Math.max(theme.hairline, 0.25 * theme.unit);
            roundedRect(cr, inset + border / 2, border / 2, width - 2 * inset - border, height - border, radius);
            setColor(cr, theme.dark ? theme.color('lightGray', 0.1) : [1, 1, 1, 1]);
            cr.fillPreserve();
            cr.setLineWidth(border);
            setColor(cr, theme.label);
            cr.stroke();
            if (this._fraction > 0) {
                roundedRect(cr, inset + border, border, (width - 2 * inset - 2 * border) * this._fraction,
                    height - 2 * border, radius);
                setColor(cr, theme.accent);
                cr.fill();
            }
        });
    }

    /** @param {number} fraction - 0..1 */
    setFraction(fraction) {
        const value = Number.isFinite(fraction) ? Math.max(0, Math.min(1, fraction)) : 0;
        if (Math.abs(value - this._fraction) < 0.001)
            return;
        this._fraction = value;
        this.actor.invalidate();
    }
}

/**
 * Small rounded square or dot in a series color (legend squares, activity dots).
 */
export class Swatch {
    /**
     * @param {string} color - palette name
     * @param {object} options - {alpha, round, styleClass}
     */
    constructor(color, {alpha = 1, round = false, styleClass = 'readout-swatch'} = {}) {
        this._color = color;
        this._alpha = alpha;
        this.actor = new Canvas({style_class: styleClass, y_align: 2 /* Clutter.ActorAlign.CENTER */},
            (cr, width, height, theme) => {
                const size = Math.min(width, height);
                if (round)
                    cr.arc(width / 2, height / 2, size / 2, 0, 2 * Math.PI);
                else
                    roundedRect(cr, (width - size) / 2, (height - size) / 2, size, size, size * 2 / 12);
                setColor(cr, theme.color(this._color, this._alpha));
                cr.fill();
            });
    }

    /**
     * @param {string} color - palette name
     * @param {number} [alpha] - 0..1
     */
    setColor(color, alpha = 1) {
        if (color === this._color && alpha === this._alpha)
            return;
        this._color = color;
        this._alpha = alpha;
        this.actor.invalidate();
    }
}

/**
 * Large battery outline for the Battery dashboard.
 */
export class BatteryIcon {
    constructor() {
        this._level = NaN;
        this.actor = new Canvas({style_class: 'readout-battery-icon'}, (cr, width, height, theme) => {
            const {unit} = theme;
            const bodyWidth = Math.min(width, 120 * unit);
            const bodyHeight = Math.min(height, 50 * unit);
            const x = (width - bodyWidth) / 2;
            const y = (height - bodyHeight) / 2;
            const outlineWidth = bodyWidth - 8 * unit;
            cr.setLineWidth(unit);
            roundedRect(cr, x + unit, y + unit, outlineWidth, bodyHeight - 2 * unit, 3 * unit);
            setColor(cr, theme.text);
            cr.stroke();
            // Terminal nub, detached from the outline by a small gap.
            const nubX = x + unit + outlineWidth + 2.5 * unit;
            cr.arc(nubX, y + bodyHeight / 2, 4 * unit, -Math.PI / 2, Math.PI / 2);
            cr.closePath();
            cr.fill();
            if (Number.isFinite(this._level) && this._level > 0) {
                const fillWidth = (outlineWidth - 4 * unit) * Math.min(1, this._level / 100);
                roundedRect(cr, x + 3 * unit, y + 3 * unit, fillWidth, bodyHeight - 6 * unit, 2 * unit);
                setColor(cr, this._level < 20 ? theme.color('red') : theme.text);
                cr.fill();
            }
        });
    }

    /** @param {number} level - percent */
    setLevel(level) {
        if (level === this._level)
            return;
        this._level = level;
        this.actor.invalidate();
    }
}

/**
 * Analog clock face (Stats Clock popup).
 */
export class AnalogClock {
    constructor() {
        this._hours = 0;
        this._minutes = 0;
        this._seconds = 0;
        this.actor = new Canvas({style_class: 'readout-analog-clock'}, (cr, width, height, theme) => {
            const size = Math.min(width, height);
            const radius = size / 2 - theme.unit;
            const cx = width / 2;
            const cy = height / 2;
            cr.arc(cx, cy, radius, 0, 2 * Math.PI);
            setColor(cr, theme.color('controlBackground'));
            cr.fillPreserve();
            cr.setLineWidth(theme.unit);
            setColor(cr, theme.dark ? hexGray(0.33) : theme.color('lightGray'));
            cr.stroke();
            const hand = (fraction, length, lineWidth, color) => {
                const angle = fraction * 2 * Math.PI - Math.PI / 2;
                cr.setLineWidth(lineWidth);
                cr.setLineCap(Cairo.LineCap.ROUND);
                cr.moveTo(cx, cy);
                cr.lineTo(cx + Math.cos(angle) * length, cy + Math.sin(angle) * length);
                setColor(cr, color);
                cr.stroke();
            };
            hand(((this._hours % 12) + this._minutes / 60) / 12, radius - 4 * theme.unit, 2 * theme.unit,
                withAlpha(theme.text, 0.85));
            hand((this._minutes + this._seconds / 60) / 60, radius - 2 * theme.unit, 2 * theme.unit, theme.label);
            hand(this._seconds / 60, radius - theme.unit, theme.unit, theme.color('plainRed'));
            cr.arc(cx, cy, 1.5 * theme.unit, 0, 2 * Math.PI);
            setColor(cr, theme.text);
            cr.fill();
        });
    }

    /**
     * @param {number} hours - 0..23
     * @param {number} minutes - 0..59
     * @param {number} seconds - 0..59
     */
    setTime(hours, minutes, seconds) {
        if (hours === this._hours && minutes === this._minutes && seconds === this._seconds)
            return;
        this._hours = hours;
        this._minutes = minutes;
        this._seconds = seconds;
        this.actor.invalidate();
    }
}

function hexGray(value) {
    return [value, value, value, 1];
}
