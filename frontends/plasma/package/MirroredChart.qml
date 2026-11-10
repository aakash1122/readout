// Upload (or write) above the center line in red, download (or read) below it in blue,
// each half scaled to its own maximum (Stats NetworkChartView).
import QtQuick
import QtQuick.Layouts

import org.kde.quickcharts as Charts

Well {
    id: chart

    property var upperValues: []
    property var lowerValues: []
    property color upperColor: appearance.red
    property color lowerColor: appearance.blue
    property bool showScale: false
    property bool bits: false
    property int capacity: 180

    readonly property real upperTop: Math.max(1024, ...upperValues)
    readonly property real lowerTop: Math.max(1024, ...lowerValues)

    implicitHeight: appearance.px(90)

    Item {
        id: upperHalf

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: parent.height / 2

        Charts.LineChart {
            anchors.fill: parent
            anchors.topMargin: 1
            direction: Charts.XYChart.ZeroAtEnd
            xRange { from: 0; to: chart.capacity - 1; automatic: false }
            yRange { from: 0; to: chart.upperTop; automatic: false }
            lineWidth: 1
            fillOpacity: 0.5
            colorSource: Charts.SingleValueSource { value: chart.upperColor }
            valueSources: Charts.ArraySource { array: chart.upperValues.slice(-chart.capacity).reverse() }
        }
    }

    Item {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height / 2
        // Mirror vertically so the lower chart grows down from the center line.
        transform: Scale { origin.y: upperHalf.height / 2; yScale: -1 }

        Charts.LineChart {
            anchors.fill: parent
            anchors.topMargin: 1
            direction: Charts.XYChart.ZeroAtEnd
            xRange { from: 0; to: chart.capacity - 1; automatic: false }
            yRange { from: 0; to: chart.lowerTop; automatic: false }
            lineWidth: 1
            fillOpacity: 0.5
            colorSource: Charts.SingleValueSource { value: chart.lowerColor }
            valueSources: Charts.ArraySource { array: chart.lowerValues.slice(-chart.capacity).reverse() }
        }
    }

    StatsText {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 1
        visible: chart.showScale
        appearance: chart.appearance
        points: 9
        font.weight: Font.Light
        text: Format.rate(chart.upperTop, chart.bits)
        color: chart.appearance.label
    }

    StatsText {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 1
        visible: chart.showScale
        appearance: chart.appearance
        points: 9
        font.weight: Font.Light
        text: Format.rate(chart.lowerTop, chart.bits)
        color: chart.appearance.label
    }
}
