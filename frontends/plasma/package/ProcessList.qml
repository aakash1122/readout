// "Top processes" with app icons and one or two value columns (Stats ProcessesView). Read-only.
// Like Stats, it always has `count` rows, blank when fewer processes are reported, so the popup
// keeps its height from the first frame and as processes come and go.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

ColumnLayout {
    id: list

    required property Appearance appearance
    property var processes: []
    property int count: 8
    // One column: its title. Two columns: their legend colors.
    property string valueTitle: i18nc("@title:column process list", "Usage")
    property var columnColors: []
    property var valuesFor: process => [""]

    readonly property var shown: (processes ?? []).slice(0, Math.max(0, count))
    readonly property bool twoColumns: columnColors.length === 2

    Layout.fillWidth: true
    visible: count > 0
    spacing: 0

    Separator {
        appearance: list.appearance
        text: i18n("Top processes")
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: list.appearance.rowHeight
        spacing: 0

        Item { Layout.preferredWidth: list.appearance.px(22) }
        StatsText {
            Layout.fillWidth: true
            appearance: list.appearance
            points: 12
            font.weight: Font.Medium
            text: i18nc("@title:column process list", "Process")
            color: list.appearance.tertiary
        }
        StatsText {
            visible: !list.twoColumns
            appearance: list.appearance
            points: 12
            font.weight: Font.Medium
            text: list.valueTitle
            color: list.appearance.tertiary
        }
        Repeater {
            model: list.twoColumns ? list.columnColors : []

            Item {
                id: legend

                required property color modelData

                Layout.preferredWidth: list.appearance.px(60)
                Layout.fillHeight: true

                Rectangle {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: list.appearance.markerSize
                    height: width
                    radius: list.appearance.px(4)
                    color: legend.modelData
                }
            }
        }
    }

    Repeater {
        model: Math.max(0, list.count)

        RowLayout {
            id: processRow

            required property int index
            readonly property bool filled: index < list.shown.length
            readonly property var modelData: list.shown[index] ?? ({ name: "" })
            readonly property var values: filled ? list.valuesFor(modelData) : []

            Layout.fillWidth: true
            Layout.preferredHeight: list.appearance.rowHeight
            spacing: 0

            Item {
                Layout.preferredWidth: list.appearance.px(22)
                Layout.fillHeight: true

                Kirigami.Icon {
                    visible: processRow.filled
                    x: list.appearance.px(2)
                    anchors.verticalCenter: parent.verticalCenter
                    width: list.appearance.markerSize
                    height: width
                    // Most apps install an icon named after their executable.
                    source: processRow.modelData.name.toLowerCase()
                    fallback: "application-x-executable"
                }
            }
            StatsText {
                Layout.fillWidth: true
                appearance: list.appearance
                points: 12
                text: processRow.modelData.name
                color: list.appearance.label
            }
            Repeater {
                model: processRow.values.length

                StatsText {
                    required property int index
                    readonly property string modelData: processRow.values[index] ?? ""

                    Layout.preferredWidth: list.twoColumns ? list.appearance.px(60) : -1
                    appearance: list.appearance
                    points: list.twoColumns ? 10 : 13
                    horizontalAlignment: Text.AlignRight
                    text: modelData
                }
            }
        }
    }
}
