pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as C
import QtQuick.Layouts
import FrameLift.Controls

Item {
    id: root
    required property var viewModel

    // Draft state for the rule editor (a rule is keyed by its folder). The questions
    // are held in `questionModel` (one row per { question, tag, threshold }).
    property string draftFolder: ""
    property string draftModel: ""
    property real draftThreshold: 0.6
    property int draftBudget: 31
    property bool draftWatch: false

    // Backing model for the questions table. Threshold is stored as a display string;
    // empty means "use the rule default".
    ListModel {
        id: questionModel
    }

    function loadRule(r) {
        root.draftFolder = r.folder;
        root.draftModel = r.modelId;
        root.draftThreshold = r.threshold;
        root.draftBudget = r.frameBudget;
        root.draftWatch = r.watch;
        questionModel.clear();
        for (let i = 0; i < r.entries.length; ++i) {
            const e = r.entries[i];
            questionModel.append({
                question: e.question,
                tag: e.tag,
                threshold: e.threshold >= 0 ? Number(e.threshold).toFixed(2) : ""
            });
        }
    }

    // Collect the questions table into the { question, tag, threshold } list saveRule wants.
    function collectQuestions() {
        const out = [];
        for (let i = 0; i < questionModel.count; ++i) {
            const row = questionModel.get(i);
            out.push({
                question: row.question,
                tag: row.tag,
                threshold: row.threshold.length > 0 ? Number(row.threshold) : -1
            });
        }
        return out;
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

                    // Live progress strip while a tagging run is active (same state the
                    // video overlay pill shows). Hidden and inert when idle.
                    Rectangle {
                        Layout.fillWidth: true
                        visible: root.viewModel.tagging
                        implicitHeight: progressRow.implicitHeight + 16
                        radius: 8
                        color: FLTheme.inputBg
                        border.color: FLTheme.accentSoft
                        border.width: 1

                        RowLayout {
                            id: progressRow
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 10

                            C.BusyIndicator {
                                implicitWidth: 18
                                implicitHeight: 18
                                running: root.viewModel.tagging
                            }
                            C.Label {
                                text: "Tagging " + (root.viewModel.taggingDone + 1) + " / " + root.viewModel.taggingTotal
                                color: FLTheme.text
                                font.pixelSize: 13
                                font.bold: true
                            }
                            C.Label {
                                text: root.viewModel.taggingFile
                                color: FLTheme.textMuted
                                font.pixelSize: 12
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            FLActionButton {
                                text: "Cancel"
                                onClicked: root.viewModel.cancelTagging()
                            }
                        }
                    }

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
                                enabled: !root.viewModel.tagging
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
                            id: modelCombo
                            // Only installed models are selectable — tagging needs the files present.
                            property var installedIds: root.viewModel.models.filter(m => m.installed).map(m => m.id)
                            model: installedIds
                            implicitWidth: 240
                            enabled: installedIds.length > 0

                            // Keep the combo and draftModel in sync, and — crucially —
                            // default draftModel to a real installed id when it's empty or
                            // stale, so a rule saved without opening the dropdown still has
                            // a model (an empty model id can't resolve its GGUF files).
                            function syncModel() {
                                if (installedIds.length === 0)
                                    return
                                let idx = installedIds.indexOf(root.draftModel)
                                if (idx < 0)
                                    idx = 0
                                currentIndex = idx
                                if (root.draftModel !== installedIds[idx])
                                    root.draftModel = installedIds[idx]
                            }

                            onModelChanged: modelCombo.syncModel()
                            Component.onCompleted: modelCombo.syncModel()
                            onActivated: root.draftModel = currentText

                            // Re-sync when a rule is loaded for editing (draftModel changes).
                            Connections {
                                target: root
                                function onDraftModelChanged() { modelCombo.syncModel() }
                            }
                        }
                    }
                    // ── Questions ───────────────────────────────────────────
                    // A table of yes/no questions and the tag each produces. The
                    // optional per-row threshold overrides the rule default.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        C.Label {
                            text: "Questions"
                            color: FLTheme.text
                            font.pixelSize: 13
                        }
                        C.Label {
                            text: "Each yes/no question produces a tag. Threshold overrides the rule default (blank = default)."
                            color: FLTheme.textMuted
                            font.pixelSize: 11
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }

                        // Column headers, widths matched by the delegate rows below.
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            spacing: 8
                            C.Label {
                                text: "Question"
                                color: FLTheme.textMuted
                                font.pixelSize: 11
                                Layout.fillWidth: true
                            }
                            C.Label {
                                text: "Tag"
                                color: FLTheme.textMuted
                                font.pixelSize: 11
                                Layout.preferredWidth: 130
                            }
                            C.Label {
                                text: "Thr."
                                color: FLTheme.textMuted
                                font.pixelSize: 11
                                Layout.preferredWidth: 70
                            }
                            // Spacer aligning with the per-row remove button.
                            Item { Layout.preferredWidth: 28 }
                        }

                        Repeater {
                            model: questionModel
                            delegate: RowLayout {
                                id: qrow
                                required property int index
                                required property string question
                                required property string tag
                                required property string threshold
                                Layout.fillWidth: true
                                spacing: 8

                                FLTextField {
                                    text: qrow.question
                                    Layout.fillWidth: true
                                    placeholderText: "Does this scene contain a beach?"
                                    onEditingFinished: questionModel.setProperty(qrow.index, "question", text)
                                }
                                FLTextField {
                                    text: qrow.tag
                                    Layout.preferredWidth: 130
                                    placeholderText: "beach"
                                    onEditingFinished: questionModel.setProperty(qrow.index, "tag", text)
                                }
                                FLTextField {
                                    text: qrow.threshold
                                    Layout.preferredWidth: 70
                                    placeholderText: "—"
                                    validator: DoubleValidator { bottom: 0.0; top: 1.0 }
                                    onEditingFinished: questionModel.setProperty(qrow.index, "threshold", text)
                                }
                                FLActionButton {
                                    text: "✕"
                                    Layout.preferredWidth: 28
                                    onClicked: questionModel.remove(qrow.index)
                                }
                            }
                        }

                        FLActionButton {
                            text: "Add question"
                            Layout.topMargin: 4
                            onClicked: questionModel.append({ question: "", tag: "", threshold: "" })
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
                                root.collectQuestions(), root.draftThreshold, root.draftBudget, root.draftWatch)
                        }
                        FLActionButton {
                            text: "Clear"
                            onClicked: { root.draftFolder = ""; questionModel.clear(); root.draftWatch = false; }
                        }
                    }
                }
            }
        }
    }
}
