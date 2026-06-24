#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <windows.h>
#include <winhttp.h>

#include <framelift/JsonHelpers.h>

#include "SemVer.h"
#include "Updater.h"
#include "Version.h"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

// ── GitHub config ─────────────────────────────────────────────────────────────
namespace
{
constexpr auto GitHubHost = L"api.github.com";
constexpr auto ReleasesPath = L"/repos/framelift/framelift/releases/latest";
constexpr auto AssetName = "FrameLift-win64.zip";

// ── WinHTTP RAII handle ───────────────────────────────────────────────────
struct WHandle
{
    HINTERNET h = nullptr;
    WHandle() = default;

    ~WHandle()
    {
        if (h)
        {
            WinHttpCloseHandle(h);
        }
    }

    WHandle(const WHandle&) = delete;
    WHandle& operator=(const WHandle&) = delete;

    // ReSharper disable once CppNonExplicitConversionOperator
    operator HINTERNET() const
    {
        return h;
    }
};

// ── HTTP helpers ──────────────────────────────────────────────────────────

DWORD QueryStatusCode(const HINTERNET req)
{
    DWORD code = 0, len = sizeof(code);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &code, &len, nullptr);
    return code;
}

std::string DrainResponse(const HINTERNET req)
{
    std::string body;
    constexpr DWORD chunk = 65536;
    std::vector<char> buf(chunk);
    while (true)
    {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0)
        {
            break;
        }
        const DWORD toRead = std::min(avail, chunk);
        DWORD got = 0;
        if (!WinHttpReadData(req, buf.data(), toRead, &got) || got == 0)
        {
            break;
        }
        body.append(buf.data(), got);
    }
    return body;
}

// Perform an HTTPS GET against a known host and path.
// Returns the response body, or nullopt on any error.
std::optional<std::string> HttpsGet(const wchar_t* host, const wchar_t* path)
{
    WHandle session, connect, request;
    session.h = WinHttpOpen(L"FrameLift-Updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!session.h)
    {
        return std::nullopt;
    }

    connect.h = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect.h)
    {
        return std::nullopt;
    }

    request.h = WinHttpOpenRequest(connect, L"GET", path, nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE);
    if (!request.h)
    {
        return std::nullopt;
    }

    if (!WinHttpSendRequest(request, nullptr, 0, nullptr, 0, 0, 0))
    {
        return std::nullopt;
    }
    if (!WinHttpReceiveResponse(request, nullptr))
    {
        return std::nullopt;
    }
    if (QueryStatusCode(request) != 200)
    {
        return std::nullopt;
    }

    return DrainResponse(request);
}

// Download a URL to a local file.  Follows redirects, handles HTTPS.
bool HttpsDownloadToFile(const std::string& url, const std::wstring& dest)
{
    const std::wstring wUrl(url.begin(), url.end());

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t hostBuf[512] = {};
    wchar_t pathBuf[4096] = {};
    wchar_t extraBuf[1024] = {};
    uc.lpszHostName = hostBuf;
    uc.dwHostNameLength = static_cast<DWORD>(std::size(hostBuf));
    uc.lpszUrlPath = pathBuf;
    uc.dwUrlPathLength = static_cast<DWORD>(std::size(pathBuf));
    uc.lpszExtraInfo = extraBuf;
    uc.dwExtraInfoLength = static_cast<DWORD>(std::size(extraBuf));

    if (!WinHttpCrackUrl(wUrl.c_str(), 0, 0, &uc))
    {
        return false;
    }

    const std::wstring fullPath = std::wstring(pathBuf) + std::wstring(extraBuf);
    const bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    WHandle session, connect, request;
    session.h = WinHttpOpen(L"FrameLift-Updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!session.h)
    {
        return false;
    }

    connect.h = WinHttpConnect(session, hostBuf, uc.nPort, 0);
    if (!connect.h)
    {
        return false;
    }

    request.h = WinHttpOpenRequest(
        connect, L"GET", fullPath.c_str(), nullptr, nullptr, nullptr, https ? WINHTTP_FLAG_SECURE : 0u
    );
    if (!request.h)
    {
        return false;
    }

    if (!WinHttpSendRequest(request, nullptr, 0, nullptr, 0, 0, 0))
    {
        return false;
    }
    if (!WinHttpReceiveResponse(request, nullptr))
    {
        return false;
    }

    const DWORD sc = QueryStatusCode(request);
    if (sc < 200 || sc >= 300)
    {
        return false;
    }

    namespace fs = std::filesystem;
    std::ofstream out(fs::path(dest), std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return false;
    }

    constexpr DWORD kChunk = 65536;
    std::vector<char> buf(kChunk);
    while (true)
    {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0)
        {
            break;
        }
        const DWORD toRead = std::min(avail, kChunk);
        DWORD got = 0;
        if (!WinHttpReadData(request, buf.data(), toRead, &got) || got == 0)
        {
            break;
        }
        out.write(buf.data(), got);
    }
    return !out.bad();
}

