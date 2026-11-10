// Style "battery" (Stats 3.8): battery glyph filled to the level with a bolt while charging,
// and the percentage before it when labels are on.
import QtQuick
import QtQuick.Layouts

RowLayout {
    id: root

    required property ModuleState widget

    readonly property bool present: !!widget.payload && widget.payload.present === true
    readonly property bool charging: !!widget.payload && widget.payload.state === "charging"

    spacing: widget.size(2)

    PanelText {
        Layout.alignment: Qt.AlignVCenter
        visible: root.widget.showLabel && root.present && root.widget.level < 100
        text: root.widget.valueText
        template: "100%"
        color: root.widget.valueColor
        font.pixelSize: root.widget.fontPixels(12)
    }

    BatteryGlyph {
        Layout.alignment: Qt.AlignVCenter
        Layout.preferredWidth: root.widget.size(22) + nubWidth + gap
        Layout.preferredHeight: root.widget.size(12)
        radius: root.widget.size(2)
        nubWidth: root.widget.size(3)
        gap: 1
        level: root.present ? root.widget.level : NaN
        outlineColor: root.widget.appearance.withAlpha(root.widget.appearance.text, 0.5)
        fillColor: root.widget.batteryColor
        knockoutColor: root.widget.appearance.background
        charging: root.charging || (!root.present && root.widget.payload?.externalPower === true)
    }
}
