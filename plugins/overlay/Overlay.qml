pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FrameLift.Controls

Item {
    id: root
    required property var viewModel
    property var vm: viewModel
    property bool controlsVisible: false

    Rectangle {
        anchors.fill: parent
        visible: root.vm !== null && root.vm.idle
        gradient: Gradient {
            GradientStop { position: 0; color: "#171125" }
            GradientStop { position: 1; color: Theme.canvas }
        }

        Column {
            anchors.centerIn: parent
            spacing: 12
            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: "Icon.png"
                sourceSize.width: 96
                sourceSize.height: 96
                width: 96
                height: 96
                fillMode: Image.PreserveAspectFit
                smooth: true
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "FrameLift"
                color: Theme.text
                font.pixelSize: 34
                font.weight: Font.DemiBold
            }
        }
    }

    GlassPanel {
        id: toast
        anchors.top: parent.top
        anchors.topMargin: 16
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(420, toastText.implicitWidth + 32)
        height: 42
        opacity: 0
        visible: opacity > 0
        Text {
            id: toastText
            anchors.centerIn: parent
            text: root.vm !== null ? root.vm.commandLabel : ""
            color: Theme.text
        }
        SequentialAnimation {
            id: toastAnimation
            NumberAnimation { target: toast; property: "opacity"; to: 1; duration: 120 }
            PauseAnimation { duration: 1800 }
            NumberAnimation { target: toast; property: "opacity"; to: 0; duration: 240 }
        }
        Connections {
            target: root.vm
            function onCommandShown() { toastAnimation.restart() }
        }
    }

    GlassPanel {
        id: controls
        visible: root.vm !== null && !root.vm.idle && !root.vm.settingsOpen
        opacity: root.controlsVisible ? 1 : 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: (root.vm !== null ? root.vm.leftInset : 0) + 16
        anchors.rightMargin: (root.vm !== null ? root.vm.rightInset : 0) + 16
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        height: 64
        Behavior on opacity { NumberAnimation { duration: 180 } }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 12
            ActionButton {
                text: root.vm !== null && root.vm.paused ? "Play" : "Pause"
                onClicked: root.vm.togglePause()
            }
            Slider {
                Layout.fillWidth: true
                from: 0
                to: Math.max(0.001, root.vm !== null ? root.vm.duration : 0)
                value: pressed ? value : root.vm !== null ? root.vm.position : 0
                onMoved: root.vm.seek(value)
            }
            Text {
                text: {
                    const position = root.vm !== null ? root.vm.position : 0
                    return Math.floor(position / 60) + ":" +
                           String(Math.floor(position % 60)).padStart(2, "0")
                }
                color: Theme.textMuted
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        onPositionChanged: {
            // Only reveal/extend the controls bar when it can actually show — not
            // on the idle screen or while the settings window is up (mirrors the
            // `controls` panel's own visible condition). Otherwise hideTimer would
            // keep restarting on every mouse move with nothing to hide.
            if (root.vm === null || root.vm.idle || root.vm.settingsOpen)
                return
            root.controlsVisible = true
            hideTimer.restart()
        }
    }
    Timer {
        id: hideTimer
        interval: 1800
        onTriggered: root.controlsVisible = false
    }
}
