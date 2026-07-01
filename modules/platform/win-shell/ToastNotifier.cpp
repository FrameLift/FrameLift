#include "ToastNotifier.h"

#include <QtCore/QString>
#include <QtGui/QGuiApplication>
#include <QtGui/QIcon>
#include <QtGui/QWindow>
#include <QtWidgets/QSystemTrayIcon>

namespace
{
QString ToQString(const char* text)
{
    return QString::fromUtf8(text ? text : "");
}

QIcon ApplicationIcon()
{
    QIcon icon = QGuiApplication::windowIcon();
    if (!icon.isNull())
    {
        return icon;
    }
    for (QWindow* window : QGuiApplication::topLevelWindows())
    {
        if (window)
        {
            icon = window->icon();
            if (!icon.isNull())
            {
                return icon;
            }
        }
    }
    return {};
}
} // namespace

ToastNotifier::ToastNotifier() noexcept
{
    if (!QSystemTrayIcon::isSystemTrayAvailable() || !QSystemTrayIcon::supportsMessages())
    {
        return;
    }

    tray_ = std::make_unique<QSystemTrayIcon>();
    const QIcon icon = ApplicationIcon();
    if (!icon.isNull())
    {
        tray_->setIcon(icon);
    }
    tray_->setToolTip(QStringLiteral("FrameLift"));
    tray_->show();
}

ToastNotifier::~ToastNotifier() = default;

bool ToastNotifier::IsAvailable() const noexcept
{
    return tray_ && QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages();
}

void ToastNotifier::Notify(const char* title, const char* body) noexcept
{
    if (!IsAvailable())
    {
        return;
    }
    tray_->showMessage(ToQString(title), ToQString(body), QSystemTrayIcon::Information, 10000);
}

void ToastNotifier::NotifyError(const char* file) noexcept
{
    Notify("Playback error", (file && *file) ? file : "Failed to play file");
}
