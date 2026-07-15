pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FrameLift.Controls

ScrollView {
    id: root
    required property var viewModel
    property var vm: viewModel
    clip: true
    contentWidth: availableWidth

    property int rev: 0
    Connections {
        target: root.vm
        function onChanged() { root.rev++ }
    }
    // Bind as root.field(root.rev, key): the rev argument makes the binding depend on rev.
    function field(rev, key) { return vm.fieldValue(key) }

    ColumnLayout {
        width: root.availableWidth
        spacing: 16

        FLSettingsGroup {
            title: "Recognised extensions"

            FLSettingRow {
                title: "Video files"
                description: "Semicolon-separated list of video file extensions."
                keyName: "files.videoExtensions"
                FLTextField {
                    implicitWidth: 280
                    text: root.field(root.rev, "files.videoExtensions")
                    onEditingFinished: root.vm.setFieldValue("files.videoExtensions", text)
                }
            }
            FLSettingRow {
                title: "Image files"
                description: "Semicolon-separated list of image file extensions."
                keyName: "files.imageExtensions"
                FLTextField {
                    implicitWidth: 280
                    text: root.field(root.rev, "files.imageExtensions")
                    onEditingFinished: root.vm.setFieldValue("files.imageExtensions", text)
                }
            }
        }

        FLSettingsGroup {
            title: "Open dialog"

            FLSettingRow {
                title: "Remember open folder"
                description: "Start the open-file dialog in the last folder used."
                keyName: "files.rememberOpenDialogDirectory"
                FLSwitch {
                    checked: root.field(root.rev, "files.rememberOpenDialogDirectory")
                    onToggled: root.vm.setFieldValue("files.rememberOpenDialogDirectory", checked)
                }
            }
        }
    }
}
