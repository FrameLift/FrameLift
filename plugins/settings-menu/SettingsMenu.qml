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
                color: "#0D0D14"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14

                    Text {
                        text: "Settings"
                        color: FLTheme.text
                        font.pixelSize: 24
                        font.weight: Font.DemiBold
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: root.vm !== null ? root.vm.pages : []
                        spacing: 4

                        delegate: Rectangle {
                            id: pageDelegate
                            required property var modelData
                            width: ListView.view.width
                            height: 38
                            radius: 8
                            color: root.vm !== null && root.vm.activePage === pageDelegate.modelData.id
                                   ? "#408B5CF6"
                                   : pageMouse.containsMouse ? "#18FFFFFF" : "transparent"

                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                text: pageDelegate.modelData.title
                                color: FLTheme.text
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

                    Item { Layout.fillWidth: true }
                    FLActionButton {
                        text: "Reset"
                        enabled: parent.pageVm !== null && typeof parent.pageVm.reset === "function"
                        onClicked: parent.pageVm.reset()
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
    }
}
