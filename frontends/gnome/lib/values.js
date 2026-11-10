// Maps daemon payloads (docs/dbus-api.md) to the values panel widgets show.

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';

import * as Format from './format.js';

/**
 * The GPU an item shows: the configured one, else the first non-secondary GPU.
 *
 * @param {object} payload - gpu payload
 * @param {object} item - panel item options
 * @returns {object|null}
 */
export function selectedGpu(payload, item) {
    const gpus = payload?.gpus ?? [];
    return gpus.find(gpu => gpu.id === item.gpu) ?? gpus.find(gpu => !gpu.secondary) ?? gpus[0] ?? null;
}

/**
 * Level in percent for level styles (mini, bars, pie, tachometer, line).
 *
 * @param {string} module - module name
 * @param {object} payload - module payload
 * @param {object} item - panel item options
 * @returns {number} NaN when unknown
 */
export function level(module, payload, item) {
    if (!payload)
        return NaN;
    switch (module) {
    case 'cpu':
    case 'memory':
    case 'disk':
        return payload.usage ?? NaN;
    case 'gpu':
        return selectedGpu(payload, item)?.usage ?? NaN;
    case 'battery':
        return payload.present ? payload.percentage ?? NaN : NaN;
    default:
        return NaN;
    }
}

/**
 * Sensors an item shows: the selected ones, or the CPU/GPU summary temperatures.
 *
 * @param {object} payload - sensors payload
 * @param {object} item - panel item options
 * @returns {Array} [{label, value}]
 */
export function sensorEntries(payload, item) {
    if (!payload)
        return [];
    const entries = [];
    if (item.sensors.length > 0) {
        for (const id of item.sensors) {
            const sensor = payload.sensors?.find(candidate => candidate.id === id);
            if (sensor)
                entries.push({label: shortSensorLabel(sensor), value: Format.sensorMini(sensor)});
        }
        return entries;
    }
    const summarySensor = value => ({kind: 'temperature', valid: true, value});
    if (Number.isFinite(payload.cpuTemperature))
        entries.push({label: _('CPU'), value: Format.sensorMini(summarySensor(payload.cpuTemperature))});
    if (Number.isFinite(payload.gpuTemperature))
        entries.push({label: _('GPU'), value: Format.sensorMini(summarySensor(payload.gpuTemperature))});
    return entries;
}

function shortSensorLabel(sensor) {
    const label = sensor.group === 'CPU' || sensor.group === 'GPU' ? sensor.group : sensor.label;
    return label.length > 6 ? label.slice(0, 6) : label;
}

/**
 * Bluetooth devices that are connected and report a battery level.
 *
 * @param {object} payload - bluetooth payload
 * @returns {Array} [{icon, label, value}]
 */
export function bluetoothEntries(payload) {
    return (payload?.devices ?? [])
        .filter(device => device.connected && Number.isFinite(device.battery))
        .slice(0, 4)
        .map(device => ({
            icon: device.icon ? `${device.icon}-symbolic` : 'bluetooth-active-symbolic',
            label: device.name,
            value: Format.percent(device.battery),
        }));
}

/**
 * Main text for the "text" style and tooltips.
 *
 * @param {string} module - module name
 * @param {object} payload - module payload
 * @param {object} item - panel item options
 * @returns {string}
 */
export function text(module, payload, item) {
    if (!payload)
        return Format.DASH;
    switch (module) {
    case 'gpu': {
        // A second GPU is only read while its popup is open, so the panel shows whether it is awake.
        const gpu = selectedGpu(payload, item);
        if (gpu?.state === 'suspended')
            return _('Off');
        if (gpu?.state === 'idle')
            return _('On');
        return Format.percent(level(module, payload, item));
    }
    case 'cpu':
    case 'memory':
    case 'disk':
        return Format.percent(level(module, payload, item));
    case 'network':
        return `↓ ${Format.speed(payload.download, item.bits)}`;
    case 'sensors': {
        const [first] = sensorEntries(payload, item);
        return first ? first.value : Format.DASH;
    }
    case 'battery':
        return payload.present ? Format.percent(payload.percentage) : _('AC');
    case 'bluetooth': {
        const [first] = bluetoothEntries(payload);
        if (first)
            return first.value;
        return payload.connectedCount > 0 ? `${payload.connectedCount}` : _('Off');
    }
    default:
        return Format.DASH;
    }
}

