#pragma once

// POD descriptor for one registered settings field, surfaced by EnumerateSettings.
// All pointers are valid only for the duration of the visit() call.
//   key          — "section.name" (e.g. "audio.defaultLanguage").
//   type         — value category: 0 bool, 1 int, 2 float, 3 string.
//   desc         — human-readable description, or nullptr.
//   defaultValue — the field's default, serialized as text (for "reset to default").
struct FrameLiftSettingDesc
{
    const char* key;
    int type;
    const char* desc;
    const char* defaultValue;
};

// Runtime field registered by a module. `getValue` returns the current value as
// text, and `setValue` updates and persists it through the owning module.
struct FrameLiftModuleSettingDesc
{
    const char* key;
    int type;
    const char* desc;
    const char* defaultValue;
    const char* (*getValue)(void* ud);
    void (*setValue)(void* ud, const char* value);
    void* ud;
};

// UI registration surface: settings pages, keybind entries, and field enumeration
// other plugins (SettingsMenu) render without compile-time knowledge of the source.
// A capability service — discover it with ctx.GetService<ISettingsRegistry>().
class ISettingsRegistry
{
public:
    static constexpr const char* InterfaceId = "framelift.ISettingsRegistry";
    virtual ~ISettingsRegistry() = default;

    // getStr(ud): returns a const char* to the current binding string (plugin-owned).
    // setStr(ud, val): updates the binding string in the plugin.
    virtual void RegisterKeybindEntry(
        const char* label, const char* actionName, const char* (*getStr)(void* ud),
        void (*setStr)(void* ud, const char* val), void* ud
    ) noexcept = 0;

    virtual void EnumerateKeybindEntries(
        void (*visit)(
            const char* label, const char* actionName, const char* (*getStr)(void* ud),
            void (*setStr)(void* ud, const char* val), void* ud, void* visitUd
        ),
        void* visitUd
    ) const noexcept = 0;

    // Visit every registered settings field in declaration order. The descriptor and
    // its strings are valid only for the duration of each visit() call.
    virtual void EnumerateSettings(
        void (*visit)(const FrameLiftSettingDesc* desc, void* visitUd), void* visitUd
    ) const noexcept = 0;

    virtual void RegisterModuleSetting(const FrameLiftModuleSettingDesc* desc) noexcept = 0;

    virtual void EnumerateModuleSettings(
        void (*visit)(const FrameLiftModuleSettingDesc* desc, void* visitUd), void* visitUd
    ) const noexcept = 0;
};
