// Small analog clock face (Stats 2.9): hour, minute and optional seconds hands.
pragma ComponentBehavior: Bound

import QtQuick

Rectangle {
    id: face

    required property Appearance appearance
    property int hour: 0
    property int minute: 0
    property int second: 0
    property bool showSeconds: false
    readonly property real handRadius: width / 2

    radius: width / 2
    color: appearance.dark ? "#1e1e1e" : "#ffffff"
    border.width: 1
    border.color: appearance.dark ? "#555555" : appearance.lightGray

    // Hands rotate around the face center; each is anchored at its bottom end.
    Repeater {
        model: [
            { width: 2, length: face.handRadius - 4, color: face.appearance.withAlpha(face.appearance.text, 0.85),
              angle: (face.hour % 12 + face.minute / 60) * 30, shown: true },
            { width: 2, length: face.handRadius - 2, color: face.appearance.label, angle: face.minute * 6, shown: true },
            { width: 1, length: face.handRadius - 1, color: face.appearance.utilizationRed, angle: face.second * 6, shown: face.showSeconds },
        ]

        Rectangle {
            required property var modelData

            visible: modelData.shown
            x: face.width / 2 - width / 2
            y: face.height / 2 - height
            width: modelData.width
            height: modelData.length
            radius: width / 2
            antialiasing: true
            color: modelData.color
            transformOrigin: Item.Bottom
            rotation: modelData.angle
        }
    }
    Rectangle {
        anchors.centerIn: parent
        width: 3
        height: 3
        radius: 1.5
        color: face.appearance.text
    }
}
