// The full page of one module.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Loader {
    id: page

    required property string module
    required property AppletContext context
    required property Appearance appearance

    Layout.fillWidth: true
    sourceComponent: {
        switch (page.module) {
        case "cpu": return cpuPage
        case "memory": return memoryPage
        case "disk": return diskPage
        case "network": return networkPage
        case "gpu": return gpuPage
        case "sensors": return sensorsPage
        case "battery": return batteryPage
        case "bluetooth": return bluetoothPage
        case "clock": return clockPage
        }
        return null
    }

    Component { id: cpuPage; CpuPage { context: page.context; appearance: page.appearance } }
    Component { id: memoryPage; MemoryPage { context: page.context; appearance: page.appearance } }
    Component { id: diskPage; DiskPage { context: page.context; appearance: page.appearance } }
    Component { id: networkPage; NetworkPage { context: page.context; appearance: page.appearance } }
    Component { id: gpuPage; GpuPage { context: page.context; appearance: page.appearance } }
    Component { id: sensorsPage; SensorsPage { context: page.context; appearance: page.appearance } }
    Component { id: batteryPage; BatteryPage { context: page.context; appearance: page.appearance } }
    Component { id: bluetoothPage; BluetoothPage { context: page.context; appearance: page.appearance } }
    Component { id: clockPage; ClockPage { context: page.context; appearance: page.appearance } }
}
