// Every module in every panel style, on a panel-colored strip, at the default panel thickness.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import plasma.applet.io.github.aakash1122.readout

Window {
    id: scene

    // The default Plasma panel thickness.
    property int thickness: 44
    readonly property var modules: ["cpu", "gpu", "memory", "disk", "network", "sensors", "battery", "bluetooth", "clock"]
    readonly property var styles: ({
        cpu: ["mini", "text", "line", "bars", "pie", "tachometer"],
        gpu: ["mini", "text", "line", "bars", "pie", "tachometer"],
        memory: ["mini", "text", "line", "bars", "pie", "tachometer", "memory"],
        disk: ["mini", "text", "bars", "pie", "speed", "dualLine", "memory"],
        network: ["speed", "text", "dualLine", "state"],
        sensors: ["stack", "text"],
        battery: ["battery", "text", "mini"],
        bluetooth: ["stack", "text"],
        clock: ["text"],
    })

    width: 1100
    height: modules.length * (thickness + 8) + 16
    visible: true

    // Items inherit colors from a themed item, not from the window, so the panel strip is an item.
    Rectangle {
        id: panelStrip

        anchors.fill: parent
        color: Kirigami.Theme.backgroundColor
        Kirigami.Theme.colorSet: Kirigami.Theme.Header
        Kirigami.Theme.inherit: false

        Column {
            x: 8
            y: 8
            spacing: 8

            Appearance {
                id: panelAppearance

                themeItem: panelStrip
            }

            Repeater {
                model: scene.modules

                Row {
                    id: strip

                    required property string modelData

                    spacing: 24
                    height: scene.thickness

                    Text {
                        width: 80
                        height: parent.height
                        verticalAlignment: Text.AlignVCenter
                        text: strip.modelData
                        color: Kirigami.Theme.disabledTextColor
                    }

                    Repeater {
                        model: scene.styles[strip.modelData]

                        Item {
                            id: cell

                            required property string modelData

                            width: widget.implicitWidth
                            height: scene.thickness

                            AppletContext {
                                id: cellContext

                                configuration: ({
                                    modules: [strip.modelData], styles: [strip.modelData + "=" + cell.modelData],
                                    showLabel: true, showIcon: false, colorMode: "accent", warningThreshold: 70,
                                    criticalThreshold: 90, processCount: 5, sensors: [], gpu: "", speedBits: false,
                                    timeZones: [], hourFormat: "locale", showSeconds: false,
                                })
                            }

                            ModuleWidget {
                                id: widget

                                height: parent.height
                                module: strip.modelData
                                context: cellContext
                                appearance: panelAppearance
                            }
                        }
                    }
                }
            }
        }
    }
}
