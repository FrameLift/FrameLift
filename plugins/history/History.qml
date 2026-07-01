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
            anchors.margins: 6
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
            FLNavigableListView {
                id: view
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: root.vm !== null ? root.vm.entries : []
                active: root.vm !== null && root.vm.open && !panel.searchOpen
                onActiveChanged: if (active) {
                    currentIndex = 0
                    forceActiveFocus()
                }
                Keys.onReturnPressed: if (currentIndex >= 0 && root.vm !== null) root.vm.activateIndex(currentIndex)
                delegate: FLListRow {
                    id: row
                    required property var modelData
                    required property int index
                    height: 58
                    selected: row.ListView.isCurrentItem
                    onSelectRequested: { view.currentIndex = row.index; view.forceActiveFocus() }
                    onActivateRequested: root.vm.activateIndex(row.index)
                    Column {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 1
                        Text { text: row.modelData.label; color: FLTheme.text; elide: Text.ElideMiddle; width: parent.width; font.pixelSize: 12 }
                        Text { text: row.modelData.directory; color: FLTheme.textMuted; elide: Text.ElideMiddle; width: parent.width; font.pixelSize: 11 }
                        Text { text: row.modelData.meta; color: FLTheme.textMuted; font.pixelSize: 11 }
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
