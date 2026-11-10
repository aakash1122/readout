// Panel widgets: one per module in a panel item, in the style the user picked
// (docs/visual-spec.md section 3, scaled per section 6.1).
//
// Stats draws widgets in an 18 px tall area; Readout uses h = min(panel height - 4, 26) and
// scales every Stats number by h / 18. The Stats styles are one Cairo canvas each, repainted
// only when the payload changed. Updates are pushed by the client; no widget polls. Only the
// clock uses a timer, and only while it is visible.

import Cairo from 'cairo';
import Clutter from 'gi://Clutter';
import GLib from 'gi://GLib';
import Pango from 'gi://Pango';
import St from 'gi://St';

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';

import {MODULES} from './catalog.js';
import {Canvas, drawEmpty, drawRing, drawSeries, drawTachometer, drawText, roundedRect, setColor} from './charts.js';
import {dim, setText} from './components.js';
import * as Format from './format.js';
import {STATS_WIDGET_HEIGHT, batteryColor, grayscale, levelColor, levelTone, seriesColor, withAlpha} from './theme.js';
import * as Values from './values.js';

const LINE_POINTS = 60;
const DUAL_LINE_POINTS = 60;
const SPEED_ACTIVE = 1024;

/**
 * Scale of the Stats widget geometry for the current panel (visual-spec.md 6.1).
 *
 * @returns {number} pixels (logical) per Stats point
 */
export function panelScale() {
    const scaleFactor = St.ThemeContext.get_for_stage(global.stage).scale_factor || 1;
    const thickness = Main.panel.height / scaleFactor;
    const height = Math.max(12, Math.min(thickness - 4, 26));
    return height / STATS_WIDGET_HEIGHT;
}

/**
 * Creates the widget for one module of a panel item.
 *
 * @param {string} module - module name
 * @param {object} item - panel item options
 * @param {object} context - {client, icon(name)}
 * @param {number} scale - panelScale()
 * @returns {object} widget with actor, update(payload), destroy()
 */
export function createPanelWidget(module, item, context, scale) {
    if (module === 'clock')
        return new ClockWidget(item);
    const style = item.styles[module];
    if (style === 'text')
        return new TextWidget(module, item, context);
    return new StatsWidget(module, style, item, context, scale);
}

/**
 * First three letters of the module label, shown vertically on chart styles.
 *
 * @param {string} module - module name
 * @returns {string}
 */
function verticalLabel(module) {
    return _(MODULES[module].shortLabel).slice(0, 3).toUpperCase();
}

/** Width of the bar chart box for a number of bars (Stats BarChart). */
function barsWidth(count) {
    if (count <= 1)
        return 11;
    if (count === 2)
        return 22;
    if (count <= 4)
        return 30;
    if (count <= 8)
        return 40;
    if (count <= 12)
        return 50;
    if (count <= 16)
        return 76;
    if (count <= 32)
        return 84;
    return 118;
}

class StatsWidget {
    constructor(module, style, item, context, scale) {
        this._module = module;
        this._style = style;
        this._item = item;
        this._context = context;
        this._scale = scale;
        this._payload = null;
        this._historyRequested = false;
        this._label = item.showLabel && ['line', 'bars', 'pie', 'tachometer', 'dualLine'].includes(style)
            ? verticalLabel(module) : '';
        this._lineValues = [];
        this._dualValues = [[], []];
        this._barCount = 1;
        this._stackWidth = 0;
        this.actor = new St.BoxLayout({
            style_class: `readout-module readout-style-${style}`,
            y_align: Clutter.ActorAlign.CENTER,
        });
        if (item.showIcon) {
            this.actor.add_child(new St.Icon({
                gicon: context.icon(MODULES[module].icon),
                style_class: 'system-status-icon readout-icon',
            }));
        }
        this._canvas = new Canvas({y_align: Clutter.ActorAlign.CENTER},
            (cr, width, height, theme) => this._draw(cr, width, height, theme));
        this.actor.add_child(this._canvas);
        this._fallbackIcon = null;
        this._resize(this._baseWidth());
    }

