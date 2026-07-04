#include "WindowChrome.h"

#include <QtQuick/QQuickWindow>

WindowChrome::WindowChrome(QQuickWindow* window, QObject* parent) : QObject(parent), window_(window)
{
    if (window_)
    {
        // Maximize / fullscreen changes flip the button glyphs and title-bar visibility.
        connect(
            window_, &QWindow::visibilityChanged, this,
            [this]
            {
                emit stateChanged();
            }
        );
    }
}

bool WindowChrome::maximized() const
{
    return window_ && window_->visibility() == QWindow::Maximized;
}

bool WindowChrome::fullscreen() const
{
    return window_ && window_->visibility() == QWindow::FullScreen;
}

void WindowChrome::SetTitle(const QString& title)
{
    if (title == title_)
    {
        return;
    }
    title_ = title;
    emit titleChanged();
}

void WindowChrome::minimize()
{
    if (window_)
    {
        window_->showMinimized();
    }
}

void WindowChrome::toggleMaximize()
{
    if (!window_)
    {
        return;
    }
    // Mirror QtAppWindow::SetFullscreen's Windowed toggle, for the maximize button.
    window_->setVisibility(window_->visibility() == QWindow::Maximized ? QWindow::Windowed : QWindow::Maximized);
}

void WindowChrome::requestClose()
{
    if (window_)
    {
        // Routes through the QEvent::Close → AppEventType::Quit path in QtAppWindow's
        // event filter, so the normal shutdown flow runs (setQuitOnLastWindowClosed is off).
        window_->close();
    }
}

void WindowChrome::beginMove()
{
    if (window_)
    {
        window_->startSystemMove();
    }
}

void WindowChrome::beginResize(int edges)
{
    if (window_)
    {
        window_->startSystemResize(Qt::Edges(edges));
    }
}
