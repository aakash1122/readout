// CPU popup (docs/visual-spec.md 2.1): usage ring between temperature and frequency gauges,
// usage history with per-core LED bars, details, load, frequency, top processes.
import QtQuick
import QtQuick.Layouts

import "Logic.js" as Logic

PageLayout {
    id: page

    readonly property var cpu: source.data.cpu ?? {}
    readonly property var info: source.info.cpu ?? {}
    readonly property var cores: cpu.cores ?? []
    readonly property var coreTypes: info.coreTypes ?? []
    readonly property bool hybrid: info.hybrid === true && coreTypes.length === cores.length
    readonly property real idle: Logic.isNumber(cpu.usage) ? Math.max(0, 100 - cpu.usage) : NaN
    readonly property bool hasFrequency: Logic.isNumber(cpu.frequency) && cpu.frequency > 0
    // The frequency only comes with detail data; when the CPU reports a maximum, its gauge and
    // rows keep their place from the first frame instead of appearing half a second later.
    readonly property bool showsFrequency: hasFrequency || (info.maxFrequency ?? 0) > 0

    function percentText(value) {
        return Logic.isNumber(value) ? Format.percent(value) : ""
    }
    function averageFrequency(type) {
        const values = (cpu.frequencies ?? []).filter((value, index) => coreTypes[index] === type)
        return values.length > 0 ? values.reduce((sum, value) => sum + value, 0) / values.length : NaN
    }
    function frequencyText(megahertz) {
        return Logic.isNumber(megahertz) && megahertz > 0 ? Format.frequency(megahertz) : ""
    }

    Dashboard {
        id: dashboard

        appearance: page.appearance

        Ring {
            readonly property int size: page.appearance.px(70)

            x: dashboard.centerAt(132, size)
            y: page.appearance.px(10)
            width: size
            height: size
            appearance: page.appearance
            values: [page.cpu.system ?? 0, page.cpu.user ?? 0]
            colors: [page.appearance.red, page.appearance.blue]
            text: page.percentText(page.cpu.usage)
        }

        Ring {
            readonly property int size: page.appearance.px(50)

            visible: Logic.isNumber(page.cpu.temperature)
            x: dashboard.centerAt(48.5, size)
            y: page.appearance.px(17)
            width: size
            height: size
            appearance: page.appearance
            gauge: true
            values: [page.cpu.temperature ?? 0]
            colors: [page.appearance.blue]
            text: Logic.isNumber(page.cpu.temperature) ? Format.temperature(page.cpu.temperature) : ""
        }

        Ring {
            readonly property int size: page.appearance.px(50)

            visible: page.showsFrequency
            x: dashboard.centerAt(215.5, size)
            y: page.appearance.px(17)
            width: size
            height: size
            appearance: page.appearance
            gauge: true
            values: [(page.info.maxFrequency ?? 0) > 0 ? (page.cpu.frequency ?? 0) * 100 / page.info.maxFrequency : 0]
            colors: [page.appearance.blue]
            text: page.hasFrequency ? Format.number(page.cpu.frequency / 1000, 1) : ""
        }
    }

    Separator { appearance: page.appearance; text: i18n("Usage history") }
    HistorySeries {
        id: usageHistory

        source: page.source
        module: "cpu"
        series: "usage"
    }
    LineHistory {
        appearance: page.appearance
        values: usageHistory.values
    }
    LedBars {
        visible: page.cores.length > 0
        appearance: page.appearance
        values: page.cores
        colors: page.hybrid ? page.coreTypes.map(type => type === "efficiency" ? page.appearance.teal : page.appearance.indigo) : []
    }

    Separator { appearance: page.appearance; text: i18n("Details") }
    InfoRow { appearance: page.appearance; label: i18n("System"); value: page.percentText(page.cpu.system); marker: page.appearance.red }
    InfoRow { appearance: page.appearance; label: i18n("User"); value: page.percentText(page.cpu.user); marker: page.appearance.blue }
    InfoRow { appearance: page.appearance; label: i18n("Idle"); value: page.percentText(page.idle); marker: page.appearance.idle }
    InfoRow {
        appearance: page.appearance
        label: i18n("Efficiency cores")
        value: page.hybrid ? page.percentText(page.cpu.efficiencyUsage) : ""
        marker: page.appearance.teal
    }
    InfoRow {
        appearance: page.appearance
        label: i18n("Performance cores")
        value: page.hybrid ? page.percentText(page.cpu.performanceUsage) : ""
        marker: page.appearance.indigo
    }
    InfoRow {
        appearance: page.appearance
        label: i18n("Uptime")
        value: Logic.isNumber(page.cpu.uptime) ? Format.duration(page.cpu.uptime) : ""
        valuePoints: 11
    }

    Separator {
        visible: Logic.isNumber(page.cpu.load1)
        appearance: page.appearance
        text: i18n("Average load")
    }
    InfoRow { appearance: page.appearance; label: i18n("1 minute"); value: Logic.isNumber(page.cpu.load1) ? Format.number(page.cpu.load1, 2) : "" }
    InfoRow { appearance: page.appearance; label: i18n("5 minutes"); value: Logic.isNumber(page.cpu.load5) ? Format.number(page.cpu.load5, 2) : "" }
    InfoRow { appearance: page.appearance; label: i18n("15 minutes"); value: Logic.isNumber(page.cpu.load15) ? Format.number(page.cpu.load15, 2) : "" }

    Separator {
        visible: page.showsFrequency
        appearance: page.appearance
        text: i18n("Frequency")
    }
    InfoRow {
        appearance: page.appearance
        label: i18n("All cores")
        value: page.hasFrequency ? page.frequencyText(page.cpu.frequency) : page.showsFrequency ? "–" : ""
    }
    InfoRow {
        appearance: page.appearance
        label: i18n("Efficiency cores")
        value: page.hybrid ? page.frequencyText(page.averageFrequency("efficiency")) : ""
        marker: page.appearance.teal
    }
    InfoRow {
        appearance: page.appearance
        label: i18n("Performance cores")
        value: page.hybrid ? page.frequencyText(page.averageFrequency("performance")) : ""
        marker: page.appearance.indigo
    }

    ProcessList {
        appearance: page.appearance
        processes: page.cpu.processes ?? []
        count: page.configuration.processCount
        processValues: (page.cpu.processes ?? []).map(process => [Format.percent(process.cpu, 1)])
    }
}