// Extract a ZIP file to destDir using PowerShell's Expand-Archive.
// destDir must already exist. Returns true if all expected files are present.
bool ExtractZip(const std::wstring& zipPath, const std::wstring& destDir)
{
    std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "
                       L"\"Expand-Archive -LiteralPath '" +
                       zipPath + L"' -DestinationPath '" + destDir + L"' -Force\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(pi.hProcess, 60'000);
    DWORD exitCode = 1;
    if (waitResult == WAIT_OBJECT_0)
    {
        GetExitCodeProcess(pi.hProcess, &exitCode);
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return exitCode == 0;
}
} // namespace

// ── Destructor ────────────────────────────────────────────────────────────────

Updater::~Updater()
{
    if (worker_.joinable())
    {
        worker_.join();
    }
}

// ── ModuleBase hooks ───────────────────────────────────────────────────────

std::vector<framelift::SettingsField> Updater::SettingsFields()
{
    return {{"autoUpdate", &autoUpdate_, true}};
}

void Updater::OnInstall(IModuleContext& ctx)
{
    SetupSettingsPage(ctx);

    json_ = ctx.GetService<IJson>();

    // Only run in production
    if (std::string(FRAMELIFT_VERSION_STRING) == "0.0.0")
    {
        Log::Debug("[Updater] Disabled in development builds");
        return;
    }

    if (autoUpdate_)
    {
        Log::Info("[Updater] Auto-update enabled — checking for updates");
        CheckNow();
    }
    else
    {
        Log::Debug("[Updater] Auto-update disabled");
    }
}

void Updater::RenderSettings(UIContext& ctx)
{
    Widgets::SectionHeader(ctx, "Version");
    ctx.Text("Version:  " FRAMELIFT_VERSION_STRING);
    Widgets::SectionHeader(ctx, "Automatic updates");
    Widgets::Checkbox(ctx, "Enable auto-update", "Automatically check for and download updates.", autoUpdate_);
    Widgets::SectionHeader(ctx, "Status");
    auto status = "";
    switch (state_.load())
    {
    case UpdaterState::Idle:
    case UpdaterState::UpToDate:
        status = "Up to date";
        break;
    case UpdaterState::Checking:
        status = "Checking for updates...";
        break;
    case UpdaterState::Downloading:
        status = "Downloading update...";
        break;
    case UpdaterState::ReadyToInstall:
        status = "Ready to install \xe2\x80\x94 applies on exit";
        break;
    case UpdaterState::Failed:
        status = "Update check failed";
        break;
    }
    ctx.Text(status);
    ctx.Dummy({0.f, 6.f});
    if (ctx.Button("Check Now", {90.f, 0.f}))
    {
        CheckNow();
    }
}

// ── IModule ──────────────────────────────────────────────────────────────────

void Updater::HandleShutdown()
{
    if (IsReadyToInstall())
    {
        ApplyUpdate();
    }
}

