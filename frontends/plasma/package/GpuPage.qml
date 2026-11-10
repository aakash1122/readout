// GPU popup (docs/visual-spec.md 2.3): per GPU, a dashboard with the usage ring between the
// temperature gauge and a memory, power or clock gauge, usage and temperature history, then every detail row.
// With several GPUs each one starts with a heading carrying its name and status dot.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import "Logic.js" as Logic

PageLayout {
    id: page

    readonly property var gpus: source.data.gpu?.gpus ?? []
    readonly property var gpuInfo: source.info.gpu?.gpus ?? []
    // Temperature history per GPU id, kept while the popup is open (the daemon has none).
    property var temperatureHistory: ({})

    function percentText(value) {
        return Logic.isNumber(value) ? Format.percent(value) : ""
    }
    function stateText(state) {
        return state === "suspended" ? i18nc("@info:status GPU", "Sleeping")
             : state === "idle" ? i18nc("@info:status GPU", "Awake") : i18nc("@info:status GPU", "Active")
    }

    Connections {
        target: page.source

        function onUpdated(module) {
            if (module !== "gpu") {
                return
            }
            const next = {}
            for (const gpu of page.gpus) {
                const previous = page.temperatureHistory[gpu.id] ?? []
                next[gpu.id] = Logic.isNumber(gpu.temperature) ? previous.concat([gpu.temperature]).slice(-180) : previous
            }
            page.temperatureHistory = next
        }
    }

    EmptyMessage {
        visible: page.gpus.length === 0
        appearance: page.appearance
        text: i18n("No GPU found")
    }

    Repeater {
        model: page.gpus.length

        ColumnLayout {
            id: section

            readonly property var modelData: page.gpus[index] ?? ({ id: "", name: "", state: "" })
            required property int index
            readonly property var gpu: modelData
            readonly property var info: page.gpuInfo.find(entry => entry.id === section.gpu.id) ?? {}
            readonly property bool active: gpu.state === "active"
            readonly property bool hasMemory: (gpu.memoryTotal ?? 0) > 0 && Logic.isNumber(gpu.memoryUsed)
            readonly property real memoryPercent: hasMemory ? gpu.memoryUsed * 100 / gpu.memoryTotal : NaN
            // The right gauge: video memory when the driver reports it (amdgpu, NVIDIA), else power
            // against its limit (xe, i915), else the core clock against its maximum.
            readonly property var sideGauge: {
                if (hasMemory) {
                    return { percent: memoryPercent, text: page.percentText(memoryPercent),
                             caption: i18nc("@label short caption under the GPU video memory gauge", "Memory") }
                }
                if (Logic.isNumber(gpu.power) && (gpu.powerLimit ?? 0) > 0) {
                    return { percent: gpu.power * 100 / gpu.powerLimit, text: Format.power(gpu.power),
                             caption: i18nc("@label short caption under the GPU power gauge", "Power") }
                }
                if ((gpu.coreClock ?? 0) > 0 && (gpu.maxCoreClock ?? 0) > 0) {
                    return { percent: gpu.coreClock * 100 / gpu.maxCoreClock, text: Format.number(gpu.coreClock / 1000, 1),
                             caption: i18nc("@label short caption under the GPU clock gauge, in GHz", "Clock") }
                }
                return null
            }

            Layout.fillWidth: true
            spacing: 0

            // Name and status dot; only needed to tell several GPUs apart.
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: page.appearance.separatorHeight
                Layout.topMargin: section.index > 0 ? page.appearance.cardSpacing : 0
                visible: page.gpus.length > 1
                spacing: page.appearance.px(6)

                Item { Layout.fillWidth: true }
                StatsText {
                    Layout.maximumWidth: section.width - page.appearance.px(24)
                    appearance: page.appearance
                    points: 13
                    text: section.gpu.name
                    font.weight: Font.Medium
                }
                Rectangle {
                    Layout.preferredWidth: page.appearance.px(6)
                    Layout.preferredHeight: page.appearance.px(6)
                    radius: width / 2
                    color: section.gpu.state === "suspended" ? page.appearance.red
                         : section.active ? page.appearance.green : page.appearance.lightGray
                }
                Item { Layout.fillWidth: true }
            }

            Dashboard {
                id: dashboard

                visible: section.active
                appearance: page.appearance

                Ring {
                    readonly property int size: page.appearance.px(70)

                    x: dashboard.centerAt(132, size)
                    y: page.appearance.px(10)
                    width: size
                    height: size
                    appearance: page.appearance
                    values: [section.gpu.usage ?? 0]
                    colors: [page.appearance.blue]
                    text: page.percentText(section.gpu.usage)
                }

                GpuGauge {
                    visible: Logic.isNumber(section.gpu.temperature)
                    x: dashboard.centerAt(48.5, width)
                    appearance: page.appearance
                    percent: section.gpu.temperature ?? 0
                    text: Logic.isNumber(section.gpu.temperature) ? Format.temperature(section.gpu.temperature) : ""
                    caption: i18nc("@label short caption under the GPU temperature gauge", "Temperature")
                }

                GpuGauge {
                    visible: section.sideGauge !== null
                    x: dashboard.centerAt(215.5, width)
                    appearance: page.appearance
                    percent: section.sideGauge?.percent ?? 0
                    text: section.sideGauge?.text ?? ""
                    caption: section.sideGauge?.caption ?? ""
                }
            }

            EmptyMessage {
                visible: !section.active
                appearance: page.appearance
                text: section.gpu.state === "suspended" ? i18nc("@info:status GPU powered down, not woken to read it", "Sleeping, not woken to read it")
                                                        : i18nc("@info:status GPU awake but not measured", "Not measured")
            }

            Separator {
                visible: section.active && Logic.isNumber(section.gpu.usage)
                appearance: page.appearance
                text: i18n("Usage history")
            }
            HistorySeries {
                id: usageHistory

                source: page.source
                module: "gpu"
                series: section.gpu.id + "/usage"
            }
            LineHistory {
                visible: section.active && Logic.isNumber(section.gpu.usage)
                appearance: page.appearance
                values: usageHistory.values
            }

            Separator {
                visible: section.active && Logic.isNumber(section.gpu.temperature)
                appearance: page.appearance
                text: i18n("Temperature history")
            }
            LineHistory {
                visible: section.active && Logic.isNumber(section.gpu.temperature)
                implicitHeight: page.appearance.px(50)
                appearance: page.appearance
                lineColor: page.appearance.orange
                capacity: 180
                values: page.temperatureHistory[section.gpu.id] ?? []
            }

            Separator { appearance: page.appearance; text: i18n("Details") }
            InfoRow { appearance: page.appearance; label: i18n("Model"); value: section.gpu.name ?? "" }
            InfoRow { appearance: page.appearance; label: i18n("Vendor"); value: section.info.vendor ?? "" }
            InfoRow { appearance: page.appearance; label: i18n("Driver"); value: section.info.driver ?? "" }
            InfoRow { appearance: page.appearance; label: i18n("Status"); value: page.stateText(section.gpu.state) }
            InfoRow { appearance: page.appearance; label: i18n("Utilization"); value: page.percentText(section.gpu.usage) }
            InfoRow {
                appearance: page.appearance
                label: i18n("Temperature")
                value: Logic.isNumber(section.gpu.temperature) ? Format.temperature(section.gpu.temperature) : ""
            }
            InfoRow {
                appearance: page.appearance
                label: i18n("Memory")
                value: section.hasMemory
                    ? i18nc("@info used of total", "%1 of %2", Format.bytes(section.gpu.memoryUsed), Format.bytes(section.gpu.memoryTotal)) : ""
            }
            CapacityBar {
                visible: section.hasMemory
                Layout.bottomMargin: page.appearance.px(4)
                appearance: page.appearance
                percent: section.memoryPercent
            }
            InfoRow {
                appearance: page.appearance
                label: i18n("Core clock")
                // Intel GPUs report no clock while idle; the row stays so the popup keeps its height.
                value: (section.gpu.coreClock ?? 0) > 0 ? Format.frequency(section.gpu.coreClock)
                     : (section.gpu.maxCoreClock ?? 0) > 0 ? i18nc("@info:status GPU core clock while the GPU is idle", "Idle") : ""
            }
            InfoRow {
                appearance: page.appearance
                label: i18n("Memory clock")
                value: (section.gpu.memoryClock ?? 0) > 0 ? Format.frequency(section.gpu.memoryClock) : ""
            }
            InfoRow {
                appearance: page.appearance
                label: i18n("Max core clock")
                value: (section.gpu.maxCoreClock ?? 0) > 0 ? Format.frequency(section.gpu.maxCoreClock) : ""
            }
            InfoRow { appearance: page.appearance; label: i18n("Power"); value: Logic.isNumber(section.gpu.power) ? Format.power(section.gpu.power) : "" }
            InfoRow {
                appearance: page.appearance
                label: i18n("Power limit")
                value: (section.gpu.powerLimit ?? 0) > 0 ? Format.power(section.gpu.powerLimit) : ""
            }
            InfoRow {
                appearance: page.appearance
                label: i18n("Fan speed")
                value: Logic.isNumber(section.gpu.fanPercent) ? Format.percent(section.gpu.fanPercent)
                     : Logic.isNumber(section.gpu.fanRpm) ? Format.rpm(section.gpu.fanRpm) : ""
            }
            InfoRow { appearance: page.appearance; label: i18n("PCI slot"); value: section.gpu.id ?? "" }
        }
    }
}
