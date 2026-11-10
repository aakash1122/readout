// Panel text with tabular figures that reserves the width of `template`, so the panel does not
// shift as values change.
import QtQuick

import org.kde.kirigami as Kirigami

Item {
    id: panelText

    property alias text: label.text
    property alias color: label.color
    property alias font: label.font
    property alias horizontalAlignment: label.horizontalAlignment
    property alias verticalAlignment: label.verticalAlignment
    property string template: ""

    implicitWidth: Math.ceil(Math.max(label.implicitWidth, metrics.advanceWidth))
    implicitHeight: label.implicitHeight

    Text {
        id: label

        anchors.fill: parent
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: Kirigami.Theme.textColor
        font.family: Kirigami.Theme.defaultFont.family
        font.features: { "tnum": 1 }
        textFormat: Text.PlainText
        maximumLineCount: 1
    }

    TextMetrics {
        id: metrics

        font: label.font
        text: panelText.template
    }
}
