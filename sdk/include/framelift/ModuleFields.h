#pragma once
#include <framelift/IModuleSettings.h>
#include <functional>
#include <span>
#include <string>

// ── Declarative plugin field/keybind descriptors ──────────────────────────────
// Plugin-side only (compiled into the plugin, nothing crosses the ABI).
// ModuleBase consumes these tables in its default hook implementations, so a
// plugin can describe its persisted settings and keybinds as data instead of
// hand-writing Load/SaveSettings and the load→register→bind keybind dance.

namespace framelift
{

// One INI-persisted member field. Construct with {key, &member, default};
// the pointer must outlive the plugin (point at a member).
class SettingsField
{
public:
    enum class Type : unsigned char
    {
        Bool,
        Int,
        Float,
        String
    };

    SettingsField(const char* key, bool* p, const bool def) noexcept
        : key_(key), type_(Type::Bool), ptr_(p), defBool_(def)
    {
    }

    SettingsField(const char* key, int* p, const int def) noexcept : key_(key), type_(Type::Int), ptr_(p), defInt_(def)
    {
    }

    SettingsField(const char* key, float* p, const float def) noexcept
        : key_(key), type_(Type::Float), ptr_(p), defFloat_(def)
    {
    }

    SettingsField(const char* key, std::string* p, const char* def) noexcept
        : key_(key), type_(Type::String), ptr_(p), defStr_(def)
    {
    }

    // *member = ps.Get<type>(key, default)
    void Load(const IModuleSettings& ps) const
    {
        switch (type_)
        {
        case Type::Bool:
            *static_cast<bool*>(ptr_) = ps.GetBool(key_, defBool_);
            break;
        case Type::Int:
            *static_cast<int*>(ptr_) = ps.GetInt(key_, defInt_);
            break;
        case Type::Float:
            *static_cast<float*>(ptr_) = ps.GetFloat(key_, defFloat_);
            break;
        case Type::String:
            *static_cast<std::string*>(ptr_) = ps.GetString(key_, defStr_);
            break;
        }
    }

    // ps.Set<type>(key, *member)
    void Save(IModuleSettings& ps) const
    {
        switch (type_)
        {
        case Type::Bool:
            ps.SetBool(key_, *static_cast<const bool*>(ptr_));
            break;
        case Type::Int:
            ps.SetInt(key_, *static_cast<const int*>(ptr_));
            break;
        case Type::Float:
            ps.SetFloat(key_, *static_cast<const float*>(ptr_));
            break;
        case Type::String:
            ps.SetString(key_, static_cast<const std::string*>(ptr_)->c_str());
            break;
        }
    }

    [[nodiscard]] const char* Key() const noexcept
    {
        return key_;
    }

    [[nodiscard]] Type FieldType() const noexcept
    {
        return type_;
    }

    [[nodiscard]] int TypeId() const noexcept
    {
        return static_cast<int>(type_);
    }

    [[nodiscard]] std::string DefaultValue() const
    {
        switch (type_)
        {
        case Type::Bool:
            return defBool_ ? "1" : "0";
        case Type::Int:
            return std::to_string(defInt_);
        case Type::Float:
            return std::to_string(defFloat_);
        case Type::String:
            return defStr_ ? defStr_ : "";
        }
        return {};
    }

    [[nodiscard]] std::string CurrentValue() const
    {
        switch (type_)
        {
        case Type::Bool:
            return *static_cast<const bool*>(ptr_) ? "1" : "0";
        case Type::Int:
            return std::to_string(*static_cast<const int*>(ptr_));
        case Type::Float:
            return std::to_string(*static_cast<const float*>(ptr_));
        case Type::String:
            return *static_cast<const std::string*>(ptr_);
        }
        return {};
    }

    void SetFromString(const char* value) const
    {
        const std::string v = value ? value : "";
        switch (type_)
        {
        case Type::Bool:
            *static_cast<bool*>(ptr_) = (v == "1" || v == "true" || v == "on");
            break;
        case Type::Int:
            *static_cast<int*>(ptr_) = std::stoi(v);
            break;
        case Type::Float:
            *static_cast<float*>(ptr_) = std::stof(v);
            break;
        case Type::String:
            *static_cast<std::string*>(ptr_) = v;
            break;
        }
    }

private:
    const char* key_;
    Type type_;
    void* ptr_;
    bool defBool_ = false;
    int defInt_ = 0;
    float defFloat_ = 0.f;
    const char* defStr_ = "";
};

inline void LoadFields(const IModuleSettings& ps, const std::span<const SettingsField> fields)
{
    for (const auto& f : fields)
    {
        f.Load(ps);
    }
}

inline void SaveFields(IModuleSettings& ps, const std::span<const SettingsField> fields)
{
    for (const auto& f : fields)
    {
        f.Save(ps);
    }
}

// One keybind declaration. Drives all four legs handled by ModuleBase: load
// from the shared [keybinds] section (namespaced "<Plugin>.<action>"), default
// seeding, RegisterKeybindEntry (Settings → Keybinds row), and the Hotkeys Bind.
struct Keybind
{
    const char* label;             // Settings → Keybinds row label
    const char* action;            // Hotkeys action name, e.g. "togglePlaylist"
    std::string* storage;          // member holding the current bind string
    const char* def;               // default bind list, e.g. "L" or "Ctrl+R"
    std::function<void()> onPress; // action to bind; empty = registered but not bound
};

} // namespace framelift
