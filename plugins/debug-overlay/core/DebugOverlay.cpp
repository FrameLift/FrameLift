#include "DebugOverlay.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>

#include "Version.h"
#include <framelift/platform.h>

#include <cinttypes>

// ── ModuleBase hooks ───────────────────────────────────────────────────────

std::vector<framelift::Keybind> DebugOverlay::Keybinds()
{
    return {
        {"Toggle debug overlay", "toggleDebugOverlay", &toggleDebugOverlayKey_, "Tab", [this]
         {
             Toggle();
         }}
    };
}

void DebugOverlay::OnInstall(IModuleContext& ctx)
{
    SetupSettingsPage(ctx, false);
}

// ── Media events ──────────────────────────────────────────────────────────────

void DebugOverlay::HandleMediaEvent(const MediaEvent& event)
{
    if (event.type != MediaEventType::PropertyChange)
    {
        return;
    }

    const auto& [prop, type, value] = event.property;

    if (prop == PlayerProperty::IdleActive && type == PropertyType::Flag)
    {
        isIdle_ = value.flag != 0;
        if (isIdle_)
        {
            filePath_.clear();
            title_.clear();
            hwDec_ = "N/A";
            videoW_ = videoH_ = 0;
            dropped_ = mistimed_ = cacheUsed_ = 0;
        }
        return;
    }

    if (prop == PlayerProperty::Pause && type == PropertyType::Flag)
    {
        isPaused_ = value.flag != 0;
        return;
    }

    if (prop == PlayerProperty::TimePos && type == PropertyType::Double)
    {
        timePos_ = value.dbl >= 0.0 ? value.dbl : 0.0;
        return;
    }

    if (prop == PlayerProperty::Duration && type == PropertyType::Double)
    {
        duration_ = value.dbl > 0.0 ? value.dbl : 0.0;
        return;
    }
}

// ── Async property refresh ────────────────────────────────────────────────────

