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
    // Bind as root.field(root.rev, key): the rev argument makes the binding depend on rev.
    function field(rev, key) { return vm.fieldValue(key) }

    ColumnLayout {
        width: root.availableWidth
        spacing: 16

        FLSettingsGroup {
            title: "Output"

            FLSettingRow {
                title: "Default volume"
                description: "Default playback volume (0-100)."
                keyName: "audio.defaultVolume"
                RowLayout {
                    spacing: 10
                    FLSlider {
                        from: 0; to: 100; stepSize: 1
                        value: root.field(root.rev, "audio.defaultVolume")
                        onMoved: root.vm.setFieldValue("audio.defaultVolume", Math.round(value))
                    }
                    Text {
                        text: Math.round(root.field(root.rev, "audio.defaultVolume"))
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
                keyName: "audio.channelMode"
                FLComboBox {
                    model: ["Auto", "Mono", "Stereo", "Surround"]
                    currentIndex: root.field(root.rev, "audio.channelMode")
                    onActivated: root.vm.setFieldValue("audio.channelMode", currentIndex)
                }
            }
            FLSettingRow {
                title: "Output device"
                description: "Preferred output device name; empty uses the system default."
                keyName: "audio.outputDevice"
                FLTextField {
                    implicitWidth: 220
                    text: root.field(root.rev, "audio.outputDevice")
                    placeholderText: "System default"
                    onEditingFinished: root.vm.setFieldValue("audio.outputDevice", text)
                }
            }
            FLSettingRow {
                title: "Preferred language"
                description: "Audio language to auto-select (ISO 639 code, e.g. eng)."
                keyName: "audio.defaultLanguage"
                FLTextField {
                    implicitWidth: 120
                    text: root.field(root.rev, "audio.defaultLanguage")
                    onEditingFinished: root.vm.setFieldValue("audio.defaultLanguage", text)
                }
            }
            FLSettingRow {
                title: "Sync offset (ms)"
                description: "Positive delays audio relative to video."
                keyName: "audio.syncOffsetMs"
                FLSpinBox {
                    from: -5000; to: 5000; stepSize: 10
                    value: root.field(root.rev, "audio.syncOffsetMs")
                    onValueModified: root.vm.setFieldValue("audio.syncOffsetMs", value)
                }
            }
        }

        FLSettingsGroup {
            title: "Ducking"

            FLSettingRow {
                title: "Enable ducking"
                description: "Reduce playback volume while app-owned transient audio is active."
                keyName: "audio.duckingEnabled"
                FLSwitch {
                    checked: root.field(root.rev, "audio.duckingEnabled")
                    onToggled: root.vm.setFieldValue("audio.duckingEnabled", checked)
                }
            }
            FLSettingRow {
                title: "Ducked level"
                description: "Playback gain while ducked, as percent of current volume."
                keyName: "audio.duckingLevel"
                RowLayout {
                    spacing: 10
                    FLSlider {
                        from: 0; to: 100; stepSize: 1
                        value: root.field(root.rev, "audio.duckingLevel")
                        onMoved: root.vm.setFieldValue("audio.duckingLevel", Math.round(value))
                    }
                    Text {
                        text: Math.round(root.field(root.rev, "audio.duckingLevel"))
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
                keyName: "audio.normalizeEnabled"
                FLSwitch {
                    checked: root.field(root.rev, "audio.normalizeEnabled")
                    onToggled: root.vm.setFieldValue("audio.normalizeEnabled", checked)
                }
            }
            FLSettingRow {
                title: "Algorithm"
                description: "Limiter boosts quiet audio and prevents clipping; Dynamic normalizer levels audio over time."
                keyName: "audio.normalizeMode"
                FLComboBox {
                    model: ["limiter", "dynaudnorm"]
                    currentIndex: Math.max(0, model.indexOf(root.field(root.rev, "audio.normalizeMode")))
                    onActivated: root.vm.setFieldValue("audio.normalizeMode", model[currentIndex])
                }
            }
            FLSettingRow {
                visible: root.field(root.rev, "audio.normalizeMode") === "limiter"
                title: "Input gain"
                description: "Gain applied before lookahead limiting."
                keyName: "audio.limiterLevelIn"
                FLTextField {
                    implicitWidth: 100
                    validator: DoubleValidator { bottom: 0.015625; top: 64.0 }
                    text: Number(root.field(root.rev, "audio.limiterLevelIn")).toFixed(2)
                    onEditingFinished: root.vm.setFieldValue("audio.limiterLevelIn", Number(text))
                }
            }
            FLSettingRow {
                visible: root.field(root.rev, "audio.normalizeMode") === "limiter"
                title: "Output gain"
                description: "Gain applied after lookahead limiting."
                keyName: "audio.limiterLevelOut"
                FLTextField {
                    implicitWidth: 100
                    validator: DoubleValidator { bottom: 0.015625; top: 64.0 }
                    text: Number(root.field(root.rev, "audio.limiterLevelOut")).toFixed(2)
                    onEditingFinished: root.vm.setFieldValue("audio.limiterLevelOut", Number(text))
                }
            }
            FLSettingRow {
                visible: root.field(root.rev, "audio.normalizeMode") === "limiter"
                title: "Peak limit"
                description: "Maximum output signal magnitude."
                keyName: "audio.limiterLimit"
                FLTextField {
                    implicitWidth: 100
                    validator: DoubleValidator { bottom: 0.0625; top: 1.0 }
                    text: Number(root.field(root.rev, "audio.limiterLimit")).toFixed(2)
                    onEditingFinished: root.vm.setFieldValue("audio.limiterLimit", Number(text))
                }
            }
            FLSettingRow {
                visible: root.field(root.rev, "audio.normalizeMode") === "limiter"
                title: "Attack (ms)"
                description: "How quickly limiting begins when a peak arrives."
                keyName: "audio.limiterAttack"
                FLTextField {
                    implicitWidth: 100
                    validator: DoubleValidator { bottom: 0.1; top: 80.0 }
                    text: Number(root.field(root.rev, "audio.limiterAttack")).toFixed(2)
                    onEditingFinished: root.vm.setFieldValue("audio.limiterAttack", Number(text))
                }
            }
            FLSettingRow {
                visible: root.field(root.rev, "audio.normalizeMode") === "limiter"
                title: "Release (ms)"
                description: "How long gain reduction takes to recover."
                keyName: "audio.limiterRelease"
                FLTextField {
                    implicitWidth: 100
                    validator: DoubleValidator { bottom: 1.0; top: 8000.0 }
                    text: Number(root.field(root.rev, "audio.limiterRelease")).toFixed(2)
                    onEditingFinished: root.vm.setFieldValue("audio.limiterRelease", Number(text))
                }
            }
            FLSettingRow {
                visible: root.field(root.rev, "audio.normalizeMode") === "dynaudnorm"
                title: "Frame length (ms)"
                description: "Filter frame length in milliseconds."
                keyName: "audio.dynaudnormFrameLen"
                FLSpinBox {
                    from: 10; to: 8000; stepSize: 10
                    value: root.field(root.rev, "audio.dynaudnormFrameLen")
                    onValueModified: root.vm.setFieldValue("audio.dynaudnormFrameLen", value)
                }
            }
            FLSettingRow {
                visible: root.field(root.rev, "audio.normalizeMode") === "dynaudnorm"
                title: "Gaussian window"
                description: "Gaussian filter window size (odd number)."
                keyName: "audio.dynaudnormGaussSize"
                FLSpinBox {
                    from: 3; to: 301; stepSize: 2
                    value: root.field(root.rev, "audio.dynaudnormGaussSize")
                    onValueModified: root.vm.setFieldValue("audio.dynaudnormGaussSize", value)
                }
            }
            FLSettingRow {
                visible: root.field(root.rev, "audio.normalizeMode") === "dynaudnorm"
                title: "Target peak"
                description: "Target peak magnitude (0.0-1.0)."
                keyName: "audio.dynaudnormPeak"
                FLTextField {
                    implicitWidth: 100
                    validator: DoubleValidator { bottom: 0.0; top: 1.0 }
                    text: Number(root.field(root.rev, "audio.dynaudnormPeak")).toFixed(2)
                    onEditingFinished: root.vm.setFieldValue("audio.dynaudnormPeak", Number(text))
                }
            }
            FLSettingRow {
                visible: root.field(root.rev, "audio.normalizeMode") === "dynaudnorm"
                title: "Max gain"
                description: "Maximum gain factor."
                keyName: "audio.dynaudnormMaxGain"
                FLTextField {
                    implicitWidth: 100
                    validator: DoubleValidator { bottom: 1.0 }
                    text: Number(root.field(root.rev, "audio.dynaudnormMaxGain")).toFixed(2)
                    onEditingFinished: root.vm.setFieldValue("audio.dynaudnormMaxGain", Number(text))
                }
            }
            FLSettingRow {
                visible: root.field(root.rev, "audio.normalizeMode") === "dynaudnorm"
                title: "Post-normalization gain"
                description: "Output gain applied after dynamic normalization."
                keyName: "audio.dynaudnormVolume"
                FLTextField {
                    implicitWidth: 100
                    validator: DoubleValidator { bottom: 0.0 }
                    text: Number(root.field(root.rev, "audio.dynaudnormVolume")).toFixed(2)
                    onEditingFinished: root.vm.setFieldValue("audio.dynaudnormVolume", Number(text))
                }
            }
        }
    }
}
