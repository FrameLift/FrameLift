#include "QtAppWindow.h"
#include "VideoItem.h"
#include "util.h"

#include <framelift/Log.h>

#include <QtCore/QDir>
#include <QtCore/QEvent>
#include <QtCore/QStandardPaths>
#include <QtCore/QString>
#include <QtGui/QGuiApplication>
#include <QtGui/QIcon>
#include <QtGui/QImage>
#include <QtGui/QKeyEvent>
#include <QtGui/QScreen>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <cstring>
#include <exception>

namespace
{
// Shared buf/cap contract used by the path getters: copy `s` into buf (truncating to
// cap-1 + NUL) and return the untruncated length. buf may be null to query length.
int CopyOut(const std::string& s, char* buf, int cap) noexcept
{
    const int len = static_cast<int>(s.size());
    if (buf && cap > 0)
    {
        const int n = len < cap - 1 ? len : cap - 1;
        std::memcpy(buf, s.data(), static_cast<std::size_t>(n));
        buf[n] = '\0';
    }
    return len;
}

// Qt::Key -> framelift Key. The public key contract mirrors Qt's numeric key
// values, so the Qt window layer can preserve the incoming key code directly.
Key TranslateKey(int qtKey)
{
    switch (qtKey)
    {
    case Qt::Key_Return:
        return Keys::Return;
    case Qt::Key_Enter:
        return Keys::Enter;
    case Qt::Key_Escape:
        return Keys::Escape;
    case Qt::Key_Tab:
        return Keys::Tab;
    case Qt::Key_Backspace:
        return Keys::Backspace;
    case Qt::Key_Space:
        return Keys::Space;
    case Qt::Key_Delete:
        return Keys::Delete;
    case Qt::Key_Insert:
        return Keys::Insert;
    case Qt::Key_Home:
        return Keys::Home;
    case Qt::Key_End:
        return Keys::End;
    case Qt::Key_PageUp:
        return Keys::PageUp;
    case Qt::Key_PageDown:
        return Keys::PageDown;
    case Qt::Key_Left:
        return Keys::Left;
    case Qt::Key_Right:
        return Keys::Right;
    case Qt::Key_Up:
        return Keys::Up;
    case Qt::Key_Down:
        return Keys::Down;
    case Qt::Key_F1:
        return Keys::F1;
    case Qt::Key_F2:
        return Keys::F2;
    case Qt::Key_F3:
        return Keys::F3;
    case Qt::Key_F4:
        return Keys::F4;
    case Qt::Key_F5:
        return Keys::F5;
    case Qt::Key_F6:
        return Keys::F6;
    case Qt::Key_F7:
        return Keys::F7;
    case Qt::Key_F8:
        return Keys::F8;
    case Qt::Key_F9:
        return Keys::F9;
    case Qt::Key_F10:
        return Keys::F10;
    case Qt::Key_F11:
        return Keys::F11;
    case Qt::Key_F12:
        return Keys::F12;
    default:
        break;
    }
    return qtKey > 0 ? static_cast<Key>(qtKey) : Keys::Unknown;
}

Mod TranslateMods(Qt::KeyboardModifiers m)
{
    uint32_t r = 0;
    if (m & Qt::ControlModifier)
    {
        r |= static_cast<uint32_t>(Mod::Ctrl);
    }
    if (m & Qt::ShiftModifier)
    {
        r |= static_cast<uint32_t>(Mod::Shift);
    }
    if (m & Qt::AltModifier)
    {
        r |= static_cast<uint32_t>(Mod::Alt);
    }
    return static_cast<Mod>(r);
}
} // namespace

// ── Constructor / Destructor ──────────────────────────────────────────────────

