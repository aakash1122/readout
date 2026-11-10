// The 90 pt band at the top of a page that holds rings, gauges or big numbers.
import QtQuick
import QtQuick.Layouts

Item {
    required property Appearance appearance

    Layout.fillWidth: true
    Layout.preferredHeight: appearance.dashboardHeight

    // Places a child's center at a Stats x coordinate of the 264 pt wide band.
    function centerAt(statsX, itemWidth) {
        return Math.round(width * statsX / 264 - itemWidth / 2)
    }
}
