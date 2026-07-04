#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>

class QQuickWindow;

// View-model backing the fallback in-app title bar (FLTitleBar.qml). It is created only
// when Qt draws no native decorations for the window — i.e. a Vulkan-RHI window on
// Wayland (see QtAppWindow's needsCustomChrome). It exposes the window title and the
// window-management actions a title bar needs, delegating to the owned QQuickWindow.
// GUI-thread only; owned by QtAppWindow, which outlives it.
class WindowChrome : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(bool maximized READ maximized NOTIFY stateChanged)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY stateChanged)

public:
    explicit WindowChrome(QQuickWindow* window, QObject* parent = nullptr);

    [[nodiscard]] QString title() const
    {
        return title_;
    }

    [[nodiscard]] bool maximized() const;
    [[nodiscard]] bool fullscreen() const;

    // Kept in sync with the window/media title by QtAppWindow::SetTitle.
    void SetTitle(const QString& title);

    Q_INVOKABLE void minimize();
    Q_INVOKABLE void toggleMaximize();
    Q_INVOKABLE void requestClose();
    // Hand the drag/resize off to the compositor via the QWindow system-move/resize
    // requests — the only way a client-side-decorated Wayland window can be moved/resized.
    Q_INVOKABLE void beginMove();
    Q_INVOKABLE void beginResize(int edges); // Qt::Edges bitmask (Qt.TopEdge | Qt.LeftEdge | ...)

signals:
    void titleChanged();
    void stateChanged();

private:
    QQuickWindow* window_;
    QString title_;
};
