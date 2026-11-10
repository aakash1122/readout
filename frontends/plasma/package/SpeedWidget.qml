// Style "speed" (Stats 3.7): two rows, upload over download (network) or write over read (disk),
// each with a dot that takes the series color while data moves.
pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root

    required property ModuleState widget

    readonly property bool isDisk: widget.module === "disk"
    readonly property bool bits: widget.configuration.speedBits === true
    readonly property real upperRate: widget.payload ? (isDisk ? widget.payload.write : widget.payload.upload) ?? 0 : 0
    readonly property real lowerRate: widget.payload ? (isDisk ? widget.payload.read : widget.payload.download) ?? 0 : 0
    // Below this a direction counts as idle.
    readonly property real activeRate: 1024
    readonly property real rowHeight: widget.drawHeight / 2

    implicitWidth: widget.size(7) + Math.max(widget.size(48), rows.implicitWidth)
    implicitHeight: widget.drawHeight

    Column {
        anchors.verticalCenter: parent.verticalCenter

        Repeater {
            model: [
                { rate: root.upperRate, color: root.widget.appearance.red },
                { rate: root.lowerRate, color: root.widget.appearance.blue },
            ]

            Item {
                id: speedRow

                required property var modelData

                width: root.width
                height: root.rowHeight

                Rectangle {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.widget.size(6)
                    height: width
                    radius: width / 2
                    color: speedRow.modelData.rate >= root.activeRate ? speedRow.modelData.color : root.widget.appearance.text
                }
                PanelText {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: Format.rate(speedRow.modelData.rate, root.bits)
                    template: root.bits ? "999.9 Mbit/s" : "999.9 kB/s"
                    horizontalAlignment: Text.AlignRight
                    color: root.widget.appearance.text
                    font.pixelSize: root.widget.fontPixels(9)
                    font.weight: Font.Light
                }
            }
        }
    }

    // Measures the widest value so the widget keeps its width.
    PanelText {
        id: rows

        visible: false
        template: root.bits ? "999.9 Mbit/s" : "999.9 kB/s"
        font.pixelSize: root.widget.fontPixels(9)
        font.weight: Font.Light
    }
}
