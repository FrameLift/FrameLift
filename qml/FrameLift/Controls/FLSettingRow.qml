pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import FrameLift.Controls

// One labelled setting: title + optional description on the left, an input control
// on the right. Designed to sit inside a FLSettingsGroup; place the control as a child:
//
//   FLSettingRow { title: "Volume"; FLSlider { ... } }
RowLayout {
    id: root

    required property string title
    property string description: ""
    default property alias content: controlSlot.data

    Layout.fillWidth: true
    spacing: 16

    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        spacing: 2

        Text {
            text: root.title
            color: FLTheme.text
            font.pixelSize: 13
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }

        Text {
            visible: root.description.length > 0
            text: root.description
            color: FLTheme.textMuted
            font.pixelSize: 11
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
    }

    // A Row positioner (not a bare Item) sizes itself from its children's implicit
    // widths without the childrenRect ⇄ width feedback loop that collapsed narrow
    // controls to zero width (and let them overflow the panel edge).
    Row {
        id: controlSlot
        Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
    }
}
