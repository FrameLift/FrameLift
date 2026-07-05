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
                title: "Acceleration mode"
                description: "Video acceleration backend. Use off to disable hardware decoding."
                keyName: "playback.hwdecMode"
                FLComboBox {
                    implicitWidth: 180
                    // Only the acceleration modes this machine can actually use
                    // (host-probed), so incompatible backends aren't offered.
                    model: root.vm.availableHwdecModes()
                    currentIndex: Math.max(0, model.indexOf((root.rev, root.vm.fieldValue("playback.hwdecMode"))))
                    onActivated: root.vm.setFieldValue("playback.hwdecMode", currentText)
                }
            }
            FLSettingRow {
                title: "Seek precision"
                description: "Smart: arrow-key seeks snap to keyframes for speed, seek-bar seeks are exact. Exact: all seeks precise (slower on long-GOP files). Keyframe: all seeks snap."
                keyName: "playback.seekMode"
                FLComboBox {
                    implicitWidth: 180
                    model: ["smart", "exact", "keyframe"]
                    currentIndex: Math.max(0, model.indexOf((root.rev, root.vm.fieldValue("playback.seekMode"))))
                    onActivated: root.vm.setFieldValue("playback.seekMode", currentText)
                }
            }
            FLSettingRow {
                title: "Fast file opening"
                description: "Speed up file opening by limiting stream probing. Unusual containers (TS/AVI) may misdetect tracks — leave off if tracks go missing."
                keyName: "playback.fastProbe"
                FLSwitch {
                    checked: (root.rev, root.vm.fieldValue("playback.fastProbe"))
                    onToggled: root.vm.setFieldValue("playback.fastProbe", checked)
                }
            }
        }

        FLSettingsGroup {
            title: "Auto-load"

            FLSettingRow {
                title: "Subtitles"
                description: "Auto-load subtitle files matching the opened media."
                keyName: "playback.subAutoLoad"
                FLSwitch {
                    checked: (root.rev, root.vm.fieldValue("playback.subAutoLoad"))
                    onToggled: root.vm.setFieldValue("playback.subAutoLoad", checked)
                }
            }
            FLSettingRow {
                title: "External audio"
                description: "Auto-load external audio tracks matching the opened media."
                keyName: "playback.audioFileAutoLoad"
                FLSwitch {
                    checked: (root.rev, root.vm.fieldValue("playback.audioFileAutoLoad"))
                    onToggled: root.vm.setFieldValue("playback.audioFileAutoLoad", checked)
                }
            }
        }
    }
}
