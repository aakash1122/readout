// Module pages in the Stats layout (docs/visual-spec.md section 2, adapted per section 6.5).
//
// A page is created the first time it is shown and receives updates only while it is
// visible (its module is at the "detail" tier then). Opening a page fetches the module's
// history again, so charts have no gap from the time spent in the panel tier.

import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Pango from 'gi://Pango';
import Shell from 'gi://Shell';
import Soup from 'gi://Soup?version=3.0';
import St from 'gi://St';

import {gettext as _, pgettext as C_} from 'resource:///org/gnome/shell/extensions/extension.js';

import {MODULES, N_} from './catalog.js';
import {
    AnalogClock, BatteryIcon, CapacityBar, CoreBars, Gauge, GridChart, LineChart, MirroredChart, PressureGauge,
    Ring, Series, Swatch,
} from './charts.js';
import {ProcessList, Rows, Separator, card, dim, faint, iconButton, overlay, setText, withColon} from './components.js';
import * as Format from './format.js';
import {formatClock, timeZoneFor, zoneShortName} from './panelWidgets.js';
import * as Values from './values.js';

const HISTORY_POINTS = 180;
const CARD_HISTORY_POINTS = 120;

/**
 * @param {string} module - module name
 * @param {object} item - panel item options
 * @param {object} context - {client, icon(name), processIcon(name), closeMenu()}
 * @param {object} shared - per-item state kept while the popup is closed
 * @returns {Page}
 */
export function createPage(module, item, context, shared) {
    const classes = {
        cpu: CpuPage,
        memory: MemoryPage,
        disk: DiskPage,
        network: NetworkPage,
        gpu: GpuPage,
        sensors: SensorsPage,
        battery: BatteryPage,
        bluetooth: BluetoothPage,
        clock: ClockPage,
    };
    return new classes[module](module, item, context, shared);
}

class Page {
    constructor(module, item, context, shared) {
        this.module = module;
        this.item = item;
        this.context = context;
        this.shared = shared;
        this.destroyed = false;
        this.actor = new St.BoxLayout({
            orientation: Clutter.Orientation.VERTICAL,
            style_class: `readout-page readout-page-${module}`,
            x_expand: true,
        });
        // History replies can arrive after the page is gone, whatever destroyed it.
        this.actor.connect('destroy', () => {
            this.destroyed = true;
        });
    }

    get title() {
        return _(MODULES[this.module].title);
    }

    add(actor) {
        this.actor.add_child(actor);
        return actor;
    }

    section(title, button = null) {
        const separator = new Separator(title, button);
        this.add(separator.actor);
        return separator;
    }

    rows(styleClass = '') {
        const rows = new Rows({styleClass});
        this.add(rows.actor);
        return rows;
    }

    // Called each time the page becomes visible.
    opened() {
        this.refreshHistory();
    }

    // Called when the page is hidden or the menu closes.
    closed() {}

    refreshHistory() {
        if (!this.historyModule)
            return;
        this.context.client.getHistory(this.historyModule).then(history => {
            if (!this.destroyed && history)
                this.applyHistory(history);
        });
    }

    get historyModule() {
        return null;
    }

    applyHistory() {}

    update() {}

    processEntries(payload, valuesOf) {
        return (payload.processes ?? []).map(process => ({name: process.name, values: valuesOf(process)}));
    }

    destroy() {
        this.closed();
        this.destroyed = true;
        this.actor.destroy();
    }
}

/**
 * The 90 pt dashboard: equal columns, each part centered in its column.
 *
 * @param {Array} parts - actors (null leaves a column empty)
 * @returns {St.BoxLayout}
 */
function dashboard(parts) {
    const board = new St.BoxLayout({style_class: 'readout-dashboard', x_expand: true});
    board.layout_manager.homogeneous = true;
    for (const part of parts) {
        if (part) {
            part.x_align = Clutter.ActorAlign.CENTER;
            part.y_align = Clutter.ActorAlign.CENTER;
        }
        board.add_child(new St.Bin({x_expand: true, child: part}));
    }
    return board;
}

function gaugeWithText(styleClass = 'readout-gauge', color = 'blue') {
    const gauge = new Gauge({styleClass, color});
    const {actor, label} = overlay(gauge.actor, 'readout-gauge-text');
    return {gauge, actor, label};
}

function ringWithText() {
    const ring = new Ring();
    const {actor, label} = overlay(ring.actor, 'readout-ring-text');
    return {ring, actor, label};
}

function message(text) {
    const label = new St.Label({text, style_class: 'readout-message', x_align: Clutter.ActorAlign.CENTER});
    label.clutter_text.line_wrap = true;
    label.clutter_text.ellipsize = Pango.EllipsizeMode.NONE;
    dim(label);
    return label;
}

// ---- CPU ----------------------------------------------------------------------------------

class CpuPage extends Page {
    constructor(...args) {
        super(...args);
        this._temperature = gaugeWithText();
        this._temperature.actor.visible = false;
        this._ring = ringWithText();
        this._frequency = gaugeWithText();
        this._frequency.actor.visible = false;
        this.add(dashboard([this._temperature.actor, this._ring.actor, this._frequency.actor]));

        this.section(_('Usage history'));
        this._chart = new LineChart({capacity: HISTORY_POINTS, styleClass: 'readout-chart-70'});
        this.add(this._chart.actor);
        this._bars = new CoreBars();
        this._bars.actor.add_style_class_name('readout-chart-50 readout-chart-gap');
        this.add(this._bars.actor);

        this.section(_('Details'));
        this._details = this.rows();
        this.section(_('Average load'));
        this._load = this.rows();
        this._frequencySection = this.section(_('Frequency'));
        this._frequencySection.actor.visible = false;
        this._frequencyRows = this.rows();
        this._processes = new ProcessList(this.context, [{title: _('Usage')}]);
        this.add(this._processes.actor);
    }

    get historyModule() {
        return 'cpu';
    }

    applyHistory(history) {
        if (history.usage?.length > 0)
            this._chart.setPoints(history.usage);
    }

