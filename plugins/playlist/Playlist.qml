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
            anchors.margins: 6
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
                FLActionButton {
                    id: actionsButton
                    text: "⋯"
                    implicitHeight: 28; implicitWidth: 32; padding: 8; font.pixelSize: 16
                    onClicked: actionsMenu.popup(actionsButton, 0, actionsButton.height + 4)
                }
            }

            FLNavigableListView {
                id: view
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: root.vm !== null ? root.vm.entries : []
                active: root.vm !== null && root.vm.open
                // On open, place the cursor on the playing item and bring it
                // into view; from there Up/Down navigate natively.
                onActiveChanged: if (active && root.vm !== null) {
                    currentIndex = root.vm.currentIndex
                    keepCurrentInView()
                    forceActiveFocus()
                }
                Keys.onReturnPressed: if (currentIndex >= 0 && root.vm !== null) root.vm.activateIndex(currentIndex)

                // The model is rebuilt whenever the active file changes (or on
                // reload/shuffle), which resets the ListView's currentIndex to
                // 0. Re-anchor the cursor on the now-playing item so it follows
                // playback instead of snapping to the first row. Plain
                // navigation doesn't rebuild the model, so it isn't affected.
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
                    height: 46
                    current: row.modelData.current
                    selected: row.ListView.isCurrentItem
                    onSelectRequested: { view.currentIndex = row.index; view.forceActiveFocus() }
                    onActivateRequested: root.vm.activateIndex(row.index)
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 6
                        spacing: 1
                        Text {
                            text: row.modelData.label
                            color: FLTheme.text
                            elide: Text.ElideMiddle
                            width: parent.width
                            font.pixelSize: 12
                        }
                        Text {
                            text: row.modelData.subfolder
                            color: FLTheme.textMuted
                            elide: Text.ElideMiddle
                            width: parent.width
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }
    }

    // Overflow menu collapsing the per-panel actions (reload, sort, shuffle) so
    // the header stays compact. Sort and shuffle are checkable to show live state.
    Menu {
        id: actionsMenu
        padding: 6
        overlap: 0

        background: Rectangle {
            implicitWidth: 180
            color: FLTheme.surfaceStrong
            radius: FLTheme.radius
            border.color: FLTheme.border
            border.width: 1
        }

        component Item_: MenuItem {
            id: item
            property bool on: false
            implicitHeight: 32
            horizontalPadding: 10
            font.pixelSize: 13
            indicator: null
            contentItem: RowLayout {
                spacing: 10
                Text {
                    Layout.preferredWidth: 14
                    text: item.checkable && item.on ? "✓" : ""
                    color: FLTheme.accent
                    font.pixelSize: 12
                    font.bold: true
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    Layout.fillWidth: true
                    text: item.text
                    color: FLTheme.text
                    font: item.font
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
            }
            background: Rectangle {
                radius: 6
                color: item.highlighted ? FLTheme.accent : "transparent"
            }
        }

        Item_ { text: "Reload"; onTriggered: root.vm.Reload() }
        Item_ {
            text: "Sort alphabetically"
            checkable: true
            on: root.vm !== null && root.vm.sortByName
            onTriggered: root.vm.toggleSortByName()
        }
        Item_ {
            text: "Shuffle"
            checkable: true
            on: root.vm !== null && root.vm.shuffleEnabled
            onTriggered: root.vm.ToggleShuffle()
        }
    }
}
