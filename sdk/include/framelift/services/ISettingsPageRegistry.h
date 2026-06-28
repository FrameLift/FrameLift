#pragma once

class QObject;

struct FrameLiftSettingsPageDesc
{
    const char* id;
    const char* title;
    const char* qmlUrl;
    QObject* viewModel;
    int order;
};

class ISettingsPageRegistry
{
public:
    static constexpr const char* InterfaceId = "framelift.ISettingsPageRegistry";
    virtual ~ISettingsPageRegistry() = default;

    // Register a settings page. The registry stores `viewModel` as a NON-OWNING
    // QObject* and never unregisters pages at runtime (there is no remove call), so
    // the caller MUST keep `viewModel` alive for the whole app session — it has to
    // outlive every settings window that may bind to it. Plugins satisfy this by
    // owning the page model for their lifetime (e.g. a member unique_ptr created in
    // OnInstall). Passing nullptr is allowed for pages whose UI is pure QML.
    virtual void RegisterSettingsPage(
        const char* id, const char* title, const char* qmlUrl, QObject* viewModel, int order
    ) noexcept = 0;

    virtual void EnumerateSettingsPages(
        void (*visit)(const FrameLiftSettingsPageDesc* desc, void* visitUd), void* visitUd
    ) const noexcept = 0;
};