    update(payload) {
        const info = this.context.client.info?.cpu ?? {};
        const types = info.hybrid && Array.isArray(info.coreTypes) ? info.coreTypes : [];

        // Dashboard.
        this._ring.ring.setSegments([
            {value: (payload.system ?? 0) / 100, color: 'red'},
            {value: (payload.user ?? 0) / 100, color: 'blue'},
        ]);
        setText(this._ring.label, Format.percent(payload.usage));
        const hasTemperature = Number.isFinite(payload.temperature);
        this._temperature.actor.visible = hasTemperature;
        if (hasTemperature) {
            this._temperature.gauge.setFraction(payload.temperature / 100);
            setText(this._temperature.label, Format.temperature(payload.temperature));
        }
        const hasFrequency = Number.isFinite(payload.frequency) && payload.frequency > 0;
        // The frequency only comes with detail data; when the CPU reports a maximum, its gauge and
        // rows keep their place from the first frame instead of appearing half a second later.
        const showsFrequency = hasFrequency || info.maxFrequency > 0;
        this._frequency.actor.visible = showsFrequency;
        if (!hasFrequency) {
            this._frequency.gauge.setFraction(0);
            setText(this._frequency.label, '');
        } else {
            this._maxFrequency = Math.max(info.maxFrequency || 0, this._maxFrequency ?? 0, payload.frequency);
            this._frequency.gauge.setFraction(payload.frequency / this._maxFrequency);
            setText(this._frequency.label, Format.gigahertz(payload.frequency));
        }

        // Usage history.
        this._chart.push(payload.usage);
        const cores = payload.cores ?? [];
        this._bars.actor.visible = cores.length > 0;
        if (cores.length > 0) {
            const colors = cores.map((_value, index) => {
                if (types[index] === 'efficiency')
                    return 'teal';
                return types[index] === 'performance' ? 'indigo' : 'blue';
            });
            this._bars.setValues(cores, colors);
        }

        // Details.
        const details = this._details;
        details.set('system', Format.percent(payload.system), {label: _('System'), swatch: 'red'});
        details.set('user', Format.percent(payload.user), {label: _('User'), swatch: 'blue'});
        details.set('idle', Format.percent(100 - (payload.usage ?? 0)),
            {label: _('Idle'), swatch: 'lightGray', swatchAlpha: 0.5});
        const hybrid = Number.isFinite(payload.efficiencyUsage);
        details.set('efficiency', hybrid ? Format.percent(payload.efficiencyUsage) : null,
            {label: _('Efficiency cores'), swatch: 'teal'});
        details.set('performance', hybrid ? Format.percent(payload.performanceUsage) : null,
            {label: _('Performance cores'), swatch: 'indigo'});
        details.set('iowait', Number.isFinite(payload.ioWait) ? Format.percent(payload.ioWait) : null,
            {label: _('I/O wait')});
        details.set('processes', Number.isFinite(payload.processCount) ? `${payload.processCount}` : null,
            {label: _('Processes')});
        details.set('threads', Number.isFinite(payload.threadCount) ? `${payload.threadCount}` : null,
            {label: _('Threads')});
        details.set('uptime', Number.isFinite(payload.uptime) ? Format.duration(payload.uptime) : null,
            {label: _('Uptime'), compact: true});

        // Average load.
        const load = value => Number.isFinite(value) ? value.toFixed(2) : Format.DASH;
        this._load.set('1', load(payload.load1), {label: _('1 minute')});
        this._load.set('5', load(payload.load5), {label: _('5 minutes')});
        this._load.set('15', load(payload.load15), {label: _('15 minutes')});

        if (showsFrequency && !hasFrequency) {
            this._frequencySection.actor.visible = true;
            this._frequencyRows.set('all', Format.DASH, {label: _('All cores')});
        }
        if (hasFrequency) {
            this._frequencySection.actor.visible = true;
            this._frequencyRows.set('all', Format.frequency(payload.frequency), {label: _('All cores')});
            const frequencies = payload.frequencies ?? [];
            if (types.length === frequencies.length && types.includes('efficiency')) {
                const average = type => {
                    const values = frequencies.filter((_value, index) => types[index] === type);
                    return values.length ? values.reduce((sum, value) => sum + value, 0) / values.length : NaN;
                };
                this._frequencyRows.set('efficiency', Format.frequency(average('efficiency')),
                    {label: _('Efficiency cores'), swatch: 'teal'});
                this._frequencyRows.set('performance', Format.frequency(average('performance')),
                    {label: _('Performance cores'), swatch: 'indigo'});
            }
        }

        this._processes.update(this.processEntries(payload,
            process => [`${(process.cpu ?? 0).toFixed(1)}%`]), this.item.processCount);
    }
}

// ---- Memory -------------------------------------------------------------------------------

/**
 * Splits memory into the Stats ring segments; "kernel" replaces macOS "wired".
 *
 * @param {object} payload - memory payload
 * @returns {object} {app, kernel, compressed, total} in bytes, app NaN in the panel tier
 */
export function memoryParts(payload) {
    const total = payload.total || 1;
    const {app} = payload;
    const compressed = payload.compressed ?? 0;
    const kernel = Number.isFinite(app) ? Math.max(0, payload.used - app - compressed) : NaN;
    return {app, kernel, compressed, total};
}

/**
 * Ring segments for memory: app, kernel and compressed, or the usage in the panel tier.
 *
 * @param {object} payload - memory payload
 * @returns {Array}
 */
export function memorySegments(payload) {
    const {app, kernel, compressed, total} = memoryParts(payload);
    if (!Number.isFinite(app))
        return [{value: (payload.usage ?? 0) / 100, color: 'blue'}];
    return [
        {value: app / total, color: 'blue'},
        {value: kernel / total, color: 'orange'},
        {value: compressed / total, color: 'pink'},
    ];
}

const PRESSURE_TITLES = {normal: N_('Normal'), warning: N_('Warning'), critical: N_('Critical')};

class MemoryPage extends Page {
    constructor(...args) {
        super(...args);
        this._pressure = new PressureGauge();
        this._pressure.actor.visible = false;
        this._ring = ringWithText();
        this.add(dashboard([this._pressure.actor, this._ring.actor, null]));

        this.section(_('Usage history'));
        this._chart = new LineChart({capacity: HISTORY_POINTS, styleClass: 'readout-chart-90'});
        this.add(this._chart.actor);
        this.section(_('Details'));
        this._details = this.rows();
        this._processes = new ProcessList(this.context, [{title: _('Usage')}]);
        this.add(this._processes.actor);
    }

    get historyModule() {
        return 'memory';
    }

    applyHistory(history) {
        if (history.usage?.length > 0)
            this._chart.setPoints(history.usage);
    }

    update(payload) {
        this._ring.ring.setSegments(memorySegments(payload));
        setText(this._ring.label, Format.percent(payload.usage));
        const level = payload.pressureLevel;
        this._pressure.actor.visible = Boolean(level) && this.context.client.info?.memory?.hasPressure !== false;
        if (level)
            this._pressure.setLevel(level);
        this._chart.push(payload.usage);

        const {app, kernel, compressed} = memoryParts(payload);
        const rows = this._details;
        rows.set('used', _('%s of %s').format(Format.size(payload.used), Format.size(payload.total)),
            {label: _('Used')});
        rows.set('app', Number.isFinite(app) ? Format.size(app) : null, {label: _('App'), swatch: 'blue'});
        rows.set('kernel', Number.isFinite(kernel) ? Format.size(kernel) : null, {label: _('Kernel'), swatch: 'orange'});
        rows.set('compressed', Number.isFinite(payload.compressed) ? Format.size(compressed) : null,
            {label: _('Compressed'), swatch: 'pink'});
        rows.set('free', Number.isFinite(payload.free) ? Format.size(payload.free) : null,
            {label: _('Free'), swatch: 'lightGray', swatchAlpha: 0.5});
        rows.set('cache', Number.isFinite(payload.cache) ? Format.size(payload.cache) : null, {label: _('Cache')});
        rows.set('available', Format.size(payload.available), {label: _('Available')});
        rows.set('swap', payload.swapTotal > 0
            ? _('%s of %s').format(Format.size(payload.swapUsed), Format.size(payload.swapTotal))
            : Format.size(payload.swapUsed ?? 0), {label: _('Swap')});
        rows.set('pressure', level ? _(PRESSURE_TITLES[level] ?? level) : null, {label: _('Pressure')});
        this._processes.update(this.processEntries(payload, process => [Format.size(process.memory)]),
            this.item.processCount);
    }
}

// ---- GPU ----------------------------------------------------------------------------------

/**
 * The right dashboard gauge: video memory when the driver reports it (amdgpu, NVIDIA), else
 * power against its limit (xe, i915), else the core clock against its maximum.
 *
 * @param {object} gpu - GPU entry of the payload
 * @returns {object|null} {fraction, text, caption}
 */
function sideGauge(gpu) {
    if (gpu.memoryTotal > 0 && Number.isFinite(gpu.memoryUsed)) {
        const percent = gpu.memoryUsed * 100 / gpu.memoryTotal;
        return {fraction: percent / 100, text: Format.percent(percent), caption: _('Memory')};
    }
    if (Number.isFinite(gpu.power) && gpu.powerLimit > 0)
        return {fraction: gpu.power / gpu.powerLimit, text: `${Math.round(gpu.power)} W`, caption: _('Power')};
    if (gpu.coreClock > 0 && gpu.maxCoreClock > 0) {
        return {
            fraction: gpu.coreClock / gpu.maxCoreClock, text: Format.gigahertz(gpu.coreClock),
            caption: C_('short caption under the GPU clock gauge, in GHz', 'Clock'),
        };
    }
    return null;
}

