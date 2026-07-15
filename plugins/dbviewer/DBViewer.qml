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

    readonly property int cellWidth: 170
    readonly property int cellHeight: 26

    FLDrawer {
        id: drawer
        open: root.vm !== null && root.vm.open
        rightSide: true
        drawerWidth: 640
        onXChanged: if (root.vm !== null) root.vm.publishVisibleWidth(Math.max(0, root.width - x))

        ColumnLayout {
            id: panel
            anchors.fill: parent
            anchors.margins: 8
            spacing: 8

            // ── Header ────────────────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    text: "Database Viewer"
                    color: FLTheme.text
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                FLActionButton {
                    text: "Refresh"
                    implicitHeight: 28; padding: 8; font.pixelSize: 12
                    onClicked: if (root.vm !== null) root.vm.refresh()
                }
            }

            Text {
                Layout.fillWidth: true
                visible: root.vm !== null && !root.vm.available
                text: "Media store unavailable — the shared SQLite database is not loaded."
                color: FLTheme.textMuted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            // ── Table picker ──────────────────────────────────────────────────
            ListView {
                id: tableStrip
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                visible: root.vm !== null && root.vm.tables.length > 0
                orientation: ListView.Horizontal
                spacing: 6
                clip: true
                model: root.vm !== null ? root.vm.tables : []
                delegate: Rectangle {
                    id: chip
                    required property var modelData
                    height: 26
                    width: chipLabel.implicitWidth + 18
                    radius: 6
                    readonly property bool current: root.vm !== null && root.vm.source === chip.modelData.name
                    color: chip.current ? FLTheme.accentSoft : FLTheme.inputBg
                    border.width: 1
                    border.color: chip.current ? FLTheme.accent : FLTheme.border
                    Text {
                        id: chipLabel
                        anchors.centerIn: parent
                        text: chip.modelData.rowCount >= 0
                              ? chip.modelData.name + "  (" + chip.modelData.rowCount + ")"
                              : chip.modelData.name
                        color: FLTheme.text
                        font.pixelSize: 12
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: if (root.vm !== null) root.vm.selectTable(chip.modelData.name)
                    }
                }
            }

            // ── SQL input ─────────────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                TextField {
                    id: sqlField
                    Layout.fillWidth: true
                    placeholderText: "Read-only SQL: SELECT … FROM …"
                    placeholderTextColor: FLTheme.textMuted
                    color: FLTheme.text
                    font.family: "monospace"
                    font.pixelSize: 12
                    leftPadding: 10; rightPadding: 10; topPadding: 6; bottomPadding: 6
                    selectByMouse: true
                    onAccepted: if (root.vm !== null) root.vm.runQuery(text)
                    background: Rectangle {
                        radius: 6
                        color: FLTheme.inputBg
                        border.width: 1
                        border.color: sqlField.activeFocus ? FLTheme.accent : FLTheme.border
                    }
                }
                FLActionButton {
                    text: "Run"
                    implicitHeight: 30; padding: 10; font.pixelSize: 12
                    onClicked: if (root.vm !== null) root.vm.runQuery(sqlField.text)
                }
            }

            // ── Error banner ──────────────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                visible: root.vm !== null && root.vm.error !== ""
                color: "#33EF4444"
                border.width: 1
                border.color: FLTheme.danger
                radius: 6
                implicitHeight: errText.implicitHeight + 12
                Text {
                    id: errText
                    anchors.fill: parent
                    anchors.margins: 6
                    text: root.vm !== null ? root.vm.error : ""
                    color: FLTheme.text
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }

            // ── Result meta ───────────────────────────────────────────────────
            Text {
                Layout.fillWidth: true
                visible: root.vm !== null && root.vm.source !== "" && root.vm.error === ""
                text: {
                    if (root.vm === null)
                        return ""
                    var n = root.vm.rows.length
                    var base = n + (n === 1 ? " row" : " rows")
                    return root.vm.truncated ? base + " (capped at " + root.vm.rowCap + ")" : base
                }
                color: FLTheme.textMuted
                font.pixelSize: 11
            }

            // ── Result grid ───────────────────────────────────────────────────
            Flickable {
                id: grid
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                flickableDirection: Flickable.HorizontalFlick
                contentWidth: Math.max(width, root.cellWidth * (root.vm !== null ? root.vm.columns.length : 0))
                contentHeight: height

                Column {
                    width: grid.contentWidth

                    Row {
                        id: headerRow
                        height: root.cellHeight
                        Repeater {
                            model: root.vm !== null ? root.vm.columns : []
                            delegate: Rectangle {
                                id: headerCell
                                required property var modelData
                                width: root.cellWidth
                                height: root.cellHeight
                                color: FLTheme.surfaceStrong
                                border.width: 1
                                border.color: FLTheme.border
                                Text {
                                    anchors.fill: parent
                                    anchors.leftMargin: 6
                                    verticalAlignment: Text.AlignVCenter
                                    text: headerCell.modelData
                                    color: FLTheme.text
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }

                    ListView {
                        id: rowsView
                        width: grid.contentWidth
                        height: grid.height - headerRow.height
                        clip: true
                        model: root.vm !== null ? root.vm.rows : []
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Row {
                            id: dataRow
                            required property var modelData
                            required property int index
                            height: root.cellHeight
                            Repeater {
                                model: dataRow.modelData
                                delegate: Rectangle {
                                    id: cell
                                    required property var modelData
                                    width: root.cellWidth
                                    height: root.cellHeight
                                    color: dataRow.index % 2 === 0 ? "transparent" : FLTheme.hover
                                    border.width: 1
                                    border.color: FLTheme.border
                                    Text {
                                        anchors.fill: parent
                                        anchors.leftMargin: 6
                                        verticalAlignment: Text.AlignVCenter
                                        text: cell.modelData === undefined ? "NULL" : cell.modelData
                                        color: cell.modelData === undefined ? FLTheme.textMuted : FLTheme.text
                                        font.italic: cell.modelData === undefined
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
