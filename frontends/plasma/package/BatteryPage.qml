// Battery popup (docs/visual-spec.md 2.6 and 6.5): large battery drawing, details, battery health,
// power adapter, then peripheral batteries as cards.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "Logic.js" as Logic

PageLayout {
    id: page

    readonly property var battery: source.data.battery ?? {}
    readonly property var batteries: battery.batteries ?? []
    readonly property var first: batteries[0] ?? {}
    readonly property var peripherals: battery.peripherals ?? []
    readonly property bool present: battery.present === true
    readonly property bool charging: battery.state === "charging"

    function levelColor(level) {
        return Logic.isNumber(level) && level < 20 ? appearance.red : appearance.text
    }
    function timeText() {
        if (battery.state === "full") {
            return i18nc("@info:status battery", "Fully charged")
        }
        const seconds = charging ? battery.timeToFull : battery.timeToEmpty
        if (battery.state !== "charging" && battery.state !== "discharging") {
            return i18nc("@info battery time", "Unknown")
        }
        return (seconds ?? 0) > 0 ? Format.duration(seconds) : i18nc("@info battery time", "Calculating")
    }

    EmptyMessage {
        visible: !page.present
        Layout.preferredHeight: page.appearance.dashboardHeight
        appearance: page.appearance
        text: page.battery.available === false ? i18n("Battery information is not available (UPower is not running).")
                                               : i18n("This computer has no battery.")
    }

    Dashboard {
        visible: page.present
        appearance: page.appearance

        BatteryGlyph {
            anchors.centerIn: parent
            width: page.appearance.px(120)
            height: page.appearance.px(50)
            radius: page.appearance.px(3)
            lineWidth: 1
            nubWidth: page.appearance.px(5)
            gap: page.appearance.px(2)
            level: page.battery.percentage ?? NaN
            outlineColor: page.appearance.text
            fillColor: page.levelColor(page.battery.percentage)
            knockoutColor: page.appearance.background
            charging: page.charging
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: page.present
        spacing: 0

        Separator { appearance: page.appearance; text: i18n("Details") }
        InfoRow {
            appearance: page.appearance
            label: i18n("Level")
            value: Logic.isNumber(page.battery.percentage) ? Format.percent(page.battery.percentage) : ""
        }
        InfoRow {
            appearance: page.appearance
            label: i18n("Source")
            value: page.battery.externalPower ? i18nc("@info power source", "AC Power") : i18nc("@info power source", "Battery Power")
        }
        InfoRow {
            appearance: page.appearance
            label: page.charging ? i18n("Time to charge") : i18n("Time to discharge")
            value: page.timeText()
        }
        InfoRow {
            appearance: page.appearance
            label: i18n("Last charge")
            value: page.context.unpluggedAt > 0 && !page.battery.externalPower
                   ? i18nc("@info time since unplugged", "%1 ago", Format.duration(Math.max(60, (Date.now() - page.context.unpluggedAt) / 1000))) : ""
        }

        Separator { appearance: page.appearance; text: i18n("Battery") }
        InfoRow {
            appearance: page.appearance
            label: i18n("Health")
            value: Logic.isNumber(page.first.health) && page.first.health > 0 ? Format.percent(page.first.health) : ""
        }
        InfoRow {
            appearance: page.appearance
            label: i18n("Capacity")
            value: (page.first.energyFull ?? 0) > 0
                   ? i18nc("@info battery energy now / full / design", "%1 / %2 / %3 Wh", Format.number(page.first.energy ?? 0, 1),
                           Format.number(page.first.energyFull, 1), Format.number(page.first.energyFullDesign ?? 0, 1))
                   : ""
        }
        InfoRow { appearance: page.appearance; label: i18n("Cycles"); value: (page.first.cycles ?? -1) >= 0 ? Format.number(page.first.cycles) : "" }
        InfoRow {
            appearance: page.appearance
            label: i18n("Temperature")
            value: (page.first.temperature ?? 0) > 0 ? Format.temperature(page.first.temperature) : ""
        }
        InfoRow { appearance: page.appearance; label: i18n("Power"); value: Logic.isNumber(page.battery.power) ? Format.power(page.battery.power) : "" }
        InfoRow {
            appearance: page.appearance
            label: i18n("Current")
            value: (page.first.voltage ?? 0) > 0 && (page.first.power ?? 0) > 0
                   ? i18nc("@info milliamperes", "%1 mA", Format.number(page.first.power / page.first.voltage * 1000)) : ""
        }
        InfoRow { appearance: page.appearance; label: i18n("Voltage"); value: (page.first.voltage ?? 0) > 0 ? Format.voltage(page.first.voltage) : "" }
        InfoRow { appearance: page.appearance; label: i18n("Technology"); value: page.first.technology ?? "" }

        Separator {
            visible: page.battery.externalPower === true
            appearance: page.appearance
            text: i18n("Power adapter")
        }
        InfoRow {
            appearance: page.appearance
            label: i18n("Is charging")
            value: page.battery.externalPower === true ? (page.charging ? i18n("Yes") : i18n("No")) : ""
        }
    }

    Separator {
        visible: page.peripherals.length > 0
        appearance: page.appearance
        text: i18n("Devices")
    }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: page.appearance.cardSpacing

        Repeater {
            model: page.peripherals.length

            Card {
                id: device

                required property int index
                readonly property var modelData: page.peripherals[index] ?? ({ kind: "", model: "", percentage: 0 })

                appearance: page.appearance
                Layout.preferredHeight: page.appearance.px(30)

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: page.appearance.px(20)
                    spacing: page.appearance.px(6)

                    Kirigami.Icon {
                        Layout.preferredWidth: page.appearance.px(16)
                        Layout.preferredHeight: page.appearance.px(16)
                        source: Logic.iconForPeripheral(device.modelData.kind)
                    }
                    StatsText {
                        Layout.fillWidth: true
                        appearance: page.appearance
                        font.weight: Font.Light
                        text: device.modelData.model
                    }
                    StatsText {
                        appearance: page.appearance
                        points: 12
                        text: Logic.isNumber(device.modelData.percentage) ? Format.percent(device.modelData.percentage) : ""
                    }
                }
            }
        }
    }
}