/**
 * A dashboard gauge with a small caption under it.
 *
 * @returns {object} {gauge, label, caption, actor}
 */
function captionedGauge() {
    const part = gaugeWithText();
    const actor = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL});
    part.actor.x_align = Clutter.ActorAlign.CENTER;
    actor.add_child(part.actor);
    const caption = new St.Label({style_class: 'readout-gauge-caption', x_align: Clutter.ActorAlign.CENTER});
    dim(caption);
    actor.add_child(caption);
    return {...part, caption, actor};
}

// One GPU: dashboard, usage and temperature history, details (docs/visual-spec.md 2.3).
class GpuSection {
    constructor(gpu, info) {
        this.id = gpu.id;
        this._info = info ?? {};
        this.actor = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL, x_expand: true});

        // Name and status dot; only shown to tell several GPUs apart.
        this.heading = new St.BoxLayout({style_class: 'readout-separator readout-gpu-heading', x_expand: true});
        this.heading.add_child(new St.Widget({x_expand: true}));
        this._name = new St.Label({style_class: 'readout-gpu-name', y_align: Clutter.ActorAlign.CENTER});
        this._name.clutter_text.ellipsize = Pango.EllipsizeMode.END;
        this.heading.add_child(this._name);
        this._dot = new Swatch('green', {round: true, styleClass: 'readout-status-dot'});
        this._dot.actor.y_align = Clutter.ActorAlign.CENTER;
        this.heading.add_child(this._dot.actor);
        this.heading.add_child(new St.Widget({x_expand: true}));
        this.actor.add_child(this.heading);

        this._temperature = captionedGauge();
        setText(this._temperature.caption, _('Temperature'));
        this._ring = ringWithText();
        this._side = captionedGauge();
        this._dashboard = dashboard([this._temperature.actor, this._ring.actor, this._side.actor]);
        this.actor.add_child(this._dashboard);

        this._status = message('');
        this.actor.add_child(this._status);

        this._usageSection = new Separator(_('Usage history'));
        this.actor.add_child(this._usageSection.actor);
        this._usageChart = new LineChart({capacity: HISTORY_POINTS, styleClass: 'readout-chart-70'});
        this.actor.add_child(this._usageChart.actor);

        this._temperatureSection = new Separator(_('Temperature history'));
        this.actor.add_child(this._temperatureSection.actor);
        this._temperatureChart = new LineChart({
            capacity: HISTORY_POINTS, styleClass: 'readout-chart-50', color: theme => theme.color('orange'),
        });
        this.actor.add_child(this._temperatureChart.actor);

        this.actor.add_child(new Separator(_('Details')).actor);
        this._details = new Rows();
        this.actor.add_child(this._details.actor);
        this._memoryBar = new CapacityBar();
        this.actor.add_child(this._memoryBar.actor);
        this._moreDetails = new Rows();
        this.actor.add_child(this._moreDetails.actor);
    }

    setUsageHistory(points) {
        if (points?.length > 0)
            this._usageChart.setPoints(points);
    }

    update(gpu) {
        const sleeping = gpu.state === 'suspended';
        const active = gpu.state === 'active';
        setText(this._name, gpu.name ?? gpu.id);
        this._dot.setColor(active ? 'green' : sleeping ? 'red' : 'lightGray');

        this._dashboard.visible = active;
        this._status.visible = !active;
        setText(this._status, sleeping ? _('Sleeping, not woken to read it') : _('Not measured'));
        const hasTemperature = active && Number.isFinite(gpu.temperature);
        const hasUsage = active && Number.isFinite(gpu.usage);
        if (active) {
            this._ring.ring.setSegments([{value: (gpu.usage ?? 0) / 100, color: 'blue'}]);
            setText(this._ring.label, Format.percent(gpu.usage));
            this._temperature.actor.opacity = hasTemperature ? 255 : 0;
            if (hasTemperature) {
                this._temperature.gauge.setFraction(gpu.temperature / 100);
                setText(this._temperature.label, Format.temperature(gpu.temperature));
            }
            const side = sideGauge(gpu);
            this._side.actor.opacity = side ? 255 : 0;
            if (side) {
                this._side.gauge.setFraction(side.fraction);
                setText(this._side.label, side.text);
                setText(this._side.caption, side.caption);
            }
        }
        this._usageSection.actor.visible = hasUsage;
        this._usageChart.actor.visible = hasUsage;
        if (hasUsage)
            this._usageChart.push(gpu.usage);
        this._temperatureSection.actor.visible = hasTemperature;
        this._temperatureChart.actor.visible = hasTemperature;
        if (hasTemperature)
            this._temperatureChart.push(gpu.temperature);

        const rows = this._details;
        const known = value => active && Number.isFinite(value);
        rows.set('model', gpu.name ?? null, {label: _('Model')});
        rows.set('vendor', this._info.vendor || null, {label: _('Vendor')});
        rows.set('driver', this._info.driver || null, {label: _('Driver')});
        rows.set('status', active ? _('Active') : sleeping ? _('Sleeping') : _('Awake'), {label: _('Status')});
        rows.set('usage', known(gpu.usage) ? Format.percent(gpu.usage) : null, {label: _('Utilization')});
        rows.set('temperature', known(gpu.temperature) ? Format.temperature(gpu.temperature) : null,
            {label: _('Temperature')});
        const hasMemory = active && gpu.memoryTotal > 0;
        rows.set('memory', hasMemory
            ? _('%s of %s').format(Format.size(gpu.memoryUsed), Format.size(gpu.memoryTotal)) : null,
        {label: _('Memory')});
        this._memoryBar.actor.visible = hasMemory;
        if (hasMemory)
            this._memoryBar.setFraction(gpu.memoryUsed / gpu.memoryTotal);

        const more = this._moreDetails;
        // Intel GPUs report no clock while idle; the row stays so the popup keeps its height.
        let coreClock = null;
        if (active && gpu.coreClock > 0)
            coreClock = Format.frequency(gpu.coreClock);
        else if (active && gpu.maxCoreClock > 0)
            coreClock = C_('GPU core clock while the GPU is idle', 'Idle');
        more.set('core', coreClock, {label: _('Core clock')});
        more.set('memoryClock', active && gpu.memoryClock > 0 ? Format.frequency(gpu.memoryClock) : null,
            {label: _('Memory clock')});
        more.set('maxCore', active && gpu.maxCoreClock > 0 ? Format.frequency(gpu.maxCoreClock) : null,
            {label: _('Max core clock')});
        more.set('power', known(gpu.power) ? Format.power(gpu.power) : null, {label: _('Power')});
        more.set('powerLimit', active && gpu.powerLimit > 0 ? Format.power(gpu.powerLimit) : null,
            {label: _('Power limit')});
        more.set('fan', known(gpu.fanPercent) ? Format.percent(gpu.fanPercent)
            : known(gpu.fanRpm) ? `${Math.round(gpu.fanRpm)} ${_('RPM')}` : null,
        {label: _('Fan speed')});
        more.set('slot', gpu.id ?? null, {label: _('PCI slot')});
    }
}

class GpuPage extends Page {
    constructor(...args) {
        super(...args);
        this.actor.add_style_class_name('readout-card-list');
        this._sections = new Map();
        this._empty = this.add(message(_('No GPU found')));
        this._empty.visible = false;
    }

