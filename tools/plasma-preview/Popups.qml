// Every module popup side by side with live data, plus a combined item, like the Stats overview.
// Each card is the real FullRepresentation, sized by its own layout hints.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import plasma.applet.io.github.aakash1122.readout

Window {
    id: scene

    readonly property var cards: [["cpu"], ["gpu"], ["memory"], ["disk"], ["network"], ["sensors"], ["battery"], ["bluetooth"], ["clock"],
                                  ["cpu", "memory", "network", "disk", "battery", "clock"]]

    width: row.implicitWidth + 32
    height: 1500
    visible: true
    color: "#6b7280"

    Row {
        id: row

        x: 16
        y: 16
        spacing: 16

        Repeater {
            model: scene.cards

            Rectangle {
                id: card

                required property var modelData

                width: popup.Layout.preferredWidth
                height: Math.min(popup.Layout.preferredHeight, scene.height - 32)
                radius: 6
                color: Kirigami.Theme.backgroundColor
                Kirigami.Theme.colorSet: Kirigami.Theme.Window
                Kirigami.Theme.inherit: false
                clip: true

                AppletContext {
                    id: cardContext

                    detail: true
                    configuration: ({
                        modules: card.modelData,
                        styles: [], showLabel: true, showIcon: false, colorMode: "accent",
                        warningThreshold: 70, criticalThreshold: 90, processCount: 8, sensors: [], gpu: "",
                        speedBits: false, timeZones: ["America/New_York", "Asia/Tokyo"], hourFormat: "locale", showSeconds: true,
                    })
                }

                FullRepresentation {
                    id: popup

                    anchors.fill: parent
                    context: cardContext
                }
            }
        }
    }
}
