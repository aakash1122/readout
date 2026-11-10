// Memory pressure: a 270 degree gauge in green, yellow and red thirds with a needle
// pointing at the current level (Stats RAM dashboard).
import QtQuick

import org.kde.quickcharts as Charts

Item {
    id: gauge

    required property Appearance appearance
    // "normal", "warning" or "critical"
    property string level: "normal"
    property string text

    readonly property real needleAngle: level === "critical" ? 75 : level === "warning" ? 0 : -75

    Charts.PieChart {
        id: arc

        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height)
        height: width
        range { from: 0; to: 100; automatic: false }
        indexingMode: Charts.Chart.IndexAllValues
        thickness: gauge.appearance.stroke
        fromAngle: -135
        toAngle: 135
        smoothEnds: true
        backgroundColor: gauge.appearance.inactive
        colorSource: Charts.ArraySource { array: [gauge.appearance.green, gauge.appearance.yellow, gauge.appearance.red] }
        valueSources: Charts.ArraySource { array: [33.4, 33.3, 33.3] }
    }

    Rectangle {
        id: needle

        x: arc.x + arc.width / 2 - width / 2
        y: arc.y + arc.height / 2 - height
        width: Math.max(2, gauge.appearance.px(3))
        height: arc.height / 2 - gauge.appearance.stroke - gauge.appearance.px(3)
        radius: width / 2
        color: gauge.appearance.blue
        transformOrigin: Item.Bottom
        rotation: gauge.needleAngle
    }

    Rectangle {
        anchors.centerIn: arc
        width: gauge.appearance.px(8)
        height: width
        radius: width / 2
        color: gauge.appearance.blue
    }

    StatsText {
        anchors.horizontalCenter: arc.horizontalCenter
        anchors.top: arc.verticalCenter
        anchors.topMargin: gauge.appearance.px(6)
        appearance: gauge.appearance
        points: 9
        text: gauge.text
        color: gauge.appearance.label
    }
}
