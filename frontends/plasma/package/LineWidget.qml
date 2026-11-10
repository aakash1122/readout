// Style "line" (Stats 3.2): recent values in a filled line chart inside a solid box.
import QtQuick
import QtQuick.Layouts

RowLayout {
    id: root

    required property ModuleState widget

    spacing: widget.size(2)

    VerticalLabel {
        Layout.alignment: Qt.AlignVCenter
        visible: root.widget.showLabel
        widget: root.widget
    }

    LineHistory {
        Layout.alignment: Qt.AlignVCenter
        Layout.fillWidth: false
        Layout.preferredWidth: root.widget.size(32)
        Layout.preferredHeight: root.widget.drawHeight
        appearance: root.widget.appearance
        color: root.widget.boxColor
        radius: root.widget.size(2)
        lineColor: root.widget.boxSeriesColor
        capacity: 60
        values: levelHistory.values

        HistorySeries {
            id: levelHistory

            source: root.widget.source
            module: root.widget.module
            series: root.widget.levelSeries
            length: 60
        }
    }
}
