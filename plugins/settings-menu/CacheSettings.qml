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
            title: "Read-ahead"

            FLSettingRow {
                title: "Enable read-ahead"
                description: "Buffer upcoming data while playing."
                keyName: "cache.readAheadEnabled"
                FLSwitch {
                    checked: root.field(root.rev, "cache.readAheadEnabled")
                    onToggled: root.vm.setFieldValue("cache.readAheadEnabled", checked)
                }
            }
            FLSettingRow {
                title: "Buffer size (MB)"
                description: "Maximum read-ahead buffer size, in megabytes."
                keyName: "cache.readAheadSizeMB"
                FLSpinBox {
                    from: 1; to: 4096; stepSize: 16
                    value: root.field(root.rev, "cache.readAheadSizeMB")
                    onValueModified: root.vm.setFieldValue("cache.readAheadSizeMB", value)
                }
            }
        }
    }
}
