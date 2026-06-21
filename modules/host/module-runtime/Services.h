#pragma once
#include <string>
#include <type_traits>
#include <unordered_map>

// Type-indexed service registry. Register any pointer type once; retrieve by type anywhere.
// Supports const types: Register<const T> stores under typeid(T); Get<const T> returns const T*.
// Uses typeid(T).name() string keys so lookups work correctly across DLL boundaries
// (MinGW static libstdc++ gives each module its own type_info pointers, but the name
// strings are identical for the same type, making string-keyed maps cross-DLL safe).
class Services
{
public:
    template <typename T>
    void Register(T* service)
    {
        using Base = std::remove_const_t<T>;
        registry_[typeid(Base).name()] = const_cast<Base*>(service);
    }

    template <typename T>
    T* Get() const
    {
        const auto it = registry_.find(typeid(std::remove_const_t<T>).name());
        return it != registry_.end() ? static_cast<T*>(it->second) : nullptr;
    }

    template <typename T>
    void Unregister()
    {
        registry_.erase(typeid(std::remove_const_t<T>).name());
    }

private:
    std::unordered_map<std::string, void*> registry_;
};