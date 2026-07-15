import QtQuick
import QtQuick.Layouts

// Reusable modal confirmation dialog, centered over the whole plugin surface.
// Set `open` to show it; it emits `accepted` / `rejected` and closes itself.
Item {
    id: control
    anchors.fill: parent
    visible: open
    z: 1000

    property bool open: false
    property string title: ""
    property string message: ""
    property string confirmText: "Confirm"
    property string cancelText: "Cancel"
    property bool destructive: false

    signal accepted
    signal rejected

    function accept() {
        control.open = false
        control.accepted()
    }
    function reject() {
        control.open = false
        control.rejected()
    }

    // Dimmed backdrop; clicking outside the card cancels.
    Rectangle {
        anchors.fill: parent
        color: "#99000000"
        MouseArea {
            anchors.fill: parent
            onClicked: control.reject()
        }
    }

    FLGlassPanel {
        anchors.centerIn: parent
        width: Math.min(360, control.width - 48)
        height: layout.implicitHeight + 32

        // Swallow clicks so they don't fall through to the backdrop.
        MouseArea { anchors.fill: parent }

        ColumnLayout {
            id: layout
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            Text {
                text: control.title
                visible: text.length > 0
                color: FLTheme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Text {
                text: control.message
                visible: text.length > 0
                color: FLTheme.textMuted
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                FLActionButton {
                    text: control.cancelText
                    implicitHeight: 30; padding: 12; font.pixelSize: 12
                    onClicked: control.reject()
                }
                FLActionButton {
                    text: control.confirmText
                    implicitHeight: 30; padding: 12; font.pixelSize: 12
                    accentColor: control.destructive ? FLTheme.danger : FLTheme.accent
                    onClicked: control.accept()
                }
            }
        }
    }
}
