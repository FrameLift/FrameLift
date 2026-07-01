import QtQuick
import FrameLift.Controls

// One keybind slot rendered as a "key cap". Click to (re)bind; a × clears it.
// `capturing` highlights the cap and shows a prompt while it's awaiting a key.
Rectangle {
    id: root

    property string text: ""
    property bool capturing: false
    signal clicked()
    signal cleared()

    implicitWidth: Math.max(70, label.implicitWidth + 22 + (clearBtn.visible ? 16 : 0))
    implicitHeight: 28
    radius: 6
    color: root.capturing ? FLTheme.accentSoft : mouse.containsMouse ? FLTheme.hover : FLTheme.inputBg
    border.color: root.capturing ? FLTheme.accent : FLTheme.border
    border.width: 1

    Text {
        id: label
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.right: clearBtn.visible ? clearBtn.left : parent.right
        anchors.rightMargin: 6
        text: root.capturing ? "Press a key…" : (root.text.length > 0 ? root.text : "Set")
        color: root.capturing || root.text.length > 0 ? FLTheme.text : FLTheme.textMuted
        font.pixelSize: 12
        elide: Text.ElideRight
        horizontalAlignment: root.text.length > 0 && !root.capturing ? Text.AlignHCenter : Text.AlignLeft
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        anchors.rightMargin: clearBtn.visible ? 18 : 0
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    Rectangle {
        id: clearBtn
        visible: root.text.length > 0 && !root.capturing
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: 16
        height: 16
        radius: 8
        color: clearMouse.containsMouse ? FLTheme.danger : "transparent"

        Text {
            anchors.centerIn: parent
            text: "×"
            color: FLTheme.text
            font.pixelSize: 12
        }

        MouseArea {
            id: clearMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.cleared()
        }
    }
}