    get historyModule() {
        return 'gpu';
    }

    applyHistory(history) {
        this._history = history;
        for (const [id, section] of this._sections)
            section.setUsageHistory(history[`${id}/usage`]);
    }

    update(payload) {
        const gpus = payload.gpus ?? [];
        this._empty.visible = gpus.length === 0;
        const infos = this.context.client.info?.gpu?.gpus ?? [];
        for (const gpu of gpus) {
            let section = this._sections.get(gpu.id);
            if (!section) {
                section = new GpuSection(gpu, infos.find(info => info.id === gpu.id));
                this._sections.set(gpu.id, section);
                this.add(section.actor);
                if (this._history)
                    section.setUsageHistory(this._history[`${gpu.id}/usage`]);
            }
            section.heading.visible = gpus.length > 1;
            section.update(gpu);
        }
    }
}

// ---- Disk ---------------------------------------------------------------------------------

class DriveCard {
    constructor(name, context, item, onChanged) {
        this.name = name;
        this._onChanged = onChanged;
        this._context = context;
        this._item = item;
        this.actor = card({style_class: 'readout-drive-card'});
        const title = new St.BoxLayout({style_class: 'readout-drive-title', x_expand: true});
        this._nameButton = new St.Button({
            label: name, style_class: 'readout-drive-name', can_focus: true, y_align: Clutter.ActorAlign.CENTER,
        });
        this._nameButton.connect('clicked', () => this._openMount());
        title.add_child(this._nameButton);
        this._readDot = new Swatch('lightGray', {alpha: 0.75, round: true, styleClass: 'readout-activity-dot readout-activity-dot-first'});
        this._writeDot = new Swatch('lightGray', {alpha: 0.75, round: true, styleClass: 'readout-activity-dot'});
        title.add_child(this._readDot.actor);
        title.add_child(this._writeDot.actor);
        title.add_child(new St.Widget({x_expand: true}));
        title.add_child(iconButton('view-more-horizontal-symbolic', _('Details'), () => {
            this._details.visible = !this._details.visible;
        }, 'readout-card-button'));
        this.actor.add_child(title);

        this._chart = new MirroredChart({capacity: CARD_HISTORY_POINTS, styleClass: 'readout-chart-36', well: 'drive'});
        this.actor.add_child(this._chart.actor);
        this._mounts = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL, x_expand: true});
        this.actor.add_child(this._mounts);
        this._mountViews = new Map();
        this._showUsed = false;

        this._details = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL, x_expand: true, visible: false});
        this._speedRows = new Rows();
        this._details.add_child(this._speedRows.actor);
        this._smartSeparator = new Separator(_('SMART'));
        this._details.add_child(this._smartSeparator.actor);
        this._smartRows = new Rows();
        this._details.add_child(this._smartRows.actor);
        this.actor.add_child(this._details);
        this._firstMount = '';
    }

    _openMount() {
        if (!this._firstMount)
            return;
        try {
            Gio.AppInfo.launch_default_for_uri(GLib.filename_to_uri(this._firstMount, null),
                global.create_app_launch_context(0, -1));
            this._context.closeMenu();
        } catch (error) {
            logError(error, 'Readout: cannot open the mount point');
        }
    }

    setTitle(text) {
        if (this._nameButton.label !== text)
            this._nameButton.label = text;
    }

    setHistory(read, write) {
        if (read?.length > 0 || write?.length > 0)
            this._chart.setPoints(write ?? [], read ?? []);
    }

    update(disk, mounts) {
        const {bits} = this._item;
        if (disk) {
            this._chart.push(disk.write, disk.read);
            this._readDot.setColor(disk.read > 0 ? 'blue' : 'lightGray', disk.read > 0 ? 1 : 0.75);
            this._writeDot.setColor(disk.write > 0 ? 'red' : 'lightGray', disk.write > 0 ? 1 : 0.75);
        }
        this._chart.actor.visible = Boolean(disk);
        this._firstMount = mounts[0]?.mountPoint ?? '';

        const seen = new Set();
        for (const mount of mounts) {
            seen.add(mount.mountPoint);
            let view = this._mountViews.get(mount.mountPoint);
            if (!view) {
                view = this._createMountView(mount.mountPoint);
                this._mountViews.set(mount.mountPoint, view);
            }
            const total = mount.total || 0;
            const used = total > 0 ? mount.used / total : 0;
            view.bar.setFraction(used);
            const free = Format.diskSize(mount.free);
            const totalText = Format.diskSize(total);
            setText(view.text, this._showUsed
                ? _('%s of %s used').format(Format.diskSize(mount.used), totalText)
                : _('%s of %s free').format(free, totalText));
            setText(view.percent, Format.percent(this._showUsed ? used * 100 : 100 - used * 100));
            setText(view.mountLabel, mount.mountPoint);
        }
        for (const [mountPoint, view] of this._mountViews) {
            if (!seen.has(mountPoint)) {
                view.actor.destroy();
                this._mountViews.delete(mountPoint);
            }
        }

        const speed = this._speedRows;
        speed.set('read', disk ? Format.speed(disk.read, bits) : null, {label: _('Read'), swatch: 'blue', compact: true});
        speed.set('write', disk ? Format.speed(disk.write, bits) : null, {label: _('Write'), swatch: 'red', compact: true});
        speed.set('busy', disk && Number.isFinite(disk.busy) ? Format.percent(disk.busy) : null,
            {label: _('Busy'), compact: true});
        speed.set('totalRead', disk && Number.isFinite(disk.totalRead) ? Format.fileSize(disk.totalRead) : null,
            {label: _('Total read'), compact: true});
        speed.set('totalWritten', disk && Number.isFinite(disk.totalWritten) ? Format.fileSize(disk.totalWritten) : null,
            {label: _('Total written'), compact: true});
        const smart = this._smartRows;
        const temperature = disk && Number.isFinite(disk.temperature) ? Format.temperature(disk.temperature) : null;
        const health = disk && disk.healthy !== undefined ? disk.healthy ? _('OK') : _('Failing') : null;
        const hours = disk && Number.isFinite(disk.powerOnHours) ? `${Math.round(disk.powerOnHours)}` : null;
        smart.set('temperature', temperature, {label: _('Temperature'), compact: true});
        smart.set('health', health, {label: _('Health'), compact: true});
        smart.set('hours', hours, {label: _('Power on hours'), compact: true});
        this._smartSeparator.actor.visible = Boolean(temperature || health || hours);
    }

    _createMountView(mountPoint) {
        const actor = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL, style_class: 'readout-mount', x_expand: true});
        const bar = new CapacityBar();
        actor.add_child(bar.actor);
        const legend = new St.Button({style_class: 'readout-mount-legend', can_focus: true, x_expand: true});
        const legendBox = new St.BoxLayout({x_expand: true});
        const mountLabel = new St.Label({text: mountPoint, style_class: 'readout-mount-point', y_align: Clutter.ActorAlign.CENTER});
        mountLabel.clutter_text.ellipsize = Pango.EllipsizeMode.MIDDLE;
        const text = new St.Label({style_class: 'readout-mount-text readout-tnum', x_expand: true, y_align: Clutter.ActorAlign.CENTER});
        text.clutter_text.ellipsize = Pango.EllipsizeMode.END;
        const percent = new St.Label({
            style_class: 'readout-mount-percent readout-tnum', x_align: Clutter.ActorAlign.END,
            y_align: Clutter.ActorAlign.CENTER,
        });
        legendBox.add_child(mountLabel);
        legendBox.add_child(text);
        legendBox.add_child(percent);
        legend.set_child(legendBox);
        legend.connect('clicked', () => {
            this._showUsed = !this._showUsed;
            this._onChanged();
        });
        actor.add_child(legend);
        this._mounts.add_child(actor);
        return {actor, bar, text, percent, mountLabel};
    }
}

