#include "Benchmark.h"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <string>

#include "Version.h"
#include <framelift/platform.h>

// ── ModuleBase hooks ───────────────────────────────────────────────────────

std::vector<framelift::SettingsField> Benchmark::SettingsFields()
{
    return {
        {"limitDuration", &limitDuration_, false},
        {"benchmarkDuration", &benchmarkDuration_, 30.0f}
    };
}

std::vector<framelift::Keybind> Benchmark::Keybinds()
{
    return {
        {"Toggle benchmark overlay", "toggleBenchmark", &toggleBenchmarkKey_, "F10", [this]
         {
             Toggle();
         }}
    };
}

void Benchmark::OnInstall(IModuleContext& ctx)
{
    // Observe playback position so the duration limit fires even when the
    // Overlay plugin (the usual TimePos observer) is disabled. Idempotent.
    if (auto* props = ctx.GetService<IMediaProperties>())
    {
        props->ObserveProperty(PlayerProperty::TimePos);
    }
    SetupSettingsPage(ctx, true);
}

void Benchmark::RenderSettings(UIContext& ctx)
{
    Widgets::SectionHeader(ctx, "Benchmark run");
    Widgets::Checkbox(
        ctx, "Limit playback duration",
        "Pause playback and freeze the results once playback reaches the duration below.", limitDuration_
    );
    Widgets::SliderFloat(
        ctx, "Duration", "Playback length of a benchmark run, in seconds.", benchmarkDuration_, 5.0f, 600.0f
    );
}

// ── Run control ────────────────────────────────────────────────────────────

void Benchmark::ResetStats()
{
    frameStat_.Reset();
    cpuStat_.Reset();
    memStat_.Reset();
    gpuStat_.Reset();
    complete_ = false;
}

void Benchmark::StartRun(const char* path)
{
    auto* player = ctx_ ? ctx_->GetService<IMediaPlayback>() : nullptr;
    if (!player || !path || !path[0])
    {
        return;
    }
    ResetStats();
    timePos_ = 0.0;
    accumulating_ = true;
    player->LoadFile(path, 0.0); // always from the start for a reproducible run
    player->SetPause(false);
}

// ── Media events ──────────────────────────────────────────────────────────────

void Benchmark::HandleMediaEvent(const MediaEvent& event)
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
            hwDec_ = "N/A";
            dropped_ = mistimed_ = 0;
        }
        // Note: don't clear accumulating_ here — loading a file can flip idle
        // transiently. Sample folding is gated on !isIdle_ in OnRender, so an
        // idle gap simply pauses collection without ending the run.
        return;
    }

    if (prop == PlayerProperty::TimePos && type == PropertyType::Double)
    {
        timePos_ = value.dbl >= 0.0 ? value.dbl : 0.0;
        if (accumulating_ && !complete_ && limitDuration_ && timePos_ >= static_cast<double>(benchmarkDuration_))
        {
            if (auto* player = ctx_ ? ctx_->GetService<IMediaPlayback>() : nullptr)
            {
                player->SetPause(true);
            }
            complete_ = true;
            accumulating_ = false;
        }
    }
}

// ── Async property refresh ────────────────────────────────────────────────────

void Benchmark::RequestRefresh()
{
    auto* player = ctx_ ? ctx_->GetService<IMediaProperties>() : nullptr;
    if (!player)
    {
        return;
    }

    // One-shot callbacks that write a value into a member field.
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

    player->GetStringAsync(PlayerProperty::HwDecCurrent, strCb, new StrField{&hwDec_, "N/A"});
    player->GetInt64Async(PlayerProperty::DroppedFrames, i64Cb, new I64Field{&dropped_, 0});
    player->GetInt64Async(PlayerProperty::MistimedFrames, i64Cb, new I64Field{&mistimed_, 0});
}

// ── Render ────────────────────────────────────────────────────────────────────

