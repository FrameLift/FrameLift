pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as C
import QtQuick.Layouts
import FrameLift.Controls

Item {
    id: root
    required property var viewModel

    C.ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: root.width
            spacing: 12

            FLSettingsGroup {
                title: "Runtime"
                Layout.fillWidth: true
                FLSettingRow {
                    title: "Loaded models"
                    description: "Maximum number of llama contexts retained in the shared LRU cache."
                    FLSpinBox {
                        from: 1
                        to: 8
                        value: root.viewModel.loadedModelLimit
                        onValueModified: root.viewModel.setLoadedModelLimit(value)
                    }
                }
            }

            FLSettingsGroup {
                title: "Models"
                Layout.fillWidth: true
                ColumnLayout {
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: root.viewModel.models
                        delegate: RowLayout {
                            id: modelRow
                            required property var modelData
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                C.Label {
                                    text: modelRow.modelData.name + (modelRow.modelData.recommended ? "  ★" : "")
                                    color: FLTheme.text
                                    font.bold: true
                                }
                                C.Label {
                                    text: modelRow.modelData.id + "  ·  " + modelRow.modelData.quant
                                        + (modelRow.modelData.vision ? "  ·  vision" : "")
                                    color: FLTheme.textMuted
                                    font.pixelSize: 12
                                }
                            }
                            FLActionButton {
                                visible: !modelRow.modelData.installed
                                enabled: root.viewModel.busyModel.length === 0
                                text: "Download"
                                onClicked: root.viewModel.download(modelRow.modelData.id)
                            }
                            FLActionButton {
                                visible: modelRow.modelData.installed
                                enabled: root.viewModel.busyModel.length === 0
                                text: "Test"
                                onClicked: root.viewModel.testModel(modelRow.modelData.id)
                            }
                            FLActionButton {
                                visible: modelRow.modelData.installed
                                enabled: root.viewModel.busyModel.length === 0
                                text: "Remove"
                                onClicked: root.viewModel.remove(modelRow.modelData.id)
                            }
                        }
                    }
                    C.ProgressBar {
                        visible: root.viewModel.busyModel.length > 0 && root.viewModel.progress > 0
                        value: root.viewModel.progress
                        Layout.fillWidth: true
                    }
                    FLActionButton {
                        visible: root.viewModel.busyModel.length > 0 && root.viewModel.progress > 0
                        text: "Cancel transfer"
                        onClicked: root.viewModel.cancelTransfer()
                    }
                    C.Label {
                        visible: root.viewModel.status.length > 0
                        text: root.viewModel.status
                        color: FLTheme.textMuted
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            FLSettingsGroup {
                title: "Import local GGUF"
                Layout.fillWidth: true
                ColumnLayout {
                    width: parent.width
                    FLTextField { id: importId; placeholderText: "Model id (letters, numbers, - or _)"; Layout.fillWidth: true }
                    FLTextField { id: importName; placeholderText: "Display name"; Layout.fillWidth: true }
                    FLTextField { id: importModel; placeholderText: "Absolute path to model .gguf"; Layout.fillWidth: true }
                    FLTextField { id: importProjector; placeholderText: "Absolute path to optional mmproj .gguf"; Layout.fillWidth: true }
                    FLActionButton {
                        text: "Import into models folder"
                        enabled: root.viewModel.busyModel.length === 0 && importId.text.length > 0 && importModel.text.length > 0
                        onClicked: root.viewModel.importModel(importId.text, importName.text, importModel.text, importProjector.text)
                    }
                }
            }
        }
    }
}
