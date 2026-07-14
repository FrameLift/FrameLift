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

    function countText() {
        if (root.vm === null)
            return "0"
        if (panel.searchOpen)
            return root.vm.entries.length + " / " + root.vm.totalCount
        if (root.vm.currentIndex >= 0)
            return (root.vm.currentIndex + 1) + " / " + root.vm.totalCount
        return root.vm.totalCount.toString()
    }

    FLDrawer {
        id: drawer
        open: root.vm !== null && root.vm.open
        drawerWidth: 340
        drawerWidthRatio: 0.32
        minimumDrawerWidth: 320
        maximumDrawerWidth: 440
        onOpenChanged: if (!open) panel.setSearchOpen(false)
        onXChanged: if (root.vm !== null) root.vm.publishVisibleWidth(Math.max(0, width + x))

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
                            view.currentIndex = root.vm.currentIndex
                            view.keepCurrentInView()
                            view.forceActiveFocus()
                        })
                    }
                }
            }

            FLPanelHeader {
                Layout.fillWidth: true
                title: "Playlist"
                countText: root.countText()
                closeToolTip: "Close playlist"
                onCloseRequested: if (root.vm !== null) root.vm.togglePanel()

                FLPanelAction {
                    text: "A–Z"
                    toolTipText: checked ? "Alphabetical sorting enabled" : "Sort alphabetically"
                    checkable: true
                    checked: root.vm !== null && root.vm.sortByName
                    enabled: root.vm !== null && root.vm.totalCount > 0
                    onClicked: root.vm.toggleSortByName()
                }

                FLPanelAction {
                    text: "Shuffle"
                    toolTipText: checked ? "Shuffle enabled" : "Enable shuffle"
                    checkable: true
                    checked: root.vm !== null && root.vm.shuffleEnabled
                    enabled: root.vm !== null && root.vm.totalCount > 0
                    onClicked: root.vm.ToggleShuffle()
                }

                FLPanelAction {
                    text: "Search"
                    toolTipText: panel.searchOpen ? "Hide playlist search" : "Search playlist"
                    checkable: true
                    checked: panel.searchOpen
                    enabled: panel.searchOpen || (root.vm !== null && root.vm.totalCount > 0)
                    onClicked: panel.setSearchOpen(!panel.searchOpen)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: root.vm !== null && root.vm.manualReloadRequired

                FLPanelAction {
                    text: root.vm !== null && root.vm.scanning ? "Scanning…" : "Reload"
                    toolTipText: "Directory watching is unavailable; rescan the current folder"
                    enabled: root.vm !== null && !root.vm.scanning
                    onClicked: root.vm.Reload()
                }
                Item { Layout.fillWidth: true }
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
                    placeholderText: "Search playlist"
                    text: root.vm !== null ? root.vm.search : ""
                    selectByMouse: true
                    onTextEdited: if (root.vm !== null) root.vm.search = text

                    Keys.onDownPressed: function(event) {
                        if (root.vm !== null && root.vm.entries.length > 0) {
                            view.currentIndex = Math.max(0, root.vm.currentIndex)
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

            Rectangle {
                Layout.fillWidth: true
                visible: root.vm !== null && root.vm.scanning && root.vm.totalCount > 0
                implicitHeight: scanStatus.implicitHeight + 8
                radius: 6
                color: FLTheme.accentFaint
                border.width: 1
                border.color: FLTheme.accentSoft

                RowLayout {
                    id: scanStatus
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    spacing: 5

                    BusyIndicator {
                        running: parent.parent.visible
                        implicitWidth: 14
                        implicitHeight: 14
                    }
                    Text {
                        text: "Scanning folder…"
                        color: FLTheme.textMuted
                        font.pixelSize: 10
                        Layout.fillWidth: true
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
                onActiveChanged: if (active && root.vm !== null) {
                    currentIndex = root.vm.currentIndex
                    keepCurrentInView()
                    forceActiveFocus()
                }
                Keys.onReturnPressed: if (currentIndex >= 0 && root.vm !== null) root.vm.activateIndex(currentIndex)

                Connections {
                    target: root.vm
                    function onPlaylistChanged() {
                        view.currentIndex = root.vm.currentIndex
                        view.keepCurrentInView()
                    }
                }

                delegate: FLListRow {
                    id: row
                    required property var modelData
                    required property int index
                    readonly property var tags: row.modelData.tags ?? []
                    height: Math.max(36, rowContent.implicitHeight + 8)
                    current: row.modelData.current
                    selected: row.ListView.isCurrentItem
                    onSelectRequested: {
                        view.currentIndex = row.index
                        view.forceActiveFocus()
                    }
                    onActivateRequested: root.vm.activateIndex(row.index)

                    Column {
                        id: rowContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 10
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1

                        Text {
                            text: row.modelData.label
                            color: FLTheme.text
                            elide: Text.ElideMiddle
                            width: parent.width
                            font.pixelSize: 12
                            font.weight: row.current ? Font.DemiBold : Font.Normal
                        }
                        Text {
                            visible: text.length > 0
                            text: row.modelData.subfolder
                            color: FLTheme.textMuted
                            elide: Text.ElideMiddle
                            width: parent.width
                            font.pixelSize: 10
                        }
                        Flow {
                            id: tagFlow
                            width: parent.width
                            spacing: 3
                            visible: row.tags.length > 0
                            height: visible ? childrenRect.height : 0

                            Repeater {
                                model: row.tags
                                delegate: Rectangle {
                                    required property string modelData
                                    radius: 5
                                    color: FLTheme.accentFaint
                                    border.width: 1
                                    border.color: FLTheme.accentSoft
                                    height: 15
                                    width: Math.min(tagFlow.width, tagLabel.implicitWidth + 10)

                                    Text {
                                        id: tagLabel
                                        anchors.fill: parent
                                        anchors.leftMargin: 5
                                        anchors.rightMargin: 5
                                        text: parent.modelData
                                        color: FLTheme.text
                                        font.pixelSize: 8
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                    }
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
                    busy: root.vm !== null && root.vm.scanning && root.vm.totalCount === 0
                    title: {
                        if (busy)
                            return "Scanning folder…"
                        return root.vm !== null && root.vm.totalCount > 0 ? "No matching files" : "No playlist yet"
                    }
                    detail: {
                        if (busy)
                            return "Looking for playable files"
                        return root.vm !== null && root.vm.totalCount > 0
                                ? "Try a different filename or folder"
                                : "Open a file to build a playlist"
                    }
                    actionText: root.vm !== null && root.vm.totalCount > 0 ? "Clear search" : ""
                    onActionRequested: {
                        root.vm.search = ""
                        searchField.forceActiveFocus()
                    }
                }
            }
        }
    }
}