class DiskPage extends Page {
    constructor(...args) {
        super(...args);
        this.actor.add_style_class_name('readout-card-list');
        this._cards = new Map();
        this._cardBox = this.add(new St.BoxLayout({
            orientation: Clutter.Orientation.VERTICAL, style_class: 'readout-card-list', x_expand: true,
        }));
        this._processes = new ProcessList(this.context, [{swatch: 'blue'}, {swatch: 'red'}]);
        this.add(this._processes.actor);
    }

    get historyModule() {
        return 'disk';
    }

    applyHistory(history) {
        this._history = history;
        for (const [name, driveCard] of this._cards)
            driveCard.setHistory(history[`${name}/read`], history[`${name}/write`]);
    }

    update(payload) {
        this._lastPayload = payload;
        const disks = payload.disks ?? [];
        const mounts = payload.mounts ?? [];
        // One card per physical drive; mounts on unknown devices get their own card.
        const names = [...disks.map(disk => disk.name)];
        for (const mount of mounts) {
            if (!names.includes(mount.disk))
                names.push(mount.disk);
        }
        for (const name of names) {
            const disk = disks.find(candidate => candidate.name === name);
            const driveMounts = mounts.filter(mount => mount.disk === name);
            if (!disk && driveMounts.length === 0)
                continue;
            let driveCard = this._cards.get(name);
            if (!driveCard) {
                driveCard = new DriveCard(name, this.context, this.item, () => this.update(this._lastPayload));
                this._cards.set(name, driveCard);
                this._cardBox.add_child(driveCard.actor);
                if (this._history)
                    driveCard.setHistory(this._history[`${name}/read`], this._history[`${name}/write`]);
            }
            driveCard.setTitle(disk?.model || name);
            driveCard.update(disk, driveMounts);
        }
        const {bits} = this.item;
        this._processes.update(this.processEntries(payload,
            process => [Format.speed(process.read, bits), Format.speed(process.write, bits)]), this.item.processCount);
    }
}

// ---- Network ------------------------------------------------------------------------------

/**
 * Wi-Fi channel from a frequency in MHz.
 *
 * @param {number} frequency - MHz
 * @returns {number} channel, or NaN
 */
function wifiChannel(frequency) {
    if (!Number.isFinite(frequency))
        return NaN;
    if (frequency >= 5955)
        return Math.round((frequency - 5950) / 5);
    if (frequency >= 5000)
        return Math.round((frequency - 5000) / 5);
    if (frequency === 2484)
        return 14;
    return Math.round((frequency - 2407) / 5);
}

class SpeedHalf {
    constructor(title, color) {
        this._color = color;
        this.actor = new St.BoxLayout({
            orientation: Clutter.Orientation.VERTICAL, x_expand: true, style_class: 'readout-speed-half',
            y_align: Clutter.ActorAlign.CENTER,
        });
        const valueLine = new St.BoxLayout({x_align: Clutter.ActorAlign.CENTER});
        this._number = new St.Label({text: '0', style_class: 'readout-big-number readout-tnum', y_align: Clutter.ActorAlign.END});
        this._unit = new St.Label({style_class: 'readout-big-unit', y_align: Clutter.ActorAlign.END});
        valueLine.add_child(this._number);
        valueLine.add_child(this._unit);
        this.actor.add_child(valueLine);
        const titleLine = new St.BoxLayout({x_align: Clutter.ActorAlign.CENTER, style_class: 'readout-speed-title'});
        this._swatch = new Swatch('lightGray', {alpha: 0.75, styleClass: 'readout-swatch readout-swatch-round4'});
        titleLine.add_child(this._swatch.actor);
        const label = new St.Label({text: title, style_class: 'readout-row-label', y_align: Clutter.ActorAlign.CENTER});
        dim(label);
        titleLine.add_child(label);
        this.actor.add_child(titleLine);
    }

    update(rate, bits) {
        const [number, unit] = Format.speedParts(rate, bits);
        setText(this._number, number);
        setText(this._unit, unit);
        const active = rate >= 1000;
        this._swatch.setColor(active ? this._color : 'lightGray', active ? 1 : 0.75);
    }
}

class NetworkPage extends Page {
    constructor(...args) {
        super(...args);
        const board = this.add(new St.BoxLayout({style_class: 'readout-dashboard', x_expand: true}));
        this._download = new SpeedHalf(_('Downloading'), 'blue');
        this._upload = new SpeedHalf(_('Uploading'), 'red');
        board.add_child(this._download.actor);
        board.add_child(this._upload.actor);

        this.section(_('Usage history'));
        this._chart = new MirroredChart({
            capacity: HISTORY_POINTS, styleClass: 'readout-chart-90', minMax: true,
            format: value => Format.speed(value, this.item.bits),
        });
        this.add(this._chart.actor);

        this.section(_('Connectivity history'));
        this._grid = new GridChart();
        this._grid.actor.add_style_class_name('readout-grid-28');
        this.add(this._grid.actor);

        this._offsets = {upload: 0, download: 0};
        this.section(_('Details'), iconButton('edit-undo-symbolic', _('Reset totals'), () => {
            const payload = this._lastPayload;
            if (payload) {
                this._offsets = {upload: payload.totalUploaded ?? 0, download: payload.totalDownloaded ?? 0};
                this.update(payload);
            }
        }));
        this._details = this.rows();

        this._interfaceDetails = false;
        this.section(_('Interface'), iconButton('view-more-horizontal-symbolic', _('More interface details'), () => {
            this._interfaceDetails = !this._interfaceDetails;
            if (this._lastPayload)
                this.update(this._lastPayload);
        }));
        this._interface = this.rows();

        this.section(_('Address'), iconButton('view-refresh-symbolic', _('Refresh public address'), () => {
            if (this._publicFetched)
                this._fetchPublicAddress();
        }));
        this._address = this.rows();
        this._publicRow = new St.BoxLayout({style_class: 'readout-row', x_expand: true});
        const publicLabel = new St.Label({
            text: withColon(_('Public IP')), style_class: 'readout-row-label', y_align: Clutter.ActorAlign.CENTER,
        });
        dim(publicLabel);
        this._publicButton = new St.Button({
            label: _('Show'), style_class: 'readout-link', can_focus: true, x_expand: true,
            x_align: Clutter.ActorAlign.END, y_align: Clutter.ActorAlign.CENTER,
        });
        this._publicButton.connect('clicked', () => this._fetchPublicAddress());
        this._publicRow.add_child(publicLabel);
        this._publicRow.add_child(this._publicButton);
        this.add(this._publicRow);
        this._publicFetched = false;
        this._cancellable = null;

        this._othersSection = this.section(_('Other interfaces'));
        this._others = this.rows();
    }

    get historyModule() {
        return 'network';
    }

    applyHistory(history) {
        if (history.upload?.length > 0 || history.download?.length > 0)
            this._chart.setPoints(history.upload ?? [], history.download ?? []);
    }

    opened() {
        super.opened();
        this._grid.setValues(this.shared.connectivity);
    }

