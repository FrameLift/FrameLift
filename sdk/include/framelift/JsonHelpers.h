#pragma once
#include <framelift/services/IJson.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

// Ergonomic RAII wrappers over the ABI-safe IJson service, compiled into the plugin
// (like ContextHelpers / IModuleContext's non-virtual templates). They keep call sites
// readable instead of juggling opaque void* handles. A null/absent node yields
// empty/default scalars and an empty array, so navigation chains never crash.
namespace framelift
{

// A non-owning view over a parsed JSON node bound to its owning IJson. Copyable
// (two pointers); does not own the document.
class JsonRef
{
public:
    JsonRef() = default;
    JsonRef(IJson* json, const void* node) noexcept : json_(json), node_(node)
    {
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return json_ && node_;
    }
    [[nodiscard]] bool isArray() const noexcept
    {
        return json_ && json_->IsArray(node_);
    }
    [[nodiscard]] int size() const noexcept
    {
        return json_ ? json_->ArraySize(node_) : 0;
    }

    [[nodiscard]] JsonRef at(int index) const noexcept
    {
        return {json_, json_ ? json_->ArrayItem(node_, index) : nullptr};
    }
    [[nodiscard]] JsonRef member(const char* key) const noexcept
    {
        return {json_, json_ ? json_->Member(node_, key) : nullptr};
    }

    // Read this node, or a named member of it (key="" ⇒ this node), as a string.
    [[nodiscard]] std::string str(const char* key = "") const
    {
        const void* n = Resolve(key);
        if (!json_ || !n)
        {
            return {};
        }
        const int len = json_->GetString(n, nullptr, 0);
        if (len <= 0)
        {
            return {};
        }
        std::string s(static_cast<std::size_t>(len), '\0');
        json_->GetString(n, s.data(), len + 1);
        return s;
    }

    // Read this node, or a named member, as a number (default `def` if absent).
    [[nodiscard]] double num(const char* key = "", double def = 0.0) const noexcept
    {
        const void* n = Resolve(key);
        return (json_ && n) ? json_->GetDouble(n, def) : def;
    }

private:
    [[nodiscard]] const void* Resolve(const char* key) const noexcept
    {
        if (key && key[0])
        {
            return json_ ? json_->Member(node_, key) : nullptr;
        }
        return node_;
    }

    IJson* json_ = nullptr;
    const void* node_ = nullptr;
};

// RAII owner of a parsed document — parse on construction, free on destruction.
class JsonDocument
{
public:
    JsonDocument(IJson& json, std::string_view text) noexcept
        : json_(&json), doc_(json.ParseDocument(text.data(), static_cast<int>(text.size())))
    {
    }
    ~JsonDocument()
    {
        if (doc_)
        {
            json_->FreeDocument(doc_);
        }
    }
    JsonDocument(const JsonDocument&) = delete;
    JsonDocument& operator=(const JsonDocument&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return doc_ != nullptr;
    }
    [[nodiscard]] JsonRef root() const noexcept
    {
        return {json_, doc_};
    }

private:
    IJson* json_;
    const void* doc_;
};

// RAII builder for serialising JSON. Construct an array or object, populate it, dump().
// Moving transfers ownership (used by append()).
class JsonWriter
{
public:
    [[nodiscard]] static JsonWriter Array(IJson& json) noexcept
    {
        return {&json, json.NewArray()};
    }
    [[nodiscard]] static JsonWriter Object(IJson& json) noexcept
    {
        return {&json, json.NewObject()};
    }

    ~JsonWriter()
    {
        if (b_)
        {
            json_->FreeBuilder(b_);
        }
    }
    JsonWriter(JsonWriter&& o) noexcept : json_(o.json_), b_(o.b_)
    {
        o.b_ = nullptr;
    }
    JsonWriter& operator=(JsonWriter&& o) noexcept
    {
        if (this != &o)
        {
            if (b_)
            {
                json_->FreeBuilder(b_);
            }
            json_ = o.json_;
            b_ = o.b_;
            o.b_ = nullptr;
        }
        return *this;
    }
    JsonWriter(const JsonWriter&) = delete;
    JsonWriter& operator=(const JsonWriter&) = delete;

    void set(const char* key, const std::string& val) noexcept
    {
        json_->SetString(b_, key, val.c_str());
    }
    void set(const char* key, const char* val) noexcept
    {
        json_->SetString(b_, key, val);
    }
    void set(const char* key, double val) noexcept
    {
        json_->SetDouble(b_, key, val);
    }

    // Append `child` to this array; consumes child (its handle moves into the array).
    void append(JsonWriter&& child) noexcept
    {
        json_->Append(b_, child.b_);
        child.b_ = nullptr;
    }

    [[nodiscard]] std::string dump(int indent = 0) const
    {
        const int len = json_->Serialize(b_, indent, nullptr, 0);
        if (len <= 0)
        {
            return {};
        }
        std::string s(static_cast<std::size_t>(len), '\0');
        json_->Serialize(b_, indent, s.data(), len + 1);
        return s;
    }

private:
    JsonWriter(IJson* json, void* b) noexcept : json_(json), b_(b)
    {
    }
    IJson* json_;
    void* b_;
};

} // namespace framelift
