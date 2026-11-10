// Readout preferences window (libadwaita). Everything is stored in GSettings; the
// extension reacts to the changes, so there is no "apply" step.

import Adw from 'gi://Adw';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Gtk from 'gi://Gtk';

import {ExtensionPreferences, gettext as _} from 'resource:///org/gnome/Shell/Extensions/js/extensions/prefs.js';

import {COLOR_MODES, MODULES, MODULE_ORDER, MODULE_STYLES, N_, STYLES, normalizeItem, parseItems} from './lib/catalog.js';

const BUS_NAME = 'io.github.aakash1122.Readout1';
const OBJECT_PATH = '/io/github/aakash1122/Readout1';
const MAX_ITEMS = 16;

const LEVEL_MODULES = ['cpu', 'memory', 'disk', 'gpu', 'battery'];
const COLOR_MODULES = [...LEVEL_MODULES, 'network'];
const PROCESS_MODULES = ['cpu', 'memory', 'disk'];
const SPEED_MODULES = ['network', 'disk'];

// Titles are translated where they are shown: gettext needs the extension to be loaded.
const PANEL_BOXES = [
    {id: 'left', title: N_('Left')},
    {id: 'center', title: N_('Center')},
    {id: 'right', title: N_('Right')},
];

// Popup look choices; ids are the GSettings choice values.
const POPUP_SPACINGS = [
    {id: 'compact', title: N_('Compact')},
    {id: 'normal', title: N_('Normal')},
    {id: 'comfortable', title: N_('Comfortable')},
];
const POPUP_WIDTHS = [
    {id: 'narrow', title: N_('Narrow')},
    {id: 'normal', title: N_('Normal')},
    {id: 'wide', title: N_('Wide')},
];
const POPUP_LOOK_KEYS = ['popup-text-scale', 'popup-spacing', 'popup-margin', 'popup-width'];

// Mirrors lib/alerts.js. Units are shown in the threshold row subtitle.
const ALERTS = [
    {key: 'cpu', title: N_('CPU usage above'), unit: '%', range: [1, 100]},
    {key: 'memory', title: N_('Memory usage above'), unit: '%', range: [1, 100]},
    {key: 'cpu-temperature', title: N_('CPU temperature above'), unit: '°C', range: [30, 120]},
    {key: 'gpu-temperature', title: N_('GPU temperature above'), unit: '°C', range: [30, 120]},
    {key: 'disk-free', title: N_('Disk free space below'), unit: '%', range: [1, 50]},
    {key: 'battery-low', title: N_('Battery level below (discharging)'), unit: '%', range: [1, 100]},
    {key: 'battery-charged', title: N_('Battery level reached (charging)'), unit: '%', range: [1, 100]},
];

/**
 * Calls GetInfo on readoutd. Resolves to null when the service is not available.
 *
 * @returns {Promise<object|null>}
 */
function fetchDaemonInfo() {
    return new Promise(resolve => {
        Gio.DBus.session.call(BUS_NAME, OBJECT_PATH, BUS_NAME, 'GetInfo', null,
            new GLib.VariantType('(a{sv})'), Gio.DBusCallFlags.NONE, 5000, null,
            (connection, result) => {
                try {
                    resolve(connection.call_finish(result).recursiveUnpack()[0]);
                } catch (error) {
                    console.debug(`Readout: GetInfo failed: ${error.message}`);
                    resolve(null);
                }
            });
    });
}

/**
 * @param {string[]} titles - option titles
 * @returns {Gtk.StringList}
 */
function stringModel(titles) {
    return Gtk.StringList.new(titles);
}

function iconButton(iconName, tooltip, onClicked) {
    const button = new Gtk.Button({
        icon_name: iconName,
        tooltip_text: tooltip,
        valign: Gtk.Align.CENTER,
        css_classes: ['flat'],
    });
    button.update_property([Gtk.AccessibleProperty.LABEL], [tooltip]);
    button.connect('clicked', onClicked);
    return button;
}

/**
 * Checks an IANA time zone id, e.g. "Europe/Berlin".
 *
 * @param {string} identifier - time zone id
 * @returns {boolean}
 */
function isValidTimeZone(identifier) {
    try {
        return GLib.TimeZone.new_identifier(identifier) !== null;
    } catch {
        return false;
    }
}