QtAppWindow::QtAppWindow(const char* title, int width, int height, GraphicsApi api) : title_(title ? title : "")
{
    backend_ = CreateGraphicsBackend(api);

    QQuickWindow::setGraphicsApi(
        std::strcmp(backend_->Name(), "Vulkan") == 0 ? QSGRendererInterface::Vulkan : QSGRendererInterface::OpenGL
    );

    // Created hidden (shown on the first painted frame in RunEventLoop) so the user never
    // sees an unpainted black framebuffer while settings/plugins load. The GL RHI and the
    // "basic" render loop are forced in main() before any QQuickWindow exists.
    //
    // Apply geometry/title/flags BEFORE ConfigureQtWindow: the Vulkan backend realizes the
    // native platform window early there (it needs a live surface for supportsPresent), and
    // a window created without its final geometry/flags loses its decorations. QQuickWindow
    // normally sizes its content item when the native window is exposed, but plugin roots
    // are created before show(), so seed the same geometry now instead of constructing every
    // QML surface at 0x0 for the first frame.
    const auto applyWindowProps = [&](QQuickWindow* w)
    {
        w->setFlags(Qt::Window);
        w->setTitle(QString::fromUtf8(title_.c_str()));
        w->resize(width, height);
        w->setColor(Qt::black);
        w->contentItem()->setSize(QSizeF(width, height));
    };

    window_ = new QQuickWindow();
    applyWindowProps(window_);
    try
    {
        backend_->ConfigureQtWindow(window_);
    }
    catch (const std::exception& e)
    {
        if (api != GraphicsApi::Auto || std::strcmp(backend_->Name(), "Vulkan") != 0)
        {
            delete window_;
            window_ = nullptr;
            throw;
        }
        Log::Warn("Vulkan device/window setup failed in auto mode ({}); using OpenGL.", e.what());
        delete window_;
        window_ = nullptr;
        backend_.reset();
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
        backend_ = CreateGraphicsBackend(GraphicsApi::OpenGL);
        window_ = new QQuickWindow();
        applyWindowProps(window_);
        backend_->ConfigureQtWindow(window_);
    }

    videoItem_ = new VideoItem(window_->contentItem());
    videoItem_->setZ(0);
    qmlCompositor_ = std::make_unique<QmlCompositor>(window_->contentItem());
    SyncVideoItemSize();
    connect(
        window_, &QQuickWindow::widthChanged, this,
        [this]
        {
            SyncVideoItemSize();
        }
    );
    connect(
        window_, &QQuickWindow::heightChanged, this,
        [this]
        {
            SyncVideoItemSize();
        }
    );
    connect(
        window_, &QQuickWindow::sceneGraphInvalidated, this,
        [this]
        {
            if (graphicsInvalidatedHandler_)
            {
                graphicsInvalidatedHandler_();
            }
        },
        Qt::DirectConnection
    );

    // Worker-thread wakes → GUI-thread slots (queued). renderUpdate schedules a repaint;
    // playerWakeup drains media events (which may itself schedule a repaint).
    connect(
        this, &QtAppWindow::renderUpdateRequested, this,
        [this]
        {
            if (window_)
            {
                window_->update();
            }
        },
        Qt::QueuedConnection
    );
    connect(
        this, &QtAppWindow::playerWakeupRequested, this,
        [this]
        {
            if (playerWakeupHandler_)
            {
                playerWakeupHandler_();
            }
        },
        Qt::QueuedConnection
    );

    window_->installEventFilter(this);
}

QtAppWindow::~QtAppWindow()
{
    qmlCompositor_.reset();
    delete window_; // deletes the content-item tree (videoItem_) with it
    window_ = nullptr;
    if (backend_)
    {
        backend_->Shutdown();
    }
}

void QtAppWindow::SyncVideoItemSize()
{
    if (videoItem_ && window_)
    {
        videoItem_->setPosition(QPointF(0, 0));
        videoItem_->setSize(QSizeF(window_->width(), window_->height()));
    }
}

// ── Host wiring ───────────────────────────────────────────────────────────────

