import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

ConfigPage {
    id: page

    Kirigami.FormLayout {
        QQC2.Label {
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
            text: i18n("An alert is sent when its condition has lasted 10 seconds, and again only after the value has gone back past the threshold. Alerts follow Do Not Disturb.")
            wrapMode: Text.WordWrap
        }

        Item { Kirigami.FormData.isSection: true }

        AlertRow {
            label: i18n("CPU usage above:")
            checked: page.cfg_alertCpuUsage
            onToggled: checked => page.cfg_alertCpuUsage = checked
            value: page.cfg_alertCpuUsageThreshold
            onValueModified: value => page.cfg_alertCpuUsageThreshold = value
            unit: "%"
        }
        AlertRow {
            label: i18n("Memory usage above:")
            checked: page.cfg_alertMemoryUsage
            onToggled: checked => page.cfg_alertMemoryUsage = checked
            value: page.cfg_alertMemoryUsageThreshold
            onValueModified: value => page.cfg_alertMemoryUsageThreshold = value
            unit: "%"
        }
        AlertRow {
            label: i18n("CPU temperature above:")
            checked: page.cfg_alertCpuTemperature
            onToggled: checked => page.cfg_alertCpuTemperature = checked
            value: page.cfg_alertCpuTemperatureThreshold
            onValueModified: value => page.cfg_alertCpuTemperatureThreshold = value
            from: 30
            to: 120
            unit: "°C"
        }
        AlertRow {
            label: i18n("GPU temperature above:")
            checked: page.cfg_alertGpuTemperature
            onToggled: checked => page.cfg_alertGpuTemperature = checked
            value: page.cfg_alertGpuTemperatureThreshold
            onValueModified: value => page.cfg_alertGpuTemperatureThreshold = value
            from: 30
            to: 120
            unit: "°C"
        }
        AlertRow {
            label: i18n("Disk free space below:")
            checked: page.cfg_alertDiskFree
            onToggled: checked => page.cfg_alertDiskFree = checked
            value: page.cfg_alertDiskFreeThreshold
            onValueModified: value => page.cfg_alertDiskFreeThreshold = value
            to: 99
            unit: "%"
        }
        AlertRow {
            label: i18n("Battery below (discharging):")
            checked: page.cfg_alertBatteryLow
            onToggled: checked => page.cfg_alertBatteryLow = checked
            value: page.cfg_alertBatteryLowThreshold
            onValueModified: value => page.cfg_alertBatteryLowThreshold = value
            to: 99
            unit: "%"
        }
        AlertRow {
            label: i18n("Battery reached (charging):")
            checked: page.cfg_alertBatteryCharged
            onToggled: checked => page.cfg_alertBatteryCharged = checked
            value: page.cfg_alertBatteryChargedThreshold
            onValueModified: value => page.cfg_alertBatteryChargedThreshold = value
            unit: "%"
        }
        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Network:")
            text: i18n("Alert when the connection is lost")
            checked: page.cfg_alertConnectivity
            onToggled: page.cfg_alertConnectivity = checked
        }
    }
}