    _baseWidth() {
        const label = this._label ? (['pie', 'tachometer'].includes(this._style) ? 10 : 8) : 0;
        switch (this._style) {
        case 'mini':
            return this._item.showLabel ? 31 : 36;
        case 'line':
            return label + 32;
        case 'bars':
            return label + barsWidth(this._barCount);
        case 'pie':
        case 'tachometer':
            return label + 18;
        case 'dualLine':
            return label + 30;
        case 'speed':
            return 55;
        case 'memory':
            return (this._item.showLabel ? 12 : 0) + 50;
        case 'state':
            return 10;
        case 'battery':
            return 26;
        case 'stack':
            return Math.max(this._stackWidth, 20);
        default:
            return 30;
        }
    }

    _resize(statsWidth) {
        const width = Math.ceil(statsWidth * this._scale);
        const height = Math.round(STATS_WIDGET_HEIGHT * this._scale);
        if (width === this._width && height === this._height)
            return;
        this._width = width;
        this._height = height;
        this._canvas.set_style(`width: ${width}px; height: ${height}px; -readout-em: ${(13 * this._scale).toFixed(2)}px;`);
    }

    /** @param {object} payload - module payload from the daemon */
    update(payload) {
        if (!payload)
            return;
        this._payload = payload;
        switch (this._style) {
        case 'line':
            this._lineValues.push(Values.level(this._module, payload, this._item));
            if (this._lineValues.length > LINE_POINTS)
                this._lineValues.shift();
            this._requestHistory(payload);
            break;
        case 'dualLine': {
            const [down, up] = Values.chartValues(this._module, payload, this._item);
            this._dualValues[0].push(up ?? 0);
            this._dualValues[1].push(down ?? 0);
            for (const series of this._dualValues) {
                if (series.length > DUAL_LINE_POINTS)
                    series.shift();
            }
            this._requestHistory(payload);
            break;
        }
        case 'bars': {
            const count = this._barValues().length;
            if (count !== this._barCount) {
                this._barCount = count;
                this._resize(this._baseWidth());
            }
            break;
        }
        case 'stack':
            this._measureStack();
            break;
        case 'battery':
            this._resizeBattery();
            break;
        }
        this._updateFallbackIcon();
        // Text-like styles repaint only when what they show changed; charts scroll every update.
        const key = this._renderKey();
        if (key !== null && key === this._renderedKey)
            return;
        this._renderedKey = key;
        this._canvas.invalidate();
    }

    _renderKey() {
        const payload = this._payload;
        const item = this._item;
        const level = Values.level(this._module, payload, item);
        const tone = levelTone(level, item);
        switch (this._style) {
        case 'mini':
            return `${Values.text(this._module, payload, item)}|${tone}`;
        case 'speed':
            return this._rates().map(rate => `${Format.speed(rate, item.bits)}:${rate >= SPEED_ACTIVE}`).join('|');
        case 'memory': {
            const {free, used, usage} = this._memoryValues();
            return `${free}|${used}|${Math.round(usage)}|${tone}`;
        }
        case 'state':
            return `${payload.connected}|${payload.connectivity}`;
        case 'stack':
            return this._stackEntries().map(entry => `${entry.label}:${entry.value}`).join('|');
        case 'battery':
            return `${payload.present}|${Math.round(payload.percentage)}|${payload.state}|${payload.externalPower}`;
        case 'pie':
        case 'tachometer': {
            // Percent values at half-percent steps: finer changes are not visible at this size.
            const total = payload.total || 1;
            const shown = [
                level, payload.system, payload.user, payload.app * 100 / total, payload.compressed * 100 / total,
            ];
            return `${shown.map(value => Number.isFinite(value) ? Math.round(value * 2) : '-').join('|')}|${tone}`;
        }
        default:
            return null;
        }
    }

