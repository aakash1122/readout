// Colors and scale shared by every drawing (docs/visual-spec.md sections 1.6 and 6.1-6.3).
//
// Text, label and background colors come from the shell theme. The Stats series palette
// (blue, red, orange, ...) picks its light or dark column from the brightness of the text
// the drawing sits on, so the panel (always dark) and the popup (light or dark) both match.

import St from 'gi://St';

// [light, dark] sRGB values of the AppKit system colors Stats uses.
const SERIES = {
    blue: ['#007AFF', '#0A84FF'],
    red: ['#FF3B30', '#FF453A'],
    orange: ['#FF9500', '#FF9F0A'],
    pink: ['#FF2D55', '#FF375F'],
    green: ['#28CD41', '#32D74B'],
    yellow: ['#FFCC00', '#FFD60A'],
    teal: ['#59ADC4', '#6AC4DC'],
    indigo: ['#5856D6', '#5E5CE6'],
    // Fixed colors.
    lightGray: ['#AAAAAA', '#AAAAAA'],
    gray: ['#808080', '#808080'],
    plainOrange: ['#FF8000', '#FF8000'],
    plainRed: ['#FF0000', '#FF0000'],
    underPage: ['#969696', '#282828'],
    controlBackground: ['#FFFFFF', '#1E1E1E'],
};

// Stats sizes are macOS points at a 13 pt body font (visual-spec.md 6.1).
export const STATS_BODY = 13;
// Stats draws panel widgets in an 18 px tall area.
export const STATS_WIDGET_HEIGHT = 18;

/**
 * @param {string} hex - "#RRGGBB"
 * @param {number} [alpha] - 0..1
 * @returns {number[]} [r, g, b, a] in 0..1
 */
export function hexColor(hex, alpha = 1) {
    const value = parseInt(hex.slice(1), 16);
    return [((value >> 16) & 255) / 255, ((value >> 8) & 255) / 255, (value & 255) / 255, alpha];
}

/**
 * @param {Cogl.Color|Clutter.Color} color - St color
 * @returns {number[]} [r, g, b, a] in 0..1
 */
export function stColor(color) {
    return [color.red / 255, color.green / 255, color.blue / 255, color.alpha / 255];
}

/**
 * @param {number[]} color - [r, g, b, a]
 * @param {number} alpha - multiplier
 * @returns {number[]}
 */
export function withAlpha(color, alpha) {
    return [color[0], color[1], color[2], color[3] * alpha];
}

/**
 * Converts a color to its gray value (Stats "monochrome accent").
 *
 * @param {number[]} color - [r, g, b, a]
 * @returns {number[]}
 */
export function grayscale(color) {
    const gray = 0.299 * color[0] + 0.587 * color[1] + 0.114 * color[2];
    return [gray, gray, gray, color[3]];
}

/**
 * Colors and sizes for drawing inside an actor, read from its theme node.
 */
export class Theme {
    /**
     * @param {St.Widget} actor - mapped actor whose theme node is used
     */
    constructor(actor) {
        const node = actor.get_theme_node();
        this.text = stColor(node.get_foreground_color());
        // Light text means a dark background.
        this.dark = 0.2126 * this.text[0] + 0.7152 * this.text[1] + 0.0722 * this.text[2] > 0.5;
        const [accent] = St.ThemeContext.get_for_stage(global.stage).get_accent_color();
        this.accent = stColor(accent);
        this.highContrast = St.Settings.get().high_contrast;
        this.fontFamily = node.get_font().get_family() ?? 'Sans';
        this.pangoContext = actor.get_pango_context();
        // One Stats point in pixels; the canvases set "-readout-em: 1em" in the stylesheet.
        const [hasEm, em] = node.lookup_length('-readout-em', false);
        this.unit = (hasEm && em > 0 ? em : STATS_BODY) / STATS_BODY;
        // Thinnest visible line: one device pixel.
        this.hairline = 1 / Math.max(1, actor.get_resource_scale());
    }

    /**
     * @param {string} name - palette name (blue, red, lightGray, ...) or "accent" / "text"
     * @param {number} [alpha] - 0..1
     * @returns {number[]}
     */
    color(name, alpha = 1) {
        if (name === 'accent')
            return withAlpha(this.accent, alpha);
        if (name === 'text')
            return withAlpha(this.text, alpha);
        const pair = SERIES[name] ?? SERIES.blue;
        return hexColor(pair[this.dark ? 1 : 0], alpha);
    }

    /** @returns {number[]} secondary label color: text at 55% (Stats secondaryLabelColor) */
    get label() {
        return withAlpha(this.text, this.highContrast ? 1 : 0.55);
    }

    /** @returns {number[]} tertiary label color: text at 30% */
    get tertiary() {
        return withAlpha(this.text, this.highContrast ? 0.8 : 0.3);
    }

    /** @returns {number[]} "inactive" remainder of rings and gauges: lightGray at 50% */
    get inactive() {
        return this.color('lightGray', this.highContrast ? 0.8 : 0.5);
    }
}

/**
 * Utilization color for a level (visual-spec.md 6.3): accent at or below the warning
 * threshold, plain orange up to the critical threshold, plain red above it.
 *
 * @param {number} value - percent
 * @param {object} item - panel item options (colorMode, warning, critical)
 * @param {Theme} theme - drawing theme
 * @returns {number[]}
 */
export function levelColor(value, item, theme) {
    if (item.colorMode === 'monochrome')
        return theme.text;
    if (item.colorMode === 'thresholds' && Number.isFinite(value)) {
        if (value > item.critical)
            return theme.color('plainRed');
        if (value > item.warning)
            return theme.color('plainOrange');
    }
    return theme.accent;
}

/**
 * Tone name for text widgets (CSS classes readout-text-warning / -critical).
 *
 * @param {number} value - percent
 * @param {object} item - panel item options
 * @returns {string} "normal", "warning" or "critical"
 */
export function levelTone(value, item) {
    if (item.colorMode !== 'thresholds' || !Number.isFinite(value))
        return 'normal';
    if (value > item.critical)
        return 'critical';
    if (value > item.warning)
        return 'warning';
    return 'normal';
}

/**
 * Stats batteryColor with colorize off: red below 20%, else the text color.
 *
 * @param {number} percent - level
 * @param {Theme} theme - drawing theme
 * @returns {number[]}
 */
export function batteryColor(percent, theme) {
    if (Number.isFinite(percent) && percent < 20)
        return theme.color('red');
    return theme.text;
}

/**
 * A series color, or its gray value in monochrome mode.
 *
 * @param {string} name - palette name
 * @param {object} item - panel item options
 * @param {Theme} theme - drawing theme
 * @returns {number[]}
 */
export function seriesColor(name, item, theme) {
    const color = theme.color(name);
    return item?.colorMode === 'monochrome' ? grayscale(color) : color;
}