class ItemsPage {
    constructor(settings) {
        this._settings = settings;
        this._info = undefined; // undefined: loading, null: unavailable
        this._items = parseItems(settings.get_string('items'));
        this._lastWritten = null;

        this.page = new Adw.PreferencesPage({
            title: _('Panel Items'),
            icon_name: 'view-list-symbolic',
        });
        this._group = new Adw.PreferencesGroup({
            title: _('Panel Items'),
            description: _('Each item is its own panel button. Add several modules to one item to combine them in one button and popup.'),
        });
        const addButton = new Gtk.Button({
            child: new Adw.ButtonContent({icon_name: 'list-add-symbolic', label: _('Add Item')}),
            valign: Gtk.Align.CENTER,
            css_classes: ['flat'],
        });
        addButton.connect('clicked', () => {
            if (this._items.length >= MAX_ITEMS)
                return;
            this._items.push(normalizeItem({modules: ['cpu']}));
            this._save();
            this._rebuild(this._items.length - 1);
        });
        this._addButton = addButton;
        this._group.set_header_suffix(addButton);
        this.page.add(this._group);
        this._rows = [];

        this._settingsId = settings.connect('changed::items', () => {
            if (settings.get_string('items') === this._lastWritten)
                return;
            this._items = parseItems(settings.get_string('items'));
            this._rebuild(-1);
        });
        this.page.connect('destroy', () => settings.disconnect(this._settingsId));

        this._rebuild(-1);
        fetchDaemonInfo().then(info => {
            this._info = info;
            this._rebuild(this._expandedIndex());
        });
    }

    _expandedIndex() {
        return this._rows.findIndex(row => row.expanded);
    }

    _save() {
        this._lastWritten = JSON.stringify(this._items);
        this._settings.set_string('items', this._lastWritten);
    }

    _rebuild(expandIndex) {
        for (const row of this._rows)
            this._group.remove(row);
        this._rows = this._items.map((item, index) => this._createItemRow(item, index));
        this._rows.forEach((row, index) => {
            row.expanded = index === expandIndex;
            this._group.add(row);
        });
        this._addButton.sensitive = this._items.length < MAX_ITEMS;
    }

    _itemTitle(item) {
        return item.modules.map(module => _(MODULES[module].title)).join(' + ');
    }

    _itemSubtitle(item) {
        return item.modules.map(module => _(STYLES[item.styles[module]].title)).join(', ');
    }

    _createItemRow(item, index) {
        const row = new Adw.ExpanderRow({title: this._itemTitle(item), subtitle: this._itemSubtitle(item)});
        const refreshTitle = () => {
            row.title = this._itemTitle(item);
            row.subtitle = this._itemSubtitle(item);
        };

        const upButton = iconButton('go-up-symbolic', _('Move Up'), () => this._move(index, -1));
        const downButton = iconButton('go-down-symbolic', _('Move Down'), () => this._move(index, 1));
        const removeButton = iconButton('user-trash-symbolic', _('Remove Item'), () => {
            this._items.splice(index, 1);
            this._save();
            this._rebuild(-1);
        });
        upButton.sensitive = index > 0;
        downButton.sensitive = index < this._items.length - 1;
        removeButton.sensitive = this._items.length > 1;
        // Adw.ExpanderRow places each new suffix before the previous ones.
        row.add_suffix(removeButton);
        row.add_suffix(downButton);
        row.add_suffix(upButton);

        // Modules and their styles.
        for (const module of item.modules)
            row.add_row(this._createModuleRow(item, index, module, refreshTitle));
        const missing = MODULE_ORDER.filter(module => !item.modules.includes(module));
        if (missing.length > 0)
            row.add_row(this._createAddModuleRow(item, index, missing));

        // Display options.
        row.add_row(this._switchRow(_('Show Label'), _('Short label such as “CPU”; vertical on charts'),
            item, 'showLabel'));
        row.add_row(this._switchRow(_('Show Icon'), null, item, 'showIcon'));

        const has = modules => item.modules.some(module => modules.includes(module));
        if (has(COLOR_MODULES)) {
            row.add_row(this._comboRow(_('Color'), _('Applies to bars and charts'),
                Object.keys(COLOR_MODES), id => _(COLOR_MODES[id].title), item, 'colorMode'));
        }
        if (has(LEVEL_MODULES)) {
            row.add_row(this._spinRow(_('Warning Threshold'), '%', 1, 100, item, 'warning'));
            row.add_row(this._spinRow(_('Critical Threshold'), '%', 1, 100, item, 'critical'));
        }
        if (has(PROCESS_MODULES))
            row.add_row(this._spinRow(_('Processes in Popup'), null, 0, 10, item, 'processCount'));
        if (has(SPEED_MODULES))
            row.add_row(this._switchRow(_('Speeds in Bits'), _('Show Mb/s instead of MB/s'), item, 'bits'));
        if (item.modules.includes('gpu'))
            row.add_row(this._createGpuRow(item));
        if (item.modules.includes('sensors'))
            row.add_row(this._createSensorsRow(item));
        if (item.modules.includes('clock')) {
            row.add_row(this._switchRow(_('24-Hour Time'), null, item, 'clock24'));
            row.add_row(this._switchRow(_('Show Seconds'), null, item, 'clockSeconds'));
            row.add_row(this._createTimeZonesRow(item));
        }
        return row;
    }

