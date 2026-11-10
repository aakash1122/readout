// One module in the panel: loads the configured style and feeds it a ModuleState.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Item {
    id: widget

    required property string module
    required property AppletContext context
    required property Appearance appearance

    readonly property bool vertical: context.vertical

    ModuleState {
        id: moduleState

        module: widget.module
        context: widget.context
        appearance: widget.appearance
        thickness: widget.vertical ? widget.width : widget.height
    }

    implicitWidth: (loader.item as Item)?.implicitWidth ?? 0
    implicitHeight: (loader.item as Item)?.implicitHeight ?? 0
    Layout.fillHeight: !vertical
    Layout.fillWidth: vertical
    Layout.preferredWidth: vertical ? -1 : implicitWidth
    Layout.preferredHeight: vertical ? implicitHeight : -1

    Loader {
        id: loader

        anchors.fill: parent
        sourceComponent: {
            switch (moduleState.style) {
            case "mini": return miniStyle
            case "line": return lineStyle
            case "bars": return barsStyle
            case "pie": return pieStyle
            case "tachometer": return tachometerStyle
            case "speed": return speedStyle
            case "dualLine": return dualLineStyle
            case "state": return stateStyle
            case "stack": return stackStyle
            case "battery": return batteryStyle
            case "memory": return memoryStyle
            }
            return textStyle
        }
    }

    Component { id: textStyle; TextWidget { widget: moduleState } }
    Component { id: miniStyle; MiniWidget { widget: moduleState } }
    Component { id: lineStyle; LineWidget { widget: moduleState } }
    Component { id: barsStyle; BarsWidget { widget: moduleState } }
    Component { id: pieStyle; PieWidget { widget: moduleState } }
    Component { id: tachometerStyle; TachometerWidget { widget: moduleState } }
    Component { id: speedStyle; SpeedWidget { widget: moduleState } }
    Component { id: dualLineStyle; DualLineWidget { widget: moduleState } }
    Component { id: stateStyle; StateWidget { widget: moduleState } }
    Component { id: stackStyle; StackWidget { widget: moduleState } }
    Component { id: batteryStyle; BatteryWidget { widget: moduleState } }
    Component { id: memoryStyle; MemoryWidget { widget: moduleState } }
}
