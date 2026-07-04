import QtQuick

// Fallback client-side window chrome, shown only when Qt draws no native decorations
// (Vulkan-RHI on Wayland). Backed by the `chrome` context property (a WindowChrome).
// The root fills the whole window at a high z; only the top bar strip and the thin edge
// grips carry MouseAreas, so the transparent interior lets clicks fall through to the
// video and plugin overlays beneath it.
Item {
    id: root

    readonly property int barHeight: 32
    readonly property int grip: 6

    // ── Title bar strip ───────────────────────────────────────────────────────
    Rectangle {
        id: bar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: root.barHeight
        visible: !chrome.fullscreen
        color: FLTheme.surfaceStrong

        // Drag to move (once the pointer actually moves, so double-click still lands),
        // double-click to toggle maximize. Buttons sit above this and take precedence.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            property bool moving: false
            onPressed: moving = false
            onPositionChanged: {
                if (!moving) {
                    moving = true;
                    chrome.beginMove();
                }
            }
            onDoubleClicked: chrome.toggleMaximize()
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.right: buttons.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: chrome.title.length > 0 ? chrome.title : "FrameLift"
            color: FLTheme.text
            font.pixelSize: 13
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        Row {
            id: buttons
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            spacing: 0

            component WinButton: Rectangle {
                property alias glyph: label.text
                property color hoverColor: FLTheme.hover
                signal activated()
                width: 46
                height: buttons.height
                color: hover.containsMouse ? hoverColor : "transparent"
                Text {
                    id: label
                    anchors.centerIn: parent
                    color: FLTheme.text
                    font.pixelSize: 15
                }
                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: parent.activated()
                }
            }

            WinButton {
                glyph: "–" // en dash — minimize
                onActivated: chrome.minimize()
            }
            WinButton {
                glyph: chrome.maximized ? "❐" : "□" // restore / maximize
                onActivated: chrome.toggleMaximize()
            }
            WinButton {
                glyph: "✕" // ✕ close
                hoverColor: FLTheme.danger
                onActivated: chrome.requestClose()
            }
        }
    }

    // ── Resize grips ──────────────────────────────────────────────────────────
    // Thin MouseAreas on the window edges/corners hand a resize off to the compositor.
    // Hidden while maximized/fullscreen (no resize there).
    Item {
        anchors.fill: parent
        visible: !chrome.fullscreen && !chrome.maximized

        component EdgeGrip: MouseArea {
            property int edges: 0
            hoverEnabled: true
            onPressed: chrome.beginResize(edges)
        }

        EdgeGrip { // left
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: root.grip
            edges: Qt.LeftEdge
            cursorShape: Qt.SizeHorCursor
        }
        EdgeGrip { // right
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            width: root.grip
            edges: Qt.RightEdge
            cursorShape: Qt.SizeHorCursor
        }
        EdgeGrip { // bottom
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: root.grip
            edges: Qt.BottomEdge
            cursorShape: Qt.SizeVerCursor
        }
        EdgeGrip { // top
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: root.grip
            edges: Qt.TopEdge
            cursorShape: Qt.SizeVerCursor
        }
        EdgeGrip { // top-left
            anchors { left: parent.left; top: parent.top }
            width: root.grip; height: root.grip
            edges: Qt.TopEdge | Qt.LeftEdge
            cursorShape: Qt.SizeFDiagCursor
        }
        EdgeGrip { // top-right
            anchors { right: parent.right; top: parent.top }
            width: root.grip; height: root.grip
            edges: Qt.TopEdge | Qt.RightEdge
            cursorShape: Qt.SizeBDiagCursor
        }
        EdgeGrip { // bottom-left
            anchors { left: parent.left; bottom: parent.bottom }
            width: root.grip; height: root.grip
            edges: Qt.BottomEdge | Qt.LeftEdge
            cursorShape: Qt.SizeBDiagCursor
        }
        EdgeGrip { // bottom-right
            anchors { right: parent.right; bottom: parent.bottom }
            width: root.grip; height: root.grip
            edges: Qt.BottomEdge | Qt.RightEdge
            cursorShape: Qt.SizeFDiagCursor
        }
    }
}
