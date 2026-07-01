#pragma once
#include <framelift/services/IJson.h>

// Host JSON service backed by Qt's QJson — the single place the JSON backend is
// chosen. Plugins reach it via ctx.GetService<IJson>() and never link a JSON library
// themselves. The service object is stateless; state lives in the per-document/
// per-builder handles. Read documents own their navigated nodes (QJson is value-
// semantic) and guard that pool with a mutex, so calls are safe from plugin worker
// threads.
class JsonServiceImpl final : public IJson
{
public:
    // ── Reading ──
    const void* ParseDocument(const char* text, int len) noexcept override;
    void FreeDocument(const void* doc) noexcept override;
    bool IsArray(const void* node) const noexcept override;
    int ArraySize(const void* node) const noexcept override;
    const void* ArrayItem(const void* node, int index) const noexcept override;
    const void* Member(const void* node, const char* key) const noexcept override;
    int GetString(const void* node, char* buf, int cap) const noexcept override;
    double GetDouble(const void* node, double def) const noexcept override;

    // ── Writing ──
    void* NewArray() noexcept override;
    void* NewObject() noexcept override;
    void SetString(void* obj, const char* key, const char* val) noexcept override;
    void SetDouble(void* obj, const char* key, double val) noexcept override;
    void Append(void* arr, void* child) noexcept override;
    int Serialize(void* builder, int indent, char* buf, int cap) noexcept override;
    void FreeBuilder(void* builder) noexcept override;
};
