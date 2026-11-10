// Bluetooth popup (docs/visual-spec.md 2.8): one card per device with its battery level.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "Logic.js" as Logic

PageLayout {
    id: page

    readonly property var bluetooth: source.data.bluetooth ?? {}
    readonly property var devices: bluetooth.devices ?? []
    readonly property var shownDevices: bluetooth.available && bluetooth.powered ? devices : []

    spacing: appearance.cardSpacing

    EmptyMessage {
        visible: page.devices.length === 0 || !page.bluetooth.available || !page.bluetooth.powered
        appearance: page.appearance
        text: !page.bluetooth.available ? i18n("Bluetooth is not available")
            : !page.bluetooth.powered ? i18n("Bluetooth is off")
            : i18n("No Bluetooth devices are available")
    }

    Repeater {
        model: page.shownDevices.length

        Card {
            id: device

            required property int index
            readonly property var modelData: page.shownDevices[index] ?? ({ name: "", icon: "", connected: false })

            appearance: page.appearance
            Layout.preferredHeight: page.appearance.px(30)
            opacity: modelData.connected ? 1 : 0.6

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: page.appearance.px(20)
                spacing: page.appearance.px(6)

                Kirigami.Icon {
                    Layout.preferredWidth: page.appearance.px(16)
                    Layout.preferredHeight: page.appearance.px(16)
                    source: Logic.iconForBluetoothDevice(device.modelData.icon)
                }
                StatsText {
                    Layout.fillWidth: true
                    appearance: page.appearance
                    font.weight: Font.Light
                    text: device.modelData.name
                }
                StatsText {
                    appearance: page.appearance
                    points: 12
                    color: Logic.isNumber(device.modelData.battery) ? page.appearance.text : page.appearance.label
                    text: Logic.isNumber(device.modelData.battery) ? Format.percent(device.modelData.battery)
                        : device.modelData.connected ? i18nc("@info:status", "Connected") : i18nc("@info:status", "Not connected")
                }
            }
        }
    }
}
