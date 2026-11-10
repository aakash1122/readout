// Compact summary of one module for an item that shows several modules (docs/visual-spec.md 6.4):
// a title row with a button to the full page, a ring or battery on the left, up to four rows.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import org.kde.plasma.components as PlasmaComponents

import "Logic.js" as Logic

ColumnLayout {
    id: summary

    required property string module
    required property AppletContext context
    required property Appearance appearance
    signal openRequested()

    readonly property ReadoutSource source: context.source
    readonly property var configuration: context.configuration
    readonly property var payload: source.data[module] ?? {}
    readonly property bool bits: configuration.speedBits

    readonly property var gpu: module === "gpu" ? Logic.selectGpu(payload.gpus, configuration.gpu) : null
    readonly property var memoryParts: {
        if (module !== "memory" || !Logic.isNumber(payload.app)) {
            return null
        }
        const total = Math.max(1, payload.total ?? 1)
        const kernel = Math.max(0, (payload.used ?? 0) - payload.app - (payload.compressed ?? 0))
        return [payload.app * 100 / total, kernel * 100 / total, (payload.compressed ?? 0) * 100 / total]
    }

    function percentText(value) {
        return Logic.isNumber(value) ? Format.percent(value) : ""
    }
    function bytesText(value) {
        return Logic.isNumber(value) ? Format.bytes(value) : ""
    }

    // Ring segments and colors, or null for no ring.
    readonly property var ring: {
        switch (module) {
        case "cpu":
            return { values: [payload.system ?? 0, payload.user ?? 0], colors: [appearance.red, appearance.blue], text: percentText(payload.usage) }
        case "memory":
            return memoryParts ? { values: memoryParts, colors: [appearance.blue, appearance.orange, appearance.pink], text: percentText(payload.usage) }
                               : { values: [payload.usage ?? 0], colors: [appearance.blue], text: percentText(payload.usage) }
        case "gpu":
            return gpu && gpu.state === "active" ? { values: [gpu.usage ?? 0], colors: [appearance.blue], text: percentText(gpu.usage) } : null
        case "disk":
            return Logic.isNumber(payload.usage) ? { values: [payload.usage], colors: [appearance.accent], text: percentText(payload.usage) } : null
        }
        return null
    }

    // [label, value, marker] rows shown beside the ring.
    readonly property var shownRows: rows.slice(0, module === "clock" ? 8 : 4)
    readonly property var rows: {
        switch (module) {
        case "cpu": {
            const list = [[i18n("Usage"), percentText(payload.usage)], [i18n("System"), percentText(payload.system), appearance.red],
                          [i18n("User"), percentText(payload.user), appearance.blue]]
            if (Logic.isNumber(payload.temperature)) {
                list.push([i18n("Temperature"), Format.temperature(payload.temperature)])
            }
            return list
        }
        case "gpu":
            if (!gpu) {
                return [[i18n("GPU"), i18n("None found")]]
            }
            return [[i18n("Usage"), gpu.state === "suspended" ? i18nc("@info:status GPU", "Sleeping") : percentText(gpu.usage)],
                    [i18n("Temperature"), Logic.isNumber(gpu.temperature) ? Format.temperature(gpu.temperature) : ""],
                    [i18n("Memory used"), (gpu.memoryTotal ?? 0) > 0 ? bytesText(gpu.memoryUsed) : ""]]
        case "memory":
            return [[i18n("Used"), bytesText(payload.used)],
                    [i18n("Free"), bytesText(payload.free ?? payload.available)],
                    [i18n("Swap"), (payload.swapTotal ?? 0) > 0 ? bytesText(payload.swapUsed) : ""],
                    [i18n("Pressure"), {normal: i18nc("@info memory pressure", "Normal"), warning: i18nc("@info memory pressure", "Warning"),
                                        critical: i18nc("@info memory pressure", "Critical")}[payload.pressureLevel] ?? ""]]
        case "disk":
            return [[i18n("Volume"), payload.mountPoint ?? ""], [i18n("Used"), bytesText(payload.used)], [i18n("Free"), bytesText(payload.free)]]
        case "network":
            return [[i18n("Upload"), Format.rate(payload.upload ?? 0, bits), appearance.red],
                    [i18n("Download"), Format.rate(payload.download ?? 0, bits), appearance.blue]]
        case "battery":
            if (payload.present !== true) {
                return [[i18n("Battery"), payload.available === false ? i18n("Not available") : i18n("None")]]
            }
            return [[i18n("Level"), percentText(payload.percentage)],
                    [i18n("Source"), payload.externalPower ? i18nc("@info power source", "AC Power") : i18nc("@info power source", "Battery Power")],
                    [i18n("Health"), Logic.isNumber(payload.batteries?.[0]?.health) ? Format.percent(payload.batteries[0].health) : ""]]
        case "sensors":
        case "bluetooth": {
            const items = context.summary.stackItems(module, source.data[module], configuration)
            if (items.length > 0) {
                return items.map(item => [item.label, item.text])
            }
            return module === "bluetooth" ? [[i18n("Devices"), payload.powered ? i18n("None connected") : i18nc("@info:status", "Off")]]
                                          : [[i18n("Sensors"), i18n("None selected")]]
        }
        case "clock":
            return [[context.clock.local.city, context.clock.time]].concat(context.clock.entries.map(entry => [entry.city, entry.time]))
        }
        return []
    }

    Layout.fillWidth: true
    spacing: 0

    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: summary.appearance.separatorHeight

        StatsText {
            Layout.fillWidth: true
            appearance: summary.appearance
            points: 13
            font.weight: Font.Medium
            text: summary.context.summary.titleFor(summary.module)
        }
        PlasmaComponents.ToolButton {
            implicitWidth: summary.appearance.px(24)
            implicitHeight: implicitWidth
            icon.name: "go-next"
            icon.width: summary.appearance.px(14)
            icon.height: summary.appearance.px(14)
            display: PlasmaComponents.AbstractButton.IconOnly
            text: i18nc("@action:button open the full page of a module", "Open %1", summary.context.summary.titleFor(summary.module))
            onClicked: summary.openRequested()
            PlasmaComponents.ToolTip.text: text
            PlasmaComponents.ToolTip.visible: hovered
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: summary.appearance.px(10)

        Item {
            visible: summary.ring !== null || summary.module === "battery" && summary.payload.present === true
            Layout.preferredWidth: summary.appearance.px(70)
            Layout.preferredHeight: summary.appearance.px(70)

            Ring {
                anchors.fill: parent
                visible: summary.ring !== null
                appearance: summary.appearance
                values: summary.ring?.values ?? []
                colors: summary.ring?.colors ?? []
                text: summary.ring?.text ?? ""
            }
            BatteryGlyph {
                anchors.centerIn: parent
                visible: summary.module === "battery"
                width: summary.appearance.px(60)
                height: summary.appearance.px(28)
                radius: summary.appearance.px(3)
                nubWidth: summary.appearance.px(4)
                level: summary.payload.percentage ?? NaN
                outlineColor: summary.appearance.text
                fillColor: (summary.payload.percentage ?? 100) < 20 ? summary.appearance.red : summary.appearance.text
                knockoutColor: summary.appearance.background
                charging: summary.payload.state === "charging"
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 0

            Repeater {
                model: summary.shownRows.length

                InfoRow {
                    required property int index
                    readonly property var modelData: summary.shownRows[index] ?? ["", ""]

                    appearance: summary.appearance
                    label: modelData[0]
                    value: modelData[1]
                    marker: modelData[2] ?? "transparent"
                }
            }
        }
    }

    MirroredChart {
        visible: summary.module === "network" || summary.module === "disk"
        Layout.topMargin: summary.appearance.px(4)
        implicitHeight: summary.appearance.px(40)
        appearance: summary.appearance
        upperValues: upperHistory.values
        lowerValues: lowerHistory.values

        HistorySeries {
            id: upperHistory

            source: summary.source
            module: summary.module
            series: summary.module === "network" ? "upload" : summary.module === "disk" ? "write" : ""
        }
        HistorySeries {
            id: lowerHistory

            source: summary.source
            module: summary.module
            series: summary.module === "network" ? "download" : summary.module === "disk" ? "read" : ""
        }
    }
    InfoRow {
        appearance: summary.appearance
        label: i18n("Write")
        value: summary.module === "disk" ? Format.rate(summary.payload.write ?? 0, summary.bits) : ""
        marker: summary.appearance.red
    }
    InfoRow {
        appearance: summary.appearance
        label: i18n("Read")
        value: summary.module === "disk" ? Format.rate(summary.payload.read ?? 0, summary.bits) : ""
        marker: summary.appearance.blue
    }
    InfoRow {
        appearance: summary.appearance
        label: i18n("Local IP")
        value: summary.module === "network" ? ((summary.payload.interfaces ?? []).find(entry => entry.name === summary.payload.interface)?.ipv4?.[0] ?? "") : ""
    }
}
