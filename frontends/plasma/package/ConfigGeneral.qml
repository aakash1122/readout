pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "Logic.js" as Logic

ConfigPage {
    id: page

    // The update interval and the popup look are shared by every Readout widget,
    // so they are saved outside this widget's config.
    property int updateInterval: Settings.updateInterval
    property int popupTextScale: Settings.popupTextScale
    property int popupSpacing: Settings.popupSpacing
    property int popupMargin: Settings.popupMargin
    property int popupWidth: Settings.popupWidth
    signal configurationChanged()

    function saveConfig() {
        Settings.updateInterval = updateInterval
        Settings.popupTextScale = popupTextScale
        Settings.popupSpacing = popupSpacing
        Settings.popupMargin = popupMargin
        Settings.popupWidth = popupWidth
    }

    function hasAny(names) {
        return (cfg_modules ?? []).some(module => names.indexOf(module) >= 0)
    }
    function toggleModule(module, checked) {
        const current = (cfg_modules ?? []).filter(name => name !== module)
        if (checked) {
            current.push(module)
        }
        // Keep the canonical order; never leave the item empty.
        const ordered = Logic.allModules.filter(name => current.indexOf(name) >= 0)
        cfg_modules = ordered.length > 0 ? ordered : ["cpu"]
    }
    function styleTitle(style) {
        switch (style) {
        case "text": return i18nc("@item:inlistbox widget style", "Text")
        case "mini": return i18nc("@item:inlistbox widget style", "Mini (label and value)")
        case "line": return i18nc("@item:inlistbox widget style", "Line chart")
        case "bars": return i18nc("@item:inlistbox widget style", "Bars")
        case "pie": return i18nc("@item:inlistbox widget style", "Pie")
        case "tachometer": return i18nc("@item:inlistbox widget style", "Gauge")
        case "speed": return i18nc("@item:inlistbox widget style", "Speed (two rows)")
        case "dualLine": return i18nc("@item:inlistbox widget style", "Two-line chart")
        case "state": return i18nc("@item:inlistbox widget style", "Connection dot")
        case "stack": return i18nc("@item:inlistbox widget style", "Stacked values")
        case "battery": return i18nc("@item:inlistbox widget style", "Battery")
        case "memory": return i18nc("@item:inlistbox widget style", "Free and used")
        }
        return style
    }

    // Static facts (sensor and GPU lists) come from the daemon.
    ReadoutSource {
        id: infoSource

        modules: page.hasAny(["sensors", "gpu"]) ? ["gpu"] : []
    }
    readonly property var sensorInfo: infoSource.info.sensors?.sensors ?? []
    readonly property var gpuInfo: infoSource.info.gpu?.gpus ?? []

    Summary {
        id: summary
    }

    WorldClock {
        id: zoneList

        active: false
    }

    Kirigami.FormLayout {
        // Modules
        ColumnLayout {
            Kirigami.FormData.label: i18n("Show:")
            spacing: 0

            Repeater {
                model: Logic.allModules

                QQC2.CheckBox {
                    required property string modelData

                    text: summary.titleFor(modelData)
                    checked: (page.cfg_modules ?? []).indexOf(modelData) >= 0
                    onToggled: page.toggleModule(modelData, checked)
                }
            }
        }

        QQC2.Label {
            visible: (page.cfg_modules ?? []).length > 1
            text: i18n("Several modules share one panel item, with one tab per module in the popup.")
            wrapMode: Text.WordWrap
            Layout.maximumWidth: Kirigami.Units.gridUnit * 18
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }

        Item { Kirigami.FormData.isSection: true }

        // One style picker per shown module.
        Repeater {
            model: (page.cfg_modules ?? []).filter(module => Logic.stylesByModule[module].length > 1)

            QQC2.ComboBox {
                id: styleBox

                required property string modelData
                readonly property var styles: Logic.stylesByModule[modelData]

                Kirigami.FormData.label: i18nc("@label:listbox style of a module, %1 is the module", "%1 style:", summary.titleFor(modelData))
                model: styles.map(style => page.styleTitle(style))
                currentIndex: Math.max(0, styles.indexOf(Logic.styleFor(modelData, page.cfg_styles)))
                onActivated: index => {
                    page.cfg_styles = Logic.withStyle(page.cfg_styles, modelData, styles[index])
                }
            }
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Panel:")
            text: i18n("Show label")
            checked: page.cfg_showLabel
            onToggled: page.cfg_showLabel = checked
        }
        QQC2.CheckBox {
            text: i18n("Show icon")
            checked: page.cfg_showIcon
            onToggled: page.cfg_showIcon = checked
        }

        QQC2.ComboBox {
            Kirigami.FormData.label: i18n("Colors:")
            readonly property var modes: ["accent", "thresholds", "monochrome"]
            model: [i18n("Accent color"), i18n("By usage level"), i18n("Text color only")]
            currentIndex: Math.max(0, modes.indexOf(page.cfg_colorMode))
            onActivated: index => page.cfg_colorMode = modes[index]
        }
        RowLayout {
            Kirigami.FormData.label: i18n("Warning at:")
            enabled: page.cfg_colorMode === "thresholds"

            QQC2.SpinBox {
                from: 1
                to: 100
                value: page.cfg_warningThreshold
                onValueModified: page.cfg_warningThreshold = value
            }
            QQC2.Label { text: i18n("% and critical at") }
            QQC2.SpinBox {
                from: 1
                to: 100
                value: page.cfg_criticalThreshold
                onValueModified: page.cfg_criticalThreshold = value
            }
            QQC2.Label { text: "%" }
        }

        Item { Kirigami.FormData.isSection: true }

        QQC2.SpinBox {
            Kirigami.FormData.label: i18n("Processes in popup:")
            visible: page.hasAny(["cpu", "memory", "disk"])
            from: 0
            to: 10
            value: page.cfg_processCount
            onValueModified: page.cfg_processCount = value
        }

        QQC2.ComboBox {
            Kirigami.FormData.label: i18n("Speed units:")
            visible: page.hasAny(["network", "disk"])
            model: [i18n("Bytes per second"), i18n("Bits per second")]
            currentIndex: page.cfg_speedBits ? 1 : 0
            onActivated: index => page.cfg_speedBits = index === 1
        }

        QQC2.ComboBox {
            Kirigami.FormData.label: i18n("GPU:")
            visible: page.hasAny(["gpu"]) && page.gpuInfo.length > 1
            readonly property var ids: [""].concat(page.gpuInfo.map(gpu => gpu.id))
            model: [i18n("Automatic")].concat(page.gpuInfo.map(gpu => gpu.name + (gpu.secondary ? " " + i18n("(secondary)") : "")))
            currentIndex: Math.max(0, ids.indexOf(page.cfg_gpu))
            onActivated: index => page.cfg_gpu = ids[index]
        }

        // Sensors shown in the panel.
        ColumnLayout {
            Kirigami.FormData.label: i18n("Sensors in panel:")
            visible: page.hasAny(["sensors"])
            spacing: 0

            QQC2.Label {
                visible: page.sensorInfo.length === 0
                text: i18n("No sensors reported yet.")
                opacity: 0.7
            }
            QQC2.Label {
                visible: page.sensorInfo.length > 0
                text: i18n("None selected shows the CPU and GPU temperature.")
                font: Kirigami.Theme.smallFont
                opacity: 0.7
            }
            Repeater {
                model: page.sensorInfo

                QQC2.CheckBox {
                    required property var modelData

                    text: modelData.label + " · " + modelData.chip
                    checked: (page.cfg_sensors ?? []).indexOf(modelData.id) >= 0
                    onToggled: {
                        const current = (page.cfg_sensors ?? []).filter(id => id !== modelData.id)
                        if (checked) {
                            current.push(modelData.id)
                        }
                        page.cfg_sensors = current
                    }
                }
            }
        }

        // Clock
        QQC2.ComboBox {
            Kirigami.FormData.label: i18n("Time format:")
            visible: page.hasAny(["clock"])
            readonly property var formats: ["locale", "24", "12"]
            model: [i18n("Use region settings"), i18n("24-hour"), i18n("12-hour")]
            currentIndex: Math.max(0, formats.indexOf(page.cfg_hourFormat))
            onActivated: index => page.cfg_hourFormat = formats[index]
        }
        QQC2.CheckBox {
            visible: page.hasAny(["clock"])
            text: i18n("Show seconds")
            checked: page.cfg_showSeconds
            onToggled: page.cfg_showSeconds = checked
        }
        ColumnLayout {
            Kirigami.FormData.label: i18n("Time zones:")
            visible: page.hasAny(["clock"])

            Repeater {
                model: page.cfg_timeZones ?? []

                RowLayout {
                    id: zoneRow

                    required property string modelData

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: zoneRow.modelData
                    }
                    QQC2.ToolButton {
                        icon.name: "list-remove"
                        onClicked: page.cfg_timeZones = (page.cfg_timeZones ?? []).filter(zone => zone !== zoneRow.modelData)
                        QQC2.ToolTip.text: i18n("Remove")
                        QQC2.ToolTip.visible: hovered
                    }
                }
            }
            RowLayout {
                QQC2.ComboBox {
                    id: zoneBox

                    Layout.preferredWidth: Kirigami.Units.gridUnit * 14
                    editable: true
                    // Loaded only when the clock is shown: the list has several hundred entries.
                    model: page.hasAny(["clock"]) ? zoneList.availableZones() : []
                }
                QQC2.Button {
                    text: i18n("Add")
                    icon.name: "list-add"
                    enabled: zoneBox.editText.length > 0
                    onClicked: {
                        const zone = zoneBox.editText
                        if ((page.cfg_timeZones ?? []).indexOf(zone) < 0 && zoneBox.find(zone) >= 0) {
                            page.cfg_timeZones = (page.cfg_timeZones ?? []).concat([zone])
                        }
                    }
                }
            }
        }

        Item { Kirigami.FormData.isSection: true }

        RowLayout {
            Kirigami.FormData.label: i18n("Update every:")

            QQC2.SpinBox {
                from: Settings.minimumInterval
                to: Settings.maximumInterval
                value: page.updateInterval
                onValueModified: {
                    page.updateInterval = value
                    page.configurationChanged()
                }
            }
            QQC2.Label { text: i18np("second", "seconds", page.updateInterval) }
        }

        Item {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Popup")
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Text size:")

            QQC2.Slider {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                from: 80
                to: 130
                stepSize: 5
                snapMode: QQC2.Slider.SnapAlways
                value: page.popupTextScale
                onMoved: {
                    page.popupTextScale = Math.round(value)
                    page.configurationChanged()
                }
            }
            QQC2.Label { text: i18nc("@label percent of the default text size", "%1%", page.popupTextScale) }
        }
        QQC2.ComboBox {
            Kirigami.FormData.label: i18n("Spacing:")
            model: [i18nc("@item:inlistbox popup spacing", "Compact"), i18nc("@item:inlistbox popup spacing", "Normal"),
                    i18nc("@item:inlistbox popup spacing", "Comfortable")]
            currentIndex: page.popupSpacing
            onActivated: index => {
                page.popupSpacing = index
                page.configurationChanged()
            }
        }
        RowLayout {
            Kirigami.FormData.label: i18n("Margins:")

            QQC2.SpinBox {
                from: 0
                to: 16
                value: page.popupMargin
                onValueModified: {
                    page.popupMargin = value
                    page.configurationChanged()
                }
            }
            QQC2.Label { text: i18nc("@label unit of the popup margin", "pt") }
        }
        QQC2.ComboBox {
            Kirigami.FormData.label: i18n("Width:")
            model: [i18nc("@item:inlistbox popup width", "Narrow"), i18nc("@item:inlistbox popup width", "Normal"),
                    i18nc("@item:inlistbox popup width", "Wide")]
            currentIndex: page.popupWidth
            onActivated: index => {
                page.popupWidth = index
                page.configurationChanged()
            }
        }
        QQC2.Button {
            text: i18n("Reset Popup Look")
            icon.name: "edit-undo"
            readonly property var defaults: Settings.popupDefaults
            enabled: page.popupTextScale !== defaults.textScale || page.popupSpacing !== defaults.spacing
                     || page.popupMargin !== defaults.margin || page.popupWidth !== defaults.width
            onClicked: {
                page.popupTextScale = defaults.textScale
                page.popupSpacing = defaults.spacing
                page.popupMargin = defaults.margin
                page.popupWidth = defaults.width
                page.configurationChanged()
            }
        }
        QQC2.Label {
            text: i18n("The update interval and the popup look are shared by all Readout widgets.")
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }
    }
}
