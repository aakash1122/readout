// Disk popup (docs/visual-spec.md 2.4 and 6.5): one card per drive with read/write activity dots,
// a mirrored activity chart, a capacity bar per mounted filesystem, optional details and SMART rows.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import org.kde.plasma.components as PlasmaComponents

import "Logic.js" as Logic

PageLayout {
    id: page

    readonly property var disk: source.data.disk ?? {}
    readonly property bool bits: configuration.speedBits
    readonly property var mounts: disk.mounts ?? []
    // Drives with their filesystems; filesystems on unknown devices get a card of their own.
    readonly property var drives: {
        const drives = (disk.disks ?? []).map(entry => ({ drive: entry, name: entry.name, mounts: [] }))
        for (const mount of mounts) {
            let drive = drives.find(entry => entry.name === mount.disk)
            if (!drive) {
                drive = { drive: null, name: mount.disk ?? "", mounts: [] }
                drives.push(drive)
            }
            drive.mounts.push(mount)
        }
        // The panel tier has only the root filesystem, until the first detail update arrives.
        if (drives.length === 0 && Logic.isNumber(disk.total)) {
            drives.push({ drive: null, name: "", mounts: [{ mountPoint: disk.mountPoint, total: disk.total, used: disk.used, free: disk.free }] })
        }
        return drives
    }
    // Shared by every legend: click to switch between free and used.
    property bool showUsed: false

    spacing: appearance.cardSpacing

    Repeater {
        model: page.drives.length

        Card {
            id: card

            required property int index
            readonly property var modelData: page.drives[index] ?? ({ name: "", drive: null, mounts: [] })
            readonly property var drive: modelData.drive ?? {}
            readonly property bool hasDrive: modelData.drive !== null
            // Read and write activity dots: [rate, color when active].
            readonly property var activity: hasDrive ? [[drive.read ?? 0, page.appearance.blue], [drive.write ?? 0, page.appearance.red]] : []
            property bool showDetails: false

            appearance: page.appearance

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: page.appearance.px(16)
                spacing: page.appearance.px(2)

                StatsText {
                    Layout.maximumWidth: card.width - page.appearance.px(70)
                    Layout.rightMargin: page.appearance.px(4)
                    appearance: page.appearance
                    text: card.hasDrive ? (card.drive.model || card.drive.name) : (card.modelData.name || i18n("Volumes"))
                    color: page.appearance.withAlpha(page.appearance.text, 0.85)
                }
                Repeater {
                    model: card.activity.length

                    Rectangle {
                        required property int index
                        readonly property var modelData: card.activity[index] ?? [0, "transparent"]

                        Layout.preferredWidth: page.appearance.px(8)
                        Layout.preferredHeight: page.appearance.px(8)
                        radius: width / 2
                        color: modelData[0] > 0 ? modelData[1] : page.appearance.withAlpha(page.appearance.lightGray, 0.75)
                    }
                }
                Item { Layout.fillWidth: true }
                PlasmaComponents.ToolButton {
                    visible: card.hasDrive
                    implicitWidth: page.appearance.px(20)
                    implicitHeight: implicitWidth
                    icon.name: "configure"
                    icon.width: page.appearance.px(15)
                    icon.height: page.appearance.px(15)
                    display: PlasmaComponents.AbstractButton.IconOnly
                    checkable: true
                    checked: card.showDetails
                    text: i18n("Details")
                    onToggled: card.showDetails = checked
                    PlasmaComponents.ToolTip.text: text
                    PlasmaComponents.ToolTip.visible: hovered
                }
            }

            MirroredChart {
                visible: card.hasDrive
                implicitHeight: page.appearance.px(36)
                appearance: page.appearance
                color: page.appearance.dark ? page.appearance.well : "#ffffff"
                capacity: 120
                upperValues: driveWriteHistory.values
                lowerValues: driveReadHistory.values

                HistorySeries {
                    id: driveWriteHistory

                    source: page.source
                    module: "disk"
                    series: card.hasDrive ? card.modelData.name + "/write" : ""
                    length: 120
                }
                HistorySeries {
                    id: driveReadHistory

                    source: page.source
                    module: "disk"
                    series: card.hasDrive ? card.modelData.name + "/read" : ""
                    length: 120
                }
            }

            Repeater {
                model: card.modelData.mounts.length

                ColumnLayout {
                    id: volume

                    required property int index
                    readonly property var modelData: card.modelData.mounts[index] ?? ({ mountPoint: "", total: 0, used: 0, free: 0 })
                    readonly property real usedPercent: modelData.total > 0 ? modelData.used * 100 / modelData.total : 0

                    Layout.fillWidth: true
                    spacing: page.appearance.px(5)

                    CapacityBar {
                        Layout.leftMargin: 1
                        Layout.rightMargin: 1
                        appearance: page.appearance
                        percent: volume.usedPercent
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: page.appearance.px(16)
                        spacing: page.appearance.px(4)

                        StatsText {
                            Layout.fillWidth: true
                            appearance: page.appearance
                            points: 11
                            font.weight: Font.Light
                            text: page.showUsed
                                  ? i18nc("@info mount point: used of total", "%1: %2 of %3 used", volume.modelData.mountPoint,
                                          Format.bytes(volume.modelData.used), Format.bytes(volume.modelData.total))
                                  : i18nc("@info mount point: free of total", "%1: %2 of %3 free", volume.modelData.mountPoint,
                                          Format.bytes(volume.modelData.free), Format.bytes(volume.modelData.total))
                        }
                        StatsText {
                            appearance: page.appearance
                            points: 11
                            text: Format.percent(page.showUsed ? volume.usedPercent : 100 - volume.usedPercent)
                        }
                    }
                    TapHandler {
                        onTapped: page.showUsed = !page.showUsed
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: card.showDetails
                spacing: 0

                InfoRow {
                    appearance: page.appearance
                    label: i18n("Read")
                    value: Format.rate(card.drive.read ?? 0, page.bits)
                    marker: page.appearance.blue
                    valuePoints: 11
                }
                InfoRow {
                    appearance: page.appearance
                    label: i18n("Write")
                    value: Format.rate(card.drive.write ?? 0, page.bits)
                    marker: page.appearance.red
                    valuePoints: 11
                }
                InfoRow {
                    appearance: page.appearance
                    label: i18n("Total read")
                    value: Logic.isNumber(card.drive.totalRead) ? Format.bytes(card.drive.totalRead) : ""
                    valuePoints: 11
                }
                InfoRow {
                    appearance: page.appearance
                    label: i18n("Total written")
                    value: Logic.isNumber(card.drive.totalWritten) ? Format.bytes(card.drive.totalWritten) : ""
                    valuePoints: 11
                }
                InfoRow {
                    appearance: page.appearance
                    label: i18n("Size")
                    value: Logic.isNumber(card.drive.size) ? i18nc("@info size, device", "%1, /dev/%2", Format.bytes(card.drive.size), card.drive.name) : ""
                    valuePoints: 11
                }

                Separator {
                    visible: Logic.isNumber(card.drive.temperature) || card.drive.healthy !== undefined || Logic.isNumber(card.drive.powerOnHours)
                    appearance: page.appearance
                    text: i18nc("@title disk health section", "SMART")
                }
                InfoRow {
                    appearance: page.appearance
                    label: i18n("Temperature")
                    value: Logic.isNumber(card.drive.temperature) ? Format.temperature(card.drive.temperature) : ""
                    valuePoints: 11
                }
                InfoRow {
                    appearance: page.appearance
                    label: i18n("Health")
                    value: card.drive.healthy === undefined ? "" : card.drive.healthy ? i18nc("@info SMART status", "OK") : i18nc("@info SMART status", "Failing")
                    valueColor: card.drive.healthy === false ? page.appearance.red : page.appearance.text
                    valuePoints: 11
                }
                InfoRow {
                    appearance: page.appearance
                    label: i18n("Power on hours")
                    value: Logic.isNumber(card.drive.powerOnHours) && card.drive.powerOnHours >= 0 ? Format.number(card.drive.powerOnHours) : ""
                    valuePoints: 11
                }
            }
        }
    }

    ProcessList {
        Layout.topMargin: -page.appearance.cardSpacing
        appearance: page.appearance
        processes: page.disk.processes ?? []
        count: page.configuration.processCount
        columnColors: [page.appearance.blue, page.appearance.red]
        valuesFor: process => [Format.rate(process.read, page.bits), Format.rate(process.write, page.bits)]
    }
}
