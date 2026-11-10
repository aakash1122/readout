// Sensors popup (docs/visual-spec.md 2.7 and 6.5): fan cards, then one section per sensor type.
// Clicking a row shows a chart of that sensor, recorded while the popup is open.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import "Logic.js" as Logic

PageLayout {
    id: page

    readonly property var sensors: (source.data.sensors?.sensors ?? [])
    readonly property var fans: sensors.filter(sensor => sensor.kind === "fan")
    // Sensor types in Stats order, each with its sensors grouped by the documented group order.
    readonly property var sections: {
        const types = [
            ["temperature", i18nc("@title sensor type", "Temperature")],
            ["voltage", i18nc("@title sensor type", "Voltage")],
            ["current", i18nc("@title sensor type", "Current")],
            ["power", i18nc("@title sensor type", "Power")],
        ]
        const result = []
        for (const [kind, title] of types) {
            const members = []
            for (const group of Logic.groupSensors(sensors.filter(sensor => sensor.kind === kind))) {
                members.push(...group.sensors)
            }
            if (members.length > 0) {
                result.push({ kind: kind, title: title, sensors: members })
            }
        }
        return result
    }
    // Sensor id -> recent values, newest last. Kept only while the popup is open.
    property var history: ({})
    property var expanded: ({})

    function valueText(sensor) {
        if (sensor.sleeping) {
            return i18nc("@info:status device asleep", "Sleeping")
        }
        return sensor.valid ? Format.sensor(sensor.value, sensor.kind) : "–"
    }
    function toggle(id) {
        const next = Object.assign({}, expanded)
        next[id] = !next[id]
        expanded = next
    }

    Connections {
        target: page.source

        function onUpdated(module) {
            if (module !== "sensors") {
                return
            }
            const next = {}
            for (const sensor of page.sensors) {
                const previous = page.history[sensor.id] ?? []
                next[sensor.id] = sensor.valid && Logic.isNumber(sensor.value) ? previous.concat([sensor.value]).slice(-120) : previous
            }
            page.history = next
        }
    }

    EmptyMessage {
        visible: page.sensors.length === 0
        appearance: page.appearance
        text: i18n("No sensors found")
    }

    Separator {
        visible: page.fans.length > 0
        appearance: page.appearance
        text: i18nc("@title sensor type", "Fans")
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: page.fans.length > 0
        spacing: page.appearance.px(2)

        Repeater {
            model: page.fans.length

            Card {
                id: fan

                required property int index
                readonly property var modelData: page.fans[index] ?? ({ id: "", label: "", chip: "", valid: false })

                appearance: page.appearance

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: page.appearance.px(16)

                    StatsText {
                        Layout.fillWidth: true
                        appearance: page.appearance
                        text: fan.modelData.label
                    }
                    StatsText {
                        appearance: page.appearance
                        text: page.valueText(fan.modelData)
                    }
                }
            }
        }
    }

    Repeater {
        model: page.sections.length

        ColumnLayout {
            id: section

            required property int index
            readonly property var modelData: page.sections[index] ?? ({ title: "", sensors: [] })

            Layout.fillWidth: true
            spacing: 0

            Separator {
                appearance: page.appearance
                text: section.modelData.title
            }

            Repeater {
                model: section.modelData.sensors.length

                ColumnLayout {
                    id: sensorRow

                    required property int index
                    readonly property var modelData: section.modelData.sensors[index] ?? ({ id: "", label: "", chip: "", valid: false })
                    readonly property bool open: page.expanded[modelData.id] === true

                    Layout.fillWidth: true
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: page.appearance.rowHeight
                        radius: page.appearance.px(3)
                        color: rowHover.hovered ? page.appearance.withAlpha(page.appearance.text, 0.05) : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            spacing: page.appearance.px(6)

                            // The label keeps its full width; the chip name gives way first.
                            StatsText {
                                Layout.preferredWidth: Math.min(implicitWidth, sensorRow.width * 0.6)
                                appearance: page.appearance
                                points: 12
                                color: page.appearance.label
                                text: sensorRow.modelData.label
                            }
                            StatsText {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 0
                                appearance: page.appearance
                                points: 11
                                color: page.appearance.tertiary
                                text: sensorRow.modelData.chip
                            }
                            StatsText {
                                appearance: page.appearance
                                text: page.valueText(sensorRow.modelData)
                                color: sensorRow.modelData.valid ? page.appearance.text : page.appearance.label
                            }
                        }

                        HoverHandler {
                            id: rowHover

                            cursorShape: Qt.PointingHandCursor
                        }
                        TapHandler {
                            onTapped: page.toggle(sensorRow.modelData.id)
                        }
                    }

                    LineHistory {
                        visible: sensorRow.open
                        implicitHeight: page.appearance.px(60)
                        appearance: page.appearance
                        capacity: 120
                        maximum: 0
                        values: sensorRow.open ? (page.history[sensorRow.modelData.id] ?? []) : []
                    }
                }
            }
        }
    }
}
