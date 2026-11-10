// Flat card for one instance (a GPU, a drive, a device, a clock).
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: card

    required property Appearance appearance
    default property alias content: column.data
    property int padding: appearance.px(5)
    property alias spacing: column.spacing

    Layout.fillWidth: true
    implicitHeight: column.implicitHeight + padding * 2
    color: appearance.card
    radius: appearance.px(2)

    ColumnLayout {
        id: column

        x: card.padding
        y: card.padding
        width: card.width - card.padding * 2
        spacing: card.appearance.px(5)
    }
}
