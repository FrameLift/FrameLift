#include "JsonServiceImpl.h"

#include <QtCore/QByteArray>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QString>

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Host JSON service backed by Qt's QJson. The ABI contract is that navigated read
// nodes stay valid until FreeDocument — but QJson is value-
// semantic (QJsonValue/Object/Array are copies, with no stable internal addresses).
// So the document owns a pool of heap-allocated nodes: navigation (Member/ArrayItem)
// allocates a JsonNode copy registered with the owning JsonDoc and hands back its
// address; FreeDocument frees the root and the whole pool. A per-doc mutex guards the
// pool so concurrent navigation of one document stays safe (the service's documented
// thread-safety guarantee). Builders are independent value-holders.

namespace
{
struct JsonDoc;

// A parsed read node: a QJson value plus a back-pointer to its document, so
// navigation off it can register freshly-allocated child nodes in the doc's pool.
struct JsonNode
{
    QJsonValue value;
    JsonDoc* owner = nullptr;
};

struct JsonDoc
{
    JsonNode root; // also the handle ParseDocument returns
    std::vector<std::unique_ptr<JsonNode>> pool;
    std::mutex mutex;

    // Allocate a child node owned by this document; returns a handle stable until
    // the document is freed.
    const void* Adopt(QJsonValue v)
    {
        auto node = std::make_unique<JsonNode>(JsonNode{std::move(v), this});
        const void* handle = node.get();
        const std::lock_guard<std::mutex> lock(mutex);
        pool.push_back(std::move(node));
        return handle;
    }
};

// A mutable JSON value under construction (object, array, or scalar).
struct JsonBuilder
{
    QJsonValue value;
};

const JsonNode* AsNode(const void* p) noexcept
{
    return static_cast<const JsonNode*>(p);
}
JsonBuilder* AsBuilder(void* p) noexcept
{
    return static_cast<JsonBuilder*>(p);
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
    const int n = len < 0 ? static_cast<int>(std::strlen(text)) : len;

    QJsonParseError err{};
    const QJsonDocument parsed = QJsonDocument::fromJson(QByteArray(text, n), &err);
    if (err.error != QJsonParseError::NoError)
    {
        return nullptr;
    }

    auto doc = std::make_unique<JsonDoc>();
    doc->root.owner = doc.get();
    if (parsed.isObject())
    {
        doc->root.value = parsed.object();
    }
    else if (parsed.isArray())
    {
        doc->root.value = parsed.array();
    }
    else
    {
        return nullptr; // QJsonDocument only roots object/array
    }
    return &doc.release()->root;
}

void JsonServiceImpl::FreeDocument(const void* doc) noexcept
{
    if (const JsonNode* n = AsNode(doc))
    {
        delete n->owner;
    }
}

bool JsonServiceImpl::IsArray(const void* node) const noexcept
{
    const JsonNode* n = AsNode(node);
    return n && n->value.isArray();
}

int JsonServiceImpl::ArraySize(const void* node) const noexcept
{
    const JsonNode* n = AsNode(node);
    return (n && n->value.isArray()) ? static_cast<int>(n->value.toArray().size()) : 0;
}

const void* JsonServiceImpl::ArrayItem(const void* node, int index) const noexcept
{
    const JsonNode* n = AsNode(node);
    if (!n || !n->value.isArray() || index < 0)
    {
        return nullptr;
    }
    const QJsonArray arr = n->value.toArray();
    if (index >= static_cast<int>(arr.size()))
    {
        return nullptr;
    }
    return n->owner->Adopt(arr.at(index));
}

const void* JsonServiceImpl::Member(const void* node, const char* key) const noexcept
{
    const JsonNode* n = AsNode(node);
    if (!n || !n->value.isObject() || !key)
    {
        return nullptr;
    }
    const QJsonObject obj = n->value.toObject();
    const auto it = obj.constFind(QString::fromUtf8(key));
    if (it == obj.constEnd())
    {
        return nullptr;
    }
    return n->owner->Adopt(it.value());
}

int JsonServiceImpl::GetString(const void* node, char* buf, int cap) const noexcept
{
    const JsonNode* n = AsNode(node);
    if (!n || !n->value.isString())
    {
        if (buf && cap > 0)
        {
            buf[0] = '\0';
        }
        return 0;
    }
    return CopyOut(n->value.toString().toStdString(), buf, cap);
}

double JsonServiceImpl::GetDouble(const void* node, double def) const noexcept
{
    const JsonNode* n = AsNode(node);
    return (n && n->value.isDouble()) ? n->value.toDouble(def) : def;
}

// ── Writing ─────────────────────────────────────────────────────────────────────

void* JsonServiceImpl::NewArray() noexcept
{
    return new (std::nothrow) JsonBuilder{QJsonArray()};
}

void* JsonServiceImpl::NewObject() noexcept
{
    return new (std::nothrow) JsonBuilder{QJsonObject()};
}

void JsonServiceImpl::SetString(void* obj, const char* key, const char* val) noexcept
{
    JsonBuilder* b = AsBuilder(obj);
    if (!b || !key || !b->value.isObject())
    {
        return;
    }
    QJsonObject o = b->value.toObject();
    o.insert(QString::fromUtf8(key), QString::fromUtf8(val ? val : ""));
    b->value = o;
}

void JsonServiceImpl::SetDouble(void* obj, const char* key, double val) noexcept
{
    JsonBuilder* b = AsBuilder(obj);
    if (!b || !key || !b->value.isObject())
    {
        return;
    }
    QJsonObject o = b->value.toObject();
    o.insert(QString::fromUtf8(key), val);
    b->value = o;
}

void JsonServiceImpl::Append(void* arr, void* child) noexcept
{
    JsonBuilder* a = AsBuilder(arr);
    JsonBuilder* c = AsBuilder(child);
    if (a && c && a->value.isArray())
    {
        QJsonArray array = a->value.toArray();
        array.append(c->value);
        a->value = array;
    }
    delete c; // child is consumed regardless of success
}

int JsonServiceImpl::Serialize(void* builder, int indent, char* buf, int cap) noexcept
{
    JsonBuilder* b = AsBuilder(builder);
    if (b)
    {
        QJsonDocument doc;
        if (b->value.isObject())
        {
            doc.setObject(b->value.toObject());
        }
        else if (b->value.isArray())
        {
            doc.setArray(b->value.toArray());
        }
        // Qt's indent width is fixed at 4 (the `indent` count isn't honoured), and
        // Indented mode appends a trailing newline — strip it to match the compact,
        // no-trailing-newline shape callers expect.
        QByteArray out = doc.toJson(indent > 0 ? QJsonDocument::Indented : QJsonDocument::Compact);
        if (out.endsWith('\n'))
        {
            out.chop(1);
        }
        return CopyOut(std::string(out.constData(), static_cast<std::size_t>(out.size())), buf, cap);
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