    _requestHistory(payload) {
        if (this._historyRequested)
            return;
        const gpuId = this._module === 'gpu' ? Values.selectedGpu(payload, this._item)?.id ?? '' : '';
        const series = Values.historySeries(this._module, gpuId);
        if (series.length === 0)
            return;
        this._historyRequested = true;
        this._context.client.getHistory(this._module).then(history => {
            if (!this._canvas || !history)
                return;
            const histories = series.map(name => (history[name] ?? []).map(point => point[1]));
            if (histories[0].length < 2)
                return;
            if (this._style === 'line') {
                this._lineValues = histories[0].slice(-LINE_POINTS);
            } else {
                // historySeries() lists [download/read, upload/write].
                this._dualValues = [histories[1].slice(-DUAL_LINE_POINTS), histories[0].slice(-DUAL_LINE_POINTS)];
            }
            this._canvas.invalidate();
        });
    }

    _barValues() {
        const payload = this._payload;
        if (this._module === 'cpu' && Array.isArray(payload?.cores) && payload.cores.length > 0)
            return payload.cores;
        return [Values.level(this._module, payload, this._item)];
    }

    // ---- Drawing --------------------------------------------------------------------------

    _draw(cr, width, height, theme) {
        const unit = height / STATS_WIDGET_HEIGHT;
        if (!this._payload) {
            drawEmpty(cr, theme, width, height, 15 * unit);
            return;
        }
        let x = 0;
        if (this._label) {
            this._drawVerticalLabel(cr, theme, unit);
            x = (['pie', 'tachometer'].includes(this._style) ? 10 : 8) * unit;
        }
        const area = {x, width: width - x, height, unit};
        switch (this._style) {
        case 'mini':
            this._drawMini(cr, theme, area);
            break;
        case 'line':
            this._drawLine(cr, theme, area);
            break;
        case 'bars':
            this._drawBars(cr, theme, area);
            break;
        case 'pie':
        case 'tachometer':
            this._drawPie(cr, theme, area);
            break;
        case 'dualLine':
            this._drawDualLine(cr, theme, area);
            break;
        case 'speed':
            this._drawSpeed(cr, theme, area);
            break;
        case 'memory':
            this._drawMemory(cr, theme, area);
            break;
        case 'state':
            this._drawState(cr, theme, area);
            break;
        case 'stack':
            this._drawStack(cr, theme, area);
            break;
        case 'battery':
            this._drawBattery(cr, theme, area);
            break;
        }
    }

    _drawVerticalLabel(cr, theme, unit) {
        const letters = [...this._label];
        letters.forEach((letter, index) => {
            drawText(cr, theme, letter, {
                x: 0, y: index * 6 * unit, width: 6 * unit, height: 6 * unit,
                size: 7 * unit, align: 'center', tabular: false,
            });
        });
    }

    _drawMini(cr, theme, {x, width, unit}) {
        const level = Values.level(this._module, this._payload, this._item);
        const value = Values.text(this._module, this._payload, this._item);
        const color = this._item.colorMode === 'thresholds' ? levelColor(level, this._item, theme) : theme.text;
        if (this._item.showLabel) {
            drawText(cr, theme, _(MODULES[this._module].shortLabel).toUpperCase(), {
                x, y: -1 * unit, width, height: 8 * unit, size: 7 * unit, weight: Pango.Weight.LIGHT,
            });
            drawText(cr, theme, value, {x, y: 5 * unit, width, height: 13 * unit, size: 12 * unit, color});
        } else {
            drawText(cr, theme, value, {
                x, y: 0, width, height: 18 * unit, size: 14 * unit, color, align: 'center',
            });
        }
    }

    // Stats box: filled with the text color, so the monochrome series uses the inverted color.
    _drawBox(cr, theme, x, width, height, unit) {
        roundedRect(cr, x + theme.hairline / 2, theme.hairline / 2, width - theme.hairline, height - theme.hairline,
            2 * unit);
        setColor(cr, theme.text);
        cr.fillPreserve();
        cr.setLineWidth(theme.hairline);
        cr.stroke();
    }

    _boxColor(level, theme) {
        if (this._item.colorMode === 'monochrome')
            return theme.dark ? [0, 0, 0, 1] : [1, 1, 1, 1];
        return levelColor(level, this._item, theme);
    }

