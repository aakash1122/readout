// Stats "Label" option: the first three letters of the module name stacked top to bottom.
pragma ComponentBehavior: Bound

import QtQuick

Column {
    id: verticalLabel

    required property ModuleState widget

    width: widget.size(6)

    Repeater {
        model: verticalLabel.widget.shortLabel.split("")

        Text {
            required property string modelData

            width: verticalLabel.width
            height: verticalLabel.widget.drawHeight / 3
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: modelData
            color: verticalLabel.widget.appearance.text
            font.pixelSize: verticalLabel.widget.fontPixels(7)
            textFormat: Text.PlainText
        }
    }
}
