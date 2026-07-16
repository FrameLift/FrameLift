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
    property bool contentLoaded: false
    onVisibleChanged: if (visible) contentLoaded = true

    Loader {
        anchors.fill: parent
        active: root.contentLoaded
        sourceComponent: Component {
            FLGlassPanel {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 18
        width: Math.min(parent.width - 36, 430)
        height: Math.min(parent.height - 36, layout.implicitHeight + 28)
        ColumnLayout {
            id: layout
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Text {
                    text: "Benchmark"
                    color: FLTheme.text
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
                Item { Layout.fillWidth: true }
                Rectangle {
                    Layout.preferredWidth: statusLabel.implicitWidth + 16
                    Layout.preferredHeight: 24
                    radius: 12
                    color: root.vm !== null && root.vm.accumulating ? FLTheme.accentSoft : FLTheme.inputBg
                    border.width: 1
                    border.color: root.vm !== null && root.vm.accumulating ? FLTheme.accent : FLTheme.border
                    Text {
                        id: statusLabel
                        anchors.centerIn: parent
                        text: root.vm !== null && root.vm.complete ? "Complete"
                              : root.vm !== null && root.vm.accumulating ? "Recording" : "Idle"
                        color: root.vm !== null && root.vm.accumulating ? FLTheme.text : FLTheme.textMuted
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }
            }

            Repeater {
                model: root.vm !== null ? root.vm.sections : []
                delegate: ColumnLayout {
                    id: section
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 5

                    Rectangle {
                        visible: section.index > 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: FLTheme.border
                        opacity: 0.55
                    }

                    Text {
                        text: section.modelData.title
                        color: FLTheme.accent
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }

                    Repeater {
                        model: section.modelData.rows
                        delegate: RowLayout {
                            id: metricRow
                            required property var modelData
                            width: section.width
                            spacing: 8

                            readonly property string detail: modelData.detail !== undefined ? modelData.detail : ""

                            Text {
                                text: metricRow.modelData.label
                                color: FLTheme.textMuted
                                font.pixelSize: 11
                                Layout.preferredWidth: 74
                                elide: Text.ElideRight
                            }
                            Text {
                                text: metricRow.modelData.value
                                color: FLTheme.text
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                Layout.preferredWidth: detailText.visible ? 98 : 0
                                Layout.fillWidth: !detailText.visible
                                elide: Text.ElideRight
                            }
                            Text {
                                id: detailText
                                visible: metricRow.detail.length > 0
                                text: metricRow.detail
                                color: FLTheme.textMuted
                                font.pixelSize: 11
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                FLActionButton { text: "Load file"; onClicked: root.vm.chooseFile() }
                FLActionButton { text: "Reset"; onClicked: root.vm.resetRun() }
                Item { Layout.fillWidth: true }
            }
        }
            }
        }
    }
}
