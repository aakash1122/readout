// Style "mini" (Stats 3.1): small module name over the value, or a larger value alone.
// The width reserves "100%" so the panel does not shift as digits change; the label and value
// are centered in it as one block, horizontally and vertically.
import QtQuick

Item {
    id: root

    required property ModuleState widget

    implicitWidth: Math.max(label.implicitWidth, value.implicitWidth) + widget.size(4)
    implicitHeight: widget.drawHeight

    Column {
        anchors.centerIn: parent
        // Text boxes include line spacing; pull the value up so the pair reads as one unit.
        spacing: -Math.round(value.font.pixelSize * 0.15)

        PanelText {
            id: label

            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.widget.showLabel
            text: root.widget.label
            color: root.widget.appearance.text
            font.pixelSize: root.widget.fontPixels(7)
            font.weight: Font.Light
        }
        PanelText {
            id: value

            anchors.horizontalCenter: parent.horizontalCenter
            text: root.widget.valueText
            template: "100%"
            color: root.widget.valueColor
            font.pixelSize: root.widget.fontPixels(root.widget.showLabel ? 12 : 14)
        }
    }
}
