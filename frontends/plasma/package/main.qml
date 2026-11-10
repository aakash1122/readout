// The applet root must live in main.qml: Plasma only attaches its Plasmoid object to the root item of that file.
pragma ComponentBehavior: Bound

import QtQuick

import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.kirigami as Kirigami

PlasmoidItem {
    id: root

    readonly property AppletContext context: AppletContext {
        configuration: Plasmoid.configuration
        vertical: Plasmoid.formFactor === PlasmaCore.Types.Vertical
        detail: root.expanded || (Plasmoid.formFactor === PlasmaCore.Types.Planar && root.fullRepresentationItem !== null
                                  && root.fullRepresentationItem.visible)
    }
    readonly property var modules: context.modules

    Connections {
        target: root.context

        function onSettingsRequested() {
            Plasmoid.internalAction("configure").trigger()
        }
        function onClosePopupRequested() {
            root.expanded = false
        }
    }
    readonly property ReadoutSource source: context.source

    switchWidth: Kirigami.Units.gridUnit * 16
    switchHeight: Kirigami.Units.gridUnit * 14

    Plasmoid.icon: context.summary.iconFor(modules[0])
    Plasmoid.status: source.status === ReadoutSource.Error ? PlasmaCore.Types.NeedsAttentionStatus : PlasmaCore.Types.ActiveStatus

    toolTipMainText: modules.length === 1 ? context.summary.titleFor(modules[0]) : i18n("System Monitor")
    toolTipSubText: {
        if (source.status === ReadoutSource.NotInstalled && modules.some(module => module !== "clock")) {
            return i18n("readoutd is not installed.")
        }
        const lines = []
        for (const module of modules) {
            const line = context.summary.tooltipLine(module, source.data[module], context.configuration, context.clock.time)
            if (line.length > 0) {
                lines.push(line)
            }
        }
        return lines.join("\n")
    }

    compactRepresentation: CompactRepresentation {
        plasmoidItem: root
        context: root.context
    }

    fullRepresentation: FullRepresentation {
        context: root.context
    }
}
