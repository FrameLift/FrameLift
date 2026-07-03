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
    // Internal INI identifier for this setting, dotted "section.name" (e.g.
    // "audio.channelMode"). Shown as "[section]/name" when FLSettingsUiState.showKeys
    // is on, so users can locate the value in the raw settings.ini / Advanced page.
    property string keyName: ""
    default property alias content: controlSlot.data

    Layout.fillWidth: true
    spacing: 16

    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        spacing: 2

        Text {
            visible: FLSettingsUiState.showKeys && root.keyName.length > 0
            // "audio.channelMode" → "[audio]/channelMode" (split on the first dot).
            text: {
                const dot = root.keyName.indexOf(".")
                return dot < 0
                    ? root.keyName
                    : "[" + root.keyName.substring(0, dot) + "]/" + root.keyName.substring(dot + 1)
            }
            color: FLTheme.textMuted
            font.pixelSize: 11
            font.family: "monospace"
            Layout.fillWidth: true
            elide: Text.ElideRight
        }

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
