// Centered secondary message for a page with nothing to show.
import QtQuick
import QtQuick.Layouts

StatsText {
    Layout.fillWidth: true
    Layout.minimumHeight: appearance.separatorHeight
    verticalAlignment: Text.AlignVCenter
    horizontalAlignment: Text.AlignHCenter
    color: appearance.label
    wrapMode: Text.WordWrap
    maximumLineCount: 3
    elide: Text.ElideNone
}