void Benchmark::OnRender(UIContext& ctx)
{
    if (!open_)
    {
        return;
    }

    // ── UI frame timing (every frame) ───────────────────────────────────────────
    // Folded outside the 1 Hz refresh below so min/avg/max capture real per-frame
    // jitter; frameMsSmoothed_ drives the live fps readout (raw 1/dt is too noisy).
    const double dtMs = static_cast<double>(ctx.GetDeltaTime()) * 1000.0;
    if (dtMs > 0.0)
    {
        frameMsSmoothed_ = frameMsSmoothed_ <= 0.0 ? dtMs : frameMsSmoothed_ + (dtMs - frameMsSmoothed_) * 0.1;
        if (accumulating_ && !complete_ && !isIdle_)
        {
            frameStat_.Add(dtMs);
        }
    }

    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - lastRefresh_).count();
    if (elapsed >= refreshInterval)
    {
        lastRefresh_ = now;
        RequestRefresh();
        sys_ = sampler_.Sample();

        if (accumulating_ && !complete_ && !isIdle_)
        {
            cpuStat_.Add(sys_.cpuPercent);
            memStat_.Add(static_cast<double>(sys_.memBytes));
            if (sys_.gpuValid)
            {
                gpuStat_.Add(sys_.gpuPercent);
            }
        }
    }

    constexpr float kW = 300.f;
    constexpr float kH = 360.f;

    // Seed the window beside the app once per open so the multi-viewport backend
    // spawns it as its own OS window (positioned past the main window's edge)
    // rather than nesting it inside the main viewport. Mirrors Panel::RenderPoppedOut.
    if (justSeeded_)
    {
        constexpr float gap = 30.f;
        const UI::Vec2 mainPos = ctx.GetMainWindowScreenPos();
        const UI::Vec2 mainSize = ctx.GetMainWindowSize();
        float screenX = mainPos.x + mainSize.x + gap;
        float screenY = mainPos.y + 40.f;

        if (auto* win = ctx_ ? ctx_->GetService<IAppWindow>() : nullptr)
        {
            // Keep the whole window on the display the main window lives on.
            const Rect b = win->GetDisplayUsableBounds();
            if (b.w > 0 && b.h > 0)
            {
                screenX = std::clamp(screenX, static_cast<float>(b.x), static_cast<float>(b.x + b.w) - kW);
                screenY = std::clamp(screenY, static_cast<float>(b.y), static_cast<float>(b.y + b.h) - kH);
            }
        }

        ctx.SetNextWindowPos({screenX - mainPos.x, screenY - mainPos.y}, UI::Cond::Always);
        ctx.SetNextWindowSize({kW, kH}, UI::Cond::Always);
        justSeeded_ = false;
    }

    // A normal titled/movable/resizable window. NoSavedSettings: the seeded pop-out
    // placement is per-session and must not leak into imgui.ini.
    const UI::WindowFlags flags =
        UI::WindowFlags::NoScrollbar | UI::WindowFlags::NoCollapse | UI::WindowFlags::NoSavedSettings;

    ctx.PushStyleVar(UI::StyleVar::WindowRounding, 4.f);
    ctx.PushStyleVar(UI::StyleVar::WindowPadding, {10.f, 8.f});
    ctx.PushStyleColor(UI::ColorSlot::WindowBg, UI::Color4f(0.04f, 0.04f, 0.04f, 1.f)); // opaque for an OS window

    if (ctx.Begin("Benchmark", &open_, flags))
    {
        constexpr UI::Color4f kLabel = {0.65f, 0.65f, 0.65f, 1.f};
        constexpr UI::Color4f kValue = {1.f, 1.f, 0.f, 1.f};

        char buf[256];

        constexpr UI::Color4f kStat = {0.5f, 0.5f, 0.5f, 1.f};

        const auto row = [&](const char* label, const char* value)
        {
            ctx.TextColored(kLabel, label);
            ctx.SameLine();
            ctx.TextColored(kValue, value);
        };

        // Running min/avg/max sub-line for one metric. scale converts the stored
        // value to display units (e.g. bytes → MB); fmt has three placeholders.
        const auto statRow = [&](const Stat& s, const double scale, const char* fmt)
        {
            if (s.count == 0)
            {
                ctx.TextColored(kStat, "   min/avg/max: -");
                return;
            }
            std::snprintf(buf, sizeof(buf), fmt, s.min * scale, s.Avg() * scale, s.max * scale);
            ctx.TextColored(kStat, buf);
        };

        const double fps = frameMsSmoothed_ > 0.0 ? 1000.0 / frameMsSmoothed_ : 0.0;
        std::snprintf(buf, sizeof(buf), "%.0f fps (%.1f ms)", fps, frameMsSmoothed_);
        row("UI: ", buf);
        statRow(frameStat_, 1.0, "   min %.1f / avg %.1f / max %.1f ms");

        std::snprintf(buf, sizeof(buf), "%.0f%%", sys_.cpuPercent);
        row("CPU: ", buf);
        statRow(cpuStat_, 1.0, "   min %.0f / avg %.0f / max %.0f %%");

        std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(sys_.memBytes) / (1024.0 * 1024.0));
        row("Memory: ", buf);
        statRow(memStat_, 1.0 / (1024.0 * 1024.0), "   min %.0f / avg %.0f / max %.0f MB");

        if (sys_.gpuValid)
        {
            std::snprintf(buf, sizeof(buf), "%.0f%%", sys_.gpuPercent);
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "N/A");
        }
        row("GPU: ", buf);
        statRow(gpuStat_, 1.0, "   min %.0f / avg %.0f / max %.0f %%");

        ctx.Dummy({0.f, 3.f});

        // hwdec-current is empty or "no" when decoding in software.
        const bool hwActive = !hwDec_.empty() && hwDec_ != "no" && hwDec_ != "N/A";
        if (hwActive)
        {
            std::snprintf(buf, sizeof(buf), "Hardware (%s)", hwDec_.c_str());
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "%s", isIdle_ ? "N/A" : "Software");
        }
        row("Decoder: ", buf);

        std::snprintf(buf, sizeof(buf), "%" PRId64, dropped_);
        row("Dropped frames: ", buf);

        std::snprintf(buf, sizeof(buf), "%" PRId64, mistimed_);
        row("Mistimed frames: ", buf);

        ctx.Dummy({0.f, 4.f});
        ctx.Separator();

        // ── Run controls ────────────────────────────────────────────────────────
        if (complete_)
        {
            std::snprintf(buf, sizeof(buf), "Complete (%.0fs)", static_cast<double>(benchmarkDuration_));
            ctx.TextColored(UI::Color4f(0.4f, 0.9f, 0.5f, 1.f), buf);
        }
        else if (accumulating_ && !isIdle_)
        {
            if (limitDuration_)
            {
                std::snprintf(
                    buf, sizeof(buf), "Recording... %.0fs / %.0fs", timePos_, static_cast<double>(benchmarkDuration_)
                );
            }
            else
            {
                std::snprintf(buf, sizeof(buf), "Recording... %.0fs", timePos_);
            }
            ctx.TextColored(UI::Color4f(0.9f, 0.7f, 0.3f, 1.f), buf);
        }
        else if (accumulating_)
        {
            ctx.TextColored(kLabel, "Waiting for playback...");
        }
        else
        {
            ctx.TextColored(kLabel, "Idle");
        }

        if (ctx.Button("Load file"))
        {
            if (auto* fd = ctx_ ? ctx_->GetService<IFileDialog>() : nullptr)
            {
                fd->OpenFile(
                    [](const char* path, const bool ok, void* ud)
                    {
                        if (ok && path && path[0])
                        {
                            static_cast<Benchmark*>(ud)->StartRun(path);
                        }
                    },
                    this
                );
            }
        }
        ctx.SameLine();
        if (ctx.Button("Reset"))
        {
            ResetStats();
            accumulating_ = !isIdle_; // keep recording if a file is still playing
        }
    }
    ctx.End();

    ctx.PopStyleColor();
    ctx.PopStyleVar(2);
}