    _drawLine(cr, theme, {x, width, height, unit}) {
        this._drawBox(cr, theme, x, width, height, unit);
        const level = Values.level(this._module, this._payload, this._item);
        const inset = theme.hairline * 1.5;
        drawSeries(cr, theme, {
            values: this._lineValues, capacity: LINE_POINTS,
            x: x + inset, y: inset, width: width - 2 * inset, height: height - 2 * inset,
            maximum: 100, color: this._boxColor(level, theme), fillAlphas: [0.4, 0.8],
        });
    }

    _drawBars(cr, theme, {x, width, height, unit}) {
        this._drawBox(cr, theme, x, width, height, unit);
        const values = this._barValues();
        const inset = theme.hairline * 1.5;
        const gap = 0.5 * unit;
        const innerWidth = width - 2 * inset;
        const innerHeight = height - 2 * inset;
        const barWidth = (innerWidth - gap * (values.length - 1)) / values.length;
        values.forEach((value, index) => {
            const fraction = Math.max(0, Math.min(1, (value || 0) / 100));
            const barHeight = innerHeight * fraction;
            cr.rectangle(x + inset + index * (barWidth + gap), inset + innerHeight - barHeight, barWidth, barHeight);
            setColor(cr, this._boxColor(value, theme));
            cr.fill();
        });
    }

    _segments(theme) {
        const payload = this._payload;
        const item = this._item;
        const level = Values.level(this._module, payload, item);
        if (item.colorMode === 'thresholds')
            return [{value: level / 100, color: levelColor(level, item, theme)}];
        const color = name => seriesColor(name, item, theme);
        if (this._module === 'cpu' && Number.isFinite(payload.system)) {
            return [
                {value: payload.system / 100, color: color('red')},
                {value: payload.user / 100, color: color('blue')},
            ];
        }
        if (this._module === 'memory' && Number.isFinite(payload.app) && payload.total > 0) {
            const compressed = payload.compressed ?? 0;
            const kernel = Math.max(0, payload.used - payload.app - compressed);
            return [
                {value: payload.app / payload.total, color: color('blue')},
                {value: kernel / payload.total, color: color('orange')},
                {value: compressed / payload.total, color: color('pink')},
            ];
        }
        return [{value: (Number.isFinite(level) ? level : 0) / 100, color: color('blue')}];
    }

    _drawPie(cr, theme, {x, width, height}) {
        const size = Math.min(width, height);
        const segments = this._segments(theme);
        const inactive = this._item.colorMode === 'monochrome' ? grayscale(theme.inactive) : theme.inactive;
        if (this._style === 'pie') {
            drawRing(cr, theme, {
                segments, cx: x + width / 2, cy: height / 2, size, stroke: 0, filled: true, inactive,
            });
        } else {
            drawTachometer(cr, theme, {segments, x: x + (width - size) / 2, y: 0, size, inactive});
        }
    }

    _drawDualLine(cr, theme, {x, width, height}) {
        const half = height / 2;
        const [top, bottom] = this._dualValues;
        const maximum = values => Math.max(1, ...values);
        const common = {capacity: DUAL_LINE_POINTS, x, width, height: half, flatFill: true};
        drawSeries(cr, theme, {
            ...common, values: top, y: 0, maximum: maximum(top), color: seriesColor('red', this._item, theme),
        });
        drawSeries(cr, theme, {
            ...common, values: bottom, y: half, maximum: maximum(bottom), flip: true,
            color: seriesColor('blue', this._item, theme),
        });
    }

    _rates() {
        const disk = this._module === 'disk';
        const payload = this._payload;
        return disk ? [payload.write, payload.read] : [payload.upload, payload.download];
    }

    _drawSpeed(cr, theme, {x, unit}) {
        const rates = this._rates();
        ['red', 'blue'].forEach((name, row) => {
            const rate = rates[row];
            const top = row * 9 * unit;
            cr.arc(x + 3.5 * unit, top + 4.5 * unit, 3 * unit, 0, 2 * Math.PI);
            setColor(cr, rate >= SPEED_ACTIVE ? seriesColor(name, this._item, theme) : theme.text);
            cr.fill();
            drawText(cr, theme, Format.speed(rate, this._item.bits), {
                x: x + 7 * unit, y: top, width: 48 * unit, height: 9 * unit,
                size: 9 * unit, weight: Pango.Weight.LIGHT, align: 'right',
            });
        });
    }

