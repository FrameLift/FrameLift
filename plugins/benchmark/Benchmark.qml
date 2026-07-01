pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import FrameLift.Controls

Item {
    id: root
    required property var viewModel
    property var vm: viewModel
    anchors.fill: parent
    visible: vm !== null && vm.open
    FLGlassPanel {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 18
        width: 340
        // Size to content so every row fits regardless of how much the summary grows.
        height: layout.implicitHeight + 24
        ColumnLayout {
            id: layout
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Benchmark"; color: FLTheme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                Item { Layout.fillWidth: true }
                FLActionButton { text: "Close"; onClicked: root.vm.close() }
            }
            Text { Layout.fillWidth: true; text: root.vm !== null ? root.vm.summary : ""; color: FLTheme.textMuted; font.family: "monospace"; font.pixelSize: 11; lineHeight: 1.2 }
            RowLayout {
                FLActionButton { text: "Load file"; onClicked: root.vm.chooseFile() }
                FLActionButton { text: "Reset"; onClicked: root.vm.resetRun() }
                Text {
                    text: root.vm !== null && root.vm.complete ? "Complete"
                          : root.vm !== null && root.vm.accumulating ? "Recording" : "Idle"
                    color: root.vm !== null && root.vm.accumulating ? FLTheme.accent : FLTheme.textMuted
                }
            }
        }
    }
}
