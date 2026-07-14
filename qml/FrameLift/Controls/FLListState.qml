import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Centered status for list-backed panels: scanning/loading, empty content, or a
// filtered no-results state with an optional recovery action.
ColumnLayout {
    id: root

    property bool busy: false
    property string title: ""
    property string detail: ""
    property string actionText: ""
    signal actionRequested()

    spacing: 7

    BusyIndicator {
        visible: root.busy
        running: root.busy
        Layout.alignment: Qt.AlignHCenter
        implicitWidth: 28
        implicitHeight: 28
    }

    Text {
        text: root.title
        color: FLTheme.text
        font.pixelSize: 13
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: 280
    }

    Text {
        visible: root.detail.length > 0
        text: root.detail
        color: FLTheme.textMuted
        font.pixelSize: 11
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: 280
    }

    FLPanelAction {
        visible: root.actionText.length > 0
        text: root.actionText
        Layout.alignment: Qt.AlignHCenter
        onClicked: root.actionRequested()
    }
}
