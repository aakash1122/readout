// One D-Bus client for the whole extension (docs/dbus-api.md, "Client checklist").
//
// Panel items register their demand (module -> tier). The client merges all demands into
// a single Subscribe() call, coalesced to one call per main-loop iteration. Updates arrive as
// a unicast signal, so nothing is polled here.

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';

export const BUS_NAME = 'io.github.aakash1122.Readout1';
const OBJECT_PATH = '/io/github/aakash1122/Readout1';
const INTERFACE = BUS_NAME;
const CALL_TIMEOUT_MS = 10000;
const MAX_RETRY_SECONDS = 60;

const TIER_RANK = {none: 0, panel: 1, detail: 2};

export const ClientState = Object.freeze({
    IDLE: 'idle',             // nothing subscribed
    CONNECTING: 'connecting', // waiting for the first reply
    READY: 'ready',
    MISSING: 'missing',       // readoutd is not installed (not activatable)
    ERROR: 'error',           // installed but failing; retrying
});

export class ReadoutClient {
    /**
     * @param {number} intervalSeconds - update interval
     */
    constructor(intervalSeconds) {
        this._intervalMs = Math.round(intervalSeconds * 1000);
        this._demands = new Map();       // owner -> {modules: {name: tier}, sensors: string[], cores: boolean}
        this._updateListeners = new Set();
        this._stateListeners = new Set();
        this._state = ClientState.IDLE;
        this._subscribedKey = '';
        // Tiers of the last Subscribe the daemon confirmed, and the tier of each module's latest Update.
        this._confirmedTiers = {};
        this._dataTiers = {};
        // Latest detail-tier payload per module, kept after the popup closes.
        this._detailPayloads = {};
        this._info = null;
        this._infoPromise = null;
        this._syncSourceId = 0;
        this._retrySourceId = 0;
        this._retrySeconds = 1;
        this._cancellable = new Gio.Cancellable();
        this._connection = Gio.DBus.session;
        this._signalId = this._connection.signal_subscribe(BUS_NAME, INTERFACE, 'Update', OBJECT_PATH, null,
            Gio.DBusSignalFlags.NONE, (connection, sender, path, iface, signal, parameters) => {
                this._onUpdate(parameters);
            });
        this._hasOwner = false;
        this._watchId = Gio.bus_watch_name_on_connection(this._connection, BUS_NAME,
            Gio.BusNameWatcherFlags.NONE,
            () => this._onNameAppeared(),
            () => this._onNameVanished());
    }

    destroy() {
        this._cancellable.cancel();
        if (this._syncSourceId)
            GLib.source_remove(this._syncSourceId);
        if (this._retrySourceId)
            GLib.source_remove(this._retrySourceId);
        this._syncSourceId = 0;
        this._retrySourceId = 0;
        if (this._signalId)
            this._connection.signal_unsubscribe(this._signalId);
        this._signalId = 0;
        if (this._watchId)
            Gio.bus_unwatch_name(this._watchId);
        this._watchId = 0;
        if (this._subscribedKey && this._hasOwner) {
            // Fire and forget: the daemon also drops us when we disconnect.
            this._connection.call(BUS_NAME, OBJECT_PATH, INTERFACE, 'Unsubscribe', null, null,
                Gio.DBusCallFlags.NO_AUTO_START, -1, null, null);
        }
        this._subscribedKey = '';
        this._demands.clear();
        this._updateListeners.clear();
        this._stateListeners.clear();
    }

    get state() {
        return this._state;
    }

    /** @returns {object|null} cached GetInfo() result */
    get info() {
        return this._info;
    }

    /**
     * @param {Function} listener - called with (modules, timestamp)
     * @returns {Function} call it to remove the listener
     */
    addUpdateListener(listener) {
        this._updateListeners.add(listener);
        return () => this._updateListeners.delete(listener);
    }

    /**
     * @param {Function} listener - called with (state)
     * @returns {Function} call it to remove the listener
     */
    addStateListener(listener) {
        this._stateListeners.add(listener);
        return () => this._stateListeners.delete(listener);
    }

    /**
     * Sets what one owner (panel item, alert monitor) needs.
     *
     * @param {string} owner - unique owner key
     * @param {object} modules - module name -> "panel" | "detail"
     * @param {string[]} [sensors] - sensor ids shown in the panel
     * @param {boolean} [cores] - per-core cpu usage in the panel tier (cpu "bars" style)
     */
    setDemand(owner, modules, sensors = [], cores = false) {
        this._demands.set(owner, {modules: {...modules}, sensors: [...sensors], cores});
        this._scheduleSync();
    }

    /** @param {string} owner - unique owner key */
    removeDemand(owner) {
        if (this._demands.delete(owner))
            this._scheduleSync();
    }

    /** @param {number} seconds - update interval */
    setInterval(seconds) {
        const intervalMs = Math.round(seconds * 1000);
        if (intervalMs === this._intervalMs)
            return;
        this._intervalMs = intervalMs;
        this._scheduleSync();
    }

    /** @returns {Promise<object|null>} GetInfo() result, cached until the daemon restarts */
    getInfo() {
        if (this._info)
            return Promise.resolve(this._info);
        if (!this._infoPromise) {
            this._infoPromise = this._call('GetInfo', null)
                .then(reply => {
                    this._info = reply.recursiveUnpack()[0];
                    return this._info;
                })
                .catch(() => null)
                .finally(() => {
                    this._infoPromise = null;
                });
        }
        return this._infoPromise;
    }

