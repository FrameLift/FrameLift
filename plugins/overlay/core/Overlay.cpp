#include "Overlay.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <utility>

#include "IconData.h"
#include "Version.h"
#include <framelift/core.h>
#include <framelift/ui.h>

#include <algorithm>

namespace
{
Overlay* Instance = nullptr;
}

void Overlay::OnInstall(IModuleContext& ctx)
{
    Instance = this;

    // Both services are discovered and may be null; playback_ is null-checked at
    // each use site (TogglePause/SeekAbsolute), props_ is guarded here before its
    // immediate ObserveProperty calls.
    playback_ = ctx.GetService<IMediaPlayback>();
    props_ = ctx.GetService<IMediaProperties>();
    if (props_)
    {
        props_->ObserveProperty(PlayerProperty::IdleActive);
        props_->ObserveProperty(PlayerProperty::TimePos);
        props_->ObserveProperty(PlayerProperty::Duration);
        props_->ObserveProperty(PlayerProperty::Pause);
    }

    framelift::Subscribe<NotificationEvent>(
        ctx,
        [this](const NotificationEvent& e)
        {
            ShowCommand(e.text);
        }
    );

    framelift::Subscribe<PanelLayoutEvent>(
        ctx,
        [this](const PanelLayoutEvent& e)
        {
            (e.side == 0 ? leftInset_ : rightInset_) = e.visibleWidth;
        }
    );

    framelift::Subscribe<SettingsVisibilityEvent>(
        ctx,
        [this](const SettingsVisibilityEvent& e)
        {
            settingsOpen_ = e.open;
        }
    );
}

void Overlay::ShowCommand(std::string label)
{
    commandLabel_ = std::move(label);
    shownAt_ = std::chrono::steady_clock::now();
}

bool Overlay::HandleEvent(const AppEvent& e)
{
    if (e.type == AppEventType::MouseMotion || e.type == AppEventType::MouseButtonDown)
    {
        mouseActiveAt_ = std::chrono::steady_clock::now();
    }
    return false;
}

void Overlay::HandleMediaEvent(const MediaEvent& event)
{
    if (event.type != MediaEventType::PropertyChange)
    {
        return;
    }

    const auto& [prop, type, value] = event.property;

    if (prop == PlayerProperty::IdleActive && type == PropertyType::Flag)
    {
        isIdle_ = value.flag != 0;
        return;
    }

    if (prop == PlayerProperty::Pause && type == PropertyType::Flag)
    {
        isPaused_ = value.flag != 0;
        return;
    }

    if (type != PropertyType::Double)
    {
        return;
    }

    const double val = value.dbl;
    if (prop == PlayerProperty::TimePos)
    {
        timePos_ = val >= 0.0 ? val : 0.0;
    }
    if (prop == PlayerProperty::Duration)
    {
        duration_ = val > 0.0 ? val : 0.0;
    }
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

// ── Controls bar ──────────────────────────────────────────────────────────────

void Overlay::RenderControlsBar(const float w, const float h, UIContext& ctx)
{
    const double barElapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - mouseActiveAt_).count();
    if (barElapsed >= barVisible + barFade)
    {
        isDraggingSeek_ = false;
        return;
    }

    float alpha = 1.f;
    if (barElapsed > barVisible)
    {
        alpha = 1.f - static_cast<float>((barElapsed - barVisible) / barFade);
    }
    alpha = std::clamp(alpha, 0.f, 1.f);

    constexpr float kBarH = 56.f;
    constexpr float kPadX = 16.f;
    constexpr float kIconW = 22.f; // play triangle width
    constexpr float kIconH = 22.f; // play triangle height
    constexpr float kTimeW = 116.f;
    constexpr float kTrackH = 4.f;
    constexpr float kThumbR = 7.f;

    const float barTop = h - kBarH;
    const float midY = barTop + kBarH * 0.5f;

    // Inset to avoid overlapping open/animating panels (kept current by
    // PanelLayoutEvent subscriptions).
    const float barL = leftInset_;
    const float barR = w - rightInset_;

    const auto& dl = ctx.GetForegroundDrawList();

    // Background
    dl.AddRectFilled({barL, barTop}, {barR, h}, UI::MakeColor32(0, 0, 0, static_cast<uint8_t>(180 * alpha)));

    // ── Play / pause icon ─────────────────────────────────────────────────────
    const float iconX = barL + kPadX;
    const auto iconA = static_cast<uint8_t>(220 * alpha);

    if (isPaused_)
    {
        dl.AddTriangleFilled(
            {iconX, midY - kIconH * 0.5f}, {iconX + kIconW, midY}, {iconX, midY + kIconH * 0.5f},
            UI::MakeColor32(220, 220, 220, iconA)
        );
    }
    else
    {
        constexpr float barW = 6.f;
        constexpr float gap = 5.f;
        const float px = iconX + (kIconW - barW * 2.f - gap) * 0.5f;
        dl.AddRectFilled(
            {px, midY - kIconH * 0.5f}, {px + barW, midY + kIconH * 0.5f}, UI::MakeColor32(220, 220, 220, iconA)
        );
        dl.AddRectFilled(
            {px + barW + gap, midY - kIconH * 0.5f}, {px + barW * 2.f + gap, midY + kIconH * 0.5f},
            UI::MakeColor32(220, 220, 220, iconA)
        );
    }

    // ── Seek track ────────────────────────────────────────────────────────────
    const float trackLeft = barL + kPadX + kIconW + kPadX;
    const float trackRight = barR - kPadX - kTimeW - kPadX;
    const float trackW = trackRight - trackLeft;

    const float frac = duration_ > 0.0 ? std::clamp(static_cast<float>(timePos_ / duration_), 0.f, 1.f) : 0.f;

    dl.AddRectFilled(
        {trackLeft, midY - kTrackH * 0.5f}, {trackRight, midY + kTrackH * 0.5f},
        UI::MakeColor32(80, 80, 80, static_cast<uint8_t>(200 * alpha)), kTrackH * 0.5f
    );

    if (frac > 0.f)
    {
        dl.AddRectFilled(
            {trackLeft, midY - kTrackH * 0.5f}, {trackLeft + frac * trackW, midY + kTrackH * 0.5f},
            UI::MakeColor32(220, 220, 220, static_cast<uint8_t>(230 * alpha)), kTrackH * 0.5f
        );
    }

    const float thumbX = trackLeft + frac * trackW;
    dl.AddCircleFilled({thumbX, midY}, kThumbR, UI::MakeColor32(255, 255, 255, static_cast<uint8_t>(255 * alpha)));

    // ── Time text ─────────────────────────────────────────────────────────────
    const std::string timeStr = FormatTime(timePos_) + " / " + FormatTime(duration_);
    dl.AddText(
        {barR - kPadX - kTimeW, midY - 6.f}, UI::MakeColor32(220, 220, 220, static_cast<uint8_t>(220 * alpha)),
        timeStr.c_str()
    );

    // ── Interaction ───────────────────────────────────────────────────────────
    const UI::Vec2 mouse = ctx.GetMousePos();
    const bool lclicked = ctx.IsMouseClicked(0);
    const bool ldown = ctx.IsMouseDown(0);

    if (!ldown)
    {
        isDraggingSeek_ = false;
    }

    // Play / pause click
    if (lclicked && mouse.y >= barTop && mouse.y <= h && mouse.x >= iconX - 6.f && mouse.x <= iconX + kIconW + 6.f)
    {
        if (playback_)
        {
            playback_->TogglePause();
        }
    }

    // Seek bar: begin drag on click, continue while held
    const bool overTrack = mouse.y >= barTop - kThumbR && mouse.y <= h && mouse.x >= trackLeft - kThumbR &&
                           mouse.x <= trackRight + kThumbR;
    if (lclicked && overTrack)
    {
        isDraggingSeek_ = true;
    }

    if (isDraggingSeek_ && ldown && duration_ > 0.0 && playback_)
    {
        const float newFrac = std::clamp((mouse.x - trackLeft) / trackW, 0.f, 1.f);
        playback_->SeekAbsolute(static_cast<double>(newFrac) * duration_);
    }
}

