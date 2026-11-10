// A 50 pt dashboard gauge with a small caption under it, for side gauges whose value alone
// does not say what it measures (GPU temperature and video memory).
import QtQuick

Item {
    id: sideGauge

    required property Appearance appearance
    property real percent: 0
    property string text
    property string caption

    width: appearance.px(64)
    height: appearance.dashboardHeight

    Ring {
        id: ring

        anchors.horizontalCenter: parent.horizontalCenter
        y: sideGauge.appearance.px(12)
        width: sideGauge.appearance.px(50)
        height: width
        appearance: sideGauge.appearance
        gauge: true
        values: [sideGauge.percent]
        colors: [sideGauge.appearance.blue]
        text: sideGauge.text
    }

    StatsText {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: ring.bottom
        anchors.topMargin: sideGauge.appearance.px(2)
        appearance: sideGauge.appearance
        points: 9
        color: sideGauge.appearance.label
        text: sideGauge.caption
    }
}