    /**
     * @param {string} module - module name
     * @returns {Promise<object>} series name -> [[timestampUs, value], ...]
     */
    async getHistory(module) {
        try {
            const reply = await this._call('GetHistory', new GLib.Variant('(s)', [module]),
                Gio.DBusCallFlags.NO_AUTO_START);
            return reply.recursiveUnpack()[0];
        } catch {
            return {};
        }
    }

    _call(method, parameters, flags = Gio.DBusCallFlags.NONE) {
        return this._connection.call(BUS_NAME, OBJECT_PATH, INTERFACE, method, parameters, null, flags,
            CALL_TIMEOUT_MS, this._cancellable);
    }

    _setState(state) {
        if (state === this._state)
            return;
        this._state = state;
        for (const listener of this._stateListeners)
            listener(state);
    }

    _scheduleSync() {
        if (this._syncSourceId)
            return;
        this._syncSourceId = GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
            this._syncSourceId = 0;
            this._sync().catch(error => logError(error, 'Readout: subscribe failed'));
            return GLib.SOURCE_REMOVE;
        });
    }

    _merged() {
        const modules = {};
        const sensors = new Set();
        let cores = false;
        for (const demand of this._demands.values()) {
            cores ||= demand.cores;
            for (const [module, tier] of Object.entries(demand.modules)) {
                if ((TIER_RANK[tier] ?? 0) > (TIER_RANK[modules[module]] ?? 0))
                    modules[module] = tier;
            }
            for (const sensor of demand.sensors)
                sensors.add(sensor);
        }
        return {modules, sensors: [...sensors].sort(), cores};
    }

    async _sync() {
        const {modules, sensors, cores} = this._merged();
        const names = Object.keys(modules).sort();
        const key = names.length === 0 ? ''
            : JSON.stringify([names.map(name => [name, modules[name]]), sensors, cores, this._intervalMs]);
        if (key === this._subscribedKey && this._state !== ClientState.ERROR && this._state !== ClientState.MISSING)
            return;
        if (key === '') {
            this._subscribedKey = '';
            if (this._hasOwner)
                await this._call('Unsubscribe', null, Gio.DBusCallFlags.NO_AUTO_START).catch(() => {});
            this._setState(ClientState.IDLE);
            return;
        }
        if (this._state === ClientState.IDLE)
            this._setState(ClientState.CONNECTING);
        const parameters = new GLib.Variant('(a{ss}ua{sv})', [
            modules,
            this._intervalMs,
            {sensors: new GLib.Variant('as', sensors), cores: new GLib.Variant('b', cores)},
        ]);
        try {
            // Auto-starts readoutd through D-Bus activation.
            await this._call('Subscribe', parameters);
            // The daemon replies after applying the subscription and the bus keeps messages in
            // order, so every Update handled from now on was sampled at these tiers.
            this._confirmedTiers = modules;
            this._subscribedKey = key;
            this._retrySeconds = 1;
            this._setState(ClientState.READY);
            if (!this._info)
                this.getInfo();
        } catch (error) {
            if (error.matches?.(Gio.IOErrorEnum, Gio.IOErrorEnum.CANCELLED))
                return;
            this._subscribedKey = '';
            const installed = await this._isActivatable();
            this._setState(installed ? ClientState.ERROR : ClientState.MISSING);
            this._scheduleRetry();
        }
    }

    async _isActivatable() {
        try {
            const reply = await this._connection.call('org.freedesktop.DBus', '/org/freedesktop/DBus',
                'org.freedesktop.DBus', 'ListActivatableNames', null, null, Gio.DBusCallFlags.NONE, -1,
                this._cancellable);
            return reply.deepUnpack()[0].includes(BUS_NAME);
        } catch {
            return false;
        }
    }

    _scheduleRetry() {
        if (this._retrySourceId)
            return;
        this._retrySourceId = GLib.timeout_add_seconds(GLib.PRIORITY_LOW, this._retrySeconds, () => {
            this._retrySourceId = 0;
            this._scheduleSync();
            return GLib.SOURCE_REMOVE;
        });
        this._retrySeconds = Math.min(this._retrySeconds * 2, MAX_RETRY_SECONDS);
    }

    _onNameAppeared() {
        this._hasOwner = true;
        // A new daemon instance knows nothing about us: subscribe again.
        this._info = null;
        this._subscribedKey = '';
        if (this._demands.size > 0)
            this._scheduleSync();
    }

    _onNameVanished() {
        const hadOwner = this._hasOwner;
        this._hasOwner = false;
        this._subscribedKey = '';
        this._info = null;
        if (hadOwner && Object.keys(this._merged().modules).length > 0) {
            // The daemon exited or crashed while we still need it: start it again.
            this._setState(ClientState.CONNECTING);
            this._scheduleSync();
        }
    }

    /**
     * @param {string} module - module name
     * @returns {string} tier of the module's latest Update ("panel" or "detail"), or "none"
     */
    dataTier(module) {
        return this._dataTiers[module] ?? 'none';
    }

    /**
     * @param {string} module - module name
     * @returns {object|undefined} the module's latest detail-tier payload, if any
     */
    detailPayload(module) {
        return this._detailPayloads[module];
    }

    _onUpdate(parameters) {
        if (this._updateListeners.size === 0)
            return;
        const [timestamp, modules] = parameters.recursiveUnpack();
        for (const [module, payload] of Object.entries(modules)) {
            const tier = this._confirmedTiers[module] ?? 'panel';
            this._dataTiers[module] = tier;
            if (tier === 'detail')
                this._detailPayloads[module] = payload;
        }
        for (const listener of this._updateListeners)
            listener(modules, timestamp);
    }
}
