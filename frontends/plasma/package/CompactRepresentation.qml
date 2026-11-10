// The panel item: one widget per module, side by side (or stacked in a vertical panel).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import org.kde.plasma.plasmoid
import org.kde.kirigami as Kirigami

MouseArea {
    id: compact

    required property PlasmoidItem plasmoidItem
    required property AppletContext context

    readonly property bool vertical: context.vertical
    readonly property bool notInstalled: context.source.status === ReadoutSource.NotInstalled
                                         && context.modules.some(module => module !== "clock")
    property bool wasExpanded: false

    // Resolves the panel's colors, which can differ from the popup's.
    Appearance {
        id: appearance

        themeItem: compact
    }

    hoverEnabled: true
    acceptedButtons: Qt.LeftButton
    onPressed: wasExpanded = plasmoidItem.expanded
    onClicked: plasmoidItem.expanded = !wasExpanded

    readonly property int padding: vertical ? 0 : appearance.px(2) * 2

    Layout.minimumWidth: vertical ? 0 : content.implicitWidth + padding
    Layout.preferredWidth: vertical ? -1 : content.implicitWidth + padding
    Layout.maximumWidth: vertical ? Infinity : content.implicitWidth + padding
    Layout.minimumHeight: vertical ? content.implicitHeight : 0
    Layout.preferredHeight: vertical ? content.implicitHeight : -1
    Layout.maximumHeight: vertical ? content.implicitHeight : Infinity

    GridLayout {
        id: content

        anchors.fill: parent
        visible: !compact.notInstalled
        flow: compact.vertical ? GridLayout.TopToBottom : GridLayout.LeftToRight
        rows: compact.vertical ? -1 : 1
        columns: compact.vertical ? 1 : -1
        rowSpacing: Kirigami.Units.smallSpacing * 2
        columnSpacing: Kirigami.Units.smallSpacing * 2
        // Stats leaves 2 pt before the first and after the last widget.
        anchors.leftMargin: compact.vertical ? 0 : appearance.px(2)
        anchors.rightMargin: compact.vertical ? 0 : appearance.px(2)

        Repeater {
            model: compact.context.modules

            ModuleWidget {
                required property string modelData

                module: modelData
                context: compact.context
                appearance: appearance
            }
        }
    }

    Kirigami.Icon {
        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height, Kirigami.Units.iconSizes.medium)
        height: width
        visible: compact.notInstalled
        source: "dialog-warning"
        active: compact.containsMouse
    }
}