    _move(index, delta) {
        const target = index + delta;
        if (target < 0 || target >= this._items.length)
            return;
        const [item] = this._items.splice(index, 1);
        this._items.splice(target, 0, item);
        this._save();
        this._rebuild(target);
    }

    _createModuleRow(item, index, module, refreshTitle) {
        const styles = MODULE_STYLES[module];
        const row = new Adw.ComboRow({
            title: _(MODULES[module].title),
            subtitle: _('Widget style'),
            model: stringModel(styles.map(style => _(STYLES[style].title))),
            selected: Math.max(0, styles.indexOf(item.styles[module])),
        });
        row.sensitive = styles.length > 1 || item.modules.length > 1;
        row.connect('notify::selected', () => {
            item.styles[module] = styles[row.selected];
            refreshTitle();
            this._save();
        });
        if (item.modules.length > 1) {
            row.add_suffix(iconButton('list-remove-symbolic', _('Remove Module'), () => {
                item.modules = item.modules.filter(candidate => candidate !== module);
                delete item.styles[module];
                this._items[index] = normalizeItem(item);
                this._save();
                this._rebuild(index);
            }));
        }
        return row;
    }

    _createAddModuleRow(item, index, missing) {
        const row = new Adw.ActionRow({
            title: _('Add Module'),
            subtitle: _('Combine another module into this item'),
        });
        const dropDown = new Gtk.DropDown({
            model: stringModel(missing.map(module => _(MODULES[module].title))),
            valign: Gtk.Align.CENTER,
        });
        row.add_suffix(dropDown);
        row.add_suffix(iconButton('list-add-symbolic', _('Add Module'), () => {
            const module = missing[dropDown.selected];
            if (!module)
                return;
            item.modules = [...item.modules, module];
            this._items[index] = normalizeItem(item);
            this._save();
            this._rebuild(index);
        }));
        return row;
    }

    _switchRow(title, subtitle, item, key) {
        const row = new Adw.SwitchRow({title, active: item[key]});
        if (subtitle)
            row.subtitle = subtitle;
        row.connect('notify::active', () => {
            item[key] = row.active;
            this._save();
        });
        return row;
    }

    _comboRow(title, subtitle, ids, titleFor, item, key) {
        const row = new Adw.ComboRow({
            title,
            subtitle,
            model: stringModel(ids.map(titleFor)),
            selected: Math.max(0, ids.indexOf(item[key])),
        });
        row.connect('notify::selected', () => {
            item[key] = ids[row.selected];
            this._save();
        });
        return row;
    }

    _spinRow(title, subtitle, minimum, maximum, item, key) {
        const row = Adw.SpinRow.new_with_range(minimum, maximum, 1);
        row.title = title;
        if (subtitle)
            row.subtitle = subtitle;
        row.value = item[key];
        row.connect('notify::value', () => {
            item[key] = Math.round(row.value);
            this._save();
        });
        return row;
    }

