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

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        propagateComposedEvents: true
        onClicked: menu.popup()
    }

    // Visual index of a given item inside the top-level menu. Used to anchor the
    // Instantiator-inserted plugin items: a Repeater inside a Menu mis-positions
    // entries whose model is populated after construction (the plugin list is
    // assembled deferred on the host side), so we insert explicitly instead.
    function indexOfItem(target) {
        for (let i = 0; i < menu.count; ++i) {
            if (menu.itemAt(i) === target) {
                return i;
            }
        }
        return menu.count - 1;
    }

    // A single styled MenuItem so every row picks up the dark glass theme: an
    // optional left check box (checkable rows), a right-aligned shortcut, and a
    // right-aligned arrow for rows that open a submenu. Used both as explicit
    // children and as each Menu's `delegate`, so auto-generated submenu rows are
    // styled identically.
    component Item_: MenuItem {
        id: item
        property string shortcut: ""
        // A hidden MenuItem still occupies its row in the Menu's ListView, so
        // collapse the height too — otherwise hidden rows leave empty gaps.
        implicitHeight: visible ? 32 : 0
        horizontalPadding: 10
        font.pixelSize: 13

        indicator: null

        arrow: Text {
            x: item.width - width - 10
            y: (item.height - height) / 2
            visible: item.subMenu !== null
            text: "▸"
            color: FLTheme.textMuted
            font.pixelSize: 12
        }

        contentItem: RowLayout {
            spacing: 10
            Item {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                Rectangle {
                    anchors.centerIn: parent
                    width: 15
                    height: 15
                    radius: 4
                    visible: item.checkable
                    color: item.checked ? FLTheme.accent : "transparent"
                    border.width: 1
                    border.color: item.checked ? FLTheme.accent : FLTheme.border
                    Text {
                        anchors.centerIn: parent
                        text: "✓"
                        font.pixelSize: 11
                        font.bold: true
                        color: FLTheme.text
                        visible: item.checked
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: item.text
                color: item.enabled ? FLTheme.text : FLTheme.textMuted
                font: item.font
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                visible: item.shortcut !== ""
                text: item.shortcut
                color: FLTheme.textMuted
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
            }
        }

        background: Rectangle {
            implicitWidth: 200
            radius: 6
            color: item.highlighted ? FLTheme.accent : "transparent"
        }
    }

    component Sep_: MenuSeparator {
        topPadding: visible ? 5 : 0
        bottomPadding: visible ? 5 : 0
        leftPadding: 8
        rightPadding: 8
        implicitHeight: visible ? implicitContentHeight + topPadding + bottomPadding : 0
        contentItem: Rectangle {
            implicitHeight: 1
            color: FLTheme.border
        }
    }

    // Reusable dark-glass Menu: shared background, padding, and item delegate.
    component ThemedMenu: Menu {
        padding: 6
        overlap: 0
        delegate: Item_ {}
        background: Rectangle {
            implicitWidth: 230
            color: FLTheme.surfaceStrong
            radius: FLTheme.radius
            border.color: FLTheme.border
            border.width: 1
        }
    }

    ThemedMenu {
        id: menu

        Item_ { text: "Open File"; shortcut: root.vm !== null ? root.vm.coreShortcuts["openFileDialog"] : ""; onTriggered: root.vm.openFile() }
        Item_ { text: "Open Network Stream…"; onTriggered: root.vm.openNetwork() }
        Sep_ {}
        Item_ { text: "Play / Pause"; shortcut: root.vm !== null ? root.vm.coreShortcuts["togglePause"] : ""; onTriggered: root.vm.togglePause() }
        Item_ { text: "Toggle Fullscreen"; shortcut: root.vm !== null ? root.vm.coreShortcuts["toggleFullscreen"] : ""; onTriggered: root.vm.toggleFullscreen() }
        Sep_ {}

        ThemedMenu {
            id: audioMenu
            title: "Audio"
            Item_ { text: "No audio tracks"; enabled: false; visible: audioTracks.count === 0 }
            Sep_ { visible: audioTracks.count > 0 }
            Item_ { text: "Mute"; checkable: true; visible: audioTracks.count > 0; shortcut: root.vm !== null ? root.vm.coreShortcuts["toggleMute"] : ""; checked: root.vm !== null && root.vm.muted; onTriggered: root.vm.toggleMute() }
            Item_ { text: "Normalize"; checkable: true; visible: audioTracks.count > 0; shortcut: root.vm !== null ? root.vm.coreShortcuts["toggleNormalize"] : ""; checked: root.vm !== null && root.vm.normalizeEnabled; onTriggered: root.vm.toggleNormalize() }

            // Tracks are populated when a file loads — see indexOfItem() note.
            Instantiator {
                id: audioTracks
                model: root.vm !== null ? root.vm.audioTracks : []
                delegate: Item_ {
                    required property var modelData
                    text: modelData.label
                    checkable: true
                    checked: modelData.selected
                    onTriggered: root.vm.selectAudioTrack(modelData.id)
                }
                onObjectAdded: (index, object) => audioMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => audioMenu.removeItem(object)
            }
        }

        ThemedMenu {
            id: subtitleMenu
            title: "Subtitle"
            // No subtitle controls when the media has no subtitle tracks.
            Item_ { text: "No subtitles"; enabled: false; visible: subTracks.count === 0 }
            Sep_ { visible: subTracks.count > 0 }
            Item_ { text: "Subtitles"; checkable: true; visible: subTracks.count > 0; shortcut: root.vm !== null ? root.vm.coreShortcuts["toggleSubtitles"] : ""; checked: root.vm !== null && root.vm.subtitlesEnabled; onTriggered: root.vm.toggleSubtitles() }

            Instantiator {
                id: subTracks
                model: root.vm !== null ? root.vm.subtitleTracks : []
                delegate: Item_ {
                    required property var modelData
                    text: modelData.label
                    checkable: true
                    checked: modelData.selected && root.vm.subtitlesEnabled
                    onTriggered: root.vm.selectSubtitleTrack(modelData.id)
                }
                onObjectAdded: (index, object) => subtitleMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => subtitleMenu.removeItem(object)
            }
        }

        Sep_ { id: extrasAnchor; visible: extras.count > 0 }
        Sep_ {}
        Item_ { text: "Quit"; shortcut: root.vm !== null ? root.vm.coreShortcuts["quit"] : ""; onTriggered: root.vm.quit() }

        // Plugin-contributed items (History, Playlist, Settings, …), inserted
        // between the submenus and Quit. Instantiator (not Repeater) so the rows
        // land at the anchor even though the host fills the model after the menu
        // is constructed.
        Instantiator {
            id: extras
            model: root.vm !== null ? root.vm.extraItems : []
            delegate: Item_ {
                required property var modelData
                text: modelData.label
                shortcut: modelData.hotkey
                onTriggered: root.vm.invokeExtra(modelData.index)
            }
            onObjectAdded: (index, object) => menu.insertItem(root.indexOfItem(extrasAnchor) + 1 + index, object)
            onObjectRemoved: (index, object) => menu.removeItem(object)
        }
    }
}
