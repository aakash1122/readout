// Module names, icons and the main value of each module, shared by the panel, tooltip and alerts.
import QtQuick

import "Logic.js" as Logic

QtObject {
    function titleFor(module) {
        switch (module) {
        case "cpu": return i18nc("@title module name", "CPU")
        case "memory": return i18nc("@title module name", "Memory")
        case "disk": return i18nc("@title module name", "Disk")
        case "network": return i18nc("@title module name", "Network")
        case "gpu": return i18nc("@title module name", "GPU")
        case "sensors": return i18nc("@title module name", "Sensors")
        case "battery": return i18nc("@title module name", "Battery")
        case "bluetooth": return i18nc("@title module name", "Bluetooth")
        case "clock": return i18nc("@title module name", "Clock")
        }
        return module
    }

    // Short panel label, at most about four characters.
    function labelFor(module) {
        switch (module) {
        case "cpu": return i18nc("@label short panel label for CPU", "CPU")
        case "memory": return i18nc("@label short panel label for memory", "RAM")
        case "disk": return i18nc("@label short panel label for disk", "SSD")
        case "network": return i18nc("@label short panel label for network", "NET")
        case "gpu": return i18nc("@label short panel label for GPU", "GPU")
        case "sensors": return i18nc("@label short panel label for sensors", "TEMP")
        case "battery": return i18nc("@label short panel label for battery", "BAT")
        case "bluetooth": return i18nc("@label short panel label for Bluetooth", "BT")
        }
        return ""
    }

    function iconFor(module) {
        switch (module) {
        case "cpu": return "cpu"
        case "memory": return "memory"
        case "disk": return "drive-harddisk"
        case "network": return "network-wired"
        case "gpu": return "video-display"
        case "sensors": return "temperature-normal"
        case "battery": return "battery-full"
        case "bluetooth": return "network-bluetooth"
        case "clock": return "clock"
        }
        return "io.github.aakash1122.readout"
    }

    // The 0..100 level a module shows in level styles, or NaN.
    function levelFor(module, payload, configuration) {
        if (!payload) {
            return NaN
        }
        switch (module) {
        case "cpu":
        case "memory":
        case "disk":
            return Logic.isNumber(payload.usage) ? payload.usage : NaN
        case "gpu": {
            const gpu = Logic.selectGpu(payload.gpus, configuration.gpu)
            return gpu && Logic.isNumber(gpu.usage) ? gpu.usage : NaN
        }
        case "battery":
            return payload.present && Logic.isNumber(payload.percentage) ? payload.percentage : NaN
        }
        return NaN
    }

    // Battery levels are good when high; every other level is good when low.
    function levelIsInverted(module) {
        return module === "battery"
    }

    // Main panel text for a module.
    function valueFor(module, payload, configuration, clockTime) {
        if (module === "clock") {
            return clockTime
        }
        if (!payload) {
            return "–"
        }
        const bits = configuration.speedBits
        switch (module) {
        case "network":
            return "↓" + Format.compactRate(payload.download || 0, bits) + " ↑" + Format.compactRate(payload.upload || 0, bits)
        case "sensors": {
            const items = stackItems(module, payload, configuration)
            return items.length > 0 ? items.map(item => item.text).join(" ") : "–"
        }
        case "bluetooth": {
            const items = stackItems(module, payload, configuration)
            if (items.length > 0) {
                return items.map(item => item.text).join(" ")
            }
            return payload.available && payload.powered ? String(payload.connectedCount || 0) : "–"
        }
        case "gpu": {
            const gpu = Logic.selectGpu(payload.gpus, configuration.gpu)
            if (gpu && gpu.state === "suspended") {
                return i18nc("@info:status GPU powered down", "Off")
            }
            // A second GPU is only read while its popup is open, so the panel shows that it is awake.
            if (gpu && gpu.state === "idle") {
                return i18nc("@info:status GPU awake, not being measured", "On")
            }
            break
        }
        case "battery":
            if (!payload.present) {
                return payload.externalPower ? i18nc("@info:status on AC power", "AC") : "–"
            }
            break
        }
        const level = levelFor(module, payload, configuration)
        return Logic.isNumber(level) ? Format.percent(level) : "–"
    }

    // Small value rows for the "stack" style: [{label, text}].
    function stackItems(module, payload, configuration) {
        if (!payload) {
            return []
        }
        if (module === "sensors") {
            const selected = configuration.sensors || []
            if (selected.length === 0) {
                const items = []
                if (Logic.isNumber(payload.cpuTemperature)) {
                    items.push({ label: i18nc("@label short CPU sensor label", "CPU"), text: Format.temperature(payload.cpuTemperature) })
                }
                if (Logic.isNumber(payload.gpuTemperature)) {
                    items.push({ label: i18nc("@label short GPU sensor label", "GPU"), text: Format.temperature(payload.gpuTemperature) })
                }
                return items
            }
            const byId = {}
            for (const sensor of payload.sensors || []) {
                byId[sensor.id] = sensor
            }
            return selected.map(id => byId[id]).filter(sensor => sensor !== undefined).map(sensor => ({
                label: sensor.label,
                text: sensor.valid ? Format.sensor(sensor.value, sensor.kind) : "–",
            }))
        }
        if (module === "bluetooth") {
            return (payload.devices || []).filter(device => device.connected && Logic.isNumber(device.battery)).slice(0, 4).map(device => ({
                label: device.name,
                text: Format.percent(device.battery),
            }))
        }
        return []
    }

    // Two-row values for the "speed" style: {upper, lower, upperRate, lowerRate}.
    function speedFor(module, payload, configuration) {
        const bits = configuration.speedBits
        if (!payload) {
            return { upper: "–", lower: "–", upperRate: 0, lowerRate: 0 }
        }
        const upperRate = module === "disk" ? payload.write || 0 : payload.upload || 0
        const lowerRate = module === "disk" ? payload.read || 0 : payload.download || 0
        return {
            upper: Format.compactRate(upperRate, bits),
            lower: Format.compactRate(lowerRate, bits),
            upperRate: upperRate,
            lowerRate: lowerRate,
        }
    }

    function tooltipLine(module, payload, configuration, clockTime) {
        if (module === "clock") {
            return clockTime
        }
        if (!payload) {
            return ""
        }
        const parts = [titleFor(module)]
        switch (module) {
        case "cpu":
            parts.push(Format.percent(payload.usage || 0))
            if (Logic.isNumber(payload.temperature)) {
                parts.push(Format.temperature(payload.temperature))
            }
            break
        case "memory":
            parts.push(Format.percent(payload.usage || 0))
            parts.push(i18nc("@info used of total memory", "%1 of %2", Format.bytes(payload.used || 0), Format.bytes(payload.total || 0)))
            break
        case "disk":
            parts.push(i18nc("@info disk read and write rates", "read %1, write %2", Format.rate(payload.read || 0, configuration.speedBits),
                             Format.rate(payload.write || 0, configuration.speedBits)))
            if (Logic.isNumber(payload.free)) {
                parts.push(i18nc("@info free disk space", "%1 free", Format.bytes(payload.free)))
            }
            break
        case "network":
            if (!payload.connected) {
                parts.push(i18nc("@info:status", "Disconnected"))
                break
            }
            parts.push("↓ " + Format.rate(payload.download || 0, configuration.speedBits))
            parts.push("↑ " + Format.rate(payload.upload || 0, configuration.speedBits))
            if (payload.ssid) {
                parts.push(payload.ssid)
            }
            break
        case "gpu": {
            const gpu = Logic.selectGpu(payload.gpus, configuration.gpu)
            if (!gpu) {
                return ""
            }
            if (gpu.state === "suspended") {
                parts.push(i18nc("@info:status GPU powered down", "Sleeping"))
                break
            }
            if (gpu.state === "idle") {
                parts.push(i18nc("@info:status GPU awake, not being measured", "Awake"))
                break
            }
            if (Logic.isNumber(gpu.usage)) {
                parts.push(Format.percent(gpu.usage))
            }
            if (Logic.isNumber(gpu.temperature)) {
                parts.push(Format.temperature(gpu.temperature))
            }
            break
        }
        case "sensors":
        case "bluetooth": {
            const items = stackItems(module, payload, configuration)
            if (items.length === 0) {
                return ""
            }
            for (const item of items) {
                parts.push(item.label + " " + item.text)
            }
            break
        }
        case "battery":
            if (!payload.present) {
                parts.push(payload.externalPower ? i18nc("@info:status", "On AC power") : i18nc("@info:status", "No battery"))
                break
            }
            parts.push(Format.percent(payload.percentage || 0))
            if (payload.state === "charging" && payload.timeToFull > 0) {
                parts.push(i18nc("@info time until battery is full", "%1 until full", Format.duration(payload.timeToFull)))
            } else if (payload.state === "discharging" && payload.timeToEmpty > 0) {
                parts.push(i18nc("@info time until battery is empty", "%1 remaining", Format.duration(payload.timeToEmpty)))
            }
            break
        }
        return parts.join(" · ")
    }
}