    _createGpuRow(item) {
        const gpus = this._info?.gpu?.gpus ?? [];
        const ids = ['', ...gpus.map(gpu => gpu.id)];
        if (item.gpu && !ids.includes(item.gpu))
            ids.push(item.gpu);
        const titleFor = id => {
            if (id === '')
                return _('Automatic');
            return gpus.find(gpu => gpu.id === id)?.name ?? id;
        };
        return this._comboRow(_('GPU'), _('Automatic picks the first non-secondary GPU'), ids, titleFor,
            item, 'gpu');
    }

    _createSensorsRow(item) {
        const row = new Adw.ExpanderRow({
            title: _('Panel Sensors'),
            subtitle: item.sensors.length > 0
                ? _('%d selected').format(item.sensors.length)
                : _('CPU and GPU temperature'),
        });
        const updateSubtitle = () => {
            row.subtitle = item.sensors.length > 0
                ? _('%d selected').format(item.sensors.length)
                : _('CPU and GPU temperature');
        };
        if (this._info === undefined) {
            row.add_row(new Adw.ActionRow({title: _('Loading sensors…')}));
            return row;
        }
        const sensors = this._info?.sensors?.sensors ?? [];
        if (sensors.length === 0) {
            row.add_row(new Adw.ActionRow({
                title: this._info === null
                    ? _('The Readout service (readoutd) is not available')
                    : _('No sensors found'),
            }));
            return row;
        }
        const sorted = [...sensors].sort((first, second) =>
            `${first.group}\u0000${first.label}`.localeCompare(`${second.group}\u0000${second.label}`));
        for (const sensor of sorted) {
            const check = new Gtk.CheckButton({
                active: item.sensors.includes(sensor.id),
                valign: Gtk.Align.CENTER,
            });
            const sensorRow = new Adw.ActionRow({
                title: GLib.markup_escape_text(sensor.label, -1),
                subtitle: GLib.markup_escape_text(`${sensor.group} · ${sensor.chip}`, -1),
                activatable_widget: check,
            });
            sensorRow.add_prefix(check);
            check.connect('toggled', () => {
                if (check.active && !item.sensors.includes(sensor.id)) {
                    if (item.sensors.length >= 16) {
                        check.active = false;
                        return;
                    }
                    item.sensors = [...item.sensors, sensor.id];
                } else if (!check.active) {
                    item.sensors = item.sensors.filter(id => id !== sensor.id);
                }
                updateSubtitle();
                this._save();
            });
            row.add_row(sensorRow);
        }
        return row;
    }

    _createTimeZonesRow(item) {
        const row = new Adw.EntryRow({
            title: _('Extra Time Zones (comma separated, e.g. Europe/Berlin)'),
            text: item.timezones.join(', '),
            show_apply_button: true,
        });
        row.connect('apply', () => {
            const zones = row.text.split(',').map(zone => zone.trim()).filter(zone => zone.length > 0);
            const invalid = zones.filter(zone => !isValidTimeZone(zone));
            if (invalid.length > 0) {
                row.add_css_class('error');
                return;
            }
            row.remove_css_class('error');
            item.timezones = zones.slice(0, 8);
            row.text = item.timezones.join(', ');
            this._save();
        });
        row.connect('changed', () => row.remove_css_class('error'));
        return row;
    }
}

function createGeneralPage(settings) {
    const page = new Adw.PreferencesPage({title: _('General'), icon_name: 'preferences-system-symbolic'});

    const panelGroup = new Adw.PreferencesGroup({title: _('Panel')});
    const interval = Adw.SpinRow.new_with_range(1, 10, 1);
    interval.title = _('Update Interval');
    interval.subtitle = _('Seconds between updates for all items');
    settings.bind('update-interval', interval, 'value', Gio.SettingsBindFlags.DEFAULT);
    panelGroup.add(interval);

    panelGroup.add(choiceRow(settings, page, 'panel-box', PANEL_BOXES,
        {title: _('Position'), subtitle: _('Area of the top bar that holds the items')}));
    page.add(panelGroup);

    page.add(createPopupGroup(settings, page));

    const serviceGroup = new Adw.PreferencesGroup({title: _('System Service')});
    const serviceRow = new Adw.ActionRow({title: _('readoutd'), subtitle: _('Checking…')});
    serviceGroup.add(serviceRow);
    page.add(serviceGroup);
    fetchDaemonInfo().then(info => {
        serviceRow.subtitle = info
            ? _('Version %s').format(info.version ?? '?')
            : _('Not installed or not running. Install the “readout” package from your distribution.');
    });
    return page;
}

