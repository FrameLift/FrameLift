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
            title: "Decoding"

            FLSettingRow {
                title: "Hardware decoding"
                description: "Enable hardware video decoding."
                FLSwitch {
                    checked: (root.rev, root.vm.fieldValue("playback.hwdec"))
                    onToggled: root.vm.setFieldValue("playback.hwdec", checked)
                }
            }
            FLSettingRow {
                title: "Acceleration mode"
                description: "Video acceleration backend used when hardware decoding is on."
                FLComboBox {
                    implicitWidth: 180
                    model: ["off", "auto", "cuda-zero-copy", "cuda", "d3d11va", "dxva2", "vaapi"]
                    currentIndex: Math.max(0, model.indexOf((root.rev, root.vm.fieldValue("playback.hwdecMode"))))
                    onActivated: root.vm.setFieldValue("playback.hwdecMode", currentText)
                }
            }
            FLSettingRow {
                title: "Precise seeking"
                description: "Use precise (high-resolution) seeking."
                FLSwitch {
                    checked: (root.rev, root.vm.fieldValue("playback.hrSeek"))
                    onToggled: root.vm.setFieldValue("playback.hrSeek", checked)
                }
            }
        }

        FLSettingsGroup {
            title: "Auto-load"

            FLSettingRow {
                title: "Subtitles"
                description: "Auto-load subtitle files matching the opened media."
                FLSwitch {
                    checked: (root.rev, root.vm.fieldValue("playback.subAutoLoad"))
                    onToggled: root.vm.setFieldValue("playback.subAutoLoad", checked)
                }
            }
            FLSettingRow {
                title: "External audio"
                description: "Auto-load external audio tracks matching the opened media."
                FLSwitch {
                    checked: (root.rev, root.vm.fieldValue("playback.audioFileAutoLoad"))
                    onToggled: root.vm.setFieldValue("playback.audioFileAutoLoad", checked)
                }
            }
        }
    }
}
