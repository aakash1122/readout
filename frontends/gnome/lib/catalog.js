// Module and widget-style catalog shared by the extension and the preferences window.
// This file must not import shell or GTK modules, because both processes load it.
// Strings are marked with N_() and translated where they are shown.

/**
 * Marks a string for translation without translating it.
 *
 * @param {string} text - the string
 * @returns {string}
 */
export const N_ = text => text;

export const MODULES = {
    cpu: {title: N_('CPU'), shortLabel: N_('CPU'), icon: 'readout-cpu-symbolic', daemon: true},
    memory: {title: N_('Memory'), shortLabel: N_('RAM'), icon: 'readout-memory-symbolic', daemon: true},
    disk: {title: N_('Disk'), shortLabel: N_('SSD'), icon: 'drive-harddisk-symbolic', daemon: true},
    network: {title: N_('Network'), shortLabel: N_('NET'), icon: 'network-transmit-receive-symbolic', daemon: true},
    gpu: {title: N_('GPU'), shortLabel: N_('GPU'), icon: 'readout-gpu-symbolic', daemon: true},
    sensors: {title: N_('Sensors'), shortLabel: N_('TEMP'), icon: 'readout-temperature-symbolic', daemon: true},
    battery: {title: N_('Battery'), shortLabel: N_('BAT'), icon: 'battery-level-50-symbolic', daemon: true},
    bluetooth: {title: N_('Bluetooth'), shortLabel: N_('BT'), icon: 'bluetooth-active-symbolic', daemon: true},
    clock: {title: N_('Clock'), shortLabel: N_('TIME'), icon: 'readout-clock-symbolic', daemon: false},
};

export const MODULE_ORDER = ['cpu', 'memory', 'disk', 'network', 'gpu', 'sensors', 'battery', 'bluetooth', 'clock'];

export const STYLES = {
    text: {title: N_('Text')},
    mini: {title: N_('Mini')},
    line: {title: N_('Line chart')},
    bars: {title: N_('Bar chart')},
    pie: {title: N_('Pie')},
    tachometer: {title: N_('Tachometer')},
    speed: {title: N_('Speed')},
    dualLine: {title: N_('Network chart')},
    state: {title: N_('State')},
    stack: {title: N_('Stack')},
    battery: {title: N_('Battery')},
    memory: {title: N_('Memory')},
};

// Which styles each module supports (docs/frontend-spec.md section 1). The first one is the default.
export const MODULE_STYLES = {
    cpu: ['mini', 'text', 'line', 'bars', 'pie', 'tachometer'],
    memory: ['mini', 'text', 'line', 'bars', 'pie', 'tachometer', 'memory'],
    disk: ['mini', 'text', 'bars', 'pie', 'speed', 'dualLine', 'memory'],
    network: ['speed', 'text', 'dualLine', 'state'],
    gpu: ['mini', 'text', 'line', 'bars', 'pie', 'tachometer'],
    sensors: ['stack', 'text'],
    battery: ['battery', 'text', 'mini'],
    bluetooth: ['stack', 'text'],
    clock: ['text'],
};

export const COLOR_MODES = {
    accent: {title: N_('Accent color')},
    thresholds: {title: N_('Thresholds')},
    monochrome: {title: N_('Monochrome')},
};

export const DEFAULT_ITEM = {
    modules: ['cpu'],
    styles: {},
    showLabel: true,
    showIcon: false,
    colorMode: 'accent',
    warning: 70,
    critical: 90,
    processCount: 8,
    sensors: [],
    gpu: '',
    bits: false,
    timezones: [],
    clock24: true,
    clockSeconds: false,
};

/**
 * Returns an item with every option present and invalid values replaced by defaults.
 *
 * @param {object} raw - item as stored in GSettings
 * @returns {object}
 */
export function normalizeItem(raw) {
    const item = {...DEFAULT_ITEM, ...(raw && typeof raw === 'object' ? raw : {})};
    let modules = Array.isArray(item.modules) ? item.modules.filter(module => module in MODULES) : [];
    modules = [...new Set(modules)];
    if (modules.length === 0)
        modules = ['cpu'];
    item.modules = modules;
    const styles = {};
    for (const module of modules) {
        const wanted = item.styles?.[module] ?? (modules.length === 1 ? raw?.style : undefined);
        styles[module] = MODULE_STYLES[module].includes(wanted) ? wanted : MODULE_STYLES[module][0];
    }
    item.styles = styles;
    if (!(item.colorMode in COLOR_MODES))
        item.colorMode = DEFAULT_ITEM.colorMode;
    item.warning = clampNumber(item.warning, 1, 100, DEFAULT_ITEM.warning);
    item.critical = clampNumber(item.critical, 1, 100, DEFAULT_ITEM.critical);
    item.processCount = Math.round(clampNumber(item.processCount, 0, 10, DEFAULT_ITEM.processCount));
    item.sensors = Array.isArray(item.sensors) ? item.sensors.filter(id => typeof id === 'string').slice(0, 16) : [];
    item.timezones = Array.isArray(item.timezones)
        ? item.timezones.filter(zone => typeof zone === 'string').slice(0, 8) : [];
    item.gpu = typeof item.gpu === 'string' ? item.gpu : '';
    for (const key of ['showLabel', 'showIcon', 'bits', 'clock24', 'clockSeconds'])
        item[key] = Boolean(item[key]);
    delete item.style;
    return item;
}

/**
 * Parses the "items" setting.
 *
 * @param {string} json - stored JSON
 * @returns {object[]}
 */
export function parseItems(json) {
    try {
        const parsed = JSON.parse(json);
        if (Array.isArray(parsed))
            return parsed.slice(0, 16).map(normalizeItem);
    } catch {
        // fall through to the default
    }
    return [normalizeItem({modules: ['cpu']})];
}

function clampNumber(value, minimum, maximum, fallback) {
    const number = Number(value);
    if (!Number.isFinite(number))
        return fallback;
    return Math.min(maximum, Math.max(minimum, number));
}
