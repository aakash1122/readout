// Faint rounded background that every chart sits in.
import QtQuick
import QtQuick.Layouts

Rectangle {
    required property Appearance appearance

    Layout.fillWidth: true
    color: appearance.well
    radius: appearance.px(3)
    clip: true
}
