#pragma once

// Host-provided native file picker. Plugins cannot open OS dialogs directly,
// so the host exposes this thin service.
// Updates and side effects (history, playlist) are NOT implied — the caller
// decides what to do with the picked path.
class IFileDialog
{
public:
    static constexpr const char* InterfaceId = "framelift.IFileDialog";
    virtual ~IFileDialog() = default;

    // Opens the native open-file picker (video+image filters from settings).
    // cb(path, ok, ud) fires on the main thread; path is NUL-terminated and
    // valid only for the duration of the callback. ok=false when the user
    // cancels. cb/ud must stay valid until the callback fires.
    virtual void OpenFile(void (*cb)(const char* path, bool ok, void* ud), void* ud) noexcept = 0;
};
