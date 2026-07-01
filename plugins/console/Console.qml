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
    visible: vm !== null && vm.open

    // Re-pin to the newest line once the panel is actually on screen. Doing this
    // only from ListView.onCountChanged misfires on the first open: the count goes
    // 0 → N while the panel is still laying out (height 0), so positionViewAtEnd
    // scrolls past the content and the view looks empty until the next entry bumps
    // the count again. Re-position when we become visible, deferred so layout has run.
    onVisibleChanged: if (visible) Qt.callLater(logList.positionViewAtEnd)

    // Console output shows each line as "[time] [LEVEL] message"; mirror that here.
    function levelLabel(level) {
        switch (level) {
        case 0: return "DEBUG"
        case 1: return "INFO"
        case 2: return "WARN"
        case 3: return "ERROR"
        case 4: return "PERF"
        default: return "LOG"
        }
    }
    function rowLabel(row) {
        if (row.kind === "input")
            return ">"
        if (row.kind === "output") {
            switch (row.level) {
            case 1: return "WARN"
            case 2: return "ERR"
            default: return "OUT"
            }
        }
        return root.levelLabel(row.level)
    }
    function levelColor(level) {
        switch (level) {
        case 1: return "#34D399" // info  — green
        case 2: return "#FBBF24" // warn  — amber
        case 3: return FLTheme.danger // error
        case 4: return FLTheme.accent // perf
        default: return FLTheme.textMuted // debug
        }
    }
    function rowColor(row) {
        if (row.kind === "input")
            return FLTheme.accent
        if (row.kind === "output") {
            switch (row.level) {
            case 1: return "#FBBF24"
            case 2: return FLTheme.danger
            default: return FLTheme.text
            }
        }
        return root.levelColor(row.level)
    }

    FLGlassPanel {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 18
        height: Math.min(parent.height * 0.55, 520)
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: "Console"; color: FLTheme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                TextField {
                    id: filterField
                    Layout.fillWidth: true
                    placeholderText: "Filter"
                    placeholderTextColor: FLTheme.textMuted
                    color: FLTheme.text
                    font.pixelSize: 13
                    leftPadding: 10
                    rightPadding: 10
                    topPadding: 6
                    bottomPadding: 6
                    selectByMouse: true
                    text: root.vm !== null ? root.vm.filterText : ""
                    onTextEdited: if (root.vm !== null) root.vm.filterText = text
                    background: Rectangle {
                        radius: 6
                        color: "#14000000"
                        border.width: 1
                        border.color: filterField.activeFocus ? FLTheme.accent : FLTheme.border
                    }
                }
                CheckBox {
                    id: perfCheck
                    text: "Performance"
                    checked: root.vm !== null && root.vm.perfOnly
                    onToggled: if (root.vm !== null) root.vm.perfOnly = checked
                    contentItem: Text {
                        text: perfCheck.text
                        color: FLTheme.text
                        font.pixelSize: 12
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: perfCheck.indicator.width + perfCheck.spacing
                    }
                }
                FLActionButton { text: "Clear"; onClicked: root.vm.clearLines() }
                FLActionButton { text: "Close"; onClicked: root.vm.close() }
            }
            ListView {
                id: logList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.vm !== null ? root.vm.lines : []
                delegate: RowLayout {
                    id: row
                    required property var modelData
                    width: ListView.view.width
                    spacing: 8
                    Text {
                        text: Qt.formatDateTime(new Date(row.modelData.timestamp), "hh:mm:ss.zzz")
                        color: FLTheme.textMuted
                        font.family: "monospace"
                        font.pixelSize: 11
                        Layout.alignment: Qt.AlignTop
                    }
                    Text {
                        text: root.rowLabel(row.modelData)
                        color: root.rowColor(row.modelData)
                        font.family: "monospace"
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredWidth: 42
                    }
                    Text {
                        text: row.modelData.message
                        color: root.rowColor(row.modelData)
                        font.family: "monospace"
                        font.pixelSize: 11
                        wrapMode: Text.WrapAnywhere
                        Layout.fillWidth: true
                    }
                }
                onCountChanged: Qt.callLater(positionViewAtEnd)
            }
            TextField {
                id: commandField
                Layout.fillWidth: true
                placeholderText: "Command"
                placeholderTextColor: FLTheme.textMuted
                color: FLTheme.text
                font.family: "monospace"
                font.pixelSize: 12
                leftPadding: 10
                rightPadding: 10
                topPadding: 7
                bottomPadding: 7
                selectByMouse: true
                onAccepted: {
                    if (root.vm !== null && text.length > 0) {
                        root.vm.submitCommand(text)
                        text = ""
                    }
                }
                Keys.onPressed: function(event) {
                    if (root.vm === null)
                        return
                    if (event.key === Qt.Key_Up) {
                        text = root.vm.historyPrevious(text)
                        cursorPosition = text.length
                        event.accepted = true
                    } else if (event.key === Qt.Key_Down) {
                        text = root.vm.historyNext()
                        cursorPosition = text.length
                        event.accepted = true
                    }
                }
                background: Rectangle {
                    radius: 6
                    color: "#14000000"
                    border.width: 1
                    border.color: commandField.activeFocus ? FLTheme.accent : FLTheme.border
                }
            }
        }
    }
}
