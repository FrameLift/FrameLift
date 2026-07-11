#pragma once

#include <cstdint>

enum class AIModelSource : std::uint8_t
{
    Catalog,
    Imported
};

struct AIModelInfo
{
    const char* id = nullptr;
    const char* name = nullptr;
    const char* quant = nullptr;
    AIModelSource source = AIModelSource::Catalog;
    long long sizeBytes = 0;
    bool installed = false;
    bool recommended = false;
    bool supportsText = true;
    bool supportsVision = false;
};

using AIModelVisitor = void (*)(const AIModelInfo* info, void* userData);
using AIModelTransferCallback = void (*)(
    std::uint64_t transferId, const char* modelId, float progress, bool finished, const char* error, void* userData
);

// Shared model catalogue and managed <application-base>/models storage. Imports are
// copied into that directory; callers never hand persistent external paths to the
// inference service. Transfer/mutation methods are application-thread operations;
// enumeration and IsInstalled are safe read-only queries.
class IAIModelManager
{
public:
    static constexpr const char* InterfaceId = "framelift.IAIModelManager";
    virtual ~IAIModelManager() = default;

    virtual void Enumerate(AIModelVisitor visitor, void* userData) const noexcept = 0;
    [[nodiscard]] virtual bool IsInstalled(const char* modelId) const noexcept = 0;

    [[nodiscard]] virtual std::uint64_t Install(
        const char* modelId, AIModelTransferCallback callback, void* userData
    ) noexcept = 0;
    [[nodiscard]] virtual std::uint64_t Import(
        const char* modelId, const char* displayName, const char* modelPath, const char* projectorPath,
        AIModelTransferCallback callback, void* userData
    ) noexcept = 0;
    virtual void CancelTransfer(std::uint64_t transferId) noexcept = 0;
    [[nodiscard]] virtual bool Remove(const char* modelId) noexcept = 0;

    [[nodiscard]] virtual int GetLoadedModelLimit() const noexcept = 0;
    virtual void SetLoadedModelLimit(int limit) noexcept = 0;
};
