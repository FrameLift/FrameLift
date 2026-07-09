import QtQuick

// Fallback client-side window chrome for the MAIN window, shown only when Qt draws no native
// decorations (Vulkan-RHI on Wayland). A thin adapter that binds the shared FLWindowChromeBar
// visuals to the `chrome` context property (a C++ WindowChrome) set up by QtAppWindow. The
// root fills the whole window at a high z; only the top bar strip and the thin edge grips
// carry MouseAreas, so the transparent interior lets clicks fall through to the video and
// plugin overlays beneath it.
// barHeight is inherited from FLWindowChromeBar; QtAppWindow reads it to size the
// video/overlay inset, so it stays part of this type's public surface.
FLWindowChromeBar {
    id: bar

    title: chrome.title
    maximized: chrome.maximized
    fullscreen: chrome.fullscreen

    onMinimizeRequested: chrome.minimize()
    onMaximizeToggleRequested: chrome.toggleMaximize()
    onCloseRequested: chrome.requestClose()
    onMoveRequested: chrome.beginMove()
    onResizeRequested: (edges) => chrome.beginResize(edges)
}