void Updater::CheckNow() noexcept
{
    const auto s = state_.load();
    if (s == UpdaterState::Checking || s == UpdaterState::Downloading)
    {
        Log::Debug("[Updater] Check requested but already in progress");
        return;
    }
    if (worker_.joinable())
    {
        worker_.join();
    }
    Log::Info("[Updater] Starting update check (current version: {})", FRAMELIFT_VERSION_STRING);
    state_ = UpdaterState::Checking;
    worker_ = std::thread(
        [this]
        {
            RunWorker();
        }
    );
}

bool Updater::IsReadyToInstall() const noexcept
{
    return state_.load() == UpdaterState::ReadyToInstall;
}

// ── Worker thread ─────────────────────────────────────────────────────────────

void Updater::RunWorker()
{
    // ── 1. Fetch latest release metadata from GitHub ──────────────────────────
    Log::Debug("[Updater] Fetching release metadata from GitHub");
    const auto body = HttpsGet(GitHubHost, ReleasesPath);
    if (!body)
    {
        Log::Warn("[Updater] Failed to fetch release metadata");
        state_ = UpdaterState::Failed;
        return;
    }

    // ── 2. Parse JSON response ────────────────────────────────────────────────
    std::string tagName;
    std::string downloadUrl;
    if (!json_)
    {
        Log::Error("[Updater] JSON service unavailable");
        state_ = UpdaterState::Failed;
        return;
    }
    {
        // GitHub's /releases/latest returns a single release object (the most recent
        // non-draft, non-prerelease). A 404 (no releases) surfaces earlier as a failed
        // HttpsGet, so by here we have a release object to parse.
        const framelift::JsonDocument doc(*json_, *body);
        if (!doc)
        {
            Log::Error("[Updater] Failed to parse release JSON");
            state_ = UpdaterState::Failed;
            return;
        }
        const framelift::JsonRef latest = doc.root();
        tagName = latest.str("tag_name");
        if (!tagName.empty() && tagName.front() == 'v')
        {
            tagName = tagName.substr(1);
        }

        const framelift::JsonRef assets = latest.member("assets");
        for (int i = 0; i < assets.size(); ++i)
        {
            const framelift::JsonRef asset = assets.at(i);
            if (asset.str("name") == AssetName)
            {
                downloadUrl = asset.str("browser_download_url");
                break;
            }
        }
    }

    if (downloadUrl.empty())
    {
        Log::Warn("[Updater] Release '{}' has no '{}' asset", tagName, AssetName);
        state_ = UpdaterState::UpToDate;
        return;
    }

    // ── 3. Compare versions ───────────────────────────────────────────────────
    if (!(ParseVersion(tagName) > ParseVersion(FRAMELIFT_VERSION_STRING)))
    {
        Log::Info("[Updater] Already up to date (latest: {}, current: {})", tagName, FRAMELIFT_VERSION_STRING);
        state_ = UpdaterState::UpToDate;
        return;
    }

    // ── 4. Download FrameLift.zip to %TEMP% and extract ────────────────────────────
    Log::Info("[Updater] New version {} available — downloading", tagName);
    state_ = UpdaterState::Downloading;

    wchar_t tempBuf[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempBuf);
    const std::wstring zipPath = std::wstring(tempBuf) + L"FrameLift_update.zip";
    const std::wstring extractDir = std::wstring(tempBuf) + L"FrameLift_update\\";

    if (!HttpsDownloadToFile(downloadUrl, zipPath))
    {
        Log::Error("[Updater] Download failed");
        state_ = UpdaterState::Failed;
        return;
    }

    CreateDirectoryW(extractDir.c_str(), nullptr);

    if (!ExtractZip(zipPath, extractDir))
    {
        Log::Error("[Updater] Extraction failed");
        DeleteFileW(zipPath.c_str());
        state_ = UpdaterState::Failed;
        return;
    }

    DeleteFileW(zipPath.c_str());

    Log::Info("[Updater] Extraction complete — update will be applied on exit");
    pendingDir_ = extractDir;
    state_ = UpdaterState::ReadyToInstall;
}

