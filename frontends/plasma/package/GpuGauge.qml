// A 50 pt dashboard gauge with a small caption under it, for side gauges whose value alone
// does not say what it measures (GPU temperature and video memory).
import QtQuick

Item {
    id: gauge

    required property Appearance appearance
    property real percent: 0
    property string text
    property string caption

    width: appearance.px(64)
    height: appearance.dashboardHeight

    Ring {
        id: ring

        anchors.horizontalCenter: parent.horizontalCenter
        y: gauge.appearance.px(12)
        width: gauge.appearance.px(50)
        height: width
        appearance: gauge.appearance
        gauge: true
        values: [gauge.percent]
        colors: [gauge.appearance.blue]
        text: gauge.text
    }

    StatsText {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: ring.bottom
        anchors.topMargin: gauge.appearance.px(2)
        appearance: gauge.appearance
        points: 9
        color: gauge.appearance.label
        text: gauge.caption
    }
}
