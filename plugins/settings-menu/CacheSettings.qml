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
            title: "Read-ahead"

            FLSettingRow {
                title: "Enable read-ahead"
                description: "Buffer upcoming data while playing."
                FLSwitch {
                    checked: (root.rev, root.vm.fieldValue("cache.readAheadEnabled"))
                    onToggled: root.vm.setFieldValue("cache.readAheadEnabled", checked)
                }
            }
            FLSettingRow {
                title: "Buffer size (MB)"
                description: "Maximum read-ahead buffer size, in megabytes."
                FLSpinBox {
                    from: 1; to: 4096; stepSize: 16
                    value: (root.rev, root.vm.fieldValue("cache.readAheadSizeMB"))
                    onValueModified: root.vm.setFieldValue("cache.readAheadSizeMB", value)
                }
            }
        }
    }
}