    _memoryValues() {
        const payload = this._payload;
        if (this._module === 'disk')
            return {free: payload.free, used: payload.used, usage: payload.usage};
        return {free: payload.available, used: payload.used, usage: payload.usage};
    }

    _drawMemory(cr, theme, {x, width, unit}) {
        const {free, used, usage} = this._memoryValues();
        const format = this._module === 'disk' ? Format.diskSize : Format.size;
        const rows = [
            {symbol: _('F:'), value: format(free), level: 100 - usage},
            {symbol: _('U:'), value: format(used), level: usage},
        ];
        const symbolWidth = this._item.showLabel ? 12 * unit : 0;
        rows.forEach((row, index) => {
            const top = index * 9 * unit;
            const color = this._item.colorMode === 'thresholds' ? levelColor(row.level, this._item, theme) : theme.text;
            if (symbolWidth > 0) {
                drawText(cr, theme, row.symbol, {
                    x, y: top, width: 8 * unit, height: 9 * unit, size: 9 * unit, weight: Pango.Weight.LIGHT,
                });
            }
            drawText(cr, theme, row.value, {
                x: x + symbolWidth, y: top, width: width - symbolWidth, height: 9 * unit,
                size: 9 * unit, weight: Pango.Weight.LIGHT, align: 'right', color,
            });
        });
    }

    _drawState(cr, theme, {x, width, height, unit}) {
        const payload = this._payload;
        const connected = Boolean(payload.connected) && payload.connectivity !== 'none';
        cr.arc(x + width / 2, height / 2, 4 * unit, 0, 2 * Math.PI);
        setColor(cr, seriesColor(connected ? 'green' : 'red', this._item, theme));
        cr.fill();
    }

    _stackEntries() {
        return this._module === 'bluetooth'
            ? Values.bluetoothEntries(this._payload) : Values.sensorEntries(this._payload, this._item);
    }

    // Columns of two rows; a leftover value is one larger row (Stats Stack, mode auto).
    _stackColumns(entries) {
        const columns = [];
        for (let start = 0; start < entries.length; start += 2)
            columns.push(entries.slice(start, start + 2));
        return columns;
    }

    _stackText(entry) {
        const label = this._item.showLabel ? entry.label.slice(0, this._module === 'bluetooth' ? 3 : 4) : '';
        return {label, value: entry.value};
    }

    _measureStack() {
        const entries = this._stackEntries();
        if (entries.length === 0) {
            this._resize(this._module === 'bluetooth' ? 0 : 20);
            return;
        }
        let total = 2;
        for (const column of this._stackColumns(entries)) {
            const single = column.length === 1;
            const size = single ? 13 : 10;
            let widest = 0;
            for (const entry of column) {
                const {label, value} = this._stackText(entry);
                const labelWidth = label ? measure(this._canvas, label, size, this._scale) + 3 : 0;
                widest = Math.max(widest, labelWidth + measure(this._canvas, value, size, this._scale));
            }
            total += (single ? widest : Math.max(20, widest)) + 2 + 2;
        }
        // Grow at once, shrink only on a clear change, so the panel does not jitter.
        if (total > this._stackWidth || total < this._stackWidth - 6)
            this._stackWidth = Math.ceil(total);
        this._resize(this._stackWidth);
    }

