// Value formatting in the Stats style (docs/visual-spec.md section 1.7).
// Speeds and file sizes are 1000-based, memory sizes are 1024-based.

import {gettext as _, ngettext} from 'resource:///org/gnome/shell/extensions/extension.js';

const DASH = '-';

/**
 * @param {number} value - percent
 * @returns {string}
 */
export function percent(value) {
    if (!Number.isFinite(value))
        return DASH;
    return `${Math.round(value)}%`;
}

function trimZeros(text) {
    return text.includes('.') ? text.replace(/\.?0+$/, '') : text;
}

/**
 * Memory size, 1024-based, up to 2 decimals ("7.83 GB", "512 MB").
 *
 * @param {number} bytes - size in bytes
 * @returns {string}
 */
export function size(bytes) {
    if (!Number.isFinite(bytes))
        return DASH;
    const units = [_('B'), _('KB'), _('MB'), _('GB'), _('TB')];
    let value = bytes;
    let unit = 0;
    while (Math.abs(value) >= 1024 && unit < units.length - 1) {
        value /= 1024;
        unit++;
    }
    const text = unit >= 3 ? trimZeros(value.toFixed(2)) : `${Math.round(value)}`;
    return `${text} ${units[unit]}`;
}

/**
 * File size, 1000-based, up to 2 decimals ("1.24 GB").
 *
 * @param {number} bytes - size in bytes
 * @returns {string}
 */
export function fileSize(bytes) {
    if (!Number.isFinite(bytes))
        return DASH;
    const units = [_('B'), _('KB'), _('MB'), _('GB'), _('TB'), _('PB')];
    let value = bytes;
    let unit = 0;
    while (Math.abs(value) >= 1000 && unit < units.length - 1) {
        value /= 1000;
        unit++;
    }
    const text = unit >= 2 ? trimZeros(value.toFixed(2)) : `${Math.round(value)}`;
    return `${text} ${units[unit]}`;
}

/**
 * Disk capacity, 1000-based: KB and MB as integers, GB and TB with 1 decimal ("245.1 GB").
 *
 * @param {number} bytes - size in bytes
 * @returns {string}
 */
export function diskSize(bytes) {
    if (!Number.isFinite(bytes))
        return DASH;
    const units = [_('B'), _('KB'), _('MB'), _('GB'), _('TB'), _('PB')];
    let value = bytes;
    let unit = 0;
    while (Math.abs(value) >= 1000 && unit < units.length - 1) {
        value /= 1000;
        unit++;
    }
    return `${unit >= 3 ? value.toFixed(1) : Math.round(value)} ${units[unit]}`;
}

/**
 * Speed split into number and unit (Stats getReadableTuple).
 *
 * @param {number} bytesPerSecond - rate
 * @param {boolean} bits - show bits per second
 * @returns {string[]} [number, unit]
 */
export function speedParts(bytesPerSecond, bits = false) {
    const units = bits ? [_('Kb/s'), _('Mb/s'), _('Gb/s')] : [_('KB/s'), _('MB/s'), _('GB/s')];
    if (!Number.isFinite(bytesPerSecond))
        return [DASH, units[0]];
    const value = bits ? bytesPerSecond * 8 : bytesPerSecond;
    if (value < 1000)
        return ['0', units[0]];
    if (value < 1e6)
        return [`${Math.round(value / 1e3)}`, units[0]];
    if (value < 1e9) {
        const megabytes = value / 1e6;
        return [megabytes < 100 ? megabytes.toFixed(1) : `${Math.round(megabytes)}`, units[1]];
    }
    return [(value / 1e9).toFixed(1), units[2]];
}

/**
 * @param {number} bytesPerSecond - rate
 * @param {boolean} bits - show bits per second
 * @returns {string} e.g. "1.2 MB/s"
 */
export function speed(bytesPerSecond, bits = false) {
    if (!Number.isFinite(bytesPerSecond))
        return DASH;
    return speedParts(bytesPerSecond, bits).join(' ');
}

/**
 * @param {number} celsius - temperature
 * @returns {string} e.g. "54°C"
 */
