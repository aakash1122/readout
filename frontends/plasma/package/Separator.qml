// Section title (docs/visual-spec.md 6.4): a gap above, then the small centered gray title with a
// thin rule on each side. Rules start at the left edge like every other section's; with an
// action button at the right edge, the right rule stops before it.
import QtQuick
import QtQuick.Layouts

import org.kde.plasma.components as PlasmaComponents

Item {
    id: separator

    required property Appearance appearance
    property string text
    // Optional small action at the right edge, for example a details toggle.
    property string actionIcon: ""
    property string actionText: ""
    signal actionTriggered()

    readonly property int ruleGap: appearance.px(8)
    readonly property int rightRuleEnd: actionButton.visible ? actionButton.x - ruleGap : width
    readonly property int ruleThickness: Math.max(1, Math.round(appearance.px(1)))

    Layout.fillWidth: true
    implicitHeight: appearance.sectionGap + appearance.separatorHeight

    Item {
        id: titleRow

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: separator.appearance.separatorHeight

        StatsText {
            id: title

            anchors.centerIn: parent
            appearance: separator.appearance
            points: 12
            text: separator.text
            color: separator.appearance.label
            font.weight: Font.Medium
        }

        Rectangle {
            x: 0
            width: Math.max(0, title.x - separator.ruleGap)
            height: separator.ruleThickness
            anchors.verticalCenter: title.verticalCenter
            color: separator.appearance.rule
            visible: separator.text.length > 0
        }

        Rectangle {
            x: title.x + title.width + separator.ruleGap
            width: Math.max(0, separator.rightRuleEnd - x)
            height: separator.ruleThickness
            anchors.verticalCenter: title.verticalCenter
            color: separator.appearance.rule
            visible: separator.text.length > 0
        }

        PlasmaComponents.ToolButton {
            id: actionButton

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: separator.actionIcon.length > 0
            implicitWidth: separator.appearance.px(22)
            implicitHeight: implicitWidth
            icon.name: separator.actionIcon
            icon.width: separator.appearance.px(15)
            icon.height: separator.appearance.px(15)
            display: PlasmaComponents.AbstractButton.IconOnly
            text: separator.actionText
            onClicked: separator.actionTriggered()
            PlasmaComponents.ToolTip.text: separator.actionText
            PlasmaComponents.ToolTip.visible: hovered && separator.actionText.length > 0
        }
    }
}
