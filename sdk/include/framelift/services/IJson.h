#pragma once

// Host JSON capability. A single host-owned implementation (Qt QJson today) lives
// behind this service so the backend can be swapped in one place; plugins never link
// a JSON library themselves. A capability service — discover it with
// ctx.GetService<IJson>() and null-check before use. For ergonomic call sites, prefer
// the RAII wrappers in <framelift/JsonHelpers.h> over this raw vtable.
//
// ABI-safe: nodes and builders cross the boundary as opaque handles; values cross as
// POD / const char* (the buf/cap idiom of ISettingsStore). Adding this service does
// not bump FRAMELIFT_ABI_VERSION.
class IJson
{
public:
    static constexpr const char* InterfaceId = "framelift.IJson";
    virtual ~IJson() = default;

    // ── Reading ────────────────────────────────────────────────────────────────
    // Parse `len` bytes of `text` (len < 0 ⇒ NUL-terminated). Returns an opaque
    // document handle (also the root node), or nullptr on parse error. Every handle
    // derived from it stays valid until FreeDocument is called on the document.
    [[nodiscard]] virtual const void* ParseDocument(const char* text, int len) noexcept = 0;
    virtual void FreeDocument(const void* doc) noexcept = 0;

    // Navigation. Member/ArrayItem return nullptr for the wrong node type, a missing
    // key, or an out-of-range index.
    [[nodiscard]] virtual bool IsArray(const void* node) const noexcept = 0;
    [[nodiscard]] virtual int ArraySize(const void* node) const noexcept = 0;
    [[nodiscard]] virtual const void* ArrayItem(const void* node, int index) const noexcept = 0;
    [[nodiscard]] virtual const void* Member(const void* node, const char* key) const noexcept = 0;

    // Scalar reads — safe on null or wrong-type nodes (return ""/def). GetString
    // writes up to cap-1 chars + NUL into buf and returns the full length excl. NUL;
    // pass buf=nullptr to query the required length.
    [[nodiscard]] virtual int GetString(const void* node, char* buf, int cap) const noexcept = 0;
    [[nodiscard]] virtual double GetDouble(const void* node, double def) const noexcept = 0;

    // ── Writing ────────────────────────────────────────────────────────────────
    // New* allocate an opaque builder node; free the root with FreeBuilder (children
    // appended/owned by it are freed with it).
    [[nodiscard]] virtual void* NewArray() noexcept = 0;
    [[nodiscard]] virtual void* NewObject() noexcept = 0;
    virtual void SetString(void* obj, const char* key, const char* val) noexcept = 0;
    virtual void SetDouble(void* obj, const char* key, double val) noexcept = 0;
    // Move `child` into array `arr`; `child` is consumed (do not use or free it after).
    virtual void Append(void* arr, void* child) noexcept = 0;
    // Serialize `builder` to JSON. indent <= 0 ⇒ compact. Writes up to cap-1 + NUL
    // into buf, returns full length excl. NUL; buf=nullptr queries the length. Does
    // not free the builder.
    [[nodiscard]] virtual int Serialize(void* builder, int indent, char* buf, int cap) noexcept = 0;
    virtual void FreeBuilder(void* builder) noexcept = 0;
};
