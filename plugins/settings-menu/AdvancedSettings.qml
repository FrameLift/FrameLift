pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FrameLift.Controls

// Raw editor over the on-disk settings.ini. Save (via the window footer) writes the
// text back and asks the host to reload; Reset re-reads the file. Advanced use — no
// syntax validation; invalid host keys revert to defaults on reload.
Item {
    id: root
    required property var viewModel
    property var vm: viewModel

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Text {
            Layout.fillWidth: true
            text: "Editing raw settings — invalid entries may be reset on reload."
            color: FLTheme.textMuted
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }

        Text {
            Layout.fillWidth: true
            text: root.vm !== null ? root.vm.filePath : ""
            color: FLTheme.textMuted
            font.pixelSize: 11
            font.family: "monospace"
            elide: Text.ElideMiddle
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            TextArea {
                id: editor
                wrapMode: TextEdit.NoWrap
                selectByMouse: true
                color: FLTheme.text
                selectionColor: FLTheme.accent
                selectedTextColor: FLTheme.text
                font.family: "monospace"
                font.pixelSize: 13
                leftPadding: 10
                topPadding: 10
                rightPadding: 10
                bottomPadding: 10

                background: Rectangle {
                    radius: 8
                    color: FLTheme.inputBg
                    border.color: editor.activeFocus ? FLTheme.accent : FLTheme.border
                    border.width: 1
                }

                onTextChanged: {
                    if (root.vm !== null)
                        root.vm.setText(text)
                }
            }
        }
    }

    // Re-seed the editor whenever the draft changes on the model side (initial load,
    // Reset, or a reload after Save) — but only when it actually diverges, so typing
    // doesn't fight the binding.
    Connections {
        target: root.vm
        function onChanged() {
            if (root.vm !== null && editor.text !== root.vm.text)
                editor.text = root.vm.text
        }
    }

    Component.onCompleted: {
        if (root.vm !== null)
            editor.text = root.vm.text
    }
}
