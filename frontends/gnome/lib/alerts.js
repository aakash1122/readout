// Threshold alerts (docs/frontend-spec.md section 4).
// An alert fires once after its condition held for 10 seconds, and re-arms only after
// the condition stopped being true (hysteresis).

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as MessageTray from 'resource:///org/gnome/shell/ui/messageTray.js';

import * as Format from './format.js';
import * as Values from './values.js';

const HOLD_MICROSECONDS = 10 * 1000 * 1000;
const OWNER = 'alerts';

// key: settings prefix; module: data needed; check(payloads, threshold) -> {active, title, body} | null
const ALERTS = [
    {
        key: 'cpu',
        modules: ['cpu'],
        check: ({cpu}, threshold) => cpu && {
            active: cpu.usage > threshold,
            title: _('High CPU usage'),
            body: _('CPU usage is %s.').format(Format.percent(cpu.usage)),
        },
    },
    {
        key: 'memory',
        modules: ['memory'],
        check: ({memory}, threshold) => memory && {
            active: memory.usage > threshold,
            title: _('High memory usage'),
            body: _('Memory usage is %s.').format(Format.percent(memory.usage)),
        },
    },
    {
        key: 'cpu-temperature',
        modules: ['cpu'],
        check: ({cpu}, threshold) => cpu && Number.isFinite(cpu.temperature) && {
            active: cpu.temperature > threshold,
            title: _('CPU is hot'),
            body: _('CPU temperature is %s.').format(Format.temperature(cpu.temperature)),
        },
    },
    {
        key: 'gpu-temperature',
        modules: ['gpu'],
        check: ({gpu}, threshold) => {
            const selected = Values.selectedGpu(gpu, {gpu: ''});
            return selected && Number.isFinite(selected.temperature) && {
                active: selected.temperature > threshold,
                title: _('GPU is hot'),
                body: _('%s temperature is %s.').format(selected.name, Format.temperature(selected.temperature)),
            };
        },
    },
    {
        key: 'disk-free',
        modules: ['disk'],
        check: ({disk}, threshold) => disk && disk.total > 0 && {
            active: disk.free * 100 / disk.total < threshold,
            title: _('Disk almost full'),
            body: _('%s free on %s.').format(Format.size(disk.free), disk.mountPoint),
        },
    },
    {
        key: 'battery-low',
        modules: ['battery'],
        check: ({battery}, threshold) => battery?.present && {
            active: battery.state === 'discharging' && battery.percentage < threshold,
            title: _('Battery low'),
            body: _('Battery level is %s.').format(Format.percent(battery.percentage)),
        },
    },
    {
        key: 'battery-charged',
        modules: ['battery'],
        check: ({battery}, threshold) => battery?.present && {
            active: battery.externalPower && battery.percentage >= threshold,
            title: _('Battery charged'),
            body: _('Battery level reached %s. You can unplug the charger.').format(Format.percent(battery.percentage)),
        },
    },
    {
        key: 'connectivity',
        modules: ['network'],
        check: ({network}) => network && network.connectivity !== 'unknown' && {
            active: network.connectivity === 'none' || !network.connected,
            title: _('Internet connection lost'),
            body: _('The network is not connected.'),
        },
    },
];

export class AlertMonitor {
    /**
     * @param {object} client - ReadoutClient
     * @param {Gio.Settings} settings - extension settings
     */
    constructor(client, settings) {
        this._client = client;
        this._settings = settings;
        this._payloads = {};
        this._states = new Map(); // key -> {since, fired}
        this._source = null;
        this._settingsId = settings.connect('changed', (_settings, key) => {
            if (key.startsWith('alert-'))
                this._configure();
        });
        this._removeListener = client.addUpdateListener((modules, timestamp) => this._onUpdate(modules, timestamp));
        this._configure();
    }

    _configure() {
        this._enabled = ALERTS.filter(alert => this._settings.get_boolean(`alert-${alert.key}-enabled`));
        const modules = {};
        for (const alert of this._enabled) {
            for (const module of alert.modules)
                modules[module] = 'panel';
        }
        if (Object.keys(modules).length > 0)
            this._client.setDemand(OWNER, modules);
        else
            this._client.removeDemand(OWNER);
    }

    _onUpdate(modules, timestamp) {
        if (this._enabled.length === 0)
            return;
        Object.assign(this._payloads, modules);
        for (const alert of this._enabled) {
            const threshold = alert.key === 'connectivity' ? 0
                : this._settings.get_double(`alert-${alert.key}-threshold`);
            const result = alert.check(this._payloads, threshold);
            let state = this._states.get(alert.key);
            if (!state) {
                state = {since: 0, fired: false};
                this._states.set(alert.key, state);
            }
            if (!result || !result.active) {
                state.since = 0;
                state.fired = false; // re-arm once the condition is gone
                continue;
            }
            if (state.since === 0)
                state.since = timestamp;
            if (!state.fired && timestamp - state.since >= HOLD_MICROSECONDS) {
                state.fired = true;
                this._notify(result.title, result.body);
            }
        }
    }

    _notify(title, body) {
        if (!this._source) {
            this._source = new MessageTray.Source({
                title: _('Readout'),
                iconName: 'dialog-warning-symbolic',
            });
            this._source.connect('destroy', () => {
                this._source = null;
            });
            Main.messageTray.add(this._source);
        }
        const notification = new MessageTray.Notification({source: this._source, title, body});
        this._source.addNotification(notification);
    }

    destroy() {
        this._removeListener();
        this._settings.disconnect(this._settingsId);
        this._client.removeDemand(OWNER);
        this._source?.destroy();
        this._source = null;
    }
}