void QtAppWindow::SetEventSink(std::function<void(const AppEvent&)> sink)
{
    eventSink_ = std::move(sink);
}

void QtAppWindow::SetPlayerWakeupHandler(std::function<void()> handler)
{
    playerWakeupHandler_ = std::move(handler);
}

void QtAppWindow::SetGraphicsInvalidatedHandler(std::function<void()> handler)
{
    graphicsInvalidatedHandler_ = std::move(handler);
}

void QtAppWindow::SetVideoRenderCallbacks(
    std::function<void(int, int)> prepareCb, std::function<void(int, int)> renderCb
)
{
    if (videoItem_)
    {
        videoItem_->SetRenderCallbacks(std::move(prepareCb), std::move(renderCb));
    }
}

void QtAppWindow::SetPluginViews(std::vector<QmlViewSpec> views)
{
    if (qmlCompositor_)
    {
        qmlCompositor_->Load(std::move(views));
    }
}

int QtAppWindow::RunEventLoop()
{
    if (window_)
    {
        window_->show();
    }
    return QGuiApplication::exec();
}

// ── Window ────────────────────────────────────────────────────────────────────

void QtAppWindow::ResizeToVideo(int videoW, int videoH, float maxDisplayRatio) noexcept
{
    if (IsFullscreen() || videoW <= 0 || videoH <= 0)
    {
        return;
    }
    const QScreen* screen = window_ ? window_->screen() : QGuiApplication::primaryScreen();
    if (!screen)
    {
        return;
    }
    const QRect usable = screen->availableGeometry();
    const int maxW = static_cast<int>(static_cast<float>(usable.width()) * maxDisplayRatio);
    const int maxH = static_cast<int>(static_cast<float>(usable.height()) * maxDisplayRatio);
    const WindowSize fit = FitWithinAspect(videoW, videoH, maxW, maxH);
    if (window_)
    {
        window_->resize(fit.w, fit.h);
    }
}

bool QtAppWindow::IsFullscreen() const noexcept
{
    return window_ && window_->visibility() == QWindow::FullScreen;
}

void QtAppWindow::SetFullscreen(bool fs) noexcept
{
    if (window_)
    {
        window_->setVisibility(fs ? QWindow::FullScreen : QWindow::Windowed);
    }
}

void* QtAppWindow::GetNativeHandle() const noexcept
{
    return window_;
}

void* QtAppWindow::GetWin32Hwnd() const noexcept
{
#ifdef Q_OS_WIN
    return window_ ? reinterpret_cast<void*>(window_->winId()) : nullptr;
#else
    return nullptr;
#endif
}

bool QtAppWindow::SetWindowIcon(const char* path) noexcept
{
    if (!window_ || !path)
    {
        return false;
    }
    QImage img(QString::fromUtf8(path));
    if (img.isNull())
    {
        return false;
    }
    window_->setIcon(QIcon(QPixmap::fromImage(img)));
    return true;
}

void QtAppWindow::SetTitle(const char* title) noexcept
{
    title_ = title ? title : "";
    if (window_)
    {
        window_->setTitle(QString::fromUtf8(title_.c_str()));
    }
}

// ── Graphics backend / presentation ─────────────────────────────────────────────

void* QtAppWindow::GetGraphicsBackend() const noexcept
{
    return backend_.get();
}

// ── Events ────────────────────────────────────────────────────────────────────

