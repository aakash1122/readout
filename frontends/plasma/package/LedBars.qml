// One LED meter per core, 16 blocks each, in a well (Stats BarChartView).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Well {
    id: bars

    property var values: []
    // One color per value; missing entries use the accent.
    property var colors: []
    readonly property int blockCount: 16
    readonly property int gap: appearance.px(2)
    readonly property color columnColor: appearance.withAlpha(appearance.dark ? "#282828" : "#969696", 0.5)
    readonly property color offColor: appearance.withAlpha(appearance.dark ? "#1e1e1e" : "#ffffff", 0.4)

    implicitHeight: appearance.px(50)

    Row {
        id: row

        anchors.fill: parent
        anchors.margins: bars.appearance.px(2)
        spacing: bars.gap

        Repeater {
            model: bars.values.length

            Rectangle {
                id: column

                required property int index
                readonly property int active: Math.round(Math.max(0, Math.min(100, bars.values[index] ?? 0)) / 100 * bars.blockCount)
                readonly property real blockHeight: (height - 3) / bars.blockCount - 1

                width: (row.width - (bars.values.length - 1) * bars.gap) / Math.max(1, bars.values.length)
                height: row.height
                radius: bars.appearance.px(3)
                color: bars.columnColor

                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 2
                    spacing: 1

                    Repeater {
                        model: bars.blockCount

                        Rectangle {
                            required property int index

                            width: Math.max(1, column.width - 4)
                            height: Math.max(1, column.blockHeight)
                            radius: 1
                            // Index 0 is the top block; the meter fills from the bottom.
                            color: bars.blockCount - index <= column.active ? (bars.colors[column.index] ?? bars.appearance.accent) : bars.offColor
                        }
                    }
                }
            }
        }
    }
}