    update(payload) {
        this._lastPayload = payload;
        const {bits} = this.item;
        this._download.update(payload.download, bits);
        this._upload.update(payload.upload, bits);
        this._chart.push(payload.upload, payload.download);
        this._grid.setValues(this.shared.connectivity);

        const details = this._details;
        const totalUp = Math.max(0, (payload.totalUploaded ?? 0) - this._offsets.upload);
        const totalDown = Math.max(0, (payload.totalDownloaded ?? 0) - this._offsets.download);
        details.set('totalUp', Format.fileSize(totalUp), {label: _('Total upload'), swatch: 'red'});
        details.set('totalDown', Format.fileSize(totalDown), {label: _('Total download'), swatch: 'blue'});
        details.set('status', payload.connected ? _('UP') : _('DOWN'), {label: _('Status')});
        const internet = {full: _('UP'), none: _('DOWN'), limited: _('DOWN'), portal: _('DOWN')}[payload.connectivity] ??
            _('Unknown');
        details.set('internet', internet, {label: _('Internet connection')});

        const types = {ethernet: _('Ethernet'), wifi: _('Wi-Fi'), vpn: _('VPN'), other: _('Other')};
        const typeName = types[payload.connectionType] ?? payload.connectionType ?? '';
        const primary = (payload.interfaces ?? []).find(entry => entry.name === payload.interface);
        const rows = this._interface;
        rows.set('interface', payload.interface
            ? typeName ? `${typeName} (${payload.interface})` : payload.interface : Format.DASH,
        {label: _('Interface')});
        rows.set('status', payload.connected ? _('UP') : _('DOWN'), {label: _('Status')});
        rows.set('mac', primary?.mac || null, {label: _('Physical address')});
        const {wifi} = payload;
        const ssid = wifi?.ssid || payload.ssid;
        const signal = wifi?.signal ?? payload.signal;
        rows.set('network', ssid ? `${ssid} (${Format.percent(signal)})` : null, {label: _('Network')});
        const more = this._interfaceDetails;
        rows.set('connection', more && payload.connectionName ? payload.connectionName : null, {label: _('Connection')});
        const channel = wifiChannel(wifi?.frequency);
        rows.set('channel', more && Number.isFinite(channel)
            ? wifi.band ? `${channel} (${wifi.band})` : `${channel}` : null, {label: _('Channel')});
        rows.set('security', more && wifi?.security ? wifi.security : null, {label: _('Security')});
        rows.set('speed', more && wifi?.bitrate > 0 ? _('%d Mb/s').format(Math.round(wifi.bitrate)) : null,
            {label: _('Speed')});

        const address = primary?.ipv4?.[0] ?? primary?.ipv6?.[0] ?? null;
        this._address.set('local', address ?? Format.DASH, {label: _('Local IP')});
        this._address.set('ipv6', primary?.ipv4?.length && primary?.ipv6?.length ? primary.ipv6[0] : null,
            {label: _('Local IPv6'), compact: true});

        const others = (payload.interfaces ?? []).filter(entry => entry.name !== payload.interface && entry.up);
        this._othersSection.actor.visible = others.length > 0;
        const otherKeys = new Set();
        for (const entry of others) {
            otherKeys.add(entry.name);
            const otherAddress = entry.ipv4?.[0] ?? entry.ipv6?.[0] ?? '';
            this._others.set(entry.name,
                `${otherAddress ? `${otherAddress}  ` : ''}↓ ${Format.speed(entry.download, bits)}  ↑ ${Format.speed(entry.upload, bits)}`,
                {label: entry.name, compact: true});
        }
        this._others.retain(otherKeys);
    }

    // Only on click: this is the extension's only network request, to api.ipify.org.
    _fetchPublicAddress() {
        if (this._cancellable)
            return;
        this._publicButton.label = _('Updating...');
        this._cancellable = new Gio.Cancellable();
        const session = new Soup.Session({timeout: 10});
        const request = Soup.Message.new('GET', 'https://api.ipify.org');
        session.send_and_read_async(request, GLib.PRIORITY_DEFAULT, this._cancellable, (source, result) => {
            let text = _('Unavailable');
            try {
                const bytes = source.send_and_read_finish(result);
                if (request.get_status() === Soup.Status.OK)
                    text = new TextDecoder().decode(bytes.get_data()).trim().slice(0, 45);
            } catch (error) {
                if (error.matches?.(Gio.IOErrorEnum, Gio.IOErrorEnum.CANCELLED))
                    return;
            }
            this._cancellable = null;
            this._publicFetched = true;
            if (!this.destroyed)
                this._publicButton.label = text;
        });
    }

    destroy() {
        this._cancellable?.cancel();
        this._cancellable = null;
        super.destroy();
    }
}

// ---- Sensors ------------------------------------------------------------------------------

const SENSOR_KINDS = [
    ['temperature', N_('Temperature')],
    ['voltage', N_('Voltage')],
    ['current', N_('Current')],
    ['power', N_('Power')],
];
const SENSOR_HISTORY_POINTS = 120;

class SensorsPage extends Page {
    constructor(...args) {
        super(...args);
        this._history = new Map();
        this._rows = new Map();

        this._fans = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL, x_expand: true, visible: false});
        this._fans.add_child(new Separator(_('Fans')).actor);
        this._fanCards = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL, style_class: 'readout-fan-list'});
        this._fans.add_child(this._fanCards);
        this.add(this._fans);

        this._sections = new Map();
        for (const [kind, title] of SENSOR_KINDS) {
            const box = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL, x_expand: true, visible: false});
            box.add_child(new Separator(_(title)).actor);
            const list = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL, x_expand: true});
            box.add_child(list);
            this.add(box);
            this._sections.set(kind, {box, list, groups: []});
        }
        this._empty = this.add(message(_('No sensors found')));
        this._empty.visible = false;
    }

    opened() {
        super.opened();
        // History is only kept while the page is open, so it starts fresh each time.
        this._history.clear();
    }

    update(payload) {
        const sensors = (payload.sensors ?? []).filter(sensor => sensor.valid || sensor.sleeping);
        this._empty.visible = sensors.length === 0;
        for (const sensor of sensors) {
            let series = this._history.get(sensor.id);
            if (!series) {
                series = new Series(SENSOR_HISTORY_POINTS);
                this._history.set(sensor.id, series);
            }
            if (sensor.valid)
                series.push(sensor.value);
            let row = this._rows.get(sensor.id);
            if (!row) {
                row = sensor.kind === 'fan' ? this._createFanCard(sensor) : this._createRow(sensor);
                if (!row)
                    continue;
                this._rows.set(sensor.id, row);
            }
            setText(row.value, Format.sensorValue(sensor));
            if (row.chart && row.chart.actor.visible)
                row.chart.setValues(series.values);
        }
    }

    _createFanCard(sensor) {
        this._fans.visible = true;
        const fanCard = card({style_class: 'readout-fan-card', orientation: Clutter.Orientation.HORIZONTAL});
        const name = new St.Label({text: sensor.label, style_class: 'readout-fan-name', x_expand: true, y_align: Clutter.ActorAlign.CENTER});
        name.clutter_text.ellipsize = Pango.EllipsizeMode.END;
        const value = new St.Label({style_class: 'readout-fan-value readout-tnum', y_align: Clutter.ActorAlign.CENTER});
        fanCard.add_child(name);
        fanCard.add_child(value);
        this._fanCards.add_child(fanCard);
        return {value, chart: null};
    }

    _createRow(sensor) {
        const section = this._sections.get(sensor.kind);
        if (!section)
            return null;
        section.box.visible = true;
        // Rows are grouped by sensor group, groups in discovery order.
        let group = section.groups.find(candidate => candidate.name === sensor.group);
        if (!group) {
            const box = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL, x_expand: true});
            section.list.add_child(box);
            group = {name: sensor.group, box};
            section.groups.push(group);
        }
        const button = new St.Button({style_class: 'readout-sensor-row', can_focus: true, x_expand: true});
        const line = new St.BoxLayout({x_expand: true});
        const label = new St.Label({text: sensor.label, style_class: 'readout-row-label', y_align: Clutter.ActorAlign.CENTER});
        label.clutter_text.ellipsize = Pango.EllipsizeMode.END;
        dim(label);
        line.add_child(label);
        if (sensor.chip) {
            const chip = new St.Label({text: sensor.chip, style_class: 'readout-sensor-chip', y_align: Clutter.ActorAlign.CENTER});
            chip.clutter_text.ellipsize = Pango.EllipsizeMode.END;
            faint(chip);
            line.add_child(chip);
        }
        const value = new St.Label({
            style_class: 'readout-row-value readout-tnum', x_expand: true, x_align: Clutter.ActorAlign.END,
            y_align: Clutter.ActorAlign.CENTER,
        });
        line.add_child(value);
        button.set_child(line);
        button.accessible_name = sensor.label;
        group.box.add_child(button);
        const row = {value, chart: null};
        button.connect('clicked', () => {
            if (!row.chart) {
                row.chart = new LineChart({capacity: SENSOR_HISTORY_POINTS, maximum: null, styleClass: 'readout-chart-60 readout-sensor-chart'});
                row.chart.actor.visible = false;
                const index = group.box.get_children().indexOf(button);
                group.box.insert_child_at_index(row.chart.actor, index + 1);
            }
            row.chart.actor.visible = !row.chart.actor.visible;
            if (row.chart.actor.visible)
                row.chart.setValues(this._history.get(sensor.id)?.values ?? []);
        });
        return row;
    }
}

