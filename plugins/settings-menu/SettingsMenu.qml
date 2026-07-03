pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import FrameLift.Controls

Item {
    id: root
    anchors.fill: parent
    visible: false
    required property var viewModel
    property var vm: viewModel

    ApplicationWindow {
        id: settingsWindow
        width: 980
        height: 680
        minimumWidth: 760
        minimumHeight: 520
        visible: root.vm !== null && root.vm.open
        title: "FrameLift Settings"
        color: FLTheme.canvas

        onClosing: function(close) {
            close.accepted = false
            if (root.vm !== null)
                root.vm.closeQml()
        }

        RowLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                Layout.preferredWidth: 220
                Layout.fillHeight: true
                color: FLTheme.canvas

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    Text {
                        text: "Settings"
                        color: FLTheme.text
                        font.pixelSize: 24
                        font.weight: Font.DemiBold
                    }

                    ListView {
                        id: navList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: root.vm !== null ? root.vm.pages : []
                        spacing: 2
                        clip: true

                        // Group name shown as a header: "core" → "Core", etc.
                        function groupLabel(g) {
                            return g === "core" ? "Core"
                                 : g === "plugin" ? "Plugins"
                                 : g === "system" ? "System" : g
                        }

                        section.property: "group"
                        section.criteria: ViewSection.FullString
                        section.delegate: ColumnLayout {
                            required property string section
                            width: ListView.view.width
                            spacing: 6

                            // Separator above every group except the first.
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.topMargin: 8
                                implicitHeight: 1
                                color: FLTheme.border
                                visible: root.vm !== null && root.vm.pages.length > 0
                                         && root.vm.pages[0].group !== parent.section
                            }
                            Text {
                                Layout.fillWidth: true
                                Layout.leftMargin: 4
                                text: navList.groupLabel(parent.section).toUpperCase()
                                color: FLTheme.textMuted
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }
                        }

                        delegate: Rectangle {
                            id: pageDelegate
                            required property var modelData
                            width: ListView.view.width
                            height: 30
                            radius: 6
                            color: root.vm !== null && root.vm.activePage === pageDelegate.modelData.id
                                   ? FLTheme.accentSoft
                                   : pageMouse.containsMouse ? FLTheme.hover : "transparent"

                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                text: pageDelegate.modelData.title
                                color: FLTheme.text
                                font.pixelSize: 13
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            MouseArea {
                                id: pageMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    if (root.vm !== null)
                                        root.vm.activePage = pageDelegate.modelData.id
                                }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 24

                Text {
                    text: root.vm !== null && root.vm.activePageViewModel !== null
                          ? root.vm.activePageViewModel.title : ""
                    color: FLTheme.text
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                }

                Loader {
                    id: pageLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    property string loadedPage: ""

                    function reloadPage(force) {
                        if (root.vm === null || root.vm.activePageUrl.length === 0
                                || root.vm.activePageViewModel === null) {
                            loadedPage = ""
                            source = ""
                            return
                        }
                        if (!force && loadedPage === root.vm.activePage)
                            return
                        loadedPage = root.vm.activePage
                        setSource(root.vm.activePageUrl, {
                            "viewModel": root.vm.activePageViewModel
                        })
                    }

                    Component.onCompleted: reloadPage(true)
                }

                Connections {
                    target: root.vm
                    function onQmlChanged() {
                        pageLoader.reloadPage(false)
                    }
                }

                RowLayout {
                    property var pageVm: root.vm !== null ? root.vm.activePageViewModel : null
                    spacing: 12

                    FLSwitch {
                        checked: FLSettingsUiState.showKeys
                        onToggled: FLSettingsUiState.showKeys = checked
                    }
                    Text {
                        text: "Show keys"
                        color: FLTheme.textMuted
                        font.pixelSize: 12
                        verticalAlignment: Text.AlignVCenter
                    }

                    Item { Layout.fillWidth: true }
                    FLActionButton {
                        text: "Reset"
                        enabled: root.vm !== null
                        onClicked: resetDialog.open = true
                    }
                    FLActionButton {
                        text: parent.pageVm !== null && parent.pageVm.dirty ? "Save *" : "Save"
                        enabled: parent.pageVm !== null && typeof parent.pageVm.save === "function"
                        onClicked: parent.pageVm.save()
                    }
                    FLActionButton {
                        text: "Close"
                        onClicked: if (root.vm !== null) root.vm.closeQml()
                    }
                }
            }
        }

        // Reset confirmation: choose between resetting only the current page or all
        // settings. Overlays the whole window; click-outside cancels.
        Item {
            id: resetDialog
            anchors.fill: parent
            visible: resetDialog.open
            z: 1000
            property bool open: false

            Rectangle {
                anchors.fill: parent
                color: "#99000000"
                MouseArea { anchors.fill: parent; onClicked: resetDialog.open = false }
            }

            FLGlassPanel {
                anchors.centerIn: parent
                width: Math.min(380, parent.width - 48)
                height: resetLayout.implicitHeight + 32

                // Swallow clicks so they don't fall through to the backdrop.
                MouseArea { anchors.fill: parent }

                ColumnLayout {
                    id: resetLayout
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    Text {
                        text: "Reset settings"
                        color: FLTheme.text
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                    }
                    Text {
                        text: "Reset only the current page, or all settings to their defaults?"
                        color: FLTheme.textMuted
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        FLActionButton {
                            text: "Cancel"
                            implicitHeight: 30; padding: 12; font.pixelSize: 12
                            onClicked: resetDialog.open = false
                        }
                        Item { Layout.fillWidth: true }
                        FLActionButton {
                            text: "This page"
                            implicitHeight: 30; padding: 12; font.pixelSize: 12
                            onClicked: {
                                if (root.vm !== null)
                                    root.vm.resetActivePageOnly()
                                resetDialog.open = false
                            }
                        }
                        FLActionButton {
                            text: "All settings"
                            implicitHeight: 30; padding: 12; font.pixelSize: 12
                            accentColor: FLTheme.danger
                            onClicked: {
                                if (root.vm !== null)
                                    root.vm.resetAllQml()
                                resetDialog.open = false
                            }
                        }
                    }
                }
            }
        }
    }
}
