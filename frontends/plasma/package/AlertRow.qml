// One alert setting: an enable checkbox and its threshold.
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

RowLayout {
    id: row

    property string label
    property bool checked
    property int value
    property int from: 1
    property int to: 100
    property string unit

    signal toggled(bool checked)
    signal valueModified(int value)

    Kirigami.FormData.label: label

    QQC2.CheckBox {
        checked: row.checked
        onToggled: row.toggled(checked)
        Accessible.name: row.label
    }
    QQC2.SpinBox {
        enabled: row.checked
        from: row.from
        to: row.to
        value: row.value
        onValueModified: row.valueModified(value)
        Accessible.name: row.label
    }
    QQC2.Label {
        text: row.unit
    }
}
