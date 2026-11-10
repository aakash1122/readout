// Style "memory" (Stats 3.10): free over used in two small rows, for memory and disk.
pragma ComponentBehavior: Bound

import QtQuick

import "Logic.js" as Logic

Item {
    id: root

    required property ModuleState widget

    readonly property var payload: widget.payload ?? {}
    readonly property real free: widget.module === "memory" ? payload.available ?? NaN : payload.free ?? NaN
    readonly property real used: payload.used ?? NaN
    readonly property real rowHeight: widget.drawHeight / 2

    implicitWidth: widget.size(8 + 4) + Math.max(widget.size(50), measure.implicitWidth)
    implicitHeight: widget.drawHeight

    Column {
        anchors.verticalCenter: parent.verticalCenter

        Repeater {
            model: [
                { symbol: i18nc("@label short for free, panel", "F:"), value: root.free, color: root.widget.appearance.text },
                { symbol: i18nc("@label short for used, panel", "U:"), value: root.used, color: root.widget.valueColor },
            ]

            Item {
                id: memoryRow

                required property var modelData

                width: root.width
                height: root.rowHeight

                PanelText {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: memoryRow.modelData.symbol
                    color: root.widget.appearance.text
                    font.pixelSize: root.widget.fontPixels(9)
                    font.weight: Font.Light
                }
                PanelText {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: Logic.isNumber(memoryRow.modelData.value) ? Format.bytes(memoryRow.modelData.value) : "–"
                    horizontalAlignment: Text.AlignRight
                    color: memoryRow.modelData.color
                    font.pixelSize: root.widget.fontPixels(9)
                    font.weight: Font.Light
                }
            }
        }
    }

    PanelText {
        id: measure

        visible: false
        template: "999.9 GiB"
        font.pixelSize: root.widget.fontPixels(9)
        font.weight: Font.Light
    }
}
