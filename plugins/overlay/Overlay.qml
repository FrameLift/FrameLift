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
    property bool seekDragging: false

    Rectangle {
        anchors.fill: parent
        visible: root.vm !== null && root.vm.idle
        gradient: Gradient {
            GradientStop { position: 0; color: "#171125" }
            GradientStop { position: 1; color: FLTheme.canvas }
        }

        Column {
            anchors.centerIn: parent
            spacing: 12
            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: "icon.svg"
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
                color: FLTheme.text
                font.pixelSize: 34
                font.weight: Font.DemiBold
            }
        }
    }

    Item {
        id: toast
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: 16
        anchors.leftMargin: 16
        width: toastText.implicitWidth
        height: toastText.implicitHeight
        opacity: 0
        visible: opacity > 0
        Text {
            id: toastText
            text: root.vm !== null ? root.vm.commandLabel : ""
            color: FLTheme.text
            font.pixelSize: 12
        }
        SequentialAnimation {
            id: toastAnimation
            PropertyAction { target: toast; property: "opacity"; value: 1 }
            PauseAnimation { duration: 900 }
            NumberAnimation { target: toast; property: "opacity"; to: 0; duration: 240 }
        }
        Connections {
            target: root.vm
            function onCommandShown() { toastAnimation.restart() }
        }
    }

    FLGlassPanel {
        id: controls
        visible: root.vm !== null && !root.vm.idle && !root.vm.settingsOpen
        opacity: root.controlsVisible || root.seekDragging ? 1 : 0

        // Fixed-width bar, always centred in the window — independent of any open
        // drawers, so it never gets squeezed when both panels are open.
        width: Math.min(720, parent.width - 32)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        height: 64
        Behavior on opacity { NumberAnimation { duration: 180 } }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 12
            FLActionButton {
                // Mouse-only: never retain keyboard focus, or a click would leave
                // the button intercepting Space and double-toggling against the host
                // play/pause hotkey (net no-op — pausing appears broken).
                focusPolicy: Qt.NoFocus
                icon.source: root.vm !== null && root.vm.paused ? "icon-play.svg" : "icon-pause.svg"
                onClicked: root.vm.togglePause()
            }
            Slider {
                id: seekBar
                // Same reason: don't steal focus and double-handle arrow-key seeks.
                focusPolicy: Qt.NoFocus
                Layout.fillWidth: true
                implicitHeight: 20
                from: 0
                to: Math.max(0.001, root.vm !== null ? root.vm.duration : 0)
                value: pressed ? value : root.vm !== null ? root.vm.position : 0
                onMoved: root.vm.seek(value)
                onPressedChanged: {
                    root.seekDragging = pressed
                    if (pressed) {
                        root.controlsVisible = true
                        hideTimer.stop()
                    } else {
                        hideTimer.restart()
                    }
                }

                background: Rectangle {
                    x: seekBar.leftPadding
                    y: seekBar.topPadding + seekBar.availableHeight / 2 - height / 2
                    width: seekBar.availableWidth
                    height: 4
                    radius: 2
                    color: FLTheme.border

                    Rectangle {
                        width: seekBar.visualPosition * parent.width
                        height: parent.height
                        radius: parent.radius
                        color: FLTheme.accent
                    }
                }

                handle: Rectangle {
                    x: seekBar.leftPadding + seekBar.visualPosition * (seekBar.availableWidth - width)
                    y: seekBar.topPadding + seekBar.availableHeight / 2 - height / 2
                    implicitWidth: 14
                    implicitHeight: 14
                    radius: width / 2
                    color: seekBar.pressed ? Qt.lighter(FLTheme.accent, 1.15) : FLTheme.text
                    border.color: FLTheme.accent
                    border.width: seekBar.hovered || seekBar.pressed ? 2 : 0
                    scale: seekBar.hovered || seekBar.pressed ? 1.1 : 1
                    Behavior on scale { NumberAnimation { duration: 120 } }
                }
            }
            Text {
                text: {
                    const position = root.vm !== null ? root.vm.position : 0
                    return Math.floor(position / 60) + ":" +
                           String(Math.floor(position % 60)).padStart(2, "0")
                }
                color: FLTheme.textMuted
            }
        }
    }

    // Minimal, non-interactable seek indicator: a thin progress bar that flashes
    // at screen centre whenever a seek is triggered (slider or keyboard), then
    // fades out almost immediately.
    Rectangle {
        id: seekFlash
        anchors.centerIn: parent
        width: 240
        height: 6
        radius: 3
        color: '#33ffffff'
        opacity: 0
        visible: opacity > 0

        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            height: parent.height
            radius: parent.radius
            width: parent.width * (root.vm !== null && root.vm.duration > 0
                                   ? Math.min(1, root.vm.position / root.vm.duration) : 0)
            color: '#ffffff'
        }

        SequentialAnimation {
            id: seekFlashAnim
            NumberAnimation { target: seekFlash; property: "opacity"; to: 0.5; duration: 70 }
            PauseAnimation { duration: 280 }
            NumberAnimation { target: seekFlash; property: "opacity"; to: 0; duration: 150 }
        }
        Connections {
            target: root.vm
            function onSeekTriggered() { seekFlashAnim.restart() }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        onPositionChanged: (mouse) => {
            // Only reveal/extend the controls bar when it can actually show — not
            // on the idle screen or while the settings window is up (mirrors the
            // `controls` panel's own visible condition). Otherwise hideTimer would
            // keep restarting on every mouse move with nothing to hide.
            if (root.vm === null || root.vm.idle || root.vm.settingsOpen)
                return
            // Don't reveal when the cursor is over an open drawer (its own region,
            // measured by the inset it publishes) — the bar belongs to the video.
            if (mouse.x < root.vm.leftInset || mouse.x > width - root.vm.rightInset)
                return
            root.controlsVisible = true
            hideTimer.restart()
        }
    }
    Timer {
        id: hideTimer
        interval: 1800
        onTriggered: {
            if (!root.seekDragging)
                root.controlsVisible = false
        }
    }
}
