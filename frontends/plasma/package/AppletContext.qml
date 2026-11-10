// Everything one Readout widget instance shares between its panel item, popup and alerts.
// Kept separate from the PlasmoidItem so the representations can be typed against it.
import QtQuick

import "Logic.js" as Logic

QtObject {
    id: context

    required property var configuration
    property bool vertical: false
    // True while the popup (or the desktop's full view) shows; switches modules to the detail tier.
    property bool detail: false

    // Modules this item shows, in order. Unknown names are dropped.
    readonly property var modules: {
        const configured = (configuration.modules ?? []).filter(module => Logic.allModules.indexOf(module) >= 0)
        return configured.length > 0 ? configured : ["cpu"]
    }

    function styleFor(module) {
        return Logic.styleFor(module, configuration.styles)
    }

    // Translated module names and one-line summaries.
    readonly property Summary summary: Summary {}

    // Handled by main.qml, which owns the Plasmoid.
    signal settingsRequested()
    signal closePopupRequested()

    readonly property ReadoutSource source: ReadoutSource {
        modules: Logic.daemonModules(context.modules)
        detail: context.detail
        sensors: context.configuration.sensors
        cores: context.modules.indexOf("cpu") >= 0 && context.styleFor("cpu") === "bars"
        intervalMilliseconds: Settings.updateInterval * 1000
    }

    readonly property WorldClock clock: WorldClock {
        zones: context.configuration.timeZones
        hourFormat: context.configuration.hourFormat
        showSeconds: context.configuration.showSeconds
        // Ticks only while the clock is shown.
        active: context.modules.indexOf("clock") >= 0
    }

    // Last full popup height per view ("cpu", ..., or "overview"), from detail-tier data.
    // While a popup waits for its first detail update it keeps this height, so it opens at its
    // final size instead of growing. Saved to the config when the popup closes.
    property var popupHeights: {
        try {
            const saved = JSON.parse(configuration.popupHeights || "{}")
            return saved !== null && typeof saved === "object" ? saved : {}
        } catch (error) {
            return {}
        }
    }

    function rememberPopupHeight(view: string, height: int) {
        if (height > 0 && popupHeights[view] !== height) {
            const next = Object.assign({}, popupHeights)
            next[view] = height
            popupHeights = next
        }
    }

    onDetailChanged: {
        const text = JSON.stringify(popupHeights)
        if (!detail && configuration.popupHeights !== text) {
            configuration.popupHeights = text
        }
    }

    // Client-side state that Stats keeps across popup openings (docs/visual-spec.md 6.5).
    // Connectivity per network update, newest last, true when online; at most 90 entries.
    property var connectivity: []
    // When external power was last removed during this session (ms since the epoch), or 0.
    property real unpluggedAt: 0
    // Network totals at the last "reset", subtracted from the daemon's counters.
    property var networkTotalsBase: ({ interface: "", downloaded: 0, uploaded: 0 })

    function resetNetworkTotals() {
        const network = source.data.network ?? {}
        networkTotalsBase = {
            interface: network.interface ?? "",
            downloaded: network.totalDownloaded ?? 0,
            uploaded: network.totalUploaded ?? 0,
        }
    }

    readonly property Connections updates: Connections {
        id: sourceUpdates

        target: context.source

        property bool wasPluggedIn: false

        function onUpdated(module) {
            if (module === "network") {
                const network = context.source.data.network ?? {}
                const online = network.connectivity !== undefined && network.connectivity !== "unknown"
                        ? network.connectivity === "full" : network.connected === true
                const next = context.connectivity.length >= 90 ? context.connectivity.slice(1) : context.connectivity.slice()
                next.push(online)
                context.connectivity = next
            } else if (module === "battery") {
                const battery = context.source.data.battery ?? {}
                const pluggedIn = battery.externalPower === true
                if (sourceUpdates.wasPluggedIn && !pluggedIn && battery.present === true) {
                    context.unpluggedAt = Date.now()
                }
                sourceUpdates.wasPluggedIn = pluggedIn
            }
        }
    }

    readonly property Alerts alerts: Alerts {
        source: context.source
        summary: context.summary
        configuration: context.configuration
        modules: context.modules
    }
}