/**
 * Tooltip / accessible description with the key values of a module.
 *
 * @param {string} module - module name
 * @param {object} payload - module payload
 * @param {object} item - panel item options
 * @returns {string}
 */
export function summary(module, payload, item) {
    if (!payload)
        return '';
    const parts = [];
    switch (module) {
    case 'cpu':
        parts.push(Format.percent(payload.usage));
        if (Number.isFinite(payload.temperature))
            parts.push(Format.temperature(payload.temperature));
        break;
    case 'memory':
        parts.push(Format.percent(payload.usage),
            _('%s of %s').format(Format.size(payload.used), Format.size(payload.total)));
        break;
    case 'disk':
        parts.push(_('%s free').format(Format.size(payload.free)),
            `↑ ${Format.speed(payload.write)}`, `↓ ${Format.speed(payload.read)}`);
        break;
    case 'network':
        parts.push(`↓ ${Format.speed(payload.download, item.bits)}`, `↑ ${Format.speed(payload.upload, item.bits)}`);
        if (payload.ssid)
            parts.push(payload.ssid);
        break;
    case 'gpu': {
        const gpu = selectedGpu(payload, item);
        if (gpu?.state === 'suspended')
            parts.push(_('Sleeping'));
        else if (gpu?.state === 'idle')
            parts.push(_('Awake'));
        else if (gpu)
            parts.push(Format.percent(gpu.usage), Format.temperature(gpu.temperature));
        break;
    }
    case 'sensors':
        for (const entry of sensorEntries(payload, item))
            parts.push(`${entry.label} ${entry.value}`);
        break;
    case 'battery':
        if (payload.present)
            parts.push(Format.percent(payload.percentage), batteryStateText(payload.state));
        else
            parts.push(_('No battery'));
        break;
    case 'bluetooth':
        parts.push(payload.powered
            ? _('%d connected').format(payload.connectedCount ?? 0) : _('Off'));
        break;
    }
    return parts.filter(part => part && part !== Format.DASH).join(' · ');
}

/**
 * @param {string} state - battery state from the daemon
 * @returns {string}
 */
export function batteryStateText(state) {
    switch (state) {
    case 'charging':
        return _('Charging');
    case 'discharging':
        return _('Discharging');
    case 'full':
        return _('Fully charged');
    case 'empty':
        return _('Empty');
    case 'pending-charge':
        return _('Not charging');
    case 'pending-discharge':
        return _('Waiting to discharge');
    default:
        return _('Unknown');
    }
}

/**
 * History series names for GetHistory().
 *
 * @param {string} module - module name
 * @param {string} [gpuId] - GPU id for the gpu module
 * @returns {string[]}
 */
export function historySeries(module, gpuId = '') {
    switch (module) {
    case 'cpu':
    case 'memory':
        return ['usage'];
    case 'gpu':
        return gpuId ? [`${gpuId}/usage`] : [];
    case 'network':
        return ['download', 'upload'];
    case 'disk':
        return ['read', 'write'];
    default:
        return [];
    }
}

/**
 * Values pushed to charts on each update, matching historySeries().
 *
 * @param {string} module - module name
 * @param {object} payload - module payload
 * @param {object} item - panel item options
 * @returns {number[]}
 */
export function chartValues(module, payload, item) {
    switch (module) {
    case 'network':
        return [payload.download, payload.upload];
    case 'disk':
        return [payload.read, payload.write];
    default:
        return [level(module, payload, item)];
    }
}
