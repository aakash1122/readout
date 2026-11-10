// Style "tachometer" (Stats 3.5): a filled upper half disk, segments left to right.
import QtQuick
import QtQuick.Layouts

import org.kde.quickcharts as Charts

RowLayout {
    id: root

    required property ModuleState widget

    spacing: widget.size(2)

    VerticalLabel {
        Layout.alignment: Qt.AlignVCenter
        visible: root.widget.showLabel
        widget: root.widget
    }

    Item {
        Layout.alignment: Qt.AlignVCenter
        Layout.preferredWidth: root.widget.drawHeight
        Layout.preferredHeight: root.widget.drawHeight
        clip: true

        Charts.PieChart {
            // The half disk's flat edge sits a little below the middle, like Stats.
            x: 0
            y: root.widget.size(4)
            width: parent.width
            height: parent.width
            range { from: 0; to: 100; automatic: false }
            indexingMode: Charts.Chart.IndexAllValues
            fromAngle: -90
            toAngle: 90
            thickness: width / 2
            backgroundColor: root.widget.inactiveColor
            colorSource: Charts.ArraySource { array: root.widget.segments.colors }
            valueSources: Charts.ArraySource { array: root.widget.segments.values }
        }
    }
}