bool QtAppWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != window_ || !eventSink_)
    {
        return QObject::eventFilter(watched, event);
    }

    AppEvent out{};
    bool deliver = true;

    switch (event->type())
    {
    case QEvent::Close:
        out.type = AppEventType::Quit;
        break;
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        const auto* ke = static_cast<QKeyEvent*>(event);
        // A held key emits a stream of auto-repeat KeyPress/KeyRelease events. Drop the
        // synthetic releases (no KeyUp until the key is physically let go), but forward
        // the repeats as KeyDown tagged repeat=true so consumers can opt in to held-key
        // repetition (seek, list nav) while toggles still ignore them.
        if (ke->isAutoRepeat() && event->type() == QEvent::KeyRelease)
        {
            deliver = false;
            break;
        }
        out.type = event->type() == QEvent::KeyPress ? AppEventType::KeyDown : AppEventType::KeyUp;
        out.key = {TranslateKey(ke->key()), TranslateMods(ke->modifiers()), ke->isAutoRepeat()};
        break;
    }
    case QEvent::MouseButtonPress:
        out.type = AppEventType::MouseButtonDown;
        break;
    case QEvent::MouseMove:
        out.type = AppEventType::MouseMotion;
        break;
    case QEvent::Wheel:
        out.type = AppEventType::MouseWheel;
        break;
    default:
        deliver = false;
        break;
    }

    if (deliver)
    {
        eventSink_(out);
        // Any input may have changed visible state (fullscreen toggle, etc.); repaint.
        if (window_ && out.type != AppEventType::Quit)
        {
            window_->update();
        }
    }
    return QObject::eventFilter(watched, event);
}

uint32_t QtAppWindow::RegisterCustomEventType() noexcept
{
    return nextCustomType_++;
}

void QtAppWindow::PushCustomEvent(uint32_t eventType, void* data1) noexcept
{
    // Marshal onto the GUI thread, then hand to the sink as a Custom AppEvent.
    QMetaObject::invokeMethod(
        this,
        [this, eventType, data1]
        {
            if (eventSink_)
            {
                AppEvent e{};
                e.type = AppEventType::Custom;
                e.custom = {eventType, data1};
                eventSink_(e);
            }
            if (window_)
            {
                window_->update();
            }
        },
        Qt::QueuedConnection
    );
}

void QtAppWindow::PushRenderUpdate() noexcept
{
    emit renderUpdateRequested();
}

void QtAppWindow::PushPlayerWakeup() noexcept
{
    emit playerWakeupRequested();
}

void QtAppWindow::PushQuitEvent() noexcept
{
    QMetaObject::invokeMethod(
        this,
        []
        {
            QGuiApplication::quit();
        },
        Qt::QueuedConnection
    );
}

void QtAppWindow::SetFrameDirty(bool videoDirty, bool uiDirty) noexcept
{
    if (backend_)
    {
        backend_->SetFrameDirty(videoDirty, uiDirty);
    }
}

// ── Platform paths ──────────────────────────────────────────────────────────────

int QtAppWindow::ResolvePrefPath(const char* org, const char* app, char* buf, int cap) noexcept
{
    // Keep FrameLift's historical pref-dir layout: <user config>/<org>/<app>/. Built from the generic
    // config location so it works before QGuiApplication org/app names are set. Always
    // ends with a separator.
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    QDir dir(base);
    if (org && *org)
    {
        dir = QDir(dir.filePath(QString::fromUtf8(org)));
    }
    if (app && *app)
    {
        dir = QDir(dir.filePath(QString::fromUtf8(app)));
    }
    dir.mkpath(".");
    QString path = QDir::toNativeSeparators(dir.absolutePath());
    if (!path.endsWith(QDir::separator()))
    {
        path += QDir::separator();
    }
    return CopyOut(path.toStdString(), buf, cap);
}

int QtAppWindow::GetPrefPath(const char* org, const char* app, char* buf, int cap) const noexcept
{
    return ResolvePrefPath(org, app, buf, cap);
}

int QtAppWindow::GetBasePath(char* buf, int cap) const noexcept
{
    QString path;
    if (QCoreApplication::instance())
    {
        path = QCoreApplication::applicationDirPath();
    }
    else
    {
        path = QDir::currentPath();
    }
    path = QDir::toNativeSeparators(path);
    if (!path.endsWith(QDir::separator()))
    {
        path += QDir::separator();
    }
    return CopyOut(path.toStdString(), buf, cap);
}
