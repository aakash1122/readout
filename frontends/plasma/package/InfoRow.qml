// "Label:  value" row, 22 pt tall (Stats popupRow / popupWithColorRow).
// Hidden when the value is empty, so absent data hides its row.
import QtQuick
import QtQuick.Layouts

RowLayout {
    id: row

    required property Appearance appearance
    property string label
    property string value
    // A 12 pt rounded legend square before the label, tying the row to a chart series.
    property color marker: "transparent"
    readonly property bool hasMarker: marker.a > 0
    property real valuePoints: 13
    property color valueColor: appearance.text

    Layout.fillWidth: true
    Layout.preferredHeight: appearance.rowHeight
    visible: value.length > 0
    spacing: appearance.px(6)

    Rectangle {
        visible: row.hasMarker
        Layout.preferredWidth: row.appearance.markerSize
        Layout.preferredHeight: row.appearance.markerSize
        Layout.leftMargin: row.appearance.px(2)
        radius: row.appearance.px(2)
        color: row.marker
    }

    StatsText {
        appearance: row.appearance
        points: 12
        text: i18nc("@label row label followed by its value", "%1:", row.label)
        color: row.appearance.label
    }

    StatsText {
        Layout.fillWidth: true
        appearance: row.appearance
        points: row.valuePoints
        text: row.value
        color: row.valueColor
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideMiddle
    }
}
