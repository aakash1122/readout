// Popup building blocks in the Stats layout (docs/visual-spec.md sections 1.2-1.4 and 1.8).
//
// Sizes live in the stylesheet in em, so they follow the shell font (one Stats point = 1em / 13).
// Labels are only written when their text changes, so an update does not relayout.

import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import Pango from 'gi://Pango';
import St from 'gi://St';

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';

import {Swatch} from './charts.js';

/**
 * Sets a label's text only if it changed (avoids relayout).
 *
 * @param {St.Label} label - target
 * @param {string} text - new text
 */
export function setText(label, text) {
    if (label.text !== text)
        label.text = text;
}

/**
 * Secondary label color: the text color at 55% (full strength in high contrast).
 *
 * @param {Clutter.Actor} actor - target
 */
export function dim(actor) {
    actor.opacity = St.Settings.get().high_contrast ? 255 : 140;
}

/**
 * Tertiary label color: the text color at 30%.
 *
 * @param {Clutter.Actor} actor - target
 */
export function faint(actor) {
    actor.opacity = St.Settings.get().high_contrast ? 200 : 77;
}

/**
 * A row label with the Stats trailing colon ("System:").
 *
 * @param {string} text - translated label without colon
 * @returns {string}
 */
export function withColon(text) {
    /* Translators: a row label followed by its value, e.g. "System:" */
    return _('%s:').format(text);
}

/**
 * Borderless icon button tinted light gray (header and section buttons).
 *
 * @param {Gio.Icon|string} icon - gicon or icon name
 * @param {string} accessibleName - name for screen readers and the tooltip-less button
 * @param {Function} onClicked - click handler
 * @param {string} [styleClass] - extra style class
 * @returns {St.Button}
 */
export function iconButton(icon, accessibleName, onClicked, styleClass = '') {
    const iconActor = typeof icon === 'string'
        ? new St.Icon({icon_name: icon, style_class: 'readout-button-icon'})
        : new St.Icon({gicon: icon, style_class: 'readout-button-icon'});
    const button = new St.Button({
        child: iconActor,
        style_class: `readout-icon-button ${styleClass}`.trim(),
        can_focus: true,
        accessible_name: accessibleName,
        y_align: Clutter.ActorAlign.CENTER,
    });
    button.connect('clicked', onClicked);
    return button;
}

/**
 * Section separator: centered gray title, no rules (Stats separatorView).
 */
/**
 * Lays out a section title: the label centered, a rule from the left edge to the label, a rule
 * from the label to the right edge, or to the action button when there is one. The gap between
 * a rule and the label is the label padding; the gap before the button is the container's
 * `spacing`.
 */
const SeparatorLayout = GObject.registerClass(
class SeparatorLayout extends Clutter.LayoutManager {
    _init(parts) {
        super._init();
        this._parts = parts;
    }

    vfunc_get_preferred_width(_container, forHeight) {
        const {label, button} = this._parts;
        const [labelMinimum, labelNatural] = label.get_preferred_width(forHeight);
        const buttonWidth = button ? button.get_preferred_width(forHeight)[1] * 2 : 0;
        return [labelMinimum + buttonWidth, labelNatural + buttonWidth];
    }

    vfunc_get_preferred_height(_container, forWidth) {
        let minimum = 0, natural = 0;
        for (const part of Object.values(this._parts).filter(Boolean)) {
            const [partMinimum, partNatural] = part.get_preferred_height(forWidth);
            minimum = Math.max(minimum, partMinimum);
            natural = Math.max(natural, partNatural);
        }
        return [minimum, natural];
    }

    vfunc_allocate(container, box) {
        const {leftRule, label, rightRule, button} = this._parts;
        const width = box.get_width();
        const top = box.y1, height = box.get_height();
        const place = (actor, x, actorWidth, actorHeight) => {
            const y = top + Math.round((height - actorHeight) / 2);
            const area = new Clutter.ActorBox();
            area.set_origin(box.x1 + x, y);
            area.set_size(Math.max(0, actorWidth), actorHeight);
            actor.allocate(area);
        };

        let rightEnd = width;
        let buttonWidth = 0;
        if (button) {
            const [, buttonNaturalWidth] = button.get_preferred_width(-1);
            const [, buttonHeight] = button.get_preferred_height(buttonNaturalWidth);
            buttonWidth = buttonNaturalWidth;
            place(button, width - buttonWidth, buttonWidth, buttonHeight);
            rightEnd = width - buttonWidth - container.get_theme_node().get_length('spacing');
        }

        // Keep the label centered, but never under the button.
        const [, labelNaturalWidth] = label.get_preferred_width(-1);
        const labelWidth = Math.min(labelNaturalWidth, width - 2 * buttonWidth);
        const [, labelHeight] = label.get_preferred_height(labelWidth);
        const labelX = Math.round((width - labelWidth) / 2);
        place(label, labelX, labelWidth, labelHeight);

        const [, ruleHeight] = leftRule.get_preferred_height(-1);
        place(leftRule, 0, labelX, ruleHeight);
        place(rightRule, labelX + labelWidth, rightEnd - labelX - labelWidth, ruleHeight);
    }
});

