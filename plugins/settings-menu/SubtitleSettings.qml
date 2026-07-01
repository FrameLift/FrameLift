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

    // Reusable colour row: hex text field with a live swatch.
    component ColorRow: FLSettingRow {
        id: colorRow
        property string settingKey: ""
        RowLayout {
            spacing: 8
            Rectangle {
                width: 24; height: 24; radius: 6
                border.color: FLTheme.border; border.width: 1
                color: {
                    const v = (root.rev, root.vm.fieldValue(colorRow.settingKey))
                    return /^#([0-9a-fA-F]{6})$/.test(v) ? v : "transparent"
                }
            }
            FLTextField {
                implicitWidth: 110
                text: (root.rev, root.vm.fieldValue(colorRow.settingKey))
                onEditingFinished: root.vm.setFieldValue(colorRow.settingKey, text)
            }
        }
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 16

        FLSettingsGroup {
            title: "Selection"

            FLSettingRow {
                title: "Preferred language"
                description: "Subtitle language to auto-select (ISO 639 code, e.g. eng)."
                FLTextField {
                    implicitWidth: 120
                    text: (root.rev, root.vm.fieldValue("subtitles.defaultLanguage"))
                    onEditingFinished: root.vm.setFieldValue("subtitles.defaultLanguage", text)
                }
            }
            FLSettingRow {
                title: "Prefer forced tracks"
                description: "Auto-select forced subtitle tracks when present."
                FLSwitch {
                    checked: (root.rev, root.vm.fieldValue("subtitles.preferForced"))
                    onToggled: root.vm.setFieldValue("subtitles.preferForced", checked)
                }
            }
            FLSettingRow {
                title: "Override file style"
                description: "Apply the style settings below instead of the file's own styling."
                FLSwitch {
                    checked: (root.rev, root.vm.fieldValue("subtitles.overrideStyle"))
                    onToggled: root.vm.setFieldValue("subtitles.overrideStyle", checked)
                }
            }
        }

        FLSettingsGroup {
            title: "Font"

            FLSettingRow {
                title: "Font family"
                description: "Subtitle font family; empty keeps the file's font."
                FLTextField {
                    implicitWidth: 200
                    text: (root.rev, root.vm.fieldValue("subtitles.fontFamily"))
                    placeholderText: "File default"
                    onEditingFinished: root.vm.setFieldValue("subtitles.fontFamily", text)
                }
            }
            FLSettingRow {
                title: "Font scale"
                description: "Font-size multiplier (1.0 = the file's default size)."
                RowLayout {
                    spacing: 10
                    FLSlider {
                        from: 0.25; to: 4.0; stepSize: 0.05
                        value: (root.rev, root.vm.fieldValue("subtitles.fontScale"))
                        onMoved: root.vm.setFieldValue("subtitles.fontScale", value)
                    }
                    Text {
                        text: Number((root.rev, root.vm.fieldValue("subtitles.fontScale"))).toFixed(2) + "×"
                        color: FLTheme.textMuted
                        font.pixelSize: 13
                        Layout.preferredWidth: 44
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }
            FLSettingRow {
                title: "Line spacing"
                description: "Extra space between subtitle lines, pixels."
                FLTextField {
                    implicitWidth: 90
                    validator: DoubleValidator {}
                    text: Number((root.rev, root.vm.fieldValue("subtitles.lineSpacing"))).toFixed(1)
                    onEditingFinished: root.vm.setFieldValue("subtitles.lineSpacing", Number(text))
                }
            }
            FLSettingRow {
                title: "Letter spacing"
                description: "Extra space between glyphs, pixels."
                FLTextField {
                    implicitWidth: 90
                    validator: DoubleValidator {}
                    text: Number((root.rev, root.vm.fieldValue("subtitles.letterSpacing"))).toFixed(1)
                    onEditingFinished: root.vm.setFieldValue("subtitles.letterSpacing", Number(text))
                }
            }
        }

        FLSettingsGroup {
            title: "Colours & style"

            ColorRow { title: "Text colour"; settingKey: "subtitles.textColor" }
            ColorRow { title: "Outline colour"; settingKey: "subtitles.outlineColor" }
            FLSettingRow {
                title: "Outline width"
                description: "Outline thickness in pixels."
                FLTextField {
                    implicitWidth: 90
                    validator: DoubleValidator { bottom: 0.0 }
                    text: Number((root.rev, root.vm.fieldValue("subtitles.outlineWidth"))).toFixed(1)
                    onEditingFinished: root.vm.setFieldValue("subtitles.outlineWidth", Number(text))
                }
            }
            ColorRow { title: "Background colour"; settingKey: "subtitles.backColor" }
            FLSettingRow {
                title: "Background opacity"
                description: "Opacity of the shadow / box background (0.0-1.0)."
                RowLayout {
                    spacing: 10
                    FLSlider {
                        from: 0.0; to: 1.0; stepSize: 0.01
                        value: (root.rev, root.vm.fieldValue("subtitles.backOpacity"))
                        onMoved: root.vm.setFieldValue("subtitles.backOpacity", value)
                    }
                    Text {
                        text: Math.round((root.rev, root.vm.fieldValue("subtitles.backOpacity")) * 100) + "%"
                        color: FLTheme.textMuted
                        font.pixelSize: 13
                        Layout.preferredWidth: 44
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }
            FLSettingRow {
                title: "Shadow depth"
                description: "Drop-shadow offset in pixels."
                FLTextField {
                    implicitWidth: 90
                    validator: DoubleValidator { bottom: 0.0 }
                    text: Number((root.rev, root.vm.fieldValue("subtitles.shadowDepth"))).toFixed(1)
                    onEditingFinished: root.vm.setFieldValue("subtitles.shadowDepth", Number(text))
                }
            }
            FLSettingRow {
                title: "Edge style"
                description: "How subtitle text edges are rendered."
                FLComboBox {
                    model: ["None", "Outline", "Drop shadow", "Opaque box"]
                    currentIndex: (root.rev, root.vm.fieldValue("subtitles.edgeStyle"))
                    onActivated: root.vm.setFieldValue("subtitles.edgeStyle", currentIndex)
                }
            }
            FLSettingRow {
                title: "Alignment"
                description: "On-screen position (numpad layout); Keep uses the file's alignment."
                FLComboBox {
                    implicitWidth: 150
                    model: ["Keep", "Bottom left", "Bottom centre", "Bottom right",
                            "Middle left", "Centre", "Middle right",
                            "Top left", "Top centre", "Top right"]
                    currentIndex: (root.rev, root.vm.fieldValue("subtitles.alignment"))
                    onActivated: root.vm.setFieldValue("subtitles.alignment", currentIndex)
                }
            }
        }
    }
}
