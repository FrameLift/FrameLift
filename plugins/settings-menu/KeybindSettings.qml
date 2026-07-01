pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FrameLift.Controls

Item {
    id: root
    required property var viewModel
    property var vm: viewModel

    // Hidden focusable item that receives the next keypress while capturing. Kept a
    // sibling of the ScrollView so the ScrollView has a single content child (else it
    // can't size its flickable content and won't scroll).
    Item {
        id: keyCatcher
        width: 0
        height: 0

        Keys.onPressed: function(event) {
            if (root.vm === null || !root.vm.capturing) {
                event.accepted = false
                return
            }
            event.accepted = true
            if (event.isAutoRepeat)
                return
            // Ignore standalone modifier presses — wait for a real key.
            if (event.key === Qt.Key_Control || event.key === Qt.Key_Shift
                    || event.key === Qt.Key_Alt || event.key === Qt.Key_Meta)
                return
            if (event.key === Qt.Key_Escape) {
                root.vm.cancelCapture()
                return
            }
            root.vm.captureKey(event.key, event.modifiers)
        }
    }

    function startCapture(action, slot) {
        if (root.vm === null)
            return
        root.vm.beginCapture(action, slot)
        keyCatcher.forceActiveFocus()
    }

    // One action row: label on the left, two key-cap slots on the right.
    component KeybindRow: FLSettingRow {
        id: bindRow
        required property var entry
        title: bindRow.entry.label

        Row {
            spacing: 8
            Repeater {
                model: 2
                delegate: FLKeyCapButton {
                    id: cap
                    required property int index
                    text: cap.index === 0 ? bindRow.entry.primary : bindRow.entry.alternate
                    capturing: root.vm !== null && root.vm.capturing
                              && root.vm.capturingAction === bindRow.entry.action
                              && root.vm.capturingSlot === cap.index
                    onClicked: root.startCapture(bindRow.entry.action, cap.index)
                    onCleared: if (root.vm !== null) root.vm.clearSlot(bindRow.entry.action, cap.index)
                }
            }
        }
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 16

            Rectangle {
                Layout.fillWidth: true
                visible: root.vm !== null && root.vm.conflict.length > 0
                implicitHeight: conflictText.implicitHeight + 16
                radius: FLTheme.radius
                color: "#33EF4444"
                border.color: FLTheme.danger
                border.width: 1

                Text {
                    id: conflictText
                    anchors.fill: parent
                    anchors.margins: 8
                    text: root.vm !== null ? root.vm.conflict : ""
                    color: FLTheme.text
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                    verticalAlignment: Text.AlignVCenter
                }
            }

            FLSettingsGroup {
                title: "Application"
                Repeater {
                    model: root.vm !== null ? root.vm.coreEntries : []
                    delegate: KeybindRow {
                        required property var modelData
                        entry: modelData
                    }
                }
            }

            Repeater {
                model: root.vm !== null ? root.vm.pluginGroups : []
                delegate: FLSettingsGroup {
                    id: pluginGroup
                    required property var modelData
                    title: pluginGroup.modelData.title

                    Repeater {
                        model: pluginGroup.modelData.entries
                        delegate: KeybindRow {
                            required property var modelData
                            entry: modelData
                        }
                    }
                }
            }
        }
    }
}
