// Style "pie" (Stats 3.4): a filled pie, segments clockwise from 12 o'clock.
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

    Charts.PieChart {
        Layout.alignment: Qt.AlignVCenter
        Layout.preferredWidth: root.widget.drawHeight
        Layout.preferredHeight: root.widget.drawHeight
        range { from: 0; to: 100; automatic: false }
        indexingMode: Charts.Chart.IndexAllValues
        thickness: root.widget.drawHeight / 2
        backgroundColor: root.widget.inactiveColor
        colorSource: Charts.ArraySource { array: root.widget.segments.colors }
        valueSources: Charts.ArraySource { array: root.widget.segments.values }
    }
}