export class Separator {
    /**
     * Section title (docs/visual-spec.md 6.4): a gap above, then the centered title with a thin
     * rule on each side. The rules start at the left edge like every other section's; with an
     * action button at the right edge, the right rule stops before it.
     *
     * @param {string} title - section title
     * @param {St.Button} [button] - optional action button at the right edge
     */
    constructor(title, button = null) {
        const leftRule = new St.Widget({style_class: 'readout-separator-rule'});
        const rightRule = new St.Widget({style_class: 'readout-separator-rule'});
        this.label = new St.Label({text: title, style_class: 'readout-separator-label'});
        this.label.clutter_text.ellipsize = Pango.EllipsizeMode.END;
        dim(this.label);
        if (button)
            button.add_style_class_name('readout-separator-button');

        this.actor = new St.Widget({
            style_class: 'readout-separator readout-section-title',
            x_expand: true,
            layout_manager: new SeparatorLayout({leftRule, label: this.label, rightRule, button}),
        });
        for (const part of [leftRule, this.label, rightRule, button].filter(Boolean))
            this.actor.add_child(part);
    }
}

/**
 * Key/value rows (Stats popupRow / popupWithColorRow). Rows are created on first use;
 * a null value hides the row.
 */
export class Rows {
    /** @param {object} [options] - {styleClass} */
    constructor({styleClass = ''} = {}) {
        this.actor = new St.BoxLayout({
            orientation: Clutter.Orientation.VERTICAL,
            style_class: `readout-rows ${styleClass}`.trim(),
            x_expand: true,
        });
        this._rows = new Map();
    }

    /**
     * @param {string} key - stable key
     * @param {string|null} value - shown value, null hides the row
     * @param {object} options - {label (translated, no colon), swatch: palette name, swatchAlpha, compact}
     */
    set(key, value, {label = key, swatch = null, swatchAlpha = 1, compact = false} = {}) {
        let row = this._rows.get(key);
        const visible = value !== null && value !== undefined;
        if (!row) {
            // Created even while hidden, so rows keep the order of the first set() calls.
            row = this._createRow(label, swatch, swatchAlpha, compact);
            this._rows.set(key, row);
        }
        row.box.visible = visible;
        if (visible) {
            setText(row.value, String(value));
            if (swatch)
                row.swatch?.setColor(swatch, swatchAlpha);
            setText(row.label, withColon(label));
        }
    }

    /**
     * Shows or hides every row whose key is not in the given set.
     *
     * @param {Set<string>} keys - keys to keep visible
     */
    retain(keys) {
        for (const [key, row] of this._rows) {
            if (!keys.has(key))
                row.box.visible = false;
        }
    }

    _createRow(label, swatch, swatchAlpha, compact) {
        const box = new St.BoxLayout({style_class: 'readout-row', x_expand: true});
        let swatchView = null;
        if (swatch) {
            swatchView = new Swatch(swatch, {alpha: swatchAlpha});
            box.add_child(swatchView.actor);
            box.add_style_class_name('readout-row-colored');
        }
        const labelActor = new St.Label({
            text: withColon(label),
            style_class: 'readout-row-label',
            y_align: Clutter.ActorAlign.CENTER,
        });
        dim(labelActor);
        const value = new St.Label({
            style_class: `readout-row-value readout-tnum${compact ? ' readout-row-value-compact' : ''}`,
            x_expand: true,
            x_align: Clutter.ActorAlign.END,
            y_align: Clutter.ActorAlign.CENTER,
        });
        value.clutter_text.ellipsize = Pango.EllipsizeMode.START;
        box.add_child(labelActor);
        box.add_child(value);
        this.actor.add_child(box);
        return {box, label: labelActor, value, swatch: swatchView};
    }
}

/**
 * Card background used for GPUs, drives, fans, Bluetooth devices and clocks.
 *
 * @param {object} [params] - extra St.BoxLayout properties
 * @returns {St.BoxLayout}
 */
