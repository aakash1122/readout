// RAM popup (docs/visual-spec.md 2.2): pressure gauge and usage ring, usage history,
// details with legend squares, top processes. Linux has no wired memory: "Kernel" takes its place.
import QtQuick
import QtQuick.Layouts

import "Logic.js" as Logic

PageLayout {
    id: page

    readonly property var memory: source.data.memory ?? {}
    readonly property var info: source.info.memory ?? {}
    readonly property real total: Math.max(1, memory.total ?? 1)
    readonly property bool detailed: Logic.isNumber(memory.app)
    readonly property real kernel: detailed ? Math.max(0, (memory.used ?? 0) - memory.app - (memory.compressed ?? 0)) : NaN

    function bytesText(value) {
        return Logic.isNumber(value) ? Format.bytes(value) : ""
    }
    function share(value) {
        return Logic.isNumber(value) ? value * 100 / total : 0
    }

    Dashboard {
        id: dashboard

        appearance: page.appearance

        PressureGauge {
            readonly property int size: page.appearance.px(56)

            visible: page.info.hasPressure === true && (page.memory.pressureLevel ?? "").length > 0
            x: dashboard.centerAt(44.5, size)
            y: page.appearance.px(10)
            width: size
            height: page.appearance.px(56)
            appearance: page.appearance
            level: page.memory.pressureLevel ?? "normal"
            text: Logic.isNumber(page.memory.pressureSome) ? Format.number(page.memory.pressureSome, 0) : ""
        }

        Ring {
            readonly property int size: page.appearance.px(70)

            x: dashboard.centerAt(132, size)
            y: page.appearance.px(10)
            width: size
            height: size
            appearance: page.appearance
            values: page.detailed ? [page.share(page.memory.app), page.share(page.kernel), page.share(page.memory.compressed)]
                                  : [page.memory.usage ?? 0]
            colors: page.detailed ? [page.appearance.blue, page.appearance.orange, page.appearance.pink] : [page.appearance.blue]
            text: Logic.isNumber(page.memory.usage) ? Format.percent(page.memory.usage) : ""
        }
    }

    Separator { appearance: page.appearance; text: i18n("Usage history") }
    LineHistory {
        appearance: page.appearance
        implicitHeight: page.appearance.px(90)
        values: usageHistory.values

        HistorySeries {
            id: usageHistory

            source: page.source
            module: "memory"
            series: "usage"
        }
    }

    Separator { appearance: page.appearance; text: i18n("Details") }
    InfoRow { appearance: page.appearance; label: i18n("Used"); value: page.bytesText(page.memory.used) }
    InfoRow { appearance: page.appearance; label: i18n("App"); value: page.bytesText(page.memory.app); marker: page.appearance.blue }
    InfoRow { appearance: page.appearance; label: i18n("Kernel"); value: page.bytesText(page.kernel); marker: page.appearance.orange }
    InfoRow {
        appearance: page.appearance
        label: i18n("Compressed")
        value: (page.memory.compressed ?? 0) > 0 ? page.bytesText(page.memory.compressed) : ""
        marker: page.appearance.pink
    }
    InfoRow { appearance: page.appearance; label: i18n("Cache"); value: page.bytesText(page.memory.cache) }
    InfoRow { appearance: page.appearance; label: i18n("Free"); value: page.bytesText(page.memory.free); marker: page.appearance.idle }
    InfoRow {
        appearance: page.appearance
        label: i18n("Swap")
        value: (page.memory.swapTotal ?? 0) > 0 ? page.bytesText(page.memory.swapUsed) : ""
    }

    ProcessList {
        appearance: page.appearance
        processes: page.memory.processes ?? []
        count: page.configuration.processCount
        processValues: (page.memory.processes ?? []).map(process => [Format.bytes(process.memory)])
    }
}