/**
 * A combo row bound to a string key with fixed choices.
 *
 * @param {Gio.Settings} settings - extension settings
 * @param {Adw.PreferencesPage} page - disconnects the handler when destroyed
 * @param {string} key - GSettings key
 * @param {object[]} choices - [{id, title}]
 * @param {object} labels - {title, subtitle}
 * @returns {Adw.ComboRow}
 */
function choiceRow(settings, page, key, choices, labels) {
    const row = new Adw.ComboRow({...labels, model: stringModel(choices.map(choice => _(choice.title)))});
    const sync = () => {
        row.selected = Math.max(0, choices.findIndex(choice => choice.id === settings.get_string(key)));
    };
    sync();
    row.connect('notify::selected', () => {
        const id = choices[row.selected]?.id;
        if (id && settings.get_string(key) !== id)
            settings.set_string(key, id);
    });
    const changedId = settings.connect(`changed::${key}`, sync);
    page.connect('destroy', () => settings.disconnect(changedId));
    return row;
}

function createPopupGroup(settings, page) {
    const group = new Adw.PreferencesGroup({
        title: _('Popup'),
        description: _('Shared by all Readout items.'),
    });

    const textScale = Adw.SpinRow.new_with_range(80, 130, 5);
    textScale.title = _('Text Size');
    textScale.subtitle = _('Percent of the shell font; charts scale with it');
    settings.bind('popup-text-scale', textScale, 'value', Gio.SettingsBindFlags.DEFAULT);
    group.add(textScale);

    group.add(choiceRow(settings, page, 'popup-spacing', POPUP_SPACINGS,
        {title: _('Spacing'), subtitle: _('Height of rows and gaps between sections')}));

    const margin = Adw.SpinRow.new_with_range(0, 16, 1);
    margin.title = _('Margins');
    margin.subtitle = _('Space around the content, in points');
    settings.bind('popup-margin', margin, 'value', Gio.SettingsBindFlags.DEFAULT);
    group.add(margin);

    group.add(choiceRow(settings, page, 'popup-width', POPUP_WIDTHS, {title: _('Width')}));

    const reset = new Adw.ButtonRow({title: _('Reset Popup Look')});
    reset.connect('activated', () => {
        for (const key of POPUP_LOOK_KEYS)
            settings.reset(key);
    });
    group.add(reset);
    return group;
}

function createAlertsPage(settings) {
    const page = new Adw.PreferencesPage({title: _('Alerts'), icon_name: 'preferences-system-notifications-symbolic'});
    const group = new Adw.PreferencesGroup({
        title: _('Alerts'),
        description: _('An alert is sent once a condition holds for 10 seconds. It can fire again after the value goes back past the threshold.'),
    });
    for (const alert of ALERTS) {
        const row = new Adw.ExpanderRow({title: _(alert.title), show_enable_switch: true});
        settings.bind(`alert-${alert.key}-enabled`, row, 'enable-expansion', Gio.SettingsBindFlags.DEFAULT);
        const threshold = Adw.SpinRow.new_with_range(alert.range[0], alert.range[1], 1);
        threshold.title = _('Threshold');
        threshold.subtitle = alert.unit;
        settings.bind(`alert-${alert.key}-threshold`, threshold, 'value', Gio.SettingsBindFlags.DEFAULT);
        row.add_row(threshold);
        group.add(row);
    }
    const connectivity = new Adw.SwitchRow({title: _('Network connectivity lost')});
    settings.bind('alert-connectivity-enabled', connectivity, 'active', Gio.SettingsBindFlags.DEFAULT);
    group.add(connectivity);
    page.add(group);
    return page;
}

export default class ReadoutPreferences extends ExtensionPreferences {
    fillPreferencesWindow(window) {
        const settings = this.getSettings();
        window.set_default_size(640, 720);
        window.add(new ItemsPage(settings).page);
        window.add(createGeneralPage(settings));
        window.add(createAlertsPage(settings));
        // Keep the settings object alive as long as the window.
        window._readoutSettings = settings;
    }
}
