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
            title: "Layout"

            FLSettingRow {
                title: "Panel width"
                description: "Width in pixels of the side panels (playlist, etc.)."
                FLTextField {
                    implicitWidth: 100
                    validator: DoubleValidator { bottom: 120.0; top: 1200.0 }
                    text: Number((root.rev, root.vm.fieldValue("ui.panelWidth"))).toFixed(0)
                    onEditingFinished: root.vm.setFieldValue("ui.panelWidth", Number(text))
                }
            }
            FLSettingRow {
                title: "Drawer slide speed"
                description: "Side drawer slide-in/out animation speed."
                FLTextField {
                    implicitWidth: 100
                    validator: DoubleValidator { bottom: 1.0; top: 100.0 }
                    text: Number((root.rev, root.vm.fieldValue("ui.slideSpeed"))).toFixed(0)
                    onEditingFinished: root.vm.setFieldValue("ui.slideSpeed", Number(text))
                }
            }
        }
    }
}
