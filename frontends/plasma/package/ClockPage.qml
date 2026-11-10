// Clock popup (docs/visual-spec.md 2.9 and 6.5): calendar, the local time, then one card per time zone.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

PageLayout {
    id: page

    readonly property WorldClock clock: context.clock
    readonly property var cards: [clock.local].concat(clock.entries)

    spacing: appearance.cardSpacing

    CalendarCard {
        appearance: page.appearance
        now: page.clock.now
    }

    Repeater {
        model: page.cards.length

        Card {
            id: zone

            readonly property var modelData: page.cards[index] ?? ({ city: "", date: "", time: "", offset: "", hour: 0, minute: 0, second: 0 })
            required property int index

            appearance: page.appearance
            Layout.preferredHeight: page.appearance.px(44)

            RowLayout {
                Layout.fillWidth: true
                spacing: page.appearance.px(5)

                AnalogClock {
                    Layout.preferredWidth: page.appearance.px(34)
                    Layout.preferredHeight: page.appearance.px(34)
                    appearance: page.appearance
                    hour: zone.modelData.hour
                    minute: zone.modelData.minute
                    second: zone.modelData.second
                    showSeconds: page.configuration.showSeconds
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: page.appearance.px(2)

                    StatsText {
                        Layout.fillWidth: true
                        appearance: page.appearance
                        points: 11
                        font.weight: Font.Light
                        text: zone.index === 0 ? zone.modelData.city
                            : i18nc("@info city (offset from local time, date)", "%1 (%2, %3)", zone.modelData.city, zone.modelData.offset, zone.modelData.date)
                    }
                    StatsText {
                        Layout.fillWidth: true
                        appearance: page.appearance
                        text: zone.modelData.time
                    }
                }
            }
        }
    }
}
