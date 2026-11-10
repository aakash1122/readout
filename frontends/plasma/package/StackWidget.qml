// Style "stack" (Stats 3.9): values paired into two-row columns; a single value is one larger row.
// Used for sensors and Bluetooth device batteries; the tooltip names each value.
pragma ComponentBehavior: Bound

import QtQuick

import org.kde.kirigami as Kirigami

Item {
    id: root

    required property ModuleState widget

    // Temperatures drop the unit letter, like Stats: "54°".
    readonly property var values: widget.summary.stackItems(widget.module, widget.payload, widget.configuration)
                                  .map(item => item.text.replace(/\s?°[CF]$/, "°"))
    readonly property var columns: {
        const result = []
        for (let index = 0; index < values.length; index += 2) {
            result.push(values.slice(index, index + 2))
        }
        return result
    }

    implicitWidth: values.length > 0 ? row.implicitWidth + widget.size(4) : placeholder.implicitWidth
    implicitHeight: widget.drawHeight

    Row {
        id: row

        anchors.centerIn: parent
        visible: root.values.length > 0
        spacing: root.widget.size(2)

        Repeater {
            model: root.columns.length

            Column {
                id: stackColumn

                required property int index
                readonly property var modelData: root.columns[index] ?? []
                // A leftover value on its own is drawn as one larger row.
                readonly property bool oneRow: modelData.length === 1

                Repeater {
                    model: stackColumn.modelData.length

                    PanelText {
                        required property int index
                        readonly property string modelData: stackColumn.modelData[index] ?? ""

                        width: Math.max(root.widget.size(20), implicitWidth) + root.widget.size(2)
                        height: stackColumn.oneRow ? root.widget.drawHeight : root.widget.drawHeight / 2
                        text: modelData
                        horizontalAlignment: Text.AlignLeft
                        color: root.widget.appearance.text
                        font.pixelSize: root.widget.fontPixels(stackColumn.oneRow ? 13 : 10)
                        font.weight: stackColumn.oneRow ? Font.Normal : Font.Light
                    }
                }
            }
        }
    }

    // Nothing selected or reported yet: the module icon keeps the item clickable.
    Kirigami.Icon {
        id: placeholder

        anchors.centerIn: parent
        visible: root.values.length === 0
        implicitWidth: root.widget.size(15)
        implicitHeight: implicitWidth
        source: root.widget.iconName
    }
}
