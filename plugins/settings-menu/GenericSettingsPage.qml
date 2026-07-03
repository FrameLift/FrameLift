pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FrameLift.Controls

// Themed generic renderer: lists every field of the page's view model as flat rows.
// Used as a fallback for pages without a bespoke grouped layout (e.g. Keybinds, and
// plugin-provided pages that opt into generic rendering).
ScrollView {
    id: root
    required property var viewModel
    clip: true
    contentWidth: availableWidth

    ColumnLayout {
        width: root.availableWidth
        spacing: 12

        FLSettingsGroup {
            title: root.viewModel !== null ? root.viewModel.title : ""

            Repeater {
                model: root.viewModel !== null ? root.viewModel.fields : []

                delegate: FLSettingRow {
                    id: fieldDelegate
                    required property var modelData
                    title: fieldDelegate.modelData.label
                    description: fieldDelegate.modelData.description
                    keyName: fieldDelegate.modelData.key

                    Loader {
                        sourceComponent: fieldDelegate.modelData.type === 0 ? boolEditor
                                       : fieldDelegate.modelData.type === 1 ? intEditor
                                                                            : textEditor
                    }

                    Component {
                        id: boolEditor
                        FLSwitch {
                            checked: fieldDelegate.modelData.value
                            onToggled: root.viewModel.setFieldValue(fieldDelegate.modelData.key, checked)
                        }
                    }
                    Component {
                        id: intEditor
                        FLSpinBox {
                            value: fieldDelegate.modelData.value
                            from: -100000
                            to: 100000
                            onValueModified: root.viewModel.setFieldValue(fieldDelegate.modelData.key, value)
                        }
                    }
                    Component {
                        id: textEditor
                        FLTextField {
                            implicitWidth: 220
                            text: fieldDelegate.modelData.type === 2
                                  ? Number(fieldDelegate.modelData.value).toString()
                                  : fieldDelegate.modelData.value
                            validator: fieldDelegate.modelData.type === 2 ? doubleValidator : null
                            onEditingFinished: root.viewModel.setFieldValue(
                                fieldDelegate.modelData.key,
                                fieldDelegate.modelData.type === 2 ? Number(text) : text)
                        }
                    }
                    DoubleValidator { id: doubleValidator }
                }
            }
        }
    }
}
