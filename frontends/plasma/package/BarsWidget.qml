// Style "bars" (Stats 3.3): one bar per core for the CPU, else a single bar, inside a solid box.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

RowLayout {
    id: root

    required property ModuleState widget

    readonly property var values: {
        const cores = widget.module === "cpu" && widget.payload ? widget.payload.cores : undefined
        return cores && cores.length > 0 ? cores : [widget.hasLevel ? widget.level : 0]
    }
    // Stats box widths by bar count.
    readonly property real boxWidth: {
        const count = values.length
        const width = count <= 1 ? 10 : count === 2 ? 22 : count <= 4 ? 30 : count <= 8 ? 40 : count <= 12 ? 50
                    : count <= 16 ? 76 : count <= 32 ? 84 : 118
        return widget.size(width) + 1
    }

    function colorFor(value) {
        const appearance = widget.appearance
        switch (widget.configuration.colorMode) {
        case "monochrome": return appearance.background
        case "thresholds":
            return value >= widget.configuration.criticalThreshold ? appearance.utilizationRed
                 : value >= widget.configuration.warningThreshold ? appearance.utilizationOrange : appearance.accent
        }
        return appearance.accent
    }

    spacing: widget.size(2)

    VerticalLabel {
        Layout.alignment: Qt.AlignVCenter
        visible: root.widget.showLabel
        widget: root.widget
    }

    Rectangle {
        id: box

        Layout.alignment: Qt.AlignVCenter
        Layout.preferredWidth: root.boxWidth
        Layout.preferredHeight: root.widget.drawHeight
        radius: root.widget.size(2)
        color: root.widget.boxColor

        Row {
            id: bars

            readonly property real gap: 0.5

            anchors.fill: parent
            anchors.margins: 1
            spacing: gap

            Repeater {
                model: root.values.length

                Item {
                    required property int index
                    readonly property real modelData: root.values[index] ?? 0

                    width: (bars.width - (root.values.length - 1) * bars.gap) / root.values.length
                    height: bars.height

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: parent.height * Math.max(0, Math.min(100, parent.modelData)) / 100
                        color: root.colorFor(parent.modelData)
                    }
                }
            }
        }
    }
}
