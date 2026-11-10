// Month calendar card (Stats 2.9): month title with arrows, weekday header, today in a red circle.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import org.kde.plasma.components as PlasmaComponents

Card {
    id: calendar

    // The current date and time; updates at least once a minute.
    // Months away from the current month.
    property int monthOffset: 0

    property date now: new Date()
    readonly property date shown: new Date(now.getFullYear(), now.getMonth() + monthOffset, 1)
    readonly property int firstWeekday: Qt.locale().firstDayOfWeek // 0 = Sunday
    readonly property var days: {
        const start = new Date(shown)
        const lead = (shown.getDay() - firstWeekday + 7) % 7
        start.setDate(1 - lead)
        const list = []
        for (let index = 0; index < 42; ++index) {
            const day = new Date(start.getFullYear(), start.getMonth(), start.getDate() + index)
            list.push({
                day: day.getDate(),
                inMonth: day.getMonth() === shown.getMonth(),
                today: day.toDateString() === now.toDateString(),
            })
        }
        return list
    }

    padding: appearance.px(8)
    spacing: appearance.px(2)

    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: calendar.appearance.px(24)

        StatsText {
            Layout.fillWidth: true
            appearance: calendar.appearance
            points: 16
            font.weight: Font.Medium
            text: calendar.shown.toLocaleDateString(Qt.locale(), "MMMM yyyy")
        }
        PlasmaComponents.ToolButton {
            implicitWidth: calendar.appearance.px(22)
            implicitHeight: implicitWidth
            icon.name: "go-previous"
            display: PlasmaComponents.AbstractButton.IconOnly
            text: i18n("Previous month")
            onClicked: calendar.monthOffset -= 1
        }
        PlasmaComponents.ToolButton {
            implicitWidth: calendar.appearance.px(22)
            implicitHeight: implicitWidth
            icon.name: "go-next"
            display: PlasmaComponents.AbstractButton.IconOnly
            text: i18n("Next month")
            onClicked: calendar.monthOffset += 1
        }
    }

    Grid {
        id: grid

        readonly property real cellWidth: width / 7
        readonly property real cellHeight: calendar.appearance.px(28)

        Layout.fillWidth: true
        columns: 7

        Repeater {
            model: 7

            StatsText {
                required property int index

                width: grid.cellWidth
                height: calendar.appearance.px(20)
                appearance: calendar.appearance
                points: 12
                color: "#808080"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: Qt.locale().dayName((calendar.firstWeekday + index) % 7, Locale.ShortFormat)
            }
        }

        Repeater {
            model: calendar.days

            Item {
                id: cell

                required property var modelData

                width: grid.cellWidth
                height: grid.cellHeight

                Rectangle {
                    anchors.centerIn: parent
                    visible: cell.modelData.today
                    width: calendar.appearance.px(25)
                    height: width
                    radius: width / 2
                    color: calendar.appearance.red
                }
                StatsText {
                    anchors.centerIn: parent
                    appearance: calendar.appearance
                    color: cell.modelData.today ? "white" : cell.modelData.inMonth ? calendar.appearance.text : calendar.appearance.lightGray
                    text: cell.modelData.day
                }
            }
        }
    }
}