export function temperature(celsius) {
    if (!Number.isFinite(celsius))
        return DASH;
    return `${Math.round(celsius)}°C`;
}

/**
 * @param {number} megahertz - frequency
 * @returns {string} e.g. "3200 MHz"
 */
export function frequency(megahertz) {
    if (!Number.isFinite(megahertz) || megahertz <= 0)
        return DASH;
    return `${Math.round(megahertz)} ${_('MHz')}`;
}

/**
 * Frequency gauge text: GHz with up to 2 decimals, no unit ("3.2").
 *
 * @param {number} megahertz - frequency
 * @returns {string}
 */
export function gigahertz(megahertz) {
    if (!Number.isFinite(megahertz) || megahertz <= 0)
        return DASH;
    return trimZeros((megahertz / 1000).toFixed(2));
}

/**
 * @param {number} watts - power
 * @returns {string}
 */
export function power(watts) {
    if (!Number.isFinite(watts))
        return DASH;
    return `${watts < 100 ? watts.toFixed(2) : Math.round(watts)} W`;
}

/**
 * Long duration, at most two units of days, hours and minutes ("2 days, 4 hours").
 *
 * @param {number} seconds - duration
 * @returns {string}
 */
export function duration(seconds) {
    if (!Number.isFinite(seconds) || seconds < 0)
        return DASH;
    const totalMinutes = Math.floor(seconds / 60);
    const days = Math.floor(totalMinutes / 1440);
    const hours = Math.floor(totalMinutes / 60) % 24;
    const minutes = totalMinutes % 60;
    const parts = [];
    if (days > 0)
        parts.push(ngettext('%d day', '%d days', days).format(days));
    if (hours > 0)
        parts.push(ngettext('%d hour', '%d hours', hours).format(hours));
    if (minutes > 0 || parts.length === 0)
        parts.push(ngettext('%d minute', '%d minutes', minutes).format(minutes));
    /* Translators: separator between duration parts, e.g. "2 days, 4 hours" */
    return parts.slice(0, 2).join(_(', '));
}

/**
 * Battery time in the long Stats form ("3h 07min").
 *
 * @param {number} seconds - duration
 * @returns {string}
 */
export function batteryTime(seconds) {
    if (!Number.isFinite(seconds) || seconds <= 0)
        return DASH;
    const minutes = Math.round(seconds / 60);
    const hours = Math.floor(minutes / 60);
    const rest = String(minutes % 60).padStart(2, '0');
    if (hours === 0)
        /* Translators: battery time in minutes, e.g. "45min" */
        return _('%smin').format(rest);
    /* Translators: battery time, e.g. "3h 07min" */
    return _('%dh %smin').format(hours, rest);
}

/**
 * Formats a sensor value with its unit (Stats Sensors popup precision).
 *
 * @param {object} sensor - sensor entry from the daemon
 * @returns {string}
 */
export function sensorValue(sensor) {
    if (!sensor || !sensor.valid)
        return sensor?.sleeping ? _('Sleeping') : DASH;
    const {value} = sensor;
    switch (sensor.kind) {
    case 'temperature':
        return `${value.toFixed(1)}°C`;
    case 'fan':
        return `${Math.round(value)} ${_('RPM')}`;
    case 'voltage':
        return `${value < 100 ? value.toFixed(3) : Math.round(value)} V`;
    case 'power':
        return `${value < 100 ? value.toFixed(2) : Math.round(value)} W`;
    case 'current':
        return `${value < 100 ? value.toFixed(2) : Math.round(value)} A`;
    default:
        return `${value}`;
    }
}

/**
 * Short sensor value for panel stacks: "54°", "1.2", "12".
 *
 * @param {object} sensor - sensor entry, or {kind, value, valid}
 * @returns {string}
 */
export function sensorMini(sensor) {
    if (!sensor || !sensor.valid || !Number.isFinite(sensor.value))
        return DASH;
    if (sensor.kind === 'temperature')
        return `${Math.round(sensor.value)}°`;
    if (sensor.kind === 'fan')
        return `${Math.round(sensor.value)}`;
    return sensor.value < 9.95 ? sensor.value.toFixed(1) : `${Math.round(sensor.value)}`;
}

export {DASH};
