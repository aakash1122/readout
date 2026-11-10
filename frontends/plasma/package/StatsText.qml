// Plain text in Stats sizes: `points` is the Stats point size, scaled to the desktop font.
import QtQuick

Text {
    required property Appearance appearance
    property real points: 13

    color: appearance.text
    font.pixelSize: appearance.fontPixels(points)
    font.features: { "tnum": 1 }
    textFormat: Text.PlainText
    elide: Text.ElideRight
    maximumLineCount: 1
}
