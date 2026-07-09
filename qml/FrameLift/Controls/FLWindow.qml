import QtQuick
import QtQuick.Controls
import QtQuick.Window

// A top-level window for plugins that transparently grows a fallback client-side title bar
// when Qt draws no native decorations (Vulkan-RHI on Wayland) — the same situation the main
// window handles with FLTitleBar. On platforms with real decorations it behaves as a plain
// ApplicationWindow. Put your UI inside it as you would any Window; it lands below the bar.
//
//     FLWindow { title: "…"; width: …; height: …
//         SomeContent { anchors.fill: parent }
//     }
ApplicationWindow {
    id: win

    // Caller UI flows into the content holder (inset below the bar); the chrome overlay and
    // holder are placed via contentData so they are NOT swept up by this redefined default.
    default property alias content: contentHolder.data

    // Mirror the host's needsCustomChrome decision (QtAppWindow: Vulkan backend on Wayland).
    // The graphics API is global (set once via QQuickWindow::setGraphicsApi), so reading it off
    // any item in the window is authoritative.
    readonly property bool _needsChrome: contentHolder.GraphicsInfo.api === GraphicsInfo.Vulkan
                                         && Qt.platform.pluginName.toLowerCase().indexOf("wayland") >= 0

    // Only opt out of the (broken) native CSD when we supply our own bar.
    flags: _needsChrome ? (Qt.Window | Qt.FramelessWindowHint) : Qt.Window

    contentData: [
        Item {
            id: contentHolder
            anchors.fill: parent
            anchors.topMargin: (win._needsChrome && win.visibility !== Window.FullScreen)
                               ? chromeBar.barHeight : 0
        },

        FLWindowChromeBar {
            id: chromeBar
            anchors.fill: parent
            z: 10000
            visible: win._needsChrome

            title: win.title
            maximized: win.visibility === Window.Maximized
            fullscreen: win.visibility === Window.FullScreen

            onMinimizeRequested: win.showMinimized()
            onMaximizeToggleRequested: win.visibility === Window.Maximized ? win.showNormal()
                                                                           : win.showMaximized()
            onCloseRequested: win.close()
            onMoveRequested: win.startSystemMove()
            onResizeRequested: (edges) => win.startSystemResize(edges)
        }
    ]
}
