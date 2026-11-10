// Filled line chart in a well, newest value on the right (Stats LineChartView).
import QtQuick
import QtQuick.Layouts

import org.kde.quickcharts as Charts

Well {
    id: chart

    property var values: []
    property color lineColor: appearance.accent
    // Fixed top of the scale, or 0 to scale to the data.
    property real maximum: 100
    property int capacity: 180

    readonly property real scaleTop: maximum > 0 ? maximum : Math.max(1, ...values) * 1.1

    implicitHeight: appearance.px(70)

    Charts.LineChart {
        anchors.fill: parent
        anchors.margins: 1
        // Index 0 is drawn at the right edge, so a short history grows in from the right.
        direction: Charts.XYChart.ZeroAtEnd
        xRange { from: 0; to: chart.capacity - 1; automatic: false }
        yRange { from: 0; to: chart.scaleTop; automatic: false }
        lineWidth: 1
        fillOpacity: 0.7
        colorSource: Charts.SingleValueSource { value: chart.lineColor }
        valueSources: Charts.ArraySource { array: chart.values.slice(-chart.capacity).reverse() }
    }
}