void DebugOverlay::RequestRefresh()
{
    auto* player = ctx_ ? ctx_->GetService<IMediaProperties>() : nullptr;
    if (!player)
    {
        return;
    }

    // Helper types: one-shot callbacks that write a value into a member field.
    struct StrField
    {
        std::string* f;
        const char* def;
    };

    struct I64Field
    {
        int64_t* f;
        int64_t def;
    };

    struct DblField
    {
        double* f;
        double def;
    };

    static const auto strCb = [](const char* v, const bool ok, void* ud)
    {
        const auto* c = static_cast<StrField*>(ud);
        *c->f = (ok && v) ? v : c->def;
        delete c;
    };
    static const auto i64Cb = [](const int64_t v, const bool ok, void* ud)
    {
        const auto* c = static_cast<I64Field*>(ud);
        *c->f = ok ? v : c->def;
        delete c;
    };
    static const auto dblCb = [](const double v, const bool ok, void* ud)
    {
        auto* c = static_cast<DblField*>(ud);
        *c->f = ok ? v : c->def;
        delete c;
    };

    player->GetStringAsync(PlayerProperty::Path, strCb, new StrField{&filePath_, ""});
    player->GetStringAsync(PlayerProperty::MediaTitle, strCb, new StrField{&title_, ""});
    player->GetStringAsync(PlayerProperty::HwDecCurrent, strCb, new StrField{&hwDec_, "N/A"});
    player->GetInt64Async(PlayerProperty::DisplayWidth, i64Cb, new I64Field{&videoW_, 0});
    player->GetInt64Async(PlayerProperty::DisplayHeight, i64Cb, new I64Field{&videoH_, 0});
    player->GetDoubleAsync(PlayerProperty::Volume, dblCb, new DblField{&volume_, 100.0});
    player->GetInt64Async(PlayerProperty::DroppedFrames, i64Cb, new I64Field{&dropped_, 0});
    player->GetInt64Async(PlayerProperty::MistimedFrames, i64Cb, new I64Field{&mistimed_, 0});
    player->GetInt64Async(PlayerProperty::CacheUsed, i64Cb, new I64Field{&cacheUsed_, 0});
    player->GetInt64Async(PlayerProperty::CacheHits, i64Cb, new I64Field{&cacheHits_, 0});
    player->GetInt64Async(PlayerProperty::CacheMisses, i64Cb, new I64Field{&cacheMisses_, 0});
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string FormatTime(const double sec)
{
    const int t = std::max(0, static_cast<int>(sec));
    const int s = t % 60;
    const int m = t / 60 % 60;
    const int h = t / 3600;
    char buf[16];
    if (h > 0)
    {
        std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    }
    return buf;
}

// ── Render ────────────────────────────────────────────────────────────────────

void DebugOverlay::OnRender(UIContext& ctx)
{
    if (!open_)
    {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - lastRefresh_).count();
    if (elapsed >= refreshInterval)
    {
        lastRefresh_ = now;
        RequestRefresh();
    }

    constexpr float kPadX = 10.f;
    constexpr float kPadY = 10.f;
    constexpr float kW = 480.f;

    ctx.SetNextWindowPos({kPadX, kPadY});
    ctx.SetNextWindowBgAlpha(0.82f);

    const UI::WindowFlags flags = UI::WindowFlags::NoTitleBar | UI::WindowFlags::NoResize | UI::WindowFlags::NoMove |
                                  UI::WindowFlags::NoScrollbar | UI::WindowFlags::NoSavedSettings |
                                  UI::WindowFlags::NoBringToFrontOnFocus;

    ctx.PushStyleVar(UI::StyleVar::WindowRounding, 4.f);
    ctx.PushStyleVar(UI::StyleVar::WindowPadding, {10.f, 8.f});
    ctx.PushStyleColor(UI::ColorSlot::WindowBg, UI::Color4f(0.04f, 0.04f, 0.04f, 0.88f));

    ctx.SetNextWindowSize({kW, 0.f});

    if (ctx.Begin("##debugOverlay", nullptr, flags))
    {
        constexpr UI::Color4f kLabel = {0.65f, 0.65f, 0.65f, 1.f};
        constexpr UI::Color4f kValue = {1.f, 1.f, 0.f, 1.f};

        char buf[512];

        const auto row = [&](const char* label, const char* value)
        {
            ctx.TextColored(kLabel, label);
            ctx.SameLine();
            ctx.TextColored(kValue, value);
        };

        row("File: ", filePath_.empty() ? "(idle)" : filePath_.c_str());

        if (!title_.empty() && title_ != filePath_)
        {
            row("Title: ", title_.c_str());
        }

        std::snprintf(buf, sizeof(buf), "%s / %s", FormatTime(timePos_).c_str(), FormatTime(duration_).c_str());
        row("Position: ", buf);

        if (videoW_ > 0 && videoH_ > 0)
        {
            std::snprintf(buf, sizeof(buf), "%" PRId64 "x%" PRId64, videoW_, videoH_);
            row("Video dimensions: ", buf);
        }

        row("HwDec: ", hwDec_.c_str());

        // Active graphics backend (OpenGL / Vulkan) — reflects any fallback, not just
        // the requested setting. Constant for the session, so query lazily once.
        if (gfxBackend_.empty())
        {
            if (auto* surface = ctx_ ? ctx_->GetService<IGraphicsSurface>() : nullptr)
            {
                gfxBackend_ = surface->GetGraphicsBackendName();
            }
        }
        if (!gfxBackend_.empty())
        {
            row("Graphics backend: ", gfxBackend_.c_str());
        }

        std::snprintf(
            buf, sizeof(buf), "Used: %" PRId64 " KB  (hits %" PRId64 " / miss %" PRId64 ")", cacheUsed_, cacheHits_,
            cacheMisses_
        );
        row("Cache: ", buf);

        std::snprintf(buf, sizeof(buf), "%" PRId64, dropped_);
        row("Dropped frames: ", buf);

        std::snprintf(buf, sizeof(buf), "%" PRId64, mistimed_);
        row("Mistimed frames: ", buf);

        ctx.Dummy({0.f, 3.f});

        std::snprintf(buf, sizeof(buf), "%.0f%%", volume_);
        row("Volume: ", buf);

        const UI::Vec2 winSize = ctx.GetMainWindowSize();
        std::snprintf(buf, sizeof(buf), "%dx%d", static_cast<int>(winSize.x), static_cast<int>(winSize.y));
        row("Window dimensions: ", buf);

        const char* status = isIdle_ ? "Idle" : (isPaused_ ? "Paused" : "Playing");
        row("Playback status: ", status);
    }
    ctx.End();

    ctx.PopStyleColor();
    ctx.PopStyleVar(2);
}
