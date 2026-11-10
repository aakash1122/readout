// Readout: system monitor in the GNOME Shell top bar.
// Data comes from readoutd over D-Bus (docs/dbus-api.md); this extension only draws it.

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Shell from 'gi://Shell';

import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';

import {AlertMonitor} from './lib/alerts.js';
import {parseItems} from './lib/catalog.js';
import {ReadoutClient} from './lib/dbusClient.js';
import {ReadoutButton} from './lib/panelItem.js';

const MAX_PROCESS_ICONS = 256;
const POPUP_LOOK_KEYS = ['popup-text-scale', 'popup-spacing', 'popup-margin', 'popup-width'];

export default class ReadoutExtension extends Extension {
    enable() {
        this._settings = this.getSettings();
        this._icons = new Map();
        this._processIcons = new Map();
        this._client = new ReadoutClient(this._settings.get_uint('update-interval'));
        this._context = {
            client: this._client,
            icon: name => this._icon(name),
            processIcon: name => this._processIcon(name),
            openPreferences: () => this.openPreferences(),
            popupLook: this._readPopupLook(),
            popupHeights: new Map(Object.entries(this._settings.get_value('popup-heights').deepUnpack())),
            savePopupHeights: () => this._savePopupHeights(),
        };
        this._buttons = [];
        this._alerts = new AlertMonitor(this._client, this._settings);
        this._settingsIds = [
            this._settings.connect('changed::items', () => this._rebuild()),
            this._settings.connect('changed::panel-box', () => this._rebuild()),
            this._settings.connect('changed::update-interval', () =>
                this._client.setInterval(this._settings.get_uint('update-interval'))),
            ...POPUP_LOOK_KEYS.map(key => this._settings.connect(`changed::${key}`, () => this._updatePopupLook())),
        ];
        this._rebuild();
    }

    disable() {
        for (const id of this._settingsIds)
            this._settings.disconnect(id);
        this._settingsIds = null;
        this._destroyButtons();
        this._alerts.destroy();
        this._alerts = null;
        this._client.destroy();
        this._client = null;
        this._context = null;
        this._icons = null;
        this._processIcons = null;
        this._settings = null;
    }

    _icon(name) {
        let icon = this._icons.get(name);
        if (!icon) {
            // Our own symbolic icons ship in icons/; everything else comes from the icon theme.
            icon = name.startsWith('readout-')
                ? Gio.FileIcon.new(this.dir.get_child('icons').get_child(`${name}.svg`))
                : Gio.ThemedIcon.new(name);
            this._icons.set(name, icon);
        }
        return icon;
    }

    // Process list icons: the app's icon when a desktop entry matches, else an icon named
    // like the process, else a generic executable icon.
    _processIcon(name) {
        let icon = this._processIcons.get(name);
        if (icon)
            return icon;
        const appSystem = Shell.AppSystem.get_default();
        const app = appSystem.lookup_app(`${name}.desktop`) ??
            appSystem.lookup_startup_wmclass(name) ??
            appSystem.lookup_desktop_wmclass(name);
        icon = app?.get_icon() ?? Gio.ThemedIcon.new_from_names([name.toLowerCase(), 'application-x-executable']);
        if (this._processIcons.size >= MAX_PROCESS_ICONS)
            this._processIcons.clear();
        this._processIcons.set(name, icon);
        return icon;
    }

    // Saved when a popup closes, and only when a height changed.
    _savePopupHeights() {
        const heights = Object.fromEntries(this._context.popupHeights);
        const value = new GLib.Variant('a{su}', heights);
        if (!value.equal(this._settings.get_value('popup-heights')))
            this._settings.set_value('popup-heights', value);
    }

    _readPopupLook() {
        return {
            textScale: this._settings.get_uint('popup-text-scale'),
            spacing: this._settings.get_string('popup-spacing'),
            margin: this._settings.get_uint('popup-margin'),
            width: this._settings.get_string('popup-width'),
        };
    }

    // Shared by all items; applied in place so open popups and per-item state survive.
    _updatePopupLook() {
        this._context.popupLook = this._readPopupLook();
        for (const button of this._buttons)
            button.setPopupLook(this._context.popupLook);
    }

    _destroyButtons() {
        for (const button of this._buttons)
            button.destroy();
        this._buttons = [];
    }

    _rebuild() {
        this._destroyButtons();
        const box = this._settings.get_string('panel-box');
        const items = parseItems(this._settings.get_string('items'));
        items.forEach((item, index) => {
            const role = `readout-${index}`;
            const button = new ReadoutButton(role, item, this._context);
            Main.panel.addToStatusArea(role, button, index, ['left', 'center', 'right'].includes(box) ? box : 'right');
            this._buttons.push(button);
        });
    }
}
