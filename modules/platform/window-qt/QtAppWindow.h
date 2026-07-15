#pragma once

#include <QtCore/QObject>
#include <framelift/platform/IAppWindow.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "IGraphicsBackend.h"
#include "QmlCompositor.h"

class QQuickWindow;
class QQuickItem;
class QQmlEngine;
class QEvent;
class VideoItem;
class WindowChrome;

// Concrete window backed by Qt6 (Qt Quick). Implements the plugin-visible window/event
// interfaces (IAppWindow / IEventPump) and, host-only, pref/base path resolution plus
// graphics-backend access. Owns one QQuickWindow with a VideoItem (QQuickItem →
// QSGRenderNode) that draws the video inside the scene-graph render pass, delegating
// the draw to an IGraphicsBackend that adopts Qt's scene-graph graphics context.
//
// This file, VideoItem, and VideoRenderNode are the only window-layer files that may
// #include <QtGui/...>/<QtQuick/...>. QObject is the first base so AUTOMOC can wire the
// signals used to wake the demand-driven loop from player worker threads.
//
// Loop model (Qt owns the loop): QGuiApplication::exec() runs the event loop. Input
// arrives via an event filter on the QQuickWindow → AppEvent → the host event sink
// (App::Dispatch) synchronously on the GUI thread. Player worker threads call the Push*
// methods, which emit queued signals delivered on the GUI thread, where they set the
// dirty state and call QQuickWindow::update() (requestUpdate) — the only thread-legal way
// to schedule a repaint.
class QtAppWindow final : public QObject, public IAppWindow, public IEventPump
{
    Q_OBJECT

public:
    QtAppWindow(const char* title, int width, int height, GraphicsApi api = GraphicsApi::OpenGL);
    ~QtAppWindow() override;

    // Resolve the user pref dir before the window exists, so the host can load settings
    // and choose the graphics backend up front. Backed by QStandardPaths. Same buf/cap
    // contract as GetPrefPath; pass buf=nullptr to query the length.
    static int ResolvePrefPath(const char* org, const char* app, char* buf, int cap) noexcept;

    // ── Host wiring (set by App after construction; not on any ABI interface) ──
    // Synchronous input/event sink (App::Dispatch), called on the GUI thread.
    void SetEventSink(std::function<void(const AppEvent&)> sink);
    // Drain handler invoked (GUI thread) when a player worker posts a wakeup.
    void SetPlayerWakeupHandler(std::function<void()> handler);
    void SetGraphicsInvalidatedHandler(std::function<void()> handler);
    // Host video draw, forwarded to an invalidatable scene-graph binding (App's
    // player_->RenderFrame). Passing two empty callbacks detaches existing nodes too.
    // The callbacks receive the video target rect in device pixels (top-left origin);
    // the origin is nonzero when the fallback title bar insets the video item.
    void SetVideoRenderCallbacks(
        std::function<void(int fbX, int fbY, int fbW, int fbH)> prepareCb,
        std::function<void(int fbX, int fbY, int fbW, int fbH)> renderCb
    );
    void SetPluginViews(std::vector<QmlViewSpec> views);
    // Request a native Qt close so launch tests exercise QEvent::Close and the same
    // host shutdown route as a user closing the window.
    void RequestClose();
    // End Qt's event loop after App has completed the plugin shutdown sequence.
    void ExitEventLoop();
    // Show the window (created hidden) and run Qt's event loop until quit. Returns the
    // QGuiApplication::exec() exit code.
    int RunEventLoop();

    // ── IAppWindow ──
    void ResizeToVideo(int videoW, int videoH, float maxDisplayRatio) noexcept;
    [[nodiscard]] bool IsFullscreen() const noexcept override;
    void SetFullscreen(bool fs) noexcept override;
    void SetTitle(const char* title) noexcept override;

    // ── IEventPump ──
    [[nodiscard]] uint32_t RegisterCustomEventType() noexcept override;
    void PushCustomEvent(uint32_t eventType, void* data1) noexcept override;
    void PushQuitEvent() noexcept override;

    // ── Host-only surface (not on any ABI interface) ──
    [[nodiscard]] void* GetNativeHandle() const noexcept;
    [[nodiscard]] void* GetGraphicsBackend() const noexcept;
    bool SetWindowIcon(const char* path) noexcept;
    void PushRenderUpdate() noexcept;
    void PushPlayerWakeup() noexcept;
    int GetPrefPath(const char* org, const char* app, char* buf, int cap) const noexcept;
    int GetBasePath(char* buf, int cap) const noexcept;
    // Native Win32 HWND (as void*), or nullptr off Windows / before the window exists.
    [[nodiscard]] void* GetWin32Hwnd() const noexcept;

signals:
    // Emitted from player worker threads; a Qt::QueuedConnection delivers them on the GUI
    // thread where QQuickWindow::update() is legal.
    void renderUpdateRequested();
    void playerWakeupRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QQuickWindow* window_ = nullptr; // owned via Qt parent/ownership
    VideoItem* videoItem_ = nullptr; // child of window_->contentItem()
    std::unique_ptr<QmlCompositor> qmlCompositor_;
    std::unique_ptr<IGraphicsBackend> backend_;
    std::string title_;

    // Fallback client-side window chrome, active only when Qt draws no native decorations
    // (Vulkan-RHI on Wayland). When active the window is frameless, FLTitleBar.qml is
    // composited on top, and the video + plugin surfaces are inset below it by
    // chromeInset_ logical pixels. All zero/false on every other platform+backend.
    bool needsCustomChrome_ = false;
    qreal barHeight_ = 0.0;   // title-bar strip height (from FLTitleBar.qml), 0 when inactive
    qreal chromeInset_ = 0.0; // video top inset: barHeight_ windowed, 0 in fullscreen
    std::unique_ptr<WindowChrome> chrome_;
    std::unique_ptr<QQmlEngine> chromeEngine_;
    QQuickItem* chromeItem_ = nullptr; // child of window_->contentItem()

    std::function<void(const AppEvent&)> eventSink_;
    std::function<void()> playerWakeupHandler_;
    std::function<void()> graphicsInvalidatedHandler_;

    // Custom-event payloads delivered via PushCustomEvent → a queued signal would need a
    // registered metatype; instead we keep a tiny monotonic counter for type ids only.
    uint32_t nextCustomType_ = 1;

    void SyncVideoItemSize();
    // Instantiate the fallback title bar (FLTitleBar.qml + WindowChrome) and set the
    // content inset. Called from the constructor only when needsCustomChrome_.
    void SetupCustomChrome();
};