// ---- Battery ------------------------------------------------------------------------------

class BatteryPage extends Page {
    constructor(...args) {
        super(...args);
        this._noBattery = this.add(message(_('This computer has no battery.')));
        this._noBattery.visible = false;
        this._main = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL, x_expand: true});
        this.add(this._main);
        this._icon = new BatteryIcon();
        this._main.add_child(dashboard([this._icon.actor]));
        this._main.add_child(new Separator(_('Details')).actor);
        this._details = new Rows();
        this._main.add_child(this._details.actor);
        this._batteries = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL, x_expand: true});
        this._main.add_child(this._batteries);
        this._batteryViews = [];
        this._adapter = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL, x_expand: true});
        this._adapter.add_child(new Separator(_('Power adapter')).actor);
        this._adapterRows = new Rows();
        this._adapter.add_child(this._adapterRows.actor);
        this._main.add_child(this._adapter);
        this._peripheralsSection = this.section(_('Devices'));
        this._peripherals = this.add(new St.BoxLayout({
            orientation: Clutter.Orientation.VERTICAL, style_class: 'readout-card-list', x_expand: true,
        }));
        this._peripheralCards = [];
    }

    update(payload) {
        const present = Boolean(payload.present);
        this._noBattery.visible = !present;
        this._main.visible = present;
        if (present)
            this._updateBattery(payload);
        const peripherals = payload.peripherals ?? [];
        this._peripheralsSection.actor.visible = peripherals.length > 0;
        while (this._peripheralCards.length < peripherals.length) {
            const deviceCard = new DeviceCard();
            this._peripherals.add_child(deviceCard.actor);
            this._peripheralCards.push(deviceCard);
        }
        this._peripheralCards.forEach((deviceCard, index) => {
            const device = peripherals[index];
            deviceCard.actor.visible = Boolean(device);
            if (device)
                deviceCard.update(device.model || device.kind, Format.percent(device.percentage), true, null);
        });
    }

    _updateBattery(payload) {
        this._icon.setLevel(payload.percentage);
        const rows = this._details;
        rows.set('level', Format.percent(payload.percentage), {label: _('Level')});
        rows.set('source', payload.externalPower ? _('AC Power') : _('Battery Power'), {label: _('Source')});
        let timeLabel = _('Time to discharge');
        let time = _('Unknown');
        if (payload.state === 'full') {
            timeLabel = _('Time to charge');
            time = _('Fully charged');
        } else if (payload.state === 'charging') {
            timeLabel = _('Time to charge');
            time = payload.timeToFull > 0 ? Format.batteryTime(payload.timeToFull) : _('Calculating');
        } else if (payload.state === 'discharging') {
            time = payload.timeToEmpty > 0 ? Format.batteryTime(payload.timeToEmpty) : _('Calculating');
        } else if (payload.externalPower) {
            timeLabel = _('Time to charge');
            time = Values.batteryStateText(payload.state);
        }
        rows.set('time', time, {label: timeLabel});
        const unplugged = this.shared.unpluggedAt;
        rows.set('lastCharge', !payload.externalPower && unplugged
            ? Format.duration((GLib.get_monotonic_time() - unplugged) / 1e6) : null, {label: _('Last charge')});

        const batteries = payload.batteries ?? [];
        batteries.forEach((battery, index) => {
            let view = this._batteryViews[index];
            if (!view) {
                const separator = new Separator(_('Battery'));
                const batteryRows = new Rows();
                this._batteries.add_child(separator.actor);
                this._batteries.add_child(batteryRows.actor);
                view = {separator, rows: batteryRows};
                this._batteryViews[index] = view;
            }
            if (batteries.length > 1)
                setText(view.separator.label, battery.model || _('Battery'));
            const batteryRows = view.rows;
            batteryRows.set('health', battery.health > 0 ? Format.percent(battery.health) : null, {label: _('Health')});
            batteryRows.set('capacity', battery.energyFull > 0
                ? `${battery.energy.toFixed(1)} / ${battery.energyFull.toFixed(1)} / ${battery.energyFullDesign.toFixed(1)} Wh`
                : null, {label: _('Capacity')});
            batteryRows.set('cycles', battery.cycles >= 0 ? `${battery.cycles}` : null, {label: _('Cycles')});
            batteryRows.set('temperature', Number.isFinite(battery.temperature) && battery.temperature > 0
                ? Format.temperature(battery.temperature) : null, {label: _('Temperature')});
            batteryRows.set('power', Number.isFinite(battery.power) ? Format.power(battery.power) : null,
                {label: _('Power')});
            batteryRows.set('current', battery.voltage > 0 && Number.isFinite(battery.power)
                ? `${Math.round(battery.power / battery.voltage * 1000)} mA` : null, {label: _('Current')});
            batteryRows.set('voltage', battery.voltage > 0 ? `${battery.voltage.toFixed(2)} V` : null, {label: _('Voltage')});
            batteryRows.set('technology', battery.technology && battery.technology !== 'unknown'
                ? battery.technology : null, {label: _('Technology')});
        });
        this._adapter.visible = Boolean(payload.externalPower);
        this._adapterRows.set('charging', payload.state === 'charging' ? _('Yes') : _('No'), {label: _('Is charging')});
    }
}

// ---- Bluetooth ----------------------------------------------------------------------------

class DeviceCard {
    constructor() {
        this.actor = card({style_class: 'readout-device-card', orientation: Clutter.Orientation.HORIZONTAL});
        this._icon = new St.Icon({style_class: 'readout-device-icon', y_align: Clutter.ActorAlign.CENTER, visible: false});
        this._name = new St.Label({style_class: 'readout-device-name', x_expand: true, y_align: Clutter.ActorAlign.CENTER});
        this._name.clutter_text.ellipsize = Pango.EllipsizeMode.END;
        this._value = new St.Label({style_class: 'readout-device-value readout-tnum', y_align: Clutter.ActorAlign.CENTER});
        this.actor.add_child(this._icon);
        this.actor.add_child(this._name);
        this.actor.add_child(this._value);
    }

    update(name, value, connected, iconName) {
        setText(this._name, name ?? '');
        setText(this._value, value ?? '');
        this._name.opacity = connected ? 255 : 140;
        this._value.opacity = connected ? 255 : 140;
        this._icon.visible = Boolean(iconName);
        if (iconName && this._icon.icon_name !== iconName)
            this._icon.icon_name = iconName;
    }
}

