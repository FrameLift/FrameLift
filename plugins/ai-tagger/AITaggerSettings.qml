pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as C
import QtQuick.Layouts
import FrameLift.Controls

Item {
    id: root
    required property var viewModel

    // Draft state for the rule editor (a rule is keyed by its folder).
    property string draftFolder: ""
    property string draftModel: ""
    property string draftQuestions: ""
    property real draftThreshold: 0.6
    property int draftBudget: 31
    property bool draftWatch: false

    function loadRule(r) {
        root.draftFolder = r.folder;
        root.draftModel = r.modelId;
        root.draftQuestions = r.questions;
        root.draftThreshold = r.threshold;
        root.draftBudget = r.frameBudget;
        root.draftWatch = r.watch;
    }

    C.ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: root.width
            spacing: 12

            // ── Models ──────────────────────────────────────────────────────
            FLSettingsGroup {
                title: "Models"
                Layout.fillWidth: true

                ColumnLayout {
                    width: parent.width
                    spacing: 6

                    Repeater {
                        model: root.viewModel.models
                        delegate: RowLayout {
                            id: mrow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                C.Label {
                                    text: mrow.modelData.name + (mrow.modelData.recommended ? "  ★" : "")
                                    color: FLTheme.text
                                    font.bold: true
                                }
                                C.Label {
                                    text: mrow.modelData.installed ? "Installed"
                                        : (mrow.modelData.downloading
                                           ? "Downloading… " + Math.round(mrow.modelData.progress * 100) + "%"
                                           : "Not installed")
                                    color: FLTheme.textMuted
                                    font.pixelSize: 12
                                }
                            }

                            FLActionButton {
                                visible: !mrow.modelData.installed && !mrow.modelData.downloading
                                text: "Download"
                                onClicked: root.viewModel.download(mrow.modelData.id)
                            }
                            FLActionButton {
                                visible: mrow.modelData.downloading
                                text: "Cancel"
                                onClicked: root.viewModel.cancelDownload()
                            }
                            FLActionButton {
                                visible: mrow.modelData.installed
                                enabled: root.viewModel.testingId.length === 0
                                text: "Test"
                                onClicked: root.viewModel.testModel(mrow.modelData.id)
                            }
                        }
                    }

                    // Test / benchmark result (one model tested at a time).
                    C.Label {
                        visible: root.viewModel.testingId.length > 0 || root.viewModel.testStatus.length > 0
                        text: root.viewModel.testStatus
                            + (root.viewModel.testMsPerFrame > 0
                               ? "  ·  " + Math.round(root.viewModel.testMsPerFrame) + " ms/frame"
                               : "")
                        color: FLTheme.textMuted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    C.Label {
                        visible: root.viewModel.lastError.length > 0
                        text: root.viewModel.lastError
                        color: "#e06c75"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            // ── Existing rules ──────────────────────────────────────────────
            FLSettingsGroup {
                title: "Folder rules"
                Layout.fillWidth: true

                ColumnLayout {
                    width: parent.width
                    spacing: 6

                    Repeater {
                        model: root.viewModel.rules
                        delegate: RowLayout {
                            id: rrow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                C.Label {
                                    text: rrow.modelData.folder
                                    color: FLTheme.text
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                C.Label {
                                    text: rrow.modelData.modelId + "  ·  " + rrow.modelData.watch ? "watched" : ""
                                    color: FLTheme.textMuted
                                    font.pixelSize: 12
                                }
                            }
                            FLActionButton {
                                text: "Edit"
                                onClicked: root.loadRule(rrow.modelData)
                            }
                            FLActionButton {
                                text: "Tag now"
                                onClicked: root.viewModel.tagFolder(rrow.modelData.folder)
                            }
                            FLActionButton {
                                text: "Delete"
                                onClicked: root.viewModel.deleteRule(rrow.modelData.id)
                            }
                        }
                    }
                    C.Label {
                        visible: root.viewModel.rules.length === 0
                        text: "No rules yet. Add one below."
                        color: FLTheme.textMuted
                    }
                }
            }

            // ── Rule editor ─────────────────────────────────────────────────
            FLSettingsGroup {
                title: "Add / edit rule"
                Layout.fillWidth: true

                ColumnLayout {
                    width: parent.width
                    spacing: 8

                    FLSettingRow {
                        title: "Folder"
                        description: "Absolute path this rule applies to (and its subfolders)."
                        FLTextField {
                            text: root.draftFolder
                            implicitWidth: 320
                            onEditingFinished: root.draftFolder = text
                        }
                    }
                    FLSettingRow {
                        title: "Model"
                        description: "Which installed model to run for this folder. Download a model above first."
                        FLComboBox {
                            // Only installed models are selectable — tagging needs the files present.
                            property var installedIds: root.viewModel.models.filter(m => m.installed).map(m => m.id)
                            model: installedIds
                            implicitWidth: 240
                            enabled: installedIds.length > 0
                            onModelChanged: currentIndex = Math.max(0, installedIds.indexOf(root.draftModel))
                            Component.onCompleted: currentIndex = Math.max(0, installedIds.indexOf(root.draftModel))
                            onActivated: root.draftModel = currentText
                        }
                    }
                    FLSettingRow {
                        title: "Questions"
                        description: "One per line as “question => tag”, e.g. Does this scene contain a beach? => beach"
                        C.TextArea {
                            text: root.draftQuestions
                            implicitWidth: 380
                            implicitHeight: 100
                            wrapMode: TextEdit.Wrap
                            color: FLTheme.text
                            onEditingFinished: root.draftQuestions = text
                        }
                    }
                    FLSettingRow {
                        title: "Threshold"
                        description: "Minimum confidence for a tag to be marked present (0–1)."
                        FLTextField {
                            text: Number(root.draftThreshold).toFixed(2)
                            validator: DoubleValidator { bottom: 0.0; top: 1.0 }
                            implicitWidth: 120
                            onEditingFinished: root.draftThreshold = Number(text)
                        }
                    }
                    FLSettingRow {
                        title: "Frame budget"
                        description: "Maximum frames sampled per video."
                        FLSpinBox {
                            from: 1; to: 255
                            value: root.draftBudget
                            implicitWidth: 140
                            onValueModified: root.draftBudget = value
                        }
                    }
                    FLSettingRow {
                        title: "Watch folder"
                        description: "Auto-tag new files appearing in this folder."
                        FLSwitch {
                            checked: root.draftWatch
                            onToggled: root.draftWatch = checked
                        }
                    }

                    RowLayout {
                        spacing: 8
                        FLActionButton {
                            text: "Save rule"
                            onClicked: root.viewModel.saveRule(root.draftFolder, root.draftModel,
                                root.draftQuestions, root.draftThreshold, root.draftBudget, root.draftWatch)
                        }
                        FLActionButton {
                            text: "Clear"
                            onClicked: { root.draftFolder = ""; root.draftQuestions = ""; root.draftWatch = false; }
                        }
                    }
                }
            }
        }
    }
}
