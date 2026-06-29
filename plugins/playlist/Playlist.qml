pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FrameLift.Controls

Item {
    id: root
    required property var viewModel
    property var vm: viewModel
    anchors.fill: parent

    FLDrawer {
        id: drawer
        open: root.vm !== null && root.vm.open
        drawerWidth: 340
        onXChanged: if (root.vm !== null) root.vm.publishVisibleWidth(Math.max(0, width + x))

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    text: "Playlist"
                    color: FLTheme.text
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
                Text {
                    text: root.vm !== null && root.vm.currentIndex >= 0
                          ? (root.vm.currentIndex + 1) + " / " + root.vm.entries.length
                          : root.vm !== null ? root.vm.entries.length : 0
                    color: FLTheme.textMuted
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }
                FLActionButton { text: "Reload"; implicitHeight: 28; padding: 8; font.pixelSize: 12; onClicked: root.vm.Reload() }
                FLActionButton {
                    text: root.vm !== null && root.vm.shuffleEnabled ? "Shuffle on" : "Shuffle"
                    implicitHeight: 28; padding: 8; font.pixelSize: 12
                    onClicked: root.vm.ToggleShuffle()
                }
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: root.vm !== null ? root.vm.entries : []
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    required property int index
                    property bool hasSubfolder: row.modelData.subfolder.length > 0
                    width: ListView.view.width
                    height: row.hasSubfolder ? 46 : 30
                    radius: 6
                    color: row.modelData.current ? "#408B5CF6"
                          : mouse.containsMouse ? "#18FFFFFF" : "transparent"
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 8
                        spacing: 1
                        Text {
                            text: row.modelData.label
                            color: FLTheme.text
                            elide: Text.ElideMiddle
                            width: parent.width
                            font.pixelSize: 12
                        }
                        Text {
                            visible: row.hasSubfolder
                            text: row.modelData.subfolder
                            color: FLTheme.textMuted
                            elide: Text.ElideMiddle
                            width: parent.width
                            font.pixelSize: 11
                        }
                    }
                    MouseArea {
                        id: mouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.vm.activateIndex(row.index)
                    }
                }
            }
        }
    }
}
