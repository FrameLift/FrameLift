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

    // Bumped whenever the draft re-seeds (open/save/reset) so field bindings re-read.
    property int rev: 0
    Connections {
        target: root.vm
        function onChanged() { root.rev++ }
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 16

        FLSettingsGroup {
            title: "Output"

            FLSettingRow {
                title: "Default volume"
                description: "Default playback volume (0-100)."
                RowLayout {
                    spacing: 10
                    FLSlider {
                        from: 0; to: 100; stepSize: 1
                        value: (root.rev, root.vm.fieldValue("audio.defaultVolume"))
                        onMoved: root.vm.setFieldValue("audio.defaultVolume", Math.round(value))
                    }
                    Text {
                        text: Math.round((root.rev, root.vm.fieldValue("audio.defaultVolume")))
                        color: FLTheme.textMuted
                        font.pixelSize: 13
                        Layout.preferredWidth: 28
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }
            FLSettingRow {
                title: "Channel mode"
                description: "How decoded audio is mapped to output channels."
                FLComboBox {
                    model: ["Auto", "Mono", "Stereo", "Surround"]
                    currentIndex: (root.rev, root.vm.fieldValue("audio.channelMode"))
                    onActivated: root.vm.setFieldValue("audio.channelMode", currentIndex)
                }
            }
            FLSettingRow {
                title: "Output device"
                description: "Preferred output device name; empty uses the system default."
                FLTextField {
                    implicitWidth: 220
                    text: (root.rev, root.vm.fieldValue("audio.outputDevice"))
                    placeholderText: "System default"
                    onEditingFinished: root.vm.setFieldValue("audio.outputDevice", text)
                }
            }
            FLSettingRow {
                title: "Preferred language"
                description: "Audio language to auto-select (ISO 639 code, e.g. eng)."
                FLTextField {
                    implicitWidth: 120
                    text: (root.rev, root.vm.fieldValue("audio.defaultLanguage"))
                    onEditingFinished: root.vm.setFieldValue("audio.defaultLanguage", text)
                }
            }
            FLSettingRow {
                title: "Sync offset (ms)"
                description: "Positive delays audio relative to video."
                FLSpinBox {
                    from: -5000; to: 5000; stepSize: 10
                    value: (root.rev, root.vm.fieldValue("audio.syncOffsetMs"))
                    onValueModified: root.vm.setFieldValue("audio.syncOffsetMs", value)
                }
            }
        }

        FLSettingsGroup {
            title: "Ducking"

            FLSettingRow {
                title: "Enable ducking"
                description: "Reduce playback volume while app-owned transient audio is active."
                FLSwitch {
                    checked: (root.rev, root.vm.fieldValue("audio.duckingEnabled"))
                    onToggled: root.vm.setFieldValue("audio.duckingEnabled", checked)
                }
            }
            FLSettingRow {
                title: "Ducked level"
                description: "Playback gain while ducked, as percent of current volume."
                RowLayout {
                    spacing: 10
                    FLSlider {
                        from: 0; to: 100; stepSize: 1
                        value: (root.rev, root.vm.fieldValue("audio.duckingLevel"))
                        onMoved: root.vm.setFieldValue("audio.duckingLevel", Math.round(value))
                    }
                    Text {
                        text: Math.round((root.rev, root.vm.fieldValue("audio.duckingLevel")))
                        color: FLTheme.textMuted
                        font.pixelSize: 13
                        Layout.preferredWidth: 28
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }
        }

        FLSettingsGroup {
            title: "Normalization"

            FLSettingRow {
                title: "Enable by default"
                description: "Enable dynamic audio normalization by default."
                FLSwitch {
                    checked: (root.rev, root.vm.fieldValue("audio.normalizeEnabled"))
                    onToggled: root.vm.setFieldValue("audio.normalizeEnabled", checked)
                }
            }
            FLSettingRow {
                title: "Frame length (ms)"
                description: "Filter frame length in milliseconds."
                FLSpinBox {
                    from: 10; to: 8000; stepSize: 10
                    value: (root.rev, root.vm.fieldValue("audio.dynaudnormFrameLen"))
                    onValueModified: root.vm.setFieldValue("audio.dynaudnormFrameLen", value)
                }
            }
            FLSettingRow {
                title: "Gaussian window"
                description: "Gaussian filter window size (odd number)."
                FLSpinBox {
                    from: 3; to: 301; stepSize: 2
                    value: (root.rev, root.vm.fieldValue("audio.dynaudnormGaussSize"))
                    onValueModified: root.vm.setFieldValue("audio.dynaudnormGaussSize", value)
                }
            }
            FLSettingRow {
                title: "Target peak"
                description: "Target peak magnitude (0.0-1.0)."
                FLTextField {
                    implicitWidth: 100
                    validator: DoubleValidator { bottom: 0.0; top: 1.0 }
                    text: Number((root.rev, root.vm.fieldValue("audio.dynaudnormPeak"))).toFixed(2)
                    onEditingFinished: root.vm.setFieldValue("audio.dynaudnormPeak", Number(text))
                }
            }
            FLSettingRow {
                title: "Max gain"
                description: "Maximum gain factor."
                FLTextField {
                    implicitWidth: 100
                    validator: DoubleValidator { bottom: 1.0 }
                    text: Number((root.rev, root.vm.fieldValue("audio.dynaudnormMaxGain"))).toFixed(2)
                    onEditingFinished: root.vm.setFieldValue("audio.dynaudnormMaxGain", Number(text))
                }
            }
            FLSettingRow {
                title: "Target RMS"
                description: "Target RMS volume factor."
                FLTextField {
                    implicitWidth: 100
                    validator: DoubleValidator { bottom: 0.0 }
                    text: Number((root.rev, root.vm.fieldValue("audio.dynaudnormVolume"))).toFixed(2)
                    onEditingFinished: root.vm.setFieldValue("audio.dynaudnormVolume", Number(text))
                }
            }
        }
    }
}
