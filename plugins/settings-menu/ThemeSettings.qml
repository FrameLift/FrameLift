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
            title: "Appearance"

            FLSettingRow {
                title: "Colour preset"
                description: "Base UI colour scheme."
                keyName: "theme.preset"
                FLComboBox {
                    model: ["dark", "light", "classic"]
                    currentIndex: Math.max(0, model.indexOf((root.rev, root.vm.fieldValue("theme.preset"))))
                    onActivated: root.vm.setFieldValue("theme.preset", currentText)
                }
            }
            FLSettingRow {
                title: "Accent colour"
                description: "Accent colour as #RRGGBB."
                keyName: "theme.accentColor"
                RowLayout {
                    spacing: 8
                    Rectangle {
                        width: 24; height: 24; radius: 6
                        border.color: FLTheme.border; border.width: 1
                        color: {
                            const v = (root.rev, root.vm.fieldValue("theme.accentColor"))
                            return /^#([0-9a-fA-F]{6})$/.test(v) ? v : "transparent"
                        }
                    }
                    FLTextField {
                        implicitWidth: 110
                        text: (root.rev, root.vm.fieldValue("theme.accentColor"))
                        onEditingFinished: root.vm.setFieldValue("theme.accentColor", text)
                    }
                }
            }
        }
    }
}