class BluetoothPage extends Page {
    constructor(...args) {
        super(...args);
        this.actor.add_style_class_name('readout-card-list');
        this._message = this.add(message(''));
        this._cards = new Map();
    }

    update(payload) {
        const devices = payload.devices ?? [];
        let text = '';
        if (!payload.available)
            text = _('No Bluetooth adapter found');
        else if (!payload.powered)
            text = _('Bluetooth is off');
        else if (devices.length === 0)
            text = _('No Bluetooth devices are available');
        this._message.visible = text !== '';
        setText(this._message, text);
        const seen = new Set();
        devices.forEach(device => {
            seen.add(device.address);
            let deviceCard = this._cards.get(device.address);
            if (!deviceCard) {
                deviceCard = new DeviceCard();
                this._cards.set(device.address, deviceCard);
                this.add(deviceCard.actor);
            }
            const value = Number.isFinite(device.battery) ? Format.percent(device.battery)
                : device.connected ? _('Connected') : _('Not connected');
            deviceCard.update(device.name, value, device.connected,
                device.icon ? `${device.icon}-symbolic` : 'bluetooth-active-symbolic');
        });
        for (const [address, deviceCard] of this._cards) {
            if (!seen.has(address)) {
                deviceCard.actor.destroy();
                this._cards.delete(address);
            }
        }
        // Connected devices first, in daemon order otherwise.
        devices.forEach((device, index) => {
            const deviceCard = this._cards.get(device.address);
            this.actor.set_child_at_index(deviceCard.actor, index + 1);
        });
    }
}

// ---- Clock --------------------------------------------------------------------------------

class Calendar {
    constructor() {
        this.actor = card({style_class: 'readout-calendar'});
        const navigation = new St.BoxLayout({style_class: 'readout-calendar-navigation', x_expand: true});
        this._title = new St.Label({style_class: 'readout-calendar-title', x_expand: true, y_align: Clutter.ActorAlign.CENTER});
        navigation.add_child(this._title);
        navigation.add_child(iconButton('go-previous-symbolic', _('Previous month'), () => this._shift(-1),
            'readout-calendar-arrow'));
        navigation.add_child(iconButton('go-next-symbolic', _('Next month'), () => this._shift(1),
            'readout-calendar-arrow'));
        this.actor.add_child(navigation);
        const layout = new Clutter.GridLayout({column_homogeneous: true});
        this._grid = new St.Widget({layout_manager: layout, style_class: 'readout-calendar-grid', x_expand: true});
        this.actor.add_child(this._grid);
        this._weekStart = Shell.util_get_week_start();
        // Weekday header.
        const sunday = GLib.DateTime.new_local(2023, 1, 1, 0, 0, 0);
        for (let column = 0; column < 7; column++) {
            const day = sunday.add_days((column + this._weekStart) % 7);
            const label = new St.Label({
                text: day.format('%a'), style_class: 'readout-calendar-weekday',
                x_align: Clutter.ActorAlign.CENTER, y_align: Clutter.ActorAlign.CENTER,
            });
            layout.attach(new St.Bin({style_class: 'readout-calendar-cell', x_expand: true, child: label}), column, 0, 1, 1);
        }
        this._days = [];
        for (let row = 0; row < 6; row++) {
            for (let column = 0; column < 7; column++) {
                const label = new St.Label({
                    style_class: 'readout-calendar-day readout-tnum',
                    x_align: Clutter.ActorAlign.CENTER, y_align: Clutter.ActorAlign.CENTER,
                });
                layout.attach(this._cell(label), column, row + 1, 1, 1);
                this._days.push(label);
            }
        }
        this._offset = 0;
        this._shown = '';
    }

    _cell(label) {
        const mark = new St.Bin({
            style_class: 'readout-calendar-mark', child: label,
            x_align: Clutter.ActorAlign.CENTER, y_align: Clutter.ActorAlign.CENTER,
        });
        return new St.Bin({style_class: 'readout-calendar-cell', x_expand: true, child: mark});
    }

    _shift(months) {
        this._offset += months;
        this.refresh(true);
    }

    /** Goes back to the current month (when the popup opens). */
    reset() {
        this._offset = 0;
        this.refresh(true);
    }

    refresh(force = false) {
        const now = GLib.DateTime.new_now_local();
        const month = now.add_months(this._offset);
        const key = `${month.get_year()}-${month.get_month()}-${now.get_day_of_month()}`;
        if (!force && key === this._shown)
            return;
        this._shown = key;
        setText(this._title, month.format('%B %Y'));
        const first = GLib.DateTime.new_local(month.get_year(), month.get_month(), 1, 0, 0, 0);
        const leading = (first.get_day_of_week() % 7 - this._weekStart + 7) % 7;
        const start = first.add_days(-leading);
        this._days.forEach((label, index) => {
            const day = start.add_days(index);
            setText(label, `${day.get_day_of_month()}`);
            const inMonth = day.get_month() === month.get_month();
            const today = day.get_year() === now.get_year() && day.get_day_of_year() === now.get_day_of_year();
            label.opacity = inMonth || today ? 255 : 110;
            const mark = label.get_parent();
            if (today)
                mark.add_style_class_name('readout-calendar-today');
            else
                mark.remove_style_class_name('readout-calendar-today');
        });
    }
}

class ClockCard {
    constructor(zone, title) {
        this._zone = zone;
        this.actor = card({style_class: 'readout-clock-card', orientation: Clutter.Orientation.HORIZONTAL});
        this._face = new AnalogClock();
        this.actor.add_child(this._face.actor);
        const column = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL, style_class: 'readout-clock-text', y_align: Clutter.ActorAlign.CENTER});
        this._name = new St.Label({style_class: 'readout-clock-name'});
        this._title = title;
        this._time = new St.Label({style_class: 'readout-clock-time readout-tnum'});
        column.add_child(this._name);
        column.add_child(this._time);
        this.actor.add_child(column);
    }

    update(item, local) {
        const now = GLib.DateTime.new_now(timeZoneFor(this._zone));
        this._face.setTime(now.get_hour(), now.get_minute(), now.get_second());
        let offset = '';
        if (this._zone) {
            const hours = (now.get_utc_offset() - local.get_utc_offset()) / 3.6e9;
            if (hours !== 0)
                offset = `, ${hours > 0 ? '+' : ''}${hours}h`;
        }
        setText(this._name, `${this._title} (${now.get_timezone_abbreviation()}${offset})`);
        setText(this._time, `${formatClock(now, item, true)}  ${now.format('%a %e %b').replace(/ {2}/g, ' ')}`);
    }
}

class ClockPage extends Page {
    constructor(...args) {
        super(...args);
        this.actor.add_style_class_name('readout-card-list');
        this._calendar = new Calendar();
        this.add(this._calendar.actor);
        this._clocks = [new ClockCard('', _('Local time'))];
        for (const zone of this.item.timezones)
            this._clocks.push(new ClockCard(zone, zoneShortName(zone)));
        for (const clockCard of this._clocks)
            this.add(clockCard.actor);
        this._timerId = 0;
    }

    opened() {
        this._calendar.reset();
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
        this._calendar.refresh();
        for (const clockCard of this._clocks)
            clockCard.update(this.item, now);
        // Once per second while the page is visible, aligned to the second.
        const delay = 1000 - Math.floor(now.get_microsecond() / 1000) + 5;
        this._timerId = GLib.timeout_add(GLib.PRIORITY_DEFAULT, delay, () => {
            this._timerId = 0;
            this._tick();
            return GLib.SOURCE_REMOVE;
        });
    }
}
