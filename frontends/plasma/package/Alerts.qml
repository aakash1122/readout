// Threshold alerts (docs/frontend-spec.md section 4).
// An alert fires once its condition has held for 10 seconds, and fires again only after the
// condition has cleared. Alerts go through KNotification, so Do Not Disturb applies.
import QtQuick

import org.kde.notification

import "Logic.js" as Logic

QtObject {
    id: alerts

    required property var configuration
    required property var modules
    required property ReadoutSource source
    required property Summary summary

    readonly property int holdMilliseconds: 10000

    // id -> {module, enabled, icon, check(payload) -> {active, value}, message(value) -> [title, text]}
    readonly property var definitions: [
        {
            id: "cpuUsage", module: "cpu", enabled: configuration.alertCpuUsage, icon: "cpu",
            check: payload => ({ active: payload.usage > configuration.alertCpuUsageThreshold, value: payload.usage }),
            message: value => [i18n("High CPU usage"), i18n("CPU usage is %1.", Format.percent(value))],
        },
        {
            id: "memoryUsage", module: "memory", enabled: configuration.alertMemoryUsage, icon: "memory",
            check: payload => ({ active: payload.usage > configuration.alertMemoryUsageThreshold, value: payload.usage }),
            message: value => [i18n("High memory usage"), i18n("Memory usage is %1.", Format.percent(value))],
        },
        {
            id: "cpuTemperature", module: "cpu", enabled: configuration.alertCpuTemperature, icon: "temperature-high",
            check: payload => ({ active: Logic.isNumber(payload.temperature) && payload.temperature > configuration.alertCpuTemperatureThreshold, value: payload.temperature }),
            message: value => [i18n("CPU is hot"), i18n("CPU temperature is %1.", Format.temperature(value))],
        },
        {
            id: "gpuTemperature", module: "gpu", enabled: configuration.alertGpuTemperature, icon: "temperature-high",
            check: payload => {
                const temperatures = (payload.gpus ?? []).filter(gpu => Logic.isNumber(gpu.temperature)).map(gpu => gpu.temperature)
                const hottest = temperatures.length > 0 ? Math.max(...temperatures) : NaN
                return { active: hottest > configuration.alertGpuTemperatureThreshold, value: hottest }
            },
            message: value => [i18n("GPU is hot"), i18n("GPU temperature is %1.", Format.temperature(value))],
        },
        {
            id: "diskFree", module: "disk", enabled: configuration.alertDiskFree, icon: "drive-harddisk",
            check: payload => {
                const freePercent = payload.total > 0 ? payload.free * 100 / payload.total : NaN
                return { active: freePercent < configuration.alertDiskFreeThreshold, value: freePercent, mount: payload.mountPoint, free: payload.free }
            },
            message: (value, result) => [i18n("Disk almost full"), i18n("Only %1 (%2) is free on %3.", Format.bytes(result.free), Format.percent(value), result.mount)],
        },
        {
            id: "batteryLow", module: "battery", enabled: configuration.alertBatteryLow, icon: "battery-caution",
            check: payload => ({ active: payload.present === true && payload.state === "discharging" && payload.percentage < configuration.alertBatteryLowThreshold, value: payload.percentage }),
            message: value => [i18n("Battery low"), i18n("Battery is at %1.", Format.percent(value))],
        },
        {
            id: "batteryCharged", module: "battery", enabled: configuration.alertBatteryCharged, icon: "battery-full-charging",
            check: payload => ({ active: payload.present === true && (payload.state === "charging" || payload.state === "full") && payload.percentage >= configuration.alertBatteryChargedThreshold, value: payload.percentage }),
            message: value => [i18n("Battery charged"), i18n("Battery has reached %1. You can unplug the charger.", Format.percent(value))],
        },
        {
            id: "connectivity", module: "network", enabled: configuration.alertConnectivity, icon: "network-disconnect",
            check: payload => ({ active: payload.connected === false || payload.connectivity === "none", value: 0 }),
            message: value => [i18n("Network connection lost"), i18n("This computer is not connected to the internet.")],
        },
    ]

    readonly property var enabledDefinitions: definitions.filter(definition => definition.enabled)
    // Modules that alerts watch but this item does not show: subscribed at the panel tier only.
    readonly property var extraModules: {
        const result = []
        for (const definition of enabledDefinitions) {
            if (modules.indexOf(definition.module) < 0 && result.indexOf(definition.module) < 0) {
                result.push(definition.module)
            }
        }
        return result
    }

    // id -> {since, fired}
    property var states: ({})

    function evaluate(module, payload) {
        if (!payload) {
            return
        }
        const now = Date.now()
        for (const definition of enabledDefinitions) {
            if (definition.module !== module) {
                continue
            }
            const result = definition.check(payload)
            const state = states[definition.id] ?? { since: 0, fired: false }
            if (!result.active) {
                states[definition.id] = { since: 0, fired: false }
                continue
            }
            if (state.since === 0) {
                state.since = now
            }
            if (!state.fired && now - state.since >= holdMilliseconds) {
                state.fired = true
                const [title, text] = definition.message(result.value, result)
                send(title, text, definition.icon)
            }
            states[definition.id] = state
        }
    }

    function send(title, text, icon) {
        const notification = notificationComponent.createObject(alerts, { title: title, text: text, iconName: icon })
        notification.sendEvent()
    }

    property Component notificationComponent: Component {
        Notification {
            componentName: "readout"
            eventId: "alert"
            urgency: Notification.HighUrgency
            autoDelete: true
        }
    }

    property ReadoutSource extraSource: ReadoutSource {
        modules: alerts.extraModules
        active: alerts.extraModules.length > 0
        intervalMilliseconds: Settings.updateInterval * 1000
    }

    property Connections sourceConnection: Connections {
        target: alerts.source
        enabled: alerts.enabledDefinitions.length > 0
        function onUpdated(module) {
            alerts.evaluate(module, alerts.source.data[module])
        }
    }

    property Connections extraConnection: Connections {
        target: alerts.extraSource
        enabled: alerts.extraModules.length > 0
        function onUpdated(module) {
            alerts.evaluate(module, alerts.extraSource.data[module])
        }
    }

    // Forget state of disabled alerts so re-enabling starts fresh.
    onEnabledDefinitionsChanged: {
        const kept = {}
        for (const definition of enabledDefinitions) {
            if (states[definition.id]) {
                kept[definition.id] = states[definition.id]
            }
        }
        states = kept
    }
}