// ── Render ────────────────────────────────────────────────────────────────────

void Overlay::OnRender(UIContext& ctx)
{
    const UI::Vec2 mainSize = ctx.GetMainWindowSize();
    const float w = mainSize.x;
    const float h = mainSize.y;

    // ── Idle / welcome screen ─────────────────────────────────────────────────
    if (isIdle_)
    {
        auto& bg = ctx.GetBackgroundDrawList();

        constexpr UI::Color32 colTL = UI::MakeColor32(160, 0, 160, 255);
        constexpr UI::Color32 colTR = UI::MakeColor32(220, 20, 0, 255);
        constexpr UI::Color32 colBL = UI::MakeColor32(0, 0, 200, 255);
        constexpr UI::Color32 colBR = UI::MakeColor32(0, 220, 0, 255);

        bg.AddRectFilledMultiColor({0.f, 0.f}, {w, h}, colTL, colTR, colBR, colBL);

        const float cx = w * 0.5f;
        const float cy = h * 0.5f;

        if (!iconLoadAttempted_)
        {
            iconLoadAttempted_ = true;
            iconTex_ = ctx.LoadTextureFromMemory(kIconData, kIconDataSize);
        }

        if (iconTex_)
        {
            const float size = std::min(w, h) * 0.22f;
            const float half = size * 0.5f;
            bg.AddImage(iconTex_, {cx - half, cy - half}, {cx + half, cy + half}, UI::MakeColor32(255, 255, 255, 200));
        }
        // No early return: notifications (e.g. "File not found") must still draw
        // over the idle screen. The controls bar below is skipped while idle.
    }

    // Suppress the foreground HUD while the settings menu is open
    // (tracked via SettingsVisibilityEvent).
    if (settingsOpen_)
    {
        return;
    }

    // ── HUD: command label ────────────────────────────────────────────────────
    const double hudElapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - shownAt_).count();
    if (hudElapsed < fadeDelay + fadeDur)
    {
        float alpha = 1.0f;
        if (hudElapsed > fadeDelay)
        {
            alpha = 1.0f - static_cast<float>((hudElapsed - fadeDelay) / fadeDur);
        }
        alpha = std::clamp(alpha, 0.f, 1.f);

        auto& dl = ctx.GetForegroundDrawList();

        if (!commandLabel_.empty())
        {
            dl.AddText(
                {12.f, 10.f}, UI::MakeColor32(255, 255, 255, static_cast<uint8_t>(255 * alpha)), commandLabel_.c_str()
            );
        }
    }

    // ── Controls bar ─────────────────────────────────────────────────────────
    if (!isIdle_)
    {
        RenderControlsBar(w, h, ctx);
    }
}
