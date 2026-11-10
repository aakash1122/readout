// Ring (Stats PieChartView) or 270 degree gauge (HalfCircleGraphView) with centered text.
// `values` are percent segments drawn clockwise from the start; the rest is the inactive color.
import QtQuick

import org.kde.quickcharts as Charts

Item {
    id: ring

    required property Appearance appearance
    property var values: []
    property var colors: []
    property string text
    property bool gauge: false
    property real textPoints: gauge ? 10 : 15
    property real thickness: appearance.stroke

    Charts.PieChart {
        anchors.fill: parent
        range { from: 0; to: 100; automatic: false }
        // Segments of one ring take their colors by position.
        indexingMode: Charts.Chart.IndexAllValues
        thickness: ring.thickness
        fromAngle: ring.gauge ? -135 : 0
        toAngle: ring.gauge ? 135 : 360
        smoothEnds: ring.gauge
        backgroundColor: ring.appearance.inactive
        colorSource: Charts.ArraySource { array: ring.colors }
        valueSources: Charts.ArraySource { array: ring.values.map(value => Math.max(0, Math.min(100, value))) }
    }

    StatsText {
        anchors.centerIn: parent
        appearance: ring.appearance
        points: ring.textPoints
        text: ring.text
    }
}
