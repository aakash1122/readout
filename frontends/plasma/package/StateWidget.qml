// Style "state" (Stats 3.11): an 8 pt dot, green while the network is connected, red otherwise.
import QtQuick

Item {
    id: root

    required property ModuleState widget

    readonly property bool connected: !!widget.payload && widget.payload.connected === true
                                      && widget.payload.connectivity !== "none"

    implicitWidth: widget.size(8)
    implicitHeight: widget.drawHeight

    Rectangle {
        anchors.centerIn: parent
        width: root.widget.size(8)
        height: width
        radius: width / 2
        color: root.connected ? root.widget.appearance.green : root.widget.appearance.red
    }
}
