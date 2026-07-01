#pragma once

#include <memory>

class QSystemTrayIcon;

// Thin seam over Qt's system-tray notification support, used here only for
// playback-error alerts. No Win32/COM headers or calls live in this module.
class ToastNotifier
{
public:
    ToastNotifier() noexcept;
    ~ToastNotifier();

    ToastNotifier(const ToastNotifier&) = delete;
    ToastNotifier& operator=(const ToastNotifier&) = delete;

    // True when Qt reports that tray-backed system messages can be shown.
    [[nodiscard]] bool IsAvailable() const noexcept;

    // Show a two-line notification (UTF-8 title + body). No-op if Qt reports
    // that tray-backed messages are unavailable.
    void Notify(const char* title, const char* body) noexcept;

    // Convenience: a "Playback error" toast for the given file (UTF-8 path/name).
    void NotifyError(const char* file) noexcept;

private:
    std::unique_ptr<QSystemTrayIcon> tray_;
};
