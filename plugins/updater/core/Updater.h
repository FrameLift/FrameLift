#pragma once

#include <framelift/core.h>
#include <framelift/ui.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

class IJson;

// Background auto-update plugin. On Install(), spawns a thread that queries
// the GitHub Releases API, downloads zip to %TEMP% and extracts it if a
// newer version exists, and draws a small status banner in the top-right corner.
enum class UpdaterState : std::uint8_t
{
    Idle,           // no check triggered yet
    Checking,       // fetching GitHub release metadata
    Downloading,    // downloading the new binary
    ReadyToInstall, // download complete; will replace exe on exit
    UpToDate,       // current version is already the latest
    Failed,         // network error or parse error
};

// Call ApplyUpdate() just before app exit to swap all binaries into place.
class Updater final : public ModuleBase, public SafeRenderable
{
public:
    ~Updater() override;

    void HandleShutdown() override;

    [[nodiscard]] bool IsReadyToInstall() const noexcept;

    [[nodiscard]] UpdaterState GetState() const noexcept
    {
        return state_.load();
    }

    // Trigger a new version check; no-op if already in progress.
    void CheckNow() noexcept;

    // Write a replacement batch script and launch it; call just before app exit.
    void ApplyUpdate() const;

    void OnRender(UIContext& ctx) override;

protected:
    const char* ModuleName() const override
    {
        return "Updater";
    }

    std::vector<framelift::SettingsField> SettingsFields() override;
    void OnInstall(IModuleContext& ctx) override;
    void RenderSettings(UIContext& ctx) override;

private:
    void RunWorker();

    std::atomic<UpdaterState> state_{UpdaterState::Idle};
    std::wstring pendingDir_; // temp dir holding extracted update files
    std::thread worker_;
    IJson* json_ = nullptr; // host JSON service for parsing the release response

    bool autoUpdate_ = true;
};

FRAMELIFT_MODULE_ENTRY(Updater, {
    .renderOrder = 40,
})
