#pragma once

#include <any>
#include <string>
#include <typeindex>
#include <unordered_map>

// The aggregate application settings. Dependency-free by design: it does NOT know
// the concrete per-module settings structs. Each section (AudioSettings,
// PlaybackSettings, …) is owned and defined by its module and stored type-erased
// here; consumers reach a section with the Get<T>() template after including that
// module's header. The former monolithic SETTINGS_FIELDS X-macro is gone — fields
// are declared via each module's RegisterXSettings() (see SettingsRegistry.h).
//
// Settings.cpp is the single place that includes every section header: its
// constructor registers each one, and BuildSettingsRegistry() binds their fields.

class SettingsRegistry;

class Settings
{
public:
    static constexpr const char* InterfaceId = "framelift.Settings";

    // Registers every section (defined in Settings.cpp where the headers are known).
    Settings();

    // Access a settings section by type. T must be a registered section; including
    // the owning module's header makes T complete at the call site.
    template <class T>
    [[nodiscard]] T& Get()
    {
        return std::any_cast<T&>(sections_.at(std::type_index(typeid(T))));
    }
    template <class T>
    [[nodiscard]] const T& Get() const
    {
        return std::any_cast<const T&>(sections_.at(std::type_index(typeid(T))));
    }

    // Reset every section to its defaults in place. The contained objects keep their
    // addresses, so a SettingsRegistry already bound to this instance stays valid.
    void ResetToDefaults();

    // Read settings from an ini-style "section.name=value" file at path.
    // Missing keys are left at their defaults; unknown keys are silently ignored.
    void Load(const std::string& path);
    // Write all settings to path synchronously, merging around sections and
    // keys owned by plugins so their data is preserved.
    void Save(const std::string& path);

private:
    // Create + store a default-constructed section instance.
    template <class T>
    void Add()
    {
        sections_[std::type_index(typeid(T))] = T{};
    }

    std::unordered_map<std::type_index, std::any> sections_;
};

// Build a registry binding every section's fields to `s`, in INI section order.
// `s` must outlive the returned registry (its closures hold references into `s`).
SettingsRegistry BuildSettingsRegistry(Settings& s);
