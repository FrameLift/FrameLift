#pragma once

#include "SettingsRegistry.h"

#include <any>
#include <functional>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

// Dependency-free settings storage. Concrete modules register their own typed
// sections from the host composition layer; this class owns only persistence,
// type-erased storage, defaults, and registry assembly.
class Settings
{
public:
    static constexpr const char* InterfaceId = "framelift.Settings";

    Settings() = default;

    template <class T, class RegisterFn>
    bool RegisterSection(RegisterFn&& registerFn)
    {
        const std::type_index type(typeid(T));
        if (sections_.contains(type))
        {
            return false;
        }

        auto fn = std::function<void(SettingsRegistry&, T&)>(std::forward<RegisterFn>(registerFn));
        sections_.emplace(type, T{});
        sectionBindings_.push_back(
            {type,
             [fn](SettingsRegistry& registry, std::any& section)
             {
                 fn(registry, std::any_cast<T&>(section));
             },
             [](std::any& section)
             {
                 std::any_cast<T&>(section) = T{};
             }}
        );

        T defaults{};
        SettingsRegistry defaultsRegistry;
        fn(defaultsRegistry, defaults);
        for (const SettingField& field : defaultsRegistry.Fields())
        {
            defaultValues_.try_emplace(field.key, field.save());
        }
        return true;
    }

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

    [[nodiscard]] SettingsRegistry BuildRegistry();

    [[nodiscard]] const std::unordered_map<std::string, std::string>& DefaultValues() const noexcept
    {
        return defaultValues_;
    }

    void ResetToDefaults();
    void Load(const std::string& path);
    void Save(const std::string& path);

private:
    struct SectionBinding
    {
        std::type_index type;
        std::function<void(SettingsRegistry&, std::any&)> bind;
        std::function<void(std::any&)> reset;
    };

    std::unordered_map<std::type_index, std::any> sections_;
    std::vector<SectionBinding> sectionBindings_;
    std::unordered_map<std::string, std::string> defaultValues_;
};
