// Style "text" (Stats 3.11): the value in 12 pt, with an optional icon before it.
// The width snaps to steps of 10 pt so the panel does not shift as digits change.
import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

RowLayout {
    id: root

    required property ModuleState widget

    readonly property real step: widget.size(10)

    spacing: widget.size(3)

    Kirigami.Icon {
        Layout.alignment: Qt.AlignVCenter
        Layout.preferredWidth: root.widget.size(14)
        Layout.preferredHeight: Layout.preferredWidth
        visible: root.widget.showIcon
        source: root.widget.iconName
    }
    PanelText {
        Layout.alignment: Qt.AlignVCenter
        Layout.preferredWidth: Math.ceil(implicitWidth / root.step) * root.step
        text: root.widget.valueText
        color: root.widget.valueColor
        font.pixelSize: root.widget.fontPixels(12)
    }
}
