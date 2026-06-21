#pragma once
#include <framelift/Abi.h>

// Per-plugin INI section accessor.
// Returned by IModuleContext::GetModuleSettings(); the host owns the storage.
// All methods are noexcept — safe to call from any plugin lifetime stage.
class IModuleSettings
{
public:
    static constexpr const char* InterfaceId = "framelift.IModuleSettings";
    virtual ~IModuleSettings() = default;

    // ── Typed getters ─────────────────────────────────────────────────────────
    // Return the stored value, or def if the key is absent.
    // GetString() returns a stable const char* into internal storage; valid until
    // the next SetString() call on the same key or the section is destroyed.
    [[nodiscard]] virtual const char* GetString(const char* key, const char* def = "") const noexcept = 0;
    [[nodiscard]] virtual int GetInt(const char* key, int def = 0) const noexcept = 0;
    [[nodiscard]] virtual float GetFloat(const char* key, float def = 0.f) const noexcept = 0;
    [[nodiscard]] virtual bool GetBool(const char* key, bool def = false) const noexcept = 0;

    // ── Typed setters (in-memory only; Save() persists) ──────────────────────
    virtual void SetString(const char* key, const char* value) noexcept = 0;
    virtual void SetInt(const char* key, int value) noexcept = 0;
    virtual void SetFloat(const char* key, float value) noexcept = 0;
    virtual void SetBool(const char* key, bool value) noexcept = 0;

    // ── Persistence ───────────────────────────────────────────────────────────
    virtual void Save() noexcept = 0;

    // Returns true if the section existed in the file during the last Load().
    [[nodiscard]] virtual bool WasLoaded() const noexcept = 0;

    // Returns the number of key-value pairs currently held in memory.
    [[nodiscard]] virtual int KeyCount() const noexcept = 0;
};
