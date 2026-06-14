#include "RemoteStream.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
// Shared id for OpenPopup / BeginPopupModal — they must match.
constexpr const char* kModalId = "Open Network Stream";

// Reference XOR cipher for the bundled flsec:// sample. NOT real security — it
// exists only to demonstrate the decryption hook. A replacement plugin swaps
// this (and the fetch) for an actual stream format and cipher.
constexpr unsigned char kRefKey[] = {0x5A, 0xA5, 0x3C, 0xC3};
} // namespace

std::string RemoteStream::ResolveStream(const std::string& url) noexcept
{
    const std::string scheme = framelift::UrlScheme(url.c_str());
    if (scheme.empty())
    {
        return {}; // not a URL — caller should not have routed it here
    }

    // Plain network streams: FFmpeg's built-in protocol handlers read them directly.
    if (scheme != framelift::kSecureStreamScheme)
    {
        return url;
    }

    // ── Custom-encryption hook (reference implementation) ─────────────────────
    // Replace this branch in your own RemoteStream DLL to fetch and decrypt your
    // real stream. The default treats "flsec://<localpath>" as a locally stored,
    // XOR-obfuscated file: it decrypts it to a temp file and plays that.
    const std::string source = url.substr(scheme.size() + 3); // strip "flsec://"

    std::ifstream in(source, std::ios::binary);
    if (!in)
    {
        Log::Error("RemoteStream: cannot open encrypted source '{}'", source);
        return {};
    }
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    for (std::size_t i = 0; i < data.size(); ++i)
    {
        data[i] = static_cast<char>(static_cast<unsigned char>(data[i]) ^ kRefKey[i % sizeof(kRefKey)]);
    }

    std::string dir = ctx_ ? framelift::GetPrefPath(*ctx_) : std::string{};
    std::error_code ec;
    if (dir.empty())
    {
        dir = std::filesystem::temp_directory_path(ec).string();
    }
    const std::filesystem::path outPath = std::filesystem::path(dir) / "remote_stream.tmp";

    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        Log::Error("RemoteStream: cannot write temp file '{}'", outPath.string());
        return {};
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out)
    {
        Log::Error("RemoteStream: failed writing temp file '{}'", outPath.string());
        return {};
    }
    return outPath.string();
}

void RemoteStream::OpenUrl(const std::string& url) noexcept
{
    if (url.empty() || !ctx_)
    {
        return;
    }

    const std::string resolved = ResolveStream(url);
    if (resolved.empty())
    {
        ctx_->Publish<NotificationEvent>({"Failed to open stream"});
        return;
    }

    auto* player = ctx_->GetService<IMediaPlayer>();
    if (!player)
    {
        return;
    }

    // Resume + history key on the original URL, not the decrypted temp path.
    auto* hist = ctx_->GetService<IHistory>();
    const double resume = hist ? hist->GetResumePos(url.c_str()) : 0.0;
    player->LoadFile(resolved.c_str(), resume > 5.0 ? resume : 0.0);
    player->SetPause(false);

    if (auto* w = ctx_->GetService<IAppWindow>())
    {
        const std::string title = "FrameLift \xe2\x80\x94 " + url;
        w->SetTitle(title.c_str());
    }

    ctx_->Publish<FileOpenedEvent>({url.c_str()});
}

void RemoteStream::OnInstall(IPluginContext& ctx)
{
    framelift::Subscribe<OpenFileRequestEvent>(
        ctx,
        [this](const OpenFileRequestEvent& e)
        {
            if (!framelift::IsRemoteUrl(e.path))
            {
                return; // local files are handled by Playlist
            }
            OpenUrl(e.path);
        }
    );

    // The host owns the "Open Network Stream…" context-menu item (placed directly
    // under "Open File"); it publishes this event to ask us to show the modal.
    framelift::Subscribe<OpenNetworkStreamRequestEvent>(
        ctx,
        [this](const OpenNetworkStreamRequestEvent&)
        {
            requestOpen_ = true;
        }
    );
}

void RemoteStream::OnRender(int windowW, int windowH, UIContext& ctx)
{
    if (requestOpen_)
    {
        ctx.OpenPopup(kModalId);
        requestOpen_ = false;
    }

    constexpr float kWidth = 460.f;
    ctx.SetNextWindowSize({kWidth, 0.f}, UI::Cond::FirstUseEver);
    ctx.SetNextWindowPos(
        {(static_cast<float>(windowW) - kWidth) * 0.5f, static_cast<float>(windowH) * 0.3f}, UI::Cond::FirstUseEver
    );

    modalOpen_ = ctx.BeginPopupModal(kModalId);
    if (!modalOpen_)
    {
        return;
    }

    ctx.Text("Stream URL (http://, https://, rtsp://, flsec://):");
    ctx.Dummy({0.f, 4.f});
    ctx.SetNextItemWidth(kWidth - 16.f);
    ctx.InputTextWithHint("##url", "https://example.com/stream.m3u8", urlInput_);

    ctx.Dummy({0.f, 8.f});
    const bool open = ctx.Button("Open", {120.f, 0.f});
    ctx.SameLine();
    const bool cancel = ctx.Button("Cancel", {120.f, 0.f});

    if (open && !urlInput_.empty())
    {
        // RemoteStream's own subscription picks this up and loads it; routing
        // through the event keeps the entry point uniform with file drops/dialogs.
        ctx_->Publish<OpenFileRequestEvent>({urlInput_.c_str(), false});
        urlInput_.clear();
        ctx.CloseCurrentPopup();
        modalOpen_ = false;
    }
    else if (cancel)
    {
        ctx.CloseCurrentPopup();
        modalOpen_ = false;
    }

    ctx.EndPopup();
}

bool RemoteStream::RedrawNeeded() const
{
    return modalOpen_ || requestOpen_;
}

FRAMELIFT_PLUGIN_EXPORT(
    RemoteStream,
    {
        .name = "RemoteStream",
        .version = {1, 0, 0},
        .render = true,
        .renderOrder = 100, // draw the modal above the panels
        .description = "Plays remote network streams (http/https/rtsp) and custom-encrypted streams",
    }
)
