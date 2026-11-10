// Column for one module page. Sections stack with no gaps; separators give the rhythm (Stats 1.1).
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    required property AppletContext context
    required property Appearance appearance

    readonly property ReadoutSource source: context.source
    readonly property var configuration: context.configuration

    spacing: 0
}
