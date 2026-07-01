pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FrameLift.Controls
import FrameLift.Plugins.SettingsMenu

Item {
    id: root
    required property var viewModel

    FLSettingRow {
        anchors.left: parent.left
        anchors.right: parent.right
        title: "Maximum entries"
        description: "Number of recently played files to retain in history."
        SpinBox {
            from: 1
            to: 10000
            value: root.viewModel.maxEntries
            implicitWidth: 140
            onValueModified: root.viewModel.maxEntries = value
        }
    }
}