    _drawStack(cr, theme, {x, height, unit}) {
        const entries = this._stackEntries();
        if (entries.length === 0) {
            if (this._module !== 'bluetooth')
                drawText(cr, theme, Format.DASH, {x, width: 20 * unit, height, size: 13 * unit, align: 'center'});
            return;
        }
        let left = x + 2 * unit;
        for (const column of this._stackColumns(entries)) {
            const single = column.length === 1;
            const size = (single ? 13 : 10) * unit;
            const weight = single ? Pango.Weight.NORMAL : Pango.Weight.LIGHT;
            let widest = 0;
            column.forEach((entry, row) => {
                const {label, value} = this._stackText(entry);
                const top = single ? 0 : row * 9 * unit;
                const rowHeight = single ? height : 9 * unit;
                let offset = 0;
                if (label) {
                    offset = drawText(cr, theme, label, {
                        x: left, y: top, height: rowHeight, size, weight, color: theme.label, tabular: false,
                    }) + 3 * this._scale;
                }
                const valueWidth = drawText(cr, theme, value,
                    {x: left + offset, y: top, height: rowHeight, size, weight});
                widest = Math.max(widest, offset + valueWidth);
            });
            left += (single ? widest : Math.max(20 * unit, widest)) + 4 * unit;
        }
    }

    _batteryInfo() {
        const payload = this._payload;
        const present = Boolean(payload.present);
        const level = present ? payload.percentage : NaN;
        const showText = this._item.showLabel && present && Number.isFinite(level) && Math.round(level) < 100;
        return {present, level, showText, text: showText ? Format.percent(level) : ''};
    }

    _resizeBattery() {
        const {showText, text} = this._batteryInfo();
        const textWidth = showText ? measure(this._canvas, text, 12, this._scale) / this._scale + 2 : 0;
        this._resize(26 + textWidth);
    }

    _drawBattery(cr, theme, {x, width, height, unit}) {
        const payload = this._payload;
        const {present, level, showText, text} = this._batteryInfo();
        const bodyWidth = 22 * unit;
        const bodyHeight = 12 * unit;
        const left = x + width - bodyWidth - 4 * unit;
        const top = (height - bodyHeight) / 2;
        if (showText) {
            drawText(cr, theme, text, {
                x, y: 0, width: left - x - 2 * unit, height, size: 12 * unit, align: 'right',
            });
        }
        // Outline and terminal nub.
        cr.setLineWidth(theme.hairline);
        roundedRect(cr, left + theme.hairline / 2, top + theme.hairline / 2, bodyWidth - theme.hairline,
            bodyHeight - theme.hairline, 2 * unit);
        setColor(cr, withAlpha(theme.text, 0.5));
        cr.stroke();
        roundedRect(cr, left + bodyWidth + unit, top + (bodyHeight - 4 * unit) / 2, 3 * unit, 4 * unit, unit);
        cr.fill();
        if (!present) {
            this._drawPlug(cr, theme, left, top, bodyWidth, bodyHeight, unit);
            return;
        }
        if (!Number.isFinite(level)) {
            drawText(cr, theme, '?', {x: left, y: top, width: bodyWidth, height: bodyHeight, size: 11 * unit, align: 'center'});
            return;
        }
        const inset = 1.5 * unit;
        const fillWidth = Math.max(unit, (bodyWidth - 2 * inset) * Math.min(1, level / 100));
        const charging = payload.state === 'charging';
        const plugged = payload.externalPower && !charging;
        cr.pushGroup();
        roundedRect(cr, left + inset, top + inset, fillWidth, bodyHeight - 2 * inset, unit);
        setColor(cr, batteryColor(level, theme));
        cr.fill();
        if (charging || plugged) {
            // The charger glyph is knocked out of the fill and drawn in the text color elsewhere.
            cr.setOperator(Cairo.Operator.DEST_OUT);
            this._glyphPath(cr, charging, left, top, bodyWidth, bodyHeight, unit);
            setColor(cr, [0, 0, 0, 1]);
            cr.fill();
            cr.setOperator(Cairo.Operator.OVER);
        }
        cr.popGroupToSource();
        cr.paint();
        if (charging || plugged) {
            cr.save();
            cr.rectangle(left + inset + fillWidth, top, bodyWidth, bodyHeight);
            cr.clip();
            this._glyphPath(cr, charging, left, top, bodyWidth, bodyHeight, unit);
            setColor(cr, theme.text);
            cr.fill();
            cr.restore();
        }
    }

