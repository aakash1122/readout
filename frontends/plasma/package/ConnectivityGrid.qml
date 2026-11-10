// 30 x 3 grid of connectivity samples: green online, red offline, newest at the bottom right
// (Stats GridChartView).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Well {
    id: grid

    // Booleans, newest last.
    property var values: []
    readonly property int columns: 30
    readonly property int rows: 3
    readonly property int gap: appearance.px(2)
    readonly property color emptyColor: appearance.withAlpha(appearance.dark ? "#282828" : "#969696", 0.4)

    implicitHeight: appearance.px(30)
    color: "transparent"

    Grid {
        anchors.fill: parent
        anchors.topMargin: 1
        anchors.bottomMargin: 1
        columns: grid.columns
        rows: grid.rows
        spacing: grid.gap

        Repeater {
            model: grid.columns * grid.rows

            Rectangle {
                required property int index
                readonly property int sample: grid.values.length - grid.columns * grid.rows + index
                readonly property var value: sample >= 0 ? grid.values[sample] : undefined

                width: (grid.width - (grid.columns - 1) * grid.gap) / grid.columns
                height: (grid.height - 2 - (grid.rows - 1) * grid.gap) / grid.rows
                radius: 1
                color: value === undefined ? grid.emptyColor : value ? grid.appearance.green : grid.appearance.red
            }
        }
    }
}
