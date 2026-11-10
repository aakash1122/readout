// Stats-style sizes and colors (docs/visual-spec.md sections 1 and 6).
// Sizes are Stats points scaled by the desktop font; colors use the Stats series palette
// with text and backgrounds from the desktop theme.
// Colors are read from `themeItem`, the panel item or the popup, whose Kirigami.Theme is always
// resolved; the panel and the popup can use different color sets. (A helper item that inherits
// its theme from a parent sometimes kept unresolved default colors inside Plasma popups.)
import QtQuick

import org.kde.kirigami as Kirigami

QtObject {
    id: appearance

    required property Item themeItem
    // True for popups: they follow the shared popup settings (text size, spacing, margins, width).
    property bool popup: false

    // Stats designs around a 13 pt body font; scale every Stats number by this factor.
    readonly property real scaleFactor: themeItem.Kirigami.Theme.defaultFont.pointSize * 4 / 3 / 13
    // Text, rings, gauges and paddings: the desktop scale times the popup text size.
    readonly property real sizeFactor: scaleFactor * (popup ? Settings.popupTextScale / 100 : 1)
    // Rows, section titles and gaps between cards.
    readonly property real spacingFactor: popup ? [0.8, 1, 1.25][Settings.popupSpacing] : 1
    function px(value) {
        return Math.round(value * sizeFactor)
    }

    readonly property bool dark: Kirigami.ColorUtils.brightnessForColor(themeItem.Kirigami.Theme.backgroundColor) === Kirigami.ColorUtils.Dark

    // Stats series palette, light and dark variants of the AppKit system colors.
    readonly property color blue: dark ? "#0a84ff" : "#007aff"
    readonly property color red: dark ? "#ff453a" : "#ff3b30"
    readonly property color orange: dark ? "#ff9f0a" : "#ff9500"
    readonly property color pink: dark ? "#ff375f" : "#ff2d55"
    readonly property color green: dark ? "#32d74b" : "#28cd41"
    readonly property color yellow: dark ? "#ffd60a" : "#ffcc00"
    readonly property color teal: dark ? "#6ac4dc" : "#59adc4"
    readonly property color indigo: dark ? "#5e5ce6" : "#5856d6"
    readonly property color utilizationOrange: "#ff8000"
    readonly property color utilizationRed: "#ff0000"
    readonly property color lightGray: "#aaaaaa"

    // The desktop accent, unless it barely stands out from the background (some Plasma themes
    // report a near-black highlight on dark popups); then Stats blue.
    readonly property color accent: Math.abs(luminance(themeItem.Kirigami.Theme.highlightColor) - luminance(background)) >= 0.2
                                    ? themeItem.Kirigami.Theme.highlightColor : blue
    readonly property color text: themeItem.Kirigami.Theme.textColor
    readonly property color background: themeItem.Kirigami.Theme.backgroundColor
    // Stats uses 55% and 30%; Plasma popups are translucent and blurred, so both are a little stronger.
    readonly property color label: withAlpha(text, 0.7)
    readonly property color tertiary: withAlpha(text, 0.45)
    readonly property color rule: withAlpha(text, 0.15)
    readonly property color well: withAlpha(lightGray, 0.1)
    readonly property color card: dark ? Qt.rgba(0, 0, 0, 0.25) : withAlpha(text, 0.04)
    // "Inactive" remainder of rings and gauges: lightGray at 50%, made opaque because
    // KQuickCharts draws chart backgrounds without alpha blending.
    readonly property color inactive: blend(background, lightGray, 0.5)
    readonly property color idle: withAlpha(lightGray, 0.5)

    // Approximate perceived brightness, 0..1.
    function luminance(color) {
        return 0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b
    }
    function withAlpha(color, alpha) {
        return Qt.rgba(color.r, color.g, color.b, alpha)
    }
    function blend(from, to, amount) {
        return Kirigami.ColorUtils.linearInterpolation(from, to, amount)
    }

    // Font pixel sizes for the Stats point sizes in use.
    readonly property real bodyPixels: themeItem.Kirigami.Theme.defaultFont.pointSize * 4 / 3
    function fontPixels(points) {
        return Math.max(6, Math.round(points * sizeFactor))
    }

    // Sizes. Narrow, normal (Stats) and wide content widths; rows keep room for their text.
    readonly property int contentWidth: px(popup ? [236, 264, 320][Settings.popupWidth] : 264)
    readonly property int rowHeight: Math.max(fontPixels(13) + 4, Math.round(px(22) * spacingFactor))
    readonly property int separatorHeight: Math.max(fontPixels(12) + 6, Math.round(px(30) * spacingFactor))
    // Extra space above each section title, so sections read as separate blocks.
    readonly property int sectionGap: Math.round(px(6) * spacingFactor)
    readonly property int dashboardHeight: px(90)
    readonly property int headerHeight: Math.round(px(42) * Math.min(1, spacingFactor))
    readonly property int stroke: Math.max(3, px(7))
    readonly property int markerSize: px(12)
    readonly property int cardSpacing: Math.round(px(8) * spacingFactor)
    // Gap between the popup edge and its content.
    readonly property int margin: Math.round((popup ? Settings.popupMargin : 8) * scaleFactor)
}