export function card(params = {}) {
    const {style_class: styleClass = '', ...rest} = params;
    return new St.BoxLayout({
        orientation: Clutter.Orientation.VERTICAL,
        style_class: `readout-card ${styleClass}`.trim(),
        x_expand: true,
        ...rest,
    });
}

/**
 * Stacks a canvas and a centered label (ring and gauge texts).
 *
 * @param {St.Widget} canvas - drawing
 * @param {string} labelClass - style class of the centered label
 * @returns {{actor: St.Widget, label: St.Label}}
 */
export function overlay(canvas, labelClass) {
    const actor = new St.Widget({layout_manager: new Clutter.BinLayout()});
    actor.add_child(canvas);
    const label = new St.Label({
        style_class: `${labelClass} readout-tnum`,
        x_align: Clutter.ActorAlign.CENTER,
        y_align: Clutter.ActorAlign.CENTER,
    });
    actor.add_child(label);
    return {actor, label};
}

/**
 * Stats top processes list: header row, then rows with icon, name and one or two values.
 */
export class ProcessList {
    /**
     * @param {object} context - {processIcon(name)}
     * @param {Array} columns - [{title, swatch}] one or two value columns
     */
    constructor(context, columns) {
        this._context = context;
        this._columns = columns;
        this.actor = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL, x_expand: true});
        this.actor.add_child(new Separator(_('Top processes')).actor);
        const header = new St.BoxLayout({style_class: 'readout-row readout-process-header', x_expand: true});
        header.add_child(new St.Widget({style_class: 'readout-process-icon-box'}));
        const title = new St.Label({
            text: _('Process'), style_class: 'readout-process-title', x_expand: true,
            y_align: Clutter.ActorAlign.CENTER,
        });
        faint(title);
        header.add_child(title);
        for (const column of columns) {
            const cell = new St.BoxLayout({
                style_class: `readout-process-column${columns.length > 1 ? ' readout-process-column-narrow' : ''}`,
                y_align: Clutter.ActorAlign.CENTER,
            });
            if (column.swatch) {
                cell.add_child(new St.Widget({x_expand: true}));
                cell.add_child(new Swatch(column.swatch, {styleClass: 'readout-swatch readout-process-swatch'}).actor);
            } else {
                const columnTitle = new St.Label({
                    text: column.title, style_class: 'readout-process-title', x_expand: true,
                    x_align: Clutter.ActorAlign.END,
                });
                faint(columnTitle);
                cell.add_child(columnTitle);
            }
            header.add_child(cell);
        }
        this.actor.add_child(header);
        this._rows = [];
    }

    /**
     * @param {Array} entries - [{name, values: string[]}]
     * @param {number} count - rows to show
     */
    update(entries, count) {
        // Like Stats, always `count` rows, blank when fewer processes are reported, so the popup
        // keeps its height as processes come and go.
        this.actor.visible = count > 0;
        if (!this.actor.visible)
            return;
        while (this._rows.length < count)
            this._rows.push(this._createRow());
        this._rows.forEach((row, index) => {
            row.box.visible = index < count;
            const entry = index < count ? entries[index] : null;
            const name = entry?.name ?? '';
            if (row.name.text !== name) {
                row.name.text = name;
                row.icon.gicon = entry ? this._context.processIcon(name) : null;
            }
            row.values.forEach((label, column) => setText(label, entry?.values[column] ?? ''));
        });
    }

    _createRow() {
        const box = new St.BoxLayout({style_class: 'readout-row readout-process-row', x_expand: true});
        const iconBox = new St.Bin({style_class: 'readout-process-icon-box'});
        const icon = new St.Icon({style_class: 'readout-process-icon'});
        iconBox.set_child(icon);
        box.add_child(iconBox);
        const name = new St.Label({style_class: 'readout-process-name', x_expand: true, y_align: Clutter.ActorAlign.CENTER});
        name.clutter_text.ellipsize = Pango.EllipsizeMode.END;
        dim(name);
        box.add_child(name);
        const values = this._columns.map(() => {
            const value = new St.Label({
                style_class: `readout-process-value readout-tnum${this._columns.length > 1 ? ' readout-process-value-small' : ''}`,
                x_align: Clutter.ActorAlign.END,
                y_align: Clutter.ActorAlign.CENTER,
            });
            const cell = new St.Bin({
                style_class: `readout-process-column${this._columns.length > 1 ? ' readout-process-column-narrow' : ''}`,
                child: value,
                x_align: Clutter.ActorAlign.END,
            });
            value.x_expand = true;
            box.add_child(cell);
            return value;
        });
        this.actor.add_child(box);
        return {box, icon, name, values};
    }
}
