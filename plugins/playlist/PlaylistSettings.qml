pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FrameLift.Controls
import FrameLift.Plugins.SettingsMenu

Item {
    id: root
    required property var viewModel

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        FLSettingRow {
            title: "Scan subdirectories"
            description: "Include child folders when building the playlist for an opened file."
            Switch { checked: root.viewModel.scanSubdirs; onToggled: root.viewModel.scanSubdirs = checked }
        }

        FLSettingRow {
            title: "Maximum scan depth"
            description: "Limits how deep recursive playlist scans may descend."
            SpinBox {
                from: 0
                to: 32
                value: root.viewModel.scanMaxDepth
                implicitWidth: 140
                onValueModified: root.viewModel.scanMaxDepth = value
            }
        }

        FLSettingRow {
            title: "Mixed playlists"
            description: "Allow playlist scans to include supported non-video media types."
            Switch { checked: root.viewModel.mixedPlaylist; onToggled: root.viewModel.mixedPlaylist = checked }
        }

        FLSettingRow {
            title: "Image slideshow"
            description: "Advance image entries automatically when mixed playlists include images."
            Switch { checked: root.viewModel.imageSlideshow; onToggled: root.viewModel.imageSlideshow = checked }
        }

        FLSettingRow {
            title: "Slideshow duration"
            description: "Seconds to show each image before moving to the next entry."
            TextField {
                text: Number(root.viewModel.slideshowDuration).toString()
                validator: DoubleValidator { bottom: 0.1 }
                implicitWidth: 180
                onEditingFinished: root.viewModel.slideshowDuration = Number(text)
            }
        }

        FLSettingRow {
            title: "Auto reload"
            description: "Refresh the playlist when watched directories change."
            Switch { checked: root.viewModel.autoReload; onToggled: root.viewModel.autoReload = checked }
        }

        FLSettingRow {
            title: "Sort by name"
            description: "Order entries alphabetically by filename, interleaving subfolders instead of grouping by folder."
            Switch { checked: root.viewModel.sortByName; onToggled: root.viewModel.sortByName = checked }
        }

        Item { Layout.fillHeight: true }
    }
}
