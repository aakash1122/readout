// Battery drawing shared by the popup dashboard and the panel style (Stats 2.6 and 3.8):
// outlined body, detached terminal nub, level fill, and a lightning bolt while charging.
import QtQuick
import QtQuick.Shapes

Item {
    id: glyph

    property real level: 0 // 0..100, NaN when unknown
    property color outlineColor: "black"
    property color fillColor: "black"
    // Drawn around the bolt so it stays visible over the fill: normally the background.
    property color knockoutColor: "white"
    property bool charging: false
    property real radius: 3
    property real lineWidth: 1
    property real nubWidth: Math.max(2, Math.round(width * 0.07))
    property real gap: 1

    readonly property bool known: !isNaN(level)

    Rectangle {
        id: body

        width: glyph.width - glyph.nubWidth - glyph.gap
        height: glyph.height
        radius: glyph.radius
        color: "transparent"
        border.width: glyph.lineWidth
        border.color: glyph.outlineColor
    }

    Rectangle {
        anchors.left: body.right
        anchors.leftMargin: glyph.gap
        anchors.verticalCenter: body.verticalCenter
        width: glyph.nubWidth
        height: Math.max(2, Math.round(glyph.height * 0.36))
        radius: Math.min(width, height) / 2
        color: glyph.outlineColor
    }

    Rectangle {
        readonly property real inset: glyph.lineWidth * 1.5

        x: inset
        y: inset
        width: glyph.known ? Math.max(1, (body.width - inset * 2) * Math.max(0, Math.min(100, glyph.level)) / 100) : 0
        height: body.height - inset * 2
        radius: Math.max(1, glyph.radius - inset / 2)
        color: glyph.fillColor
    }

    Shape {
        id: bolt

        anchors.centerIn: body
        width: Math.round(body.height * 0.5)
        height: Math.round(body.height * 0.9)
        visible: glyph.charging
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            fillColor: glyph.outlineColor
            strokeColor: glyph.knockoutColor
            strokeWidth: Math.max(1, glyph.lineWidth)
            joinStyle: ShapePath.MiterJoin

            PathPolyline {
                path: [
                    Qt.point(bolt.width * 0.62, 0),
                    Qt.point(bolt.width * 0.05, bolt.height * 0.56),
                    Qt.point(bolt.width * 0.45, bolt.height * 0.56),
                    Qt.point(bolt.width * 0.38, bolt.height),
                    Qt.point(bolt.width * 0.95, bolt.height * 0.44),
                    Qt.point(bolt.width * 0.55, bolt.height * 0.44),
                    Qt.point(bolt.width * 0.62, 0),
                ]
            }
        }
    }

    Text {
        anchors.centerIn: body
        visible: !glyph.known
        text: "?"
        color: glyph.outlineColor
        font.pixelSize: Math.max(6, Math.round(body.height * 0.75))
    }
}
