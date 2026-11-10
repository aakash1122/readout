// Pure helpers shared by the widget and its pages. No state.
.pragma library

const allModules = ["cpu", "memory", "disk", "network", "gpu", "sensors", "battery", "bluetooth", "clock"]

// docs/frontend-spec.md section 1: styles allowed per module, first entry is the default.
const stylesByModule = {
    cpu: ["mini", "text", "line", "bars", "pie", "tachometer"],
    memory: ["mini", "text", "line", "bars", "pie", "tachometer", "memory"],
    disk: ["mini", "text", "bars", "pie", "speed", "dualLine", "memory"],
    network: ["speed", "text", "dualLine", "state"],
    gpu: ["mini", "text", "line", "bars", "pie", "tachometer"],
    sensors: ["stack", "text"],
    battery: ["battery", "text", "mini"],
    bluetooth: ["stack", "text"],
    clock: ["text"],
}

// The modules the daemon provides (the clock is local).
function daemonModules(modules) {
    return modules.filter(module => module !== "clock")
}

function defaultStyle(module) {
    return (stylesByModule[module] || ["text"])[0]
}

// `entries` is a list of "module=style" strings from the configuration.
function styleFor(module, entries) {
    const allowed = stylesByModule[module] || ["text"]
    for (const entry of entries || []) {
        const separator = entry.indexOf("=")
        if (separator > 0 && entry.slice(0, separator) === module) {
            const style = entry.slice(separator + 1)
            return allowed.indexOf(style) >= 0 ? style : allowed[0]
        }
    }
    return allowed[0]
}

function withStyle(entries, module, style) {
    const result = (entries || []).filter(entry => !entry.startsWith(module + "="))
    result.push(module + "=" + style)
    return result
}

function isNumber(value) {
    return typeof value === "number" && isFinite(value)
}

// The GPU an item shows: the configured id, else the first non-secondary GPU, else the first.
function selectGpu(gpus, configuredId) {
    if (!gpus || gpus.length === 0) {
        return null
    }
    if (configuredId) {
        const match = gpus.find(gpu => gpu.id === configuredId)
        if (match) {
            return match
        }
    }
    return gpus.find(gpu => !gpu.secondary) || gpus[0]
}

// Sensor groups in display order (docs/dbus-api.md).
const sensorGroups = ["CPU", "GPU", "Storage", "Memory", "Wireless", "Network", "Power", "System"]

function groupSensors(sensors) {
    const groups = []
    for (const group of sensorGroups) {
        const members = (sensors || []).filter(sensor => sensor.group === group)
        if (members.length > 0) {
            groups.push({ name: group, sensors: members })
        }
    }
    const known = new Set(sensorGroups)
    const others = (sensors || []).filter(sensor => !known.has(sensor.group))
    if (others.length > 0) {
        groups.push({ name: "", sensors: others })
    }
    return groups
}

function iconForBluetoothDevice(icon) {
    // BlueZ reports freedesktop names like "input-mouse" or "audio-headset"; Breeze has all of them.
    return icon && icon.length > 0 ? icon : "preferences-system-bluetooth"
}

function iconForPeripheral(kind) {
    switch (kind) {
    case "mouse": return "input-mouse"
    case "keyboard": return "input-keyboard"
    case "headset": return "audio-headset"
    case "headphones": return "audio-headphones"
    case "gaming-input": return "input-gamepad"
    case "tablet": return "input-tablet"
    case "phone": return "smartphone"
    case "touchpad": return "input-touchpad"
    case "pen": return "draw-freehand"
    default: return "battery"
    }
}

// Picks a Breeze battery icon name for a level and charging state.
function batteryIcon(percentage, charging) {
    const level = Math.max(0, Math.min(100, Math.round(percentage / 10) * 10))
    const name = "battery-" + String(level).padStart(3, "0")
    return charging ? name + "-charging" : name
}
