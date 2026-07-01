import QtQuick
import FrameLift.Controls

Item {
    id: root
    required property var viewModel
    property var vm: viewModel
    anchors.fill: parent
    visible: vm !== null && vm.open
    FLGlassPanel {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 0
        // Flush in the window corner — square off the top-left so it meets the edges.
        topLeftRadius: 0
        width: Math.min(560, content.implicitWidth + 24)
        height: content.implicitHeight + 24

        Column {
            id: content
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            Repeater {
                model: root.vm !== null ? root.vm.sections : []
                delegate: Column {
                    id: section
                    property var entry: modelData
                    spacing: 1

                    Text {
                        text: section.entry !== undefined ? section.entry.title : ""
                        color: FLTheme.accent
                        font.family: "monospace"
                        font.pixelSize: 10
                        font.bold: true
                    }
                    Text {
                        text: section.entry !== undefined ? section.entry.body : ""
                        color: FLTheme.text
                        font.family: "monospace"
                        font.pixelSize: 10
                        lineHeight: 1.2
                    }
                }
            }
        }
    }
}
