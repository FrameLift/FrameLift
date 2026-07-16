pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as C
import FrameLift.Controls

// Small progress pill shown while a tagging run is active (the module itself is the
// view model). Hidden and inert otherwise, so it never repaints during playback.
Item {
    id: root
    required property var viewModel
    anchors.fill: parent
    visible: root.viewModel.running
    property bool contentLoaded: false
    onVisibleChanged: if (visible) contentLoaded = true

    Loader {
        anchors.fill: parent
        active: root.contentLoaded
        sourceComponent: Component {
            Rectangle {
        id: pill
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 16
        radius: 10
        color: "#CC1C1C22"
        border.color: FLTheme.accentSoft
        border.width: 1
        implicitWidth: content.implicitWidth + 24
        implicitHeight: content.implicitHeight + 16

        Row {
            id: content
            anchors.centerIn: parent
            spacing: 10

            C.BusyIndicator {
                width: 18
                height: 18
                running: root.viewModel.running
            }
            Column {
                spacing: 1
                C.Label {
                    text: "Tagging " + (root.viewModel.filesDone + 1) + " / " + root.viewModel.filesTotal
                    color: FLTheme.text
                    font.pixelSize: 12
                    font.bold: true
                }
                C.Label {
                    text: root.viewModel.currentFile
                    color: FLTheme.textMuted
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                    width: Math.min(implicitWidth, 260)
                }
            }
        }
            }
        }
    }
}
