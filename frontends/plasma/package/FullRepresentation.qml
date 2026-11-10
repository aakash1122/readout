// The popup (docs/visual-spec.md 1.1, 1.2 and 6.4): a 42 pt header with the system monitor button,
// the centered title and the settings button, then the page. An item with several modules shows
// a compact summary of each; "›" opens a module's full page and the left button goes back.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Window

import org.kde.ksvg as KSvg
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.extras as PlasmaExtras
import org.kde.kirigami as Kirigami

PlasmaExtras.Representation {
    id: full

    required property AppletContext context

    readonly property var modules: context.modules
    readonly property bool combined: modules.length > 1
    // In a combined item: the module whose full page is open, or "" for the summaries.
    property string openedModule: ""
    readonly property string shownModule: combined ? openedModule : modules[0]
    readonly property int status: context.source.status
    readonly property bool needsDaemon: modules.some(module => module !== "clock")
    readonly property bool hasData: !needsDaemon || status === ReadoutSource.Ready
                                    || status === ReadoutSource.Connecting && Object.keys(context.source.data).length > 0
    // The visible gap equals the margin setting: when the Plasma theme cannot collapse the popup
    // frame (it declares no inset), the frame already adds its own padding, so ours is reduced by that.
    readonly property bool frameCollapsed: dialogFrame.inset.left >= 0 && dialogFrame.inset.right >= 0
                                           && dialogFrame.inset.top >= 0 && dialogFrame.inset.bottom >= 0
    readonly property int margin: Math.max(0, appearance.margin - (frameCollapsed || !inPopup ? 0 : Math.round(dialogFrame.fixedMargins.left)))
    readonly property bool inPopup: Window.window instanceof PlasmaCore.AppletPopup

    Appearance {
        id: appearance

        themeItem: full
        popup: true
    }

    KSvg.FrameSvgItem {
        id: dialogFrame

        visible: false
        imagePath: "dialogs/background"
    }

    // Back to the summaries whenever the popup closes or the module list changes.
    onModulesChanged: openedModule = ""
    Connections {
        target: full.context

        function onDetailChanged() {
            if (!full.context.detail) {
                full.openedModule = ""
            }
        }
    }

    // The popup is exactly as tall as its content, up to 70% of the available screen height;
    // only then does the body scroll. Minimum, preferred and maximum are all equal because Plasma's AppletPopup
    // (libplasma appletpopup.cpp) saves the popup size whenever it closes and, once a size is
    // saved, ignores the preferred size and only clamps the saved one to the minimum and maximum.
    // Plasma also truncates the hints to whole pixels, so the height is rounded up: a fraction
    // of a pixel too little would make the body scrollable.
    // Until the first detail-tier update arrives (about 500 ms after opening, so rates have a
    // baseline) the page only has panel-tier data and is shorter; the height it had last time
    // is kept meanwhile, so the popup opens at its final size instead of growing.
    readonly property string view: shownModule.length > 0 ? shownModule : "overview"
    readonly property int naturalHeight: Math.ceil(appearance.headerHeight
        + (hasData ? body.implicitHeight + margin * 2 : Kirigami.Units.gridUnit * 12))
    readonly property bool settling: context.detail && hasData && !context.source.detailReady
    readonly property int popupWidth: Math.ceil(appearance.contentWidth + margin * 2)
    readonly property int popupHeight: Math.ceil(Math.min(
        settling ? Math.max(naturalHeight, context.popupHeights[view] ?? 0) : naturalHeight,
        Screen.desktopAvailableHeight * 0.7))

    onNaturalHeightChanged: rememberHeight()
    onSettlingChanged: rememberHeight()
    onViewChanged: rememberHeight()
    function rememberHeight() {
        if (context.detail && hasData && !settling) {
            context.rememberPopupHeight(view, naturalHeight)
        }
    }

    Layout.minimumWidth: popupWidth
    Layout.preferredWidth: popupWidth
    Layout.maximumWidth: popupWidth
    Layout.minimumHeight: popupHeight
    Layout.preferredHeight: popupHeight
    Layout.maximumHeight: popupHeight
    collapseMarginsHint: true

    header: Item {
        implicitHeight: appearance.headerHeight

        PlasmaComponents.ToolButton {
            id: leftButton

            readonly property bool back: full.combined && full.openedModule.length > 0

            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Math.max(appearance.px(2), full.margin / 2)
            visible: back || SystemMonitor.available
            icon.name: back ? "go-previous" : "office-chart-line"
            icon.color: back ? appearance.text : appearance.lightGray
            display: PlasmaComponents.AbstractButton.IconOnly
            text: back ? i18nc("@action:button back to all modules", "Back") : i18nc("@action:button", "Open System Monitor")
            onClicked: {
                if (back) {
                    full.openedModule = ""
                } else {
                    SystemMonitor.open()
                    full.context.closePopupRequested()
                }
            }
            PlasmaComponents.ToolTip.text: text
            PlasmaComponents.ToolTip.visible: hovered
            PlasmaComponents.ToolTip.delay: Kirigami.Units.toolTipDelay
        }

        StatsText {
            anchors.centerIn: parent
            width: parent.width - (leftButton.width + full.margin) * 2
            appearance: appearance
            points: 16
            horizontalAlignment: Text.AlignHCenter
            text: full.shownModule.length > 0 ? full.context.summary.titleFor(full.shownModule) : i18nc("@title popup of several modules", "System")
        }

        PlasmaComponents.ToolButton {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: Math.max(appearance.px(2), full.margin / 2)
            icon.name: "configure"
            icon.color: appearance.lightGray
            display: PlasmaComponents.AbstractButton.IconOnly
            text: i18nc("@action:button", "Configure Readout…")
            onClicked: full.context.settingsRequested()
            PlasmaComponents.ToolTip.text: text
            PlasmaComponents.ToolTip.visible: hovered
            PlasmaComponents.ToolTip.delay: Kirigami.Units.toolTipDelay
        }
    }

    contentItem: PlasmaComponents.ScrollView {
        id: scroll

        contentWidth: availableWidth
        contentHeight: body.implicitHeight + full.margin * 2
        PlasmaComponents.ScrollBar.horizontal.policy: PlasmaComponents.ScrollBar.AlwaysOff

        ColumnLayout {
            id: body

            // The scroll bar sits inside the popup frame, so the frame padding that replaces part
            // of the margin on the outer edges is not there; keep a gap of its own before it.
            readonly property int scrollBarGap: scroll.PlasmaComponents.ScrollBar.vertical.visible
                                                ? Math.max(appearance.px(6), appearance.margin - full.margin) : 0

            x: full.margin
            width: scroll.availableWidth - full.margin * 2 - scrollBarGap
            y: full.margin
            spacing: 0
            visible: full.hasData

            // Only the visible page exists; hidden pages cost nothing.
            ModulePage {
                active: full.hasData && full.shownModule.length > 0
                visible: active
                module: full.shownModule
                context: full.context
                appearance: appearance
            }

            Repeater {
                model: full.hasData && full.combined && full.openedModule.length === 0 ? full.modules : []

                ModuleSummary {
                    required property string modelData
                    required property int index

                    Layout.topMargin: index > 0 ? appearance.cardSpacing : 0
                    module: modelData
                    context: full.context
                    appearance: appearance
                    onOpenRequested: full.openedModule = modelData
                }
            }
        }
    }

    PlasmaExtras.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 2
        visible: !full.hasData
        iconName: full.status === ReadoutSource.NotInstalled ? "dialog-warning" : (full.status === ReadoutSource.Error ? "dialog-error" : "")
        text: {
            switch (full.status) {
            case ReadoutSource.NotInstalled:
                return i18n("readoutd is not installed")
            case ReadoutSource.Error:
                return i18n("Cannot reach readoutd")
            }
            return i18n("Connecting…")
        }
        explanation: {
            switch (full.status) {
            case ReadoutSource.NotInstalled:
                return i18n("This widget reads its values from the readoutd service. Install the readout package from your distribution, or build it from source with “cmake --install”.")
            case ReadoutSource.Error:
                return full.context.source.errorText
            }
            return ""
        }
    }
}
