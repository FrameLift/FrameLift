#pragma once

#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// ── Per-module settings registration ──────────────────────────────────────────
// Replaces the former monolithic SETTINGS_FIELDS X-macro. Each module registers
// the fields it owns by binding a typed sub-struct member to a "section.name" key.
// The registry is the single source of truth for INI load/save and the ABI-stable
// per-key getters exposed through IModuleContext.
//
// A field's value is reached through type-erased closures captured over the bound
// member, so the registry stays decoupled from the concrete settings structs.

enum class SettingType : int
{
    Bool = 0,
    Int = 1,
    Float = 2,
    String = 3
};

struct SettingField
{
    std::string key;  // "audio.defaultLanguage"
    SettingType type; // value category
    std::string desc; // "# ..." documentation comment / ABI description

    // Serialize the current value (may apply a save transform) / parse text into it.
    std::function<std::string()> save;
    std::function<void(const std::string&)> load;

    // Typed accessors for the ABI. Only the closure matching `type` is populated;
    // the others stay empty and are never invoked (callers gate on `type`).
    std::function<float()> getFloat;
    std::function<void(float)> setFloat;
    std::function<bool()> getBool;
    std::function<void(bool)> setBool;
    std::function<int()> getInt;
    std::function<void(int)> setInt;
    std::function<std::string()> getString;
    std::function<void(const std::string&)> setString;
};

class SettingsRegistry
{
public:
    using SaveFn = std::function<std::string()>;

    // Bind `member` to `key`. `save` overrides serialization for the few fields that
    // need a transform (e.g. graphics.backend normalization); empty = plain to-string.
    void AddBool(std::string key, bool& member, std::string desc, SaveFn save = {})
    {
        SettingField f;
        f.key = std::move(key);
        f.type = SettingType::Bool;
        f.desc = std::move(desc);
        f.save = save ? std::move(save) : SaveFn([&member] { return std::string(member ? "1" : "0"); });
        f.load = [&member](const std::string& v) { member = (v == "1"); };
        f.getBool = [&member] { return member; };
        f.setBool = [&member](bool v) { member = v; };
        Append(std::move(f));
    }

    void AddInt(std::string key, int& member, std::string desc, SaveFn save = {})
    {
        SettingField f;
        f.key = std::move(key);
        f.type = SettingType::Int;
        f.desc = std::move(desc);
        f.save = save ? std::move(save) : SaveFn([&member] { return std::to_string(member); });
        f.load = [&member](const std::string& v) { member = std::stoi(v); };
        f.getInt = [&member] { return member; };
        f.setInt = [&member](int v) { member = v; };
        Append(std::move(f));
    }

    void AddFloat(std::string key, float& member, std::string desc, SaveFn save = {})
    {
        SettingField f;
        f.key = std::move(key);
        f.type = SettingType::Float;
        f.desc = std::move(desc);
        f.save = save ? std::move(save) : SaveFn([&member] { return std::to_string(member); });
        f.load = [&member](const std::string& v) { member = std::stof(v); };
        f.getFloat = [&member] { return member; };
        f.setFloat = [&member](float v) { member = v; };
        Append(std::move(f));
    }

    void AddString(std::string key, std::string& member, std::string desc, SaveFn save = {})
    {
        SettingField f;
        f.key = std::move(key);
        f.type = SettingType::String;
        f.desc = std::move(desc);
        f.save = save ? std::move(save) : SaveFn([&member] { return member; });
        f.load = [&member](const std::string& v) { member = v; };
        f.getString = [&member] { return member; };
        f.setString = [&member](const std::string& v) { member = v; };
        Append(std::move(f));
    }

    // Registered by modules whose fields need cross-field reconciliation after a
    // load (e.g. playback hwdec/hwdecMode, graphics backend). `seen` is the set of
    // keys that were actually present in the file.
    void AddPostLoad(std::function<void(const std::set<std::string>&)> fn)
    {
        postLoad_.push_back(std::move(fn));
    }

    [[nodiscard]] const std::vector<SettingField>& Fields() const
    {
        return fields_;
    }

    [[nodiscard]] const SettingField* Find(const std::string& key) const
    {
        const auto it = index_.find(key);
        return it == index_.end() ? nullptr : &fields_[it->second];
    }

    void RunPostLoad(const std::set<std::string>& seen) const
    {
        for (const auto& fn : postLoad_)
        {
            fn(seen);
        }
    }

private:
    void Append(SettingField f)
    {
        index_.emplace(f.key, fields_.size());
        fields_.push_back(std::move(f));
    }

    std::vector<SettingField> fields_;
    std::unordered_map<std::string, std::size_t> index_;
    std::vector<std::function<void(const std::set<std::string>&)>> postLoad_;
};