    _glyphPath(cr, charging, left, top, bodyWidth, bodyHeight, unit) {
        const cx = left + bodyWidth / 2;
        const cy = top + bodyHeight / 2;
        if (charging) {
            const glyphHeight = bodyHeight - 2 * unit;
            const glyphWidth = glyphHeight * 0.55;
            cr.moveTo(cx + glyphWidth * 0.15, cy - glyphHeight / 2);
            cr.lineTo(cx - glyphWidth / 2, cy + glyphHeight * 0.08);
            cr.lineTo(cx - glyphWidth * 0.02, cy + glyphHeight * 0.08);
            cr.lineTo(cx - glyphWidth * 0.15, cy + glyphHeight / 2);
            cr.lineTo(cx + glyphWidth / 2, cy - glyphHeight * 0.08);
            cr.lineTo(cx + glyphWidth * 0.02, cy - glyphHeight * 0.08);
            cr.closePath();
        } else {
            this._plugPath(cr, cx, cy, bodyHeight - 3 * unit, unit);
        }
    }

    _plugPath(cr, cx, cy, size, unit) {
        const bodyWidth = size * 0.6;
        roundedRect(cr, cx - bodyWidth / 2, cy - size * 0.15, bodyWidth, size * 0.45, unit * 0.8);
        cr.rectangle(cx - bodyWidth * 0.3, cy - size / 2, unit, size * 0.35);
        cr.rectangle(cx + bodyWidth * 0.3 - unit, cy - size / 2, unit, size * 0.35);
        cr.rectangle(cx - unit / 2, cy + size * 0.3, unit, size * 0.2);
    }

    _drawPlug(cr, theme, left, top, bodyWidth, bodyHeight, unit) {
        this._plugPath(cr, left + bodyWidth / 2, top + bodyHeight / 2, bodyHeight - 3 * unit, unit);
        setColor(cr, theme.text);
        cr.fill();
    }

    _updateFallbackIcon() {
        // Bluetooth with no device battery to show: the adapter state icon instead.
        if (this._style !== 'stack' || this._module !== 'bluetooth')
            return;
        const empty = Values.bluetoothEntries(this._payload).length === 0;
        if (empty && !this._fallbackIcon) {
            this._fallbackIcon = new St.Icon({style_class: 'system-status-icon'});
            this.actor.add_child(this._fallbackIcon);
        }
        if (this._fallbackIcon) {
            this._fallbackIcon.visible = empty;
            const powered = this._payload.available && this._payload.powered;
            const name = powered ? 'bluetooth-active-symbolic' : 'bluetooth-disabled-symbolic';
            if (this._fallbackIcon.icon_name !== name)
                this._fallbackIcon.icon_name = name;
        }
        this._canvas.visible = !empty;
    }

    destroy() {
        this._canvas = null;
        this.actor.destroy();
    }
}

/**
 * Text width in logical pixels for a font size in Stats points.
 *
 * @param {Clutter.Actor} actor - actor providing the Pango context
 * @param {string} text - text
 * @param {number} size - Stats points
 * @param {number} scale - pixels per Stats point
 * @returns {number}
 */
function measure(actor, text, size, scale) {
    if (!actor.get_stage())
        return text.length * size * scale * 0.6;
    const layout = actor.create_pango_layout(text);
    const font = actor.get_theme_node().get_font().copy();
    font.set_absolute_size(size * scale * Pango.SCALE);
    layout.set_font_description(font);
    const [, logical] = layout.get_pixel_extents();
    return logical.width;
}

/**
 * The "text" style: the value as text, with an optional short label before it.
 */
class TextWidget {
    constructor(module, item, context) {
        this._module = module;
        this._item = item;
        this._tone = 'normal';
        this.actor = new St.BoxLayout({style_class: 'readout-module readout-style-text', y_align: Clutter.ActorAlign.CENTER});
        if (item.showIcon) {
            this.actor.add_child(new St.Icon({
                gicon: context.icon(MODULES[module].icon),
                style_class: 'system-status-icon readout-icon',
            }));
        }
        if (item.showLabel) {
            const caption = new St.Label({
                text: _(MODULES[module].shortLabel), style_class: 'readout-caption',
                y_align: Clutter.ActorAlign.CENTER,
            });
            dim(caption);
            this.actor.add_child(caption);
        }
        this._value = new St.Label({
            text: Format.DASH, style_class: 'readout-value readout-tnum',
            y_align: Clutter.ActorAlign.CENTER,
        });
        this.actor.add_child(this._value);
    }

