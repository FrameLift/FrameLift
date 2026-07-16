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
    visible: vm !== null && vm.dialogOpen
    property bool contentLoaded: false
    onVisibleChanged: if (visible) contentLoaded = true

    Loader {
        anchors.fill: parent
        active: root.contentLoaded
        sourceComponent: Component {
            Item {
                anchors.fill: parent

                Rectangle {
                    anchors.fill: parent
                    color: "#88000000"
                    MouseArea { anchors.fill: parent; onClicked: root.vm.cancel() }
                }
                FLGlassPanel {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 40, 520)
                    height: 180
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 12
                        Text { text: "Open network stream"; color: FLTheme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
                        TextField {
                            id: input
                            Layout.fillWidth: true
                            text: root.vm !== null ? root.vm.url : ""
                            placeholderText: "https://example.com/stream.m3u8"
                            onTextEdited: if (root.vm !== null) root.vm.url = text
                            onAccepted: root.vm.submit()
                        }
                        RowLayout {
                            Item { Layout.fillWidth: true }
                            FLActionButton { text: "Cancel"; onClicked: root.vm.cancel() }
                            FLActionButton { text: "Open"; enabled: input.text.length > 0; onClicked: root.vm.submit() }
                        }
                    }
                }
            }
        }
    }
}
