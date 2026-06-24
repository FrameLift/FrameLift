#pragma once

// Thin seam over Windows toast notifications, used here only for playback-error
// alerts. Toasts are reached through the classic Win32/COM ABI of
// Windows.UI.Notifications (RoGetActivationFactory + IToastNotification*), not the
// C++/WinRT projection, which keeps this buildable under MinGW. The header is free
// of <windows.h>; all COM/WinRT-ABI calls live in ToastNotifier.cpp.
class ToastNotifier
{
public:
    ToastNotifier() noexcept;
    ~ToastNotifier();

    ToastNotifier(const ToastNotifier&) = delete;
    ToastNotifier& operator=(const ToastNotifier&) = delete;

    // True once a per-user Start-Menu shortcut carrying the AppUserModelID exists
    // (Windows requires this for an unpackaged app's toasts to appear).
    [[nodiscard]] bool IsRegistered() const noexcept;
    // Create the shortcut + HKCU AUMID entry. Returns true on success. User-driven
    // from the Settings page so we never write the shortcut without consent.
    bool Register() noexcept;

    // Show a two-line toast (UTF-8 title + body). No-op if the AUMID isn't
    // registered or the platform can't build the toast.
    void Notify(const char* title, const char* body) noexcept;

    // Convenience: a "Playback error" toast for the given file (UTF-8 path/name).
    void NotifyError(const char* file) noexcept;

private:
    bool roInitialized_ = false; // true ⇒ this object owns an RoInitialize to balance
};
