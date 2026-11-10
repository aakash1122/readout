// The values one panel module shows, computed once and shared by whichever style draws them.
// Sizes follow Stats menu-bar widgets (docs/visual-spec.md 3 and 6.1): Stats draws them 18 pt tall,
// so every Stats number is multiplied by `unit` = drawable height / 18.
import QtQuick

import "Logic.js" as Logic

QtObject {
    id: moduleState

    required property string module
    required property AppletContext context
    required property Appearance appearance

    readonly property var configuration: context.configuration
    readonly property Summary summary: context.summary
    readonly property ReadoutSource source: context.source
    readonly property string style: context.styleFor(module)
    readonly property bool vertical: context.vertical
    // Panel thickness: the height of a horizontal panel or the width of a vertical one. Set by ModuleWidget.
    property real thickness: 0

    // Drawable height: the panel minus a small margin, capped so thick panels do not get huge widgets.
    readonly property real drawHeight: Math.max(12, vertical ? Math.min(thickness / 2, appearance.scaleFactor * 26)
                                                             : Math.min(thickness - 4, appearance.scaleFactor * 26))
    readonly property real unit: drawHeight / 18
    function size(statsValue) {
        return Math.round(statsValue * unit)
    }
    function fontPixels(points) {
        return Math.max(6, Math.round(points * unit))
    }

    readonly property var payload: source.data[module]
    readonly property real level: summary.levelFor(module, payload, configuration)
    readonly property bool hasLevel: Logic.isNumber(level)
    readonly property string valueText: summary.valueFor(module, payload, configuration, context.clock.time)
    // Three-letter label drawn vertically beside chart styles, or the small label of "mini".
    readonly property bool showLabel: configuration.showLabel === true
    readonly property string label: summary.labelFor(module)
    readonly property string shortLabel: label.slice(0, 3).toUpperCase()
    readonly property bool showIcon: configuration.showIcon === true
    readonly property string iconName: summary.iconFor(module)

    // "normal", "warning" or "critical" by the configured thresholds.
    readonly property string levelState: {
        if (!hasLevel) {
            return "normal"
        }
        // Battery is inverted: with the default 70/90 thresholds it warns at 30% and is critical at 10%.
        const value = summary.levelIsInverted(module) ? 100 - level : level
        if (value >= configuration.criticalThreshold) {
            return "critical"
        }
        return value >= configuration.warningThreshold ? "warning" : "normal"
    }
    // Stats utilization colors: the accent, then plain orange, then plain red.
    readonly property color thresholdColor: levelState === "critical" ? appearance.utilizationRed
                                          : levelState === "warning" ? appearance.utilizationOrange : appearance.accent
    // Battery colors are fixed in every mode: red below 20%, else the text color.
    readonly property color batteryColor: hasLevel && level < 20 ? appearance.red : appearance.text

    // Fill of charts, bars and pies.
    readonly property color seriesColor: {
        if (module === "battery") {
            return batteryColor
        }
        switch (configuration.colorMode) {
        case "monochrome": return appearance.text
        case "thresholds": return thresholdColor
        }
        return appearance.accent
    }
    // Panel text is monochrome, tinted only in thresholds mode (and for a low battery).
    readonly property color valueColor: {
        if (module === "battery") {
            return batteryColor
        }
        return configuration.colorMode === "thresholds" && levelState !== "normal" ? thresholdColor : appearance.text
    }
    // Stats boxes behind line and bar charts: black on a light panel, white on a dark one.
    readonly property color boxColor: appearance.text
    // Monochrome content inside a box uses the opposite color.
    readonly property color boxSeriesColor: configuration.colorMode === "monochrome" ? appearance.background : seriesColor
    // Remainder of pies and tachometers, made opaque because KQuickCharts draws backgrounds without alpha.
    readonly property color inactiveColor: appearance.inactive

    // Segments for pie, tachometer and stacked bars: CPU splits system and user like Stats.
    readonly property var segments: {
        if (module === "cpu" && payload && Logic.isNumber(payload.system) && configuration.colorMode === "accent") {
            return { values: [payload.system, payload.user ?? 0], colors: [appearance.red, appearance.blue] }
        }
        return { values: [hasLevel ? level : 0], colors: [seriesColor] }
    }

    // History series the line style draws.
    readonly property string levelSeries: {
        if (module === "gpu") {
            const gpu = payload ? Logic.selectGpu(payload.gpus, configuration.gpu) : null
            return gpu ? gpu.id + "/usage" : ""
        }
        return module === "battery" ? "level" : "usage"
    }
}
