// Compact module summaries for combined items (docs/visual-spec.md section 6.4).
//
// The overview stacks one summary per module: a header row with the module name and a "›"
// button that opens the module's full page, then a ring (or the battery icon) beside up to
// four key/value rows. Summaries use panel-tier data, so the overview costs no detail sampling.

import Clutter from 'gi://Clutter';
import GLib from 'gi://GLib';
import St from 'gi://St';

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';

import {MODULES} from './catalog.js';
import {BatteryIcon, MirroredChart, Ring} from './charts.js';
import {Rows, overlay, setText} from './components.js';
import * as Format from './format.js';
import {formatClock, timeZoneFor, zoneShortName} from './panelWidgets.js';
import {memorySegments} from './popups.js';
import * as Values from './values.js';

const CHART_POINTS = 90;

/**
 * @param {string} module - module name
 * @param {object} item - panel item options
 * @param {object} context - {client}
 * @param {Function} onOpen - called with the module name when "›" is clicked
 * @returns {Summary}
 */
export function createSummary(module, item, context, onOpen) {
    return new Summary(module, item, context, onOpen);
}

class Summary {
    constructor(module, item, context, onOpen) {
        this.module = module;
        this._item = item;
        this._context = context;
        this._destroyed = false;
        this._timerId = 0;
        this.actor = new St.BoxLayout({
            orientation: Clutter.Orientation.VERTICAL,
            style_class: 'readout-summary',
            x_expand: true,
        });
        this.actor.connect('destroy', () => {
            this._destroyed = true;
        });

        const header = new St.BoxLayout({style_class: 'readout-summary-header', x_expand: true});
        header.add_child(new St.Label({
            text: _(MODULES[module].title), style_class: 'readout-summary-title', x_expand: true,
            y_align: Clutter.ActorAlign.CENTER,
        }));
        const open = new St.Button({
            label: '›', style_class: 'readout-summary-open', can_focus: true,
            accessible_name: _('Show %s details').format(_(MODULES[module].title)),
            y_align: Clutter.ActorAlign.CENTER,
        });
        open.connect('clicked', () => onOpen(module));
        header.add_child(open);
        this.actor.add_child(header);

        const body = new St.BoxLayout({style_class: 'readout-summary-body', x_expand: true});
        this.actor.add_child(body);
        if (['cpu', 'gpu', 'memory', 'disk'].includes(module)) {
            this._ring = new Ring();
            const {actor, label} = overlay(this._ring.actor, 'readout-ring-text');
            this._ringLabel = label;
            actor.y_align = Clutter.ActorAlign.CENTER;
            body.add_child(actor);
        } else if (module === 'battery') {
            this._battery = new BatteryIcon();
            this._battery.actor.add_style_class_name('readout-summary-battery');
            this._battery.actor.y_align = Clutter.ActorAlign.CENTER;
            body.add_child(this._battery.actor);
        }
        const column = new St.BoxLayout({
            orientation: Clutter.Orientation.VERTICAL, x_expand: true, y_align: Clutter.ActorAlign.CENTER,
        });
        body.add_child(column);
        this._rows = new Rows();
        column.add_child(this._rows.actor);
        if (module === 'network' || module === 'disk') {
            this._chart = new MirroredChart({capacity: CHART_POINTS, styleClass: 'readout-chart-40'});
            this.actor.add_child(this._chart.actor);
            this._after = new Rows();
            this.actor.add_child(this._after.actor);
        }
    }

    opened() {
        if (this._chart) {
            this._context.client.getHistory(this.module).then(history => {
                if (this._destroyed || !history)
                    return;
                const [top, bottom] = this.module === 'disk' ? ['write', 'read'] : ['upload', 'download'];
                if (history[top]?.length > 0 || history[bottom]?.length > 0)
                    this._chart.setPoints(history[top] ?? [], history[bottom] ?? []);
            });
        }
        if (this.module === 'clock')
            this._tick();
    }

    closed() {
        if (this._timerId)
            GLib.source_remove(this._timerId);
        this._timerId = 0;
    }

    _tick() {
        this.closed();
        const now = GLib.DateTime.new_now_local();
        this._rows.set('local', formatClock(now, this._item, true), {label: _('Local time')});
        for (const zone of this._item.timezones) {
            const zoned = GLib.DateTime.new_now(timeZoneFor(zone));
            this._rows.set(zone, formatClock(zoned, this._item, true), {label: zoneShortName(zone)});
        }
        const delay = 1000 - Math.floor(now.get_microsecond() / 1000) + 5;
        this._timerId = GLib.timeout_add(GLib.PRIORITY_DEFAULT, delay, () => {
            this._timerId = 0;
            this._tick();
            return GLib.SOURCE_REMOVE;
        });
    }

