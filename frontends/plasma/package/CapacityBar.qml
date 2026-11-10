// Used space bar: accent fill on a light track with a hairline border (Stats disk BarView).
import QtQuick
import QtQuick.Layouts

Rectangle {
    required property Appearance appearance
    property real percent: 0

    Layout.fillWidth: true
    implicitHeight: appearance.px(10)
    radius: appearance.px(3)
    color: appearance.dark ? appearance.well : "#ffffff"
    border.width: 0.5
    border.color: appearance.label

    Rectangle {
        x: 0
        y: 0
        width: parent.width * Math.max(0, Math.min(1, parent.percent / 100))
        height: parent.height
        radius: parent.radius
        color: parent.appearance.accent
    }
}
