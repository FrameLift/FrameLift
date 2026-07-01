pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FrameLift.Controls

Item {
    id: root
    required property var viewModel

    ListView {
        anchors.fill: parent
        model: root.viewModel !== null ? root.viewModel.plugins : []
        spacing: 8
        clip: true

        delegate: FLGlassPanel {
            id: pluginDelegate
            required property var modelData
            width: ListView.view.width
            height: 72

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Text {
                        text: pluginDelegate.modelData.name
                        color: FLTheme.text
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Text {
                        text: pluginDelegate.modelData.description
                        color: FLTheme.textMuted
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text {
                        text: pluginDelegate.modelData.loadFailed
                              ? "Failed to load"
                              : pluginDelegate.modelData.loaded ? "Loaded" : "Pending restart"
                        color: pluginDelegate.modelData.loadFailed ? FLTheme.danger : FLTheme.textMuted
                        font.pixelSize: 12
                    }
                }

                Switch {
                    checked: pluginDelegate.modelData.enabled
                    onToggled: {
                        if (root.viewModel !== null)
                            root.viewModel.setPluginEnabled(pluginDelegate.modelData.id, checked)
                    }
                }
            }
        }
    }
}
