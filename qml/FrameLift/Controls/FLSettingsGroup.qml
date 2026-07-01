pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import FrameLift.Controls

// Titled group of related settings. Compose FLSettingRow children inside it:
//
//   FLSettingsGroup {
//       title: "Output"
//       FLSettingRow { title: "Volume"; FLSlider { ... } }
//       FLSettingRow { title: "Device"; FLTextField { ... } }
//   }
ColumnLayout {
    id: root

    required property string title
    default property alias content: body.data

    Layout.fillWidth: true
    spacing: 8

    Text {
        text: root.title
        color: FLTheme.text
        font.pixelSize: 15
        font.weight: Font.DemiBold
        Layout.fillWidth: true
        Layout.bottomMargin: 2
    }

    FLGlassPanel {
        Layout.fillWidth: true
        implicitHeight: body.implicitHeight + 24

        ColumnLayout {
            id: body
            anchors.fill: parent
            anchors.margins: 12
            spacing: 6
        }
    }
}
