// Style "dualLine" (Stats 3.6): upload above the center line in red, download below it in blue
// (write and read for the disk), each half scaled to its own maximum.
import QtQuick
import QtQuick.Layouts

RowLayout {
    id: root

    required property ModuleState widget

    readonly property bool isDisk: widget.module === "disk"

    spacing: widget.size(2)

    VerticalLabel {
        Layout.alignment: Qt.AlignVCenter
        visible: root.widget.showLabel
        widget: root.widget
    }

    MirroredChart {
        Layout.alignment: Qt.AlignVCenter
        Layout.fillWidth: false
        Layout.preferredWidth: root.widget.size(30)
        Layout.preferredHeight: root.widget.drawHeight
        appearance: root.widget.appearance
        color: "transparent"
        capacity: 60
        upperValues: upperHistory.values
        lowerValues: lowerHistory.values

        HistorySeries {
            id: upperHistory

            source: root.widget.source
            module: root.widget.module
            series: root.isDisk ? "write" : "upload"
            length: 60
        }
        HistorySeries {
            id: lowerHistory

            source: root.widget.source
            module: root.widget.module
            series: root.isDisk ? "read" : "download"
            length: 60
        }
    }
}
