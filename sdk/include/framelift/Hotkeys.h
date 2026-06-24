#pragma once
#include <framelift/AppEvent.h>

// Serialise a Key+Mod pair to a human-readable string (e.g. "Ctrl+Shift+F").
// Returns "?" for unrecognised keys. Implemented by the host (src/HotkeysImpl.cpp).
const char* KeyBindToString(Key key, Mod mods, char* buf, int cap) noexcept;

// Maps (key, modifier) pairs to zero-argument callbacks.
// Named bindings can be rebound at runtime via Rebind().
// All virtual methods use a stable C-compatible ABI — no std::function.
// Use the framelift::Bind() helpers in HotkeyHelpers.h for lambda/std::function bindings.
class Hotkeys
{
public:
    static constexpr const char* InterfaceId = "framelift.Hotkeys";
    virtual ~Hotkeys() = default;

    // ── Raw bindings (fn-ptr + ud) ────────────────────────────────────────────
    // cleanup(ud) is called when Clear() is called (plugin unload).
    virtual void BindRaw(
        Key key, Mod mods, void (*action)(void* ud), void* ud, void (*cleanup)(void* ud) = nullptr
    ) noexcept = 0;

    // Named binding: parseable bind-list string (e.g. "Ctrl+F;F2").
    // The first entry is named (rebindable); the rest are unnamed aliases.
    virtual void BindNamedRaw(
        const char* name, const char* bindList, void (*action)(void* ud), void* ud, void (*cleanup)(void* ud) = nullptr
    ) noexcept = 0;

    virtual bool Rebind(const char* name, Key newKey, Mod newMods) noexcept = 0;
    virtual void Unbind(const char* name) noexcept = 0;

    // Replace every key bound to `name` with the parsed bind-list (e.g. "Ctrl+F;F2"),
    // reusing the existing action callback so the caller need not own it. The first
    // entry stays the named/rebindable binding; the rest are aliases for the same
    // action. No-op if `name` isn't currently bound. Empty list ⇒ same as Unbind().
    virtual void RebindList(const char* name, const char* bindList) noexcept = 0;

    // Returns a human-readable shortcut string for a named binding.
    // Writes to buf[0..cap-1]+NUL; returns length excl. NUL (0 if not found).
    virtual int GetShortcutString(const char* name, char* buf, int cap) const noexcept = 0;

    // Drop all bindings (calls cleanup callbacks). Call before FreeLibrary.
    virtual void Clear() noexcept = 0;

    // Dispatch — call from the event loop with every KeyDown event.
    virtual bool Handle(const AppEvent& e) const noexcept = 0;
};
