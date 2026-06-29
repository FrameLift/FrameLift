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
        rightSide: true
        drawerWidth: 380
        onXChanged: if (root.vm !== null) root.vm.publishVisibleWidth(Math.max(0, root.width - x))

        ColumnLayout {
            id: panel
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            property bool searchOpen: false

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    text: "History"
                    color: FLTheme.text
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                FLActionButton {
                    text: "Search"
                    implicitHeight: 28; padding: 8; font.pixelSize: 12
                    onClicked: {
                        panel.searchOpen = !panel.searchOpen
                        if (!panel.searchOpen && root.vm !== null) root.vm.search = ""
                    }
                }
                FLActionButton { text: "Clear"; implicitHeight: 28; padding: 8; font.pixelSize: 12; onClicked: clearConfirm.open = true }
            }
            TextField {
                id: searchField
                Layout.fillWidth: true
                visible: panel.searchOpen
                placeholderText: "Search recent files"
                placeholderTextColor: FLTheme.textMuted
                color: FLTheme.text
                font.pixelSize: 13
                leftPadding: 10
                rightPadding: 10
                topPadding: 6
                bottomPadding: 6
                selectByMouse: true
                text: root.vm !== null ? root.vm.search : ""
                onTextEdited: if (root.vm !== null) root.vm.search = text
                onVisibleChanged: if (visible) forceActiveFocus()
                background: Rectangle {
                    radius: 6
                    color: "#14000000"
                    border.width: 1
                    border.color: searchField.activeFocus ? FLTheme.accent : FLTheme.border
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
                    width: ListView.view.width
                    height: 58
                    radius: 6
                    color: row.modelData.selected ? "#408B5CF6" : mouse.containsMouse ? "#18FFFFFF" : "transparent"
                    Column {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 1
                        Text { text: row.modelData.label; color: FLTheme.text; elide: Text.ElideMiddle; width: parent.width; font.pixelSize: 12 }
                        Text { text: row.modelData.directory; color: FLTheme.textMuted; elide: Text.ElideMiddle; width: parent.width; font.pixelSize: 11 }
                        Text { text: row.modelData.meta; color: FLTheme.textMuted; font.pixelSize: 11 }
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

    FLConfirmDialog {
        id: clearConfirm
        title: "Clear history"
        message: "Remove all entries from your recent files history? This cannot be undone."
        confirmText: "Clear"
        destructive: true
        onAccepted: if (root.vm !== null) root.vm.Clear()
    }
}
