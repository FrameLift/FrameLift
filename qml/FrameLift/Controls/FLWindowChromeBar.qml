pragma ComponentBehavior: Bound

import QtQuick

// Presentational fallback window chrome: a top title-bar strip (title + minimize /
// maximize-restore / close buttons) and thin edge/corner resize grips, laid over the whole
// window at a high z with a transparent interior so clicks fall through to the content
// beneath. It carries no window logic of its own — state comes in via the title/maximized/
// fullscreen properties and actions go out via signals, so it can back both the main window
// (FLTitleBar, delegating to the C++ WindowChrome) and plugin windows (FLWindow, delegating
// to the QQuickWindow directly). Shown only when Qt draws no native decorations
// (Vulkan-RHI on Wayland).
Item {
    id: root

    readonly property int barHeight: 32
    readonly property int grip: 6

    // Window state, driven by the owner.
    property string title: ""
    property bool maximized: false
    property bool fullscreen: false

    // Actions the owner performs on the window.
    signal minimizeRequested()
    signal maximizeToggleRequested()
    signal closeRequested()
    signal moveRequested()
    signal resizeRequested(int edges)

    // ── Title bar strip ───────────────────────────────────────────────────────
    Rectangle {
        id: bar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: root.barHeight
        visible: !root.fullscreen
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
                    root.moveRequested();
                }
            }
            onDoubleClicked: root.maximizeToggleRequested()
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.right: buttons.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: root.title.length > 0 ? root.title : "FrameLift"
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
                onActivated: root.minimizeRequested()
            }
            WinButton {
                glyph: root.maximized ? "❐" : "□" // restore / maximize
                onActivated: root.maximizeToggleRequested()
            }
            WinButton {
                glyph: "✕" // ✕ close
                hoverColor: FLTheme.danger
                onActivated: root.closeRequested()
            }
        }
    }

    // ── Resize grips ──────────────────────────────────────────────────────────
    // Thin MouseAreas on the window edges/corners hand a resize off to the compositor.
    // Hidden while maximized/fullscreen (no resize there).
    Item {
        anchors.fill: parent
        visible: !root.fullscreen && !root.maximized

        component EdgeGrip: MouseArea {
            property int edges: 0
            hoverEnabled: true
            onPressed: root.resizeRequested(edges)
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
