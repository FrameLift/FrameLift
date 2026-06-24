#include "JsonServiceImpl.h"

#include <cstring>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace
{
using Json = nlohmann::json;

const Json* AsNode(const void* p) noexcept
{
    return static_cast<const Json*>(p);
}
Json* AsBuilder(void* p) noexcept
{
    return static_cast<Json*>(p);
}

// Copy `s` into buf/cap per the ISettingsStore idiom: write ≤cap-1 chars + NUL,
// return the full length excl. NUL; buf=nullptr just queries the length.
int CopyOut(const std::string& s, char* buf, int cap) noexcept
{
    const int len = static_cast<int>(s.size());
    if (buf && cap > 0)
    {
        const int n = len < cap - 1 ? len : cap - 1;
        std::memcpy(buf, s.data(), static_cast<std::size_t>(n));
        buf[n] = '\0';
    }
    return len;
}
} // namespace

// ── Reading ─────────────────────────────────────────────────────────────────────

const void* JsonServiceImpl::ParseDocument(const char* text, int len) noexcept
{
    if (!text)
    {
        return nullptr;
    }
    const std::size_t n = len < 0 ? std::strlen(text) : static_cast<std::size_t>(len);
    try
    {
        auto parsed = Json::parse(text, text + n, nullptr, /*allow_exceptions=*/false);
        if (parsed.is_discarded())
        {
            return nullptr;
        }
        return new Json(std::move(parsed));
    }
    catch (...)
    {
        return nullptr;
    }
}

void JsonServiceImpl::FreeDocument(const void* doc) noexcept
{
    delete const_cast<Json*>(AsNode(doc));
}

bool JsonServiceImpl::IsArray(const void* node) const noexcept
{
    const Json* n = AsNode(node);
    return n && n->is_array();
}

int JsonServiceImpl::ArraySize(const void* node) const noexcept
{
    const Json* n = AsNode(node);
    return (n && n->is_array()) ? static_cast<int>(n->size()) : 0;
}

const void* JsonServiceImpl::ArrayItem(const void* node, int index) const noexcept
{
    const Json* n = AsNode(node);
    if (!n || !n->is_array() || index < 0 || index >= static_cast<int>(n->size()))
    {
        return nullptr;
    }
    return &(*n)[static_cast<std::size_t>(index)];
}

const void* JsonServiceImpl::Member(const void* node, const char* key) const noexcept
{
    const Json* n = AsNode(node);
    if (!n || !n->is_object() || !key)
    {
        return nullptr;
    }
    const auto it = n->find(key);
    return it != n->end() ? &(*it) : nullptr;
}

int JsonServiceImpl::GetString(const void* node, char* buf, int cap) const noexcept
{
    const Json* n = AsNode(node);
    if (!n || !n->is_string())
    {
        if (buf && cap > 0)
        {
            buf[0] = '\0';
        }
        return 0;
    }
    return CopyOut(n->get_ref<const std::string&>(), buf, cap);
}

double JsonServiceImpl::GetDouble(const void* node, double def) const noexcept
{
    const Json* n = AsNode(node);
    return (n && n->is_number()) ? n->get<double>() : def;
}

// ── Writing ─────────────────────────────────────────────────────────────────────

void* JsonServiceImpl::NewArray() noexcept
{
    try
    {
        return new Json(Json::array());
    }
    catch (...)
    {
        return nullptr;
    }
}

void* JsonServiceImpl::NewObject() noexcept
{
    try
    {
        return new Json(Json::object());
    }
    catch (...)
    {
        return nullptr;
    }
}

void JsonServiceImpl::SetString(void* obj, const char* key, const char* val) noexcept
{
    Json* o = AsBuilder(obj);
    if (!o || !key)
    {
        return;
    }
    try
    {
        (*o)[key] = val ? val : "";
    }
    catch (...)
    {
    }
}

void JsonServiceImpl::SetDouble(void* obj, const char* key, double val) noexcept
{
    Json* o = AsBuilder(obj);
    if (!o || !key)
    {
        return;
    }
    try
    {
        (*o)[key] = val;
    }
    catch (...)
    {
    }
}

void JsonServiceImpl::Append(void* arr, void* child) noexcept
{
    Json* a = AsBuilder(arr);
    Json* c = AsBuilder(child);
    if (a && c)
    {
        try
        {
            a->push_back(std::move(*c));
        }
        catch (...)
        {
        }
    }
    delete c; // child is consumed regardless of success
}

int JsonServiceImpl::Serialize(void* builder, int indent, char* buf, int cap) noexcept
{
    Json* b = AsBuilder(builder);
    if (b)
    {
        try
        {
            const std::string s = indent > 0 ? b->dump(indent) : b->dump();
            return CopyOut(s, buf, cap);
        }
        catch (...)
        {
        }
    }
    if (buf && cap > 0)
    {
        buf[0] = '\0';
    }
    return 0;
}

void JsonServiceImpl::FreeBuilder(void* builder) noexcept
{
    delete AsBuilder(builder);
}
