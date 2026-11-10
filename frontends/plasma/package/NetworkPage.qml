// Network popup (docs/visual-spec.md 2.5 and 6.5): download and upload numbers, mirrored usage
// history, connectivity grid, details, interface and addresses. Linux has no per-process traffic.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import "Logic.js" as Logic

PageLayout {
    id: page

    readonly property var network: source.data.network ?? {}
    readonly property var wifi: network.wifi ?? {}
    readonly property bool bits: configuration.speedBits
    readonly property var interfaces: network.interfaces ?? []
    readonly property var primary: interfaces.find(entry => entry.name === network.interface) ?? {}
    readonly property var others: interfaces.filter(entry => entry.name !== network.interface && entry.up)
    readonly property bool isWifi: network.connectionType === "wifi" || (network.ssid ?? "").length > 0
    readonly property var totalsBase: context.networkTotalsBase.interface === network.interface ? context.networkTotalsBase : ({ downloaded: 0, uploaded: 0 })
    property bool showInterfaceDetails: false

    // Public IP is fetched only when the user asks, never in the background.
    property string publicAddress
    property bool fetchingPublicAddress: false

    function fetchPublicAddress() {
        fetchingPublicAddress = true
        const request = new XMLHttpRequest()
        request.onreadystatechange = () => {
            if (request.readyState !== XMLHttpRequest.DONE) {
                return
            }
            fetchingPublicAddress = false
            const text = request.status === 200 ? request.responseText.trim() : ""
            publicAddress = /^[0-9a-fA-F:.]{2,45}$/.test(text) ? text : i18n("Unavailable")
        }
        request.open("GET", "https://api.ipify.org")
        request.send()
    }

    function typeText(type) {
        switch (type) {
        case "ethernet": return i18nc("@info connection type", "Ethernet")
        case "wifi": return i18nc("@info connection type", "Wi-Fi")
        case "vpn": return i18nc("@info connection type", "VPN")
        }
        return type ?? ""
    }
    function upDown(value) {
        return value ? i18nc("@info:status network", "UP") : i18nc("@info:status network", "DOWN")
    }
    function channel(frequency) {
        if (!Logic.isNumber(frequency) || frequency <= 0) {
            return ""
        }
        const number = frequency >= 5950 ? (frequency - 5950) / 5 : frequency >= 5000 ? (frequency - 5000) / 5 : (frequency - 2407) / 5
        const band = (wifi.band ?? "").length > 0 ? wifi.band : Format.number(frequency / 1000, 1) + " GHz"
        return i18nc("@info Wi-Fi channel and band", "%1 (%2)", Math.round(number), band)
    }
    function totalText(value, base) {
        return Logic.isNumber(value) ? Format.bytes(Math.max(0, value - base)) : ""
    }

    Dashboard {
        appearance: page.appearance

        RowLayout {
            anchors.fill: parent
            spacing: 0

            Repeater {
                model: [
                    { rate: page.network.download ?? 0, color: page.appearance.blue, title: i18n("Downloading") },
                    { rate: page.network.upload ?? 0, color: page.appearance.red, title: i18n("Uploading") },
                ]

                Item {
                    id: half

                    required property var modelData
                    readonly property var parts: {
                        const text = Format.rate(half.modelData.rate, page.bits)
                        const match = /^(.*?)[\s  ]+(\S+)$/.exec(text)
                        return match ? [match[1], match[2]] : [text, ""]
                    }

                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Column {
                        anchors.centerIn: parent
                        spacing: 0

                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: page.appearance.px(5)

                            StatsText {
                                id: rateNumber

                                appearance: page.appearance
                                points: 26
                                font.weight: Font.Light
                                text: half.parts[0]
                            }
                            StatsText {
                                anchors.baseline: rateNumber.baseline
                                appearance: page.appearance
                                points: 13
                                font.weight: Font.Light
                                color: page.appearance.withAlpha(page.appearance.text, 0.85)
                                text: half.parts[1]
                            }
                        }
                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: page.appearance.px(4)

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: page.appearance.markerSize
                                height: width
                                radius: page.appearance.px(4)
                                color: half.modelData.rate >= 1 ? half.modelData.color : page.appearance.withAlpha(page.appearance.lightGray, 0.75)
                            }
                            StatsText {
                                appearance: page.appearance
                                points: 12
                                color: page.appearance.label
                                text: half.modelData.title
                            }
                        }
                    }
                }
            }
        }
    }

    Separator { appearance: page.appearance; text: i18n("Usage history") }
    MirroredChart {
        appearance: page.appearance
        showScale: true
        bits: page.bits
        upperValues: uploadHistory.values
        lowerValues: downloadHistory.values

        HistorySeries {
            id: uploadHistory

            source: page.source
            module: "network"
            series: "upload"
        }
        HistorySeries {
            id: downloadHistory

            source: page.source
            module: "network"
            series: "download"
        }
    }

    Separator { appearance: page.appearance; text: i18n("Connectivity history") }
    ConnectivityGrid {
        appearance: page.appearance
        values: page.context.connectivity
    }

    Separator {
        appearance: page.appearance
        text: i18n("Details")
        actionIcon: "edit-undo"
        actionText: i18n("Reset totals")
        onActionTriggered: page.context.resetNetworkTotals()
    }
    InfoRow {
        appearance: page.appearance
        label: i18n("Total upload")
        value: page.totalText(page.network.totalUploaded, page.totalsBase.uploaded)
        marker: page.appearance.red
    }
    InfoRow {
        appearance: page.appearance
        label: i18n("Total download")
        value: page.totalText(page.network.totalDownloaded, page.totalsBase.downloaded)
        marker: page.appearance.blue
    }
    InfoRow { appearance: page.appearance; label: i18n("Status"); value: page.upDown(page.network.connected === true) }
    InfoRow {
        appearance: page.appearance
        label: i18n("Internet connection")
        value: {
            switch (page.network.connectivity) {
            case "full": return page.upDown(true)
            case "none":
            case "limited":
            case "portal": return page.upDown(false)
            }
            return page.network.connected === undefined ? "" : i18nc("@info:status network connectivity", "Unknown")
        }
    }

    Separator {
        appearance: page.appearance
        text: i18n("Interface")
        actionIcon: "configure"
        actionText: page.showInterfaceDetails ? i18n("Fewer details") : i18n("More details")
        onActionTriggered: page.showInterfaceDetails = !page.showInterfaceDetails
    }
    InfoRow {
        appearance: page.appearance
        label: i18n("Interface")
        value: {
            const name = page.network.interface ?? ""
            const type = page.typeText(page.network.connectionType)
            return type.length > 0 && name.length > 0 ? i18nc("@info connection type (interface)", "%1 (%2)", type, name) : name
        }
    }
    InfoRow { appearance: page.appearance; label: i18n("Status"); value: page.primary.up === undefined ? "" : page.upDown(page.primary.up) }
    InfoRow { appearance: page.appearance; label: i18n("Physical address"); value: page.primary.mac ?? "" }
    InfoRow {
        appearance: page.appearance
        label: i18n("Network")
        value: !page.isWifi ? (page.network.connectionName ?? "")
             : Logic.isNumber(page.network.signal) ? i18nc("@info Wi-Fi network (signal)", "%1 (%2)", page.network.ssid ?? "", Format.percent(page.network.signal))
             : (page.network.ssid ?? "")
    }
    InfoRow {
        appearance: page.appearance
        label: i18n("Channel")
        value: page.showInterfaceDetails && page.isWifi ? page.channel(page.wifi.frequency) : ""
    }
    InfoRow {
        appearance: page.appearance
        label: i18n("Security")
        value: page.showInterfaceDetails && page.isWifi ? (page.wifi.security ?? "") : ""
    }
    InfoRow {
        appearance: page.appearance
        label: i18n("Speed")
        value: page.showInterfaceDetails && (page.wifi.bitrate ?? 0) > 0 ? i18nc("@info bitrate", "%1 Mbit/s", Format.number(page.wifi.bitrate)) : ""
    }

    Separator { appearance: page.appearance; text: i18n("Address") }
    InfoRow {
        appearance: page.appearance
        label: i18n("Local IP")
        value: (page.primary.ipv4 ?? [])[0] ?? (page.primary.ipv6 ?? [])[0] ?? ""
    }
    InfoRow {
        appearance: page.appearance
        label: i18n("Local IPv6")
        value: (page.primary.ipv4 ?? []).length > 0 ? ((page.primary.ipv6 ?? [])[0] ?? "") : ""
        valuePoints: 11
    }
    InfoRow {
        appearance: page.appearance
        label: i18n("Public IP")
        value: page.publicAddress.length > 0 ? page.publicAddress
             : page.fetchingPublicAddress ? i18n("Updating…") : i18nc("@action:button reveal public IP address", "Show")
        valueColor: page.publicAddress.length > 0 || page.fetchingPublicAddress ? page.appearance.text : page.appearance.accent

        HoverHandler {
            id: publicHover

            enabled: page.publicAddress.length === 0 && !page.fetchingPublicAddress
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            enabled: publicHover.enabled
            onTapped: page.fetchPublicAddress()
        }
    }

    Separator {
        visible: page.others.length > 0
        appearance: page.appearance
        text: i18n("Other interfaces")
    }
    Repeater {
        model: page.others.length

        InfoRow {
            required property int index
            readonly property var modelData: page.others[index] ?? ({ name: "", download: 0, upload: 0 })

            appearance: page.appearance
            label: modelData.name
            value: i18nc("@info download and upload rates", "↓ %1  ↑ %2", Format.rate(modelData.download ?? 0, page.bits), Format.rate(modelData.upload ?? 0, page.bits))
            valuePoints: 11
        }
    }
}
