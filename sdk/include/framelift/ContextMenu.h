#pragma once

class Hotkeys;

class ContextMenu
{
public:
    static constexpr const char* InterfaceId = "framelift.ContextMenu";
    virtual ~ContextMenu() = default;

    virtual void AddItemRaw(
        const char* label, void (*action)(void* ud), void* ud, void (*cleanup)(void* ud) = nullptr
    ) noexcept = 0;

    virtual void AddItemWithHotkeyRaw(
        const char* label, const char* hotkeyName, void (*action)(void* ud), void* ud,
        void (*cleanup)(void* ud) = nullptr
    ) noexcept = 0;

    virtual void AddSeparator() noexcept = 0;
    virtual void Clear() noexcept = 0;
    virtual void SetKeys(Hotkeys* keys) noexcept = 0;

    virtual void AddSectionRaw(
        void (*builder)(ContextMenu& menu, void* ud), void* ud, void (*cleanup)(void* ud) = nullptr
    ) noexcept = 0;
};