// ── ApplyUpdate ───────────────────────────────────────────────────────────────

void Updater::ApplyUpdate() const
{
    if (!IsReadyToInstall())
    {
        return;
    }

    wchar_t exeBuf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exeBuf, MAX_PATH);

    namespace fs = std::filesystem;
    const fs::path exeDir(fs::path(exeBuf).parent_path());
    const fs::path pending(pendingDir_);

    // Apply every file from the extracted update, preserving its relative path so
    // framelift.exe, the runtime DLLs, and the plugin package DLLs are all replaced.
    // Windows allows renaming loaded executables and DLLs (including this Updater
    // DLL); the new files take effect on the next launch.
    std::error_code ec;
    int applied = 0;
    for (auto it = fs::recursive_directory_iterator(pending, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec))
    {
        if (!it->is_regular_file(ec))
        {
            continue;
        }

        const fs::path rel = fs::relative(it->path(), pending, ec);
        if (ec || rel.empty())
        {
            continue;
        }

        const fs::path dest = exeDir / rel;
        fs::create_directories(dest.parent_path(), ec); // no-op for files at the root

        const std::wstring current = dest.wstring();
        const std::wstring oldPath = current + L".old";
        const std::wstring src = it->path().wstring();
        const bool destExists = fs::exists(dest, ec);

        // Rename an existing target aside first (the in-use file keeps working).
        if (destExists && !MoveFileExW(current.c_str(), oldPath.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            Log::Error("[Updater] Failed to rename {} (error {})", rel.string(), GetLastError());
            continue;
        }

        if (!MoveFileExW(src.c_str(), current.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING))
        {
            Log::Error("[Updater] Failed to move {} into place (error {})", rel.string(), GetLastError());
            if (destExists)
            {
                MoveFileExW(oldPath.c_str(), current.c_str(), MOVEFILE_REPLACE_EXISTING);
            }
            continue;
        }

        if (destExists)
        {
            MoveFileExW(oldPath.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        }
        ++applied;
    }

    Log::Info("[Updater] Update applied — {} file(s) replaced; launch FrameLift to run the update", applied);
}

// ── IRenderable ───────────────────────────────────────────────────────────────

void Updater::OnRender(UIContext& ctx)
{
    const auto s = state_.load();
    if (s == UpdaterState::Idle || s == UpdaterState::UpToDate)
    {
        return;
    }

    // Checking/Downloading are driven by a background thread that flips state_ without an
    // event; keep repainting while in a transient state so the loop notices when it settles.
    if (s == UpdaterState::Checking || s == UpdaterState::Downloading)
    {
        ctx.RequestRedraw();
    }

    const char* msg;
    switch (s)
    {
    case UpdaterState::Checking:
        msg = "Checking for updates...";
        break;
    case UpdaterState::Downloading:
        msg = "Downloading update...";
        break;
    case UpdaterState::ReadyToInstall:
        msg = "Update ready - installs on exit";
        break;
    case UpdaterState::Failed:
        msg = "Update check failed";
        break;
    default:
        return;
    }

    constexpr float w = 260.f;
    constexpr float h = 22.f;
    constexpr float padX = 10.f;
    constexpr float padY = 4.f;
    constexpr float margin = 8.f;

    const float x = ctx.GetMainWindowSize().x - w - margin;
    constexpr float y = margin;

    auto& dl = ctx.GetForegroundDrawList();

    const UI::Color32 bg = s == UpdaterState::ReadyToInstall ? UI::MakeColor32(35, 120, 45, 210)
                           : s == UpdaterState::Failed       ? UI::MakeColor32(140, 35, 35, 210)
                                                             : UI::MakeColor32(25, 25, 75, 210);

    dl.AddRectFilled({x, y}, {x + w, y + h}, bg, 4.f);
    dl.AddText({x + padX, y + padY}, UI::MakeColor32(230, 230, 230, 255), msg);
}