    update(payload) {
        if (!payload)
            return;
        setText(this._value, Values.text(this._module, payload, this._item));
        const tone = levelTone(Values.level(this._module, payload, this._item), this._item);
        if (tone !== this._tone) {
            this._value.remove_style_class_name(`readout-text-${this._tone}`);
            this._value.add_style_class_name(`readout-text-${tone}`);
            this._tone = tone;
        }
    }

    destroy() {
        this.actor.destroy();
    }
}

/**
 * Formats a time with the item's clock options.
 *
 * @param {GLib.DateTime} dateTime - time
 * @param {object} item - panel item options
 * @param {boolean} [seconds] - include seconds (default: the item option)
 * @returns {string}
 */
export function formatClock(dateTime, item, seconds = item.clockSeconds) {
    let format = item.clock24 ? '%H:%M' : '%l:%M';
    if (seconds)
        format += ':%S';
    if (!item.clock24)
        format += ' %p';
    return dateTime.format(format).trim();
}

/**
 * @param {string} identifier - IANA zone, or '' for local
 * @returns {GLib.TimeZone}
 */
export function timeZoneFor(identifier) {
    if (identifier) {
        const zone = GLib.TimeZone.new_identifier(identifier);
        if (zone)
            return zone;
    }
    return GLib.TimeZone.new_local();
}

/**
 * Client-side clock. Ticks once per minute (or second, if seconds are shown),
 * aligned to the boundary, and only while visible.
 */
class ClockWidget {
    constructor(item) {
        this._item = item;
        this._timerId = 0;
        this.actor = new St.BoxLayout({style_class: 'readout-module readout-style-text', y_align: Clutter.ActorAlign.CENTER});
        this._zone = item.timezones[0] ?? '';
        if (item.showLabel && this._zone) {
            const caption = new St.Label({
                text: zoneShortName(this._zone), style_class: 'readout-caption',
                y_align: Clutter.ActorAlign.CENTER,
            });
            dim(caption);
            this.actor.add_child(caption);
        }
        this._value = new St.Label({style_class: 'readout-value readout-tnum', y_align: Clutter.ActorAlign.CENTER});
        this.actor.add_child(this._value);
        this._mappedId = this.actor.connect('notify::mapped', () => this._sync());
        this._tick();
    }

    _sync() {
        if (this.actor.mapped && !this._timerId)
            this._tick();
        else if (!this.actor.mapped && this._timerId)
            this._stop();
    }

    _stop() {
        if (this._timerId)
            GLib.source_remove(this._timerId);
        this._timerId = 0;
    }

    _tick() {
        this._stop();
        const now = GLib.DateTime.new_now(timeZoneFor(this._zone));
        setText(this._value, formatClock(now, this._item));
        if (!this.actor.mapped)
            return;
        const milliseconds = Math.floor(now.get_microsecond() / 1000);
        const delay = this._item.clockSeconds
            ? 1000 - milliseconds
            : (60 - now.get_second()) * 1000 - milliseconds;
        this._timerId = GLib.timeout_add(GLib.PRIORITY_DEFAULT, Math.max(delay, 1) + 5, () => {
            this._timerId = 0;
            this._tick();
            return GLib.SOURCE_REMOVE;
        });
    }

    update() {}

    destroy() {
        this._stop();
        this.actor.disconnect(this._mappedId);
        this.actor.destroy();
    }
}

/**
 * "America/New_York" -> "New York".
 *
 * @param {string} zone - IANA identifier
 * @returns {string}
 */
export function zoneShortName(zone) {
    const city = zone.split('/').pop() ?? zone;
    return city.replace(/_/g, ' ');
}
