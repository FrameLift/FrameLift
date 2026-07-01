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

    ColumnLayout {
        width: root.availableWidth
        spacing: 16

        FLSettingsGroup {
            title: "Recognised extensions"

            FLSettingRow {
                title: "Video files"
                description: "Semicolon-separated list of video file extensions."
                FLTextField {
                    implicitWidth: 280
                    text: (root.rev, root.vm.fieldValue("files.videoExtensions"))
                    onEditingFinished: root.vm.setFieldValue("files.videoExtensions", text)
                }
            }
            FLSettingRow {
                title: "Image files"
                description: "Semicolon-separated list of image file extensions."
                FLTextField {
                    implicitWidth: 280
                    text: (root.rev, root.vm.fieldValue("files.imageExtensions"))
                    onEditingFinished: root.vm.setFieldValue("files.imageExtensions", text)
                }
            }
        }
    }
}
