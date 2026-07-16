pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import FrameLift.Controls

Item {
    id: root
    required property var viewModel
    property var vm: viewModel
    anchors.fill: parent

    function countText() {
        if (root.vm === null)
            return "0"
        return drawer.contentItem !== null && drawer.contentItem.searchOpen
                ? root.vm.entries.length + " / " + root.vm.totalCount
                : root.vm.totalCount.toString()
    }

    FLDrawer {
        id: drawer
        open: root.vm !== null && root.vm.open
        rightSide: true
        drawerWidth: 380
        drawerWidthRatio: 0.32
        minimumDrawerWidth: 320
        maximumDrawerWidth: 440
        onOpenChanged: if (!open && contentItem !== null) contentItem.setSearchOpen(false)
        onXChanged: if (root.vm !== null) root.vm.publishVisibleWidth(Math.max(0, root.width - x))

        ColumnLayout {
            id: panel
            anchors.fill: parent
            anchors.margins: 6
            spacing: 4

            property bool searchOpen: false

            function setSearchOpen(value) {
                if (searchOpen === value) {
                    if (!value && root.vm !== null && root.vm.search.length > 0)
                        root.vm.search = ""
                    return
                }
                searchOpen = value
                if (root.vm === null)
                    return
                if (value) {
                    Qt.callLater(searchField.forceActiveFocus)
                } else {
                    root.vm.search = ""
                    if (drawer.open && view.visible) {
                        Qt.callLater(function() {
                            view.currentIndex = 0
                            view.forceActiveFocus()
                        })
                    }
                }
            }

            FLPanelHeader {
                Layout.fillWidth: true
                title: "History"
                countText: root.countText()
                closeToolTip: "Close history"
                onCloseRequested: if (root.vm !== null) root.vm.togglePanel()

                FLPanelAction {
                    text: "Search"
                    toolTipText: panel.searchOpen ? "Hide history search" : "Search history"
                    checkable: true
                    checked: panel.searchOpen
                    enabled: panel.searchOpen || (root.vm !== null && root.vm.totalCount > 0)
                    onClicked: panel.setSearchOpen(!panel.searchOpen)
                }
                FLPanelAction {
                    text: "Clear"
                    toolTipText: "Clear playback history"
                    destructive: true
                    enabled: root.vm !== null && root.vm.totalCount > 0
                    onClicked: clearConfirm.open = true
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: panel.searchOpen

                FLTextField {
                    id: searchField
                    Layout.fillWidth: true
                    implicitHeight: 28
                    font.pixelSize: 12
                    placeholderText: "Search recent files"
                    text: root.vm !== null ? root.vm.search : ""
                    selectByMouse: true
                    onTextEdited: if (root.vm !== null) root.vm.search = text

                    Keys.onDownPressed: function(event) {
                        if (root.vm !== null && root.vm.entries.length > 0) {
                            view.currentIndex = Math.max(0, view.currentIndex)
                            view.forceActiveFocus()
                            event.accepted = true
                        }
                    }
                }

                FLPanelAction {
                    visible: root.vm !== null && root.vm.search.length > 0
                    text: "×"
                    font.pixelSize: 14
                    toolTipText: "Clear search"
                    onClicked: {
                        root.vm.search = ""
                        searchField.forceActiveFocus()
                    }
                }
            }

            FLNavigableListView {
                id: view
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.vm !== null && root.vm.entries.length > 0
                model: root.vm !== null ? root.vm.entries : []
                active: root.vm !== null && root.vm.open && visible && !panel.searchOpen
                onActiveChanged: if (active) {
                    currentIndex = 0
                    forceActiveFocus()
                }
                Keys.onReturnPressed: if (currentIndex >= 0 && root.vm !== null) root.vm.activateIndex(currentIndex)

                delegate: FLListRow {
                    id: row
                    required property var modelData
                    required property int index
                    height: 54
                    selected: row.ListView.isCurrentItem
                    onSelectRequested: {
                        view.currentIndex = row.index
                        view.forceActiveFocus()
                    }
                    onActivateRequested: root.vm.activateIndex(row.index)

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 8
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 0

                        Text {
                            text: row.modelData.label
                            color: FLTheme.text
                            elide: Text.ElideMiddle
                            font.pixelSize: 12
                            Layout.fillWidth: true
                        }
                        Text {
                            visible: text.length > 0
                            text: row.modelData.directory
                            color: FLTheme.textMuted
                            elide: Text.ElideMiddle
                            font.pixelSize: 10
                            Layout.fillWidth: true
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                text: "Played " + row.modelData.playedAt
                                color: FLTheme.textMuted
                                font.pixelSize: 9
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Rectangle {
                                radius: 5
                                color: FLTheme.inputBg
                                border.width: 1
                                border.color: FLTheme.border
                                implicitWidth: resumeLabel.implicitWidth + 8
                                implicitHeight: 17

                                Text {
                                    id: resumeLabel
                                    anchors.centerIn: parent
                                    text: "Resume " + row.modelData.resumeText
                                    color: FLTheme.text
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                }
                            }
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.vm === null || root.vm.entries.length === 0

                FLListState {
                    anchors.centerIn: parent
                    title: root.vm !== null && root.vm.totalCount > 0 ? "No matching files" : "No history yet"
                    detail: root.vm !== null && root.vm.totalCount > 0
                            ? "Try a different filename or folder"
                            : "Recently played files will appear here"
                    actionText: root.vm !== null && root.vm.totalCount > 0 ? "Clear search" : ""
                    onActionRequested: {
                        root.vm.search = ""
                        searchField.forceActiveFocus()
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
        onAccepted: if (root.vm !== null) {
            root.vm.Clear()
            if (drawer.contentItem !== null)
                drawer.contentItem.setSearchOpen(false)
        }
    }
}