    /** @param {object} payload - module payload (panel or detail tier) */
    update(payload) {
        if (!payload)
            return;
        const rows = this._rows;
        const item = this._item;
        switch (this.module) {
        case 'cpu':
            this._ring.setSegments([
                {value: (payload.system ?? 0) / 100, color: 'red'},
                {value: (payload.user ?? 0) / 100, color: 'blue'},
            ]);
            setText(this._ringLabel, Format.percent(payload.usage));
            rows.set('usage', Format.percent(payload.usage), {label: _('Usage')});
            rows.set('idle', Format.percent(100 - (payload.usage ?? 0)), {label: _('Idle')});
            rows.set('efficiency', Number.isFinite(payload.efficiencyUsage) ? Format.percent(payload.efficiencyUsage) : null,
                {label: _('E-cores'), swatch: 'teal'});
            rows.set('performance', Number.isFinite(payload.performanceUsage) ? Format.percent(payload.performanceUsage) : null,
                {label: _('P-cores'), swatch: 'indigo'});
            break;
        case 'gpu': {
            const gpu = Values.selectedGpu(payload, item);
            const active = gpu?.state === 'active';
            this._ring.setSegments([{value: active ? (gpu.usage ?? 0) / 100 : 0, color: 'blue'}]);
            setText(this._ringLabel, active ? Format.percent(gpu.usage) : Format.DASH);
            rows.set('usage', active ? Format.percent(gpu.usage) : gpu?.state === 'suspended' ? _('Sleeping') : Format.DASH,
                {label: _('Usage')});
            rows.set('temperature', active && Number.isFinite(gpu.temperature) ? Format.temperature(gpu.temperature) : null,
                {label: _('Temperature')});
            rows.set('memory', active && gpu.memoryTotal > 0 ? Format.size(gpu.memoryUsed) : null, {label: _('Memory used')});
            break;
        }
        case 'memory':
            this._ring.setSegments(memorySegments(payload));
            setText(this._ringLabel, Format.percent(payload.usage));
            rows.set('used', Format.size(payload.used), {label: _('Used')});
            rows.set('free', Format.size(payload.free ?? payload.available), {label: _('Free')});
            rows.set('swap', Format.size(payload.swapUsed ?? 0), {label: _('Swap')});
            rows.set('pressure', payload.pressureLevel ? payload.pressureLevel === 'normal' ? _('Normal')
                : payload.pressureLevel === 'warning' ? _('Warning') : _('Critical') : null, {label: _('Pressure')});
            break;
        case 'disk': {
            const usage = payload.total > 0 ? payload.used * 100 / payload.total : payload.usage;
            this._ring.setSegments([{value: (usage ?? 0) / 100, color: 'blue'}]);
            setText(this._ringLabel, Format.percent(usage));
            rows.set('name', payload.mountPoint || Format.DASH, {label: _('Name')});
            rows.set('used', Format.diskSize(payload.used), {label: _('Used')});
            rows.set('free', Format.diskSize(payload.free), {label: _('Free')});
            this._chart.push(payload.write, payload.read);
            this._after.set('write', Format.speed(payload.write, item.bits), {label: _('Write'), swatch: 'red'});
            this._after.set('read', Format.speed(payload.read, item.bits), {label: _('Read'), swatch: 'blue'});
            break;
        }
        case 'network': {
            rows.set('upload', Format.speed(payload.upload, item.bits), {label: _('Upload'), swatch: 'red'});
            rows.set('download', Format.speed(payload.download, item.bits), {label: _('Download'), swatch: 'blue'});
            this._chart.push(payload.upload, payload.download);
            const primary = (payload.interfaces ?? []).find(entry => entry.name === payload.interface);
            const address = primary?.ipv4?.[0] ?? primary?.ipv6?.[0] ?? null;
            this._after.set('local', address, {label: _('Local IP')});
            break;
        }
        case 'sensors': {
            const entries = this._sensorRows(payload);
            entries.forEach(entry => rows.set(entry.key, entry.value, {label: entry.label}));
            rows.set('none', entries.length === 0 ? Format.DASH : null, {label: _('Temperature')});
            break;
        }
        case 'battery': {
            const present = Boolean(payload.present);
            this._battery.setLevel(present ? payload.percentage : NaN);
            rows.set('level', present ? Format.percent(payload.percentage) : _('No battery'), {label: _('Level')});
            rows.set('status', present ? Values.batteryStateText(payload.state) : null, {label: _('Status')});
            const health = payload.batteries?.[0]?.health;
            rows.set('health', health > 0 ? Format.percent(health) : null, {label: _('Health')});
            break;
        }
        case 'bluetooth': {
            const devices = (payload.devices ?? []).filter(device => device.connected).slice(0, 4);
            const keys = new Set();
            for (const device of devices) {
                keys.add(device.address);
                rows.set(device.address, Number.isFinite(device.battery) ? Format.percent(device.battery) : _('Connected'),
                    {label: device.name});
            }
            keys.add('none');
            rows.retain(keys);
            rows.set('none', devices.length === 0 ? payload.powered ? _('None') : _('Off') : null,
                {label: _('Devices')});
            break;
        }
        }
    }

    _sensorRows(payload) {
        if (this._item.sensors.length > 0) {
            return this._item.sensors
                .map(id => payload.sensors?.find(sensor => sensor.id === id))
                .filter(Boolean)
                .slice(0, 4)
                .map(sensor => ({key: sensor.id, label: sensor.label, value: Format.sensorValue(sensor)}));
        }
        const entries = [];
        if (Number.isFinite(payload.cpuTemperature))
            entries.push({key: 'cpu', label: _('CPU'), value: Format.temperature(payload.cpuTemperature)});
        if (Number.isFinite(payload.gpuTemperature))
            entries.push({key: 'gpu', label: _('GPU'), value: Format.temperature(payload.gpuTemperature)});
        return entries;
    }

    destroy() {
        this.closed();
        this._destroyed = true;
        this.actor.destroy();
    }
}
