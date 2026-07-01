#include "DebugOverlay.h"
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <chrono>
#include <cstdint>
#include <string>

#include "Version.h"
#include <framelift/platform.h>

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
    // Push-observe EOF so it arrives via OnMediaEvent (no synchronous getter).
    // Pause/idle are already observed by the host. Seeking/paused-for-cache are
    // deliberately NOT shown: they are transient and, for sparse streams, can stick
    // (e.g. an empty subtitle queue keeps a worker "stalled"), which would wedge the
    // status line at "Buffering" during normal playback.
    if (auto* props = ctx.GetService<IMediaProperties>())
    {
        props->ObserveProperty(PlayerProperty::EofReached);
    }

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(1000);
    connect(
        refreshTimer_, &QTimer::timeout, this,
        [this]
        {
            RequestRefresh();
            Q_EMIT changed();
        }
    );
    // Timer is gated on open state (started in SetOpen), not running while hidden.
}

void DebugOverlay::SetOpen(const bool open)
{
    if (open_ == open)
    {
        return;
    }
    open_ = open;
    if (refreshTimer_)
    {
        if (open_)
        {
            RequestRefresh(); // refresh immediately so the overlay isn't stale on show
            refreshTimer_->start();
        }
        else
        {
            refreshTimer_->stop();
        }
    }
    Q_EMIT changed();
}

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
            dropped_ = mistimed_ = decodeErrors_ = cacheUsed_ = 0;
            eofReached_ = false;
            audioTrackCount_ = subTrackCount_ = 0;
            audioSelLabel_.clear();
            subSelLabel_.clear();
        }
        Q_EMIT changed(); // discrete transition — refresh now
        return;
    }

    if (type != PropertyType::Flag && type != PropertyType::Double)
    {
        return;
    }

    // Only discrete, user-meaningful transitions (pause / eof) trigger an immediate
    // refresh; the high-frequency time-pos tick is cached silently and picked up by
    // the 1 s poll, so the overlay doesn't re-render on every event.
    switch (prop)
    {
    case PlayerProperty::Pause:
        isPaused_ = value.flag != 0;
        Q_EMIT changed();
        break;
    case PlayerProperty::EofReached:
        eofReached_ = value.flag != 0;
        Q_EMIT changed();
        break;
    case PlayerProperty::TimePos:
        timePos_ = value.dbl >= 0.0 ? value.dbl : 0.0;
        break;
    case PlayerProperty::Duration:
        duration_ = value.dbl > 0.0 ? value.dbl : 0.0;
        break;
    default:
        break;
    }
}

namespace
{
// "1:23:45" / "2:05" — compact clock from seconds.
QString FormatClock(const double seconds)
{
    const auto total = static_cast<int64_t>(seconds < 0.0 ? 0.0 : seconds);
    const int64_t h = total / 3600, m = total / 60 % 60, s = total % 60;
    return h > 0 ? QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'))
                 : QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

// Human-readable size from a KiB count (cache-used is reported in KB).
QString FormatKiB(const int64_t kib)
{
    if (kib >= 1024)
    {
        return QStringLiteral("%1 MiB").arg(static_cast<double>(kib) / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 KiB").arg(kib);
}

QVariantMap Section(const QString& title, const QStringList& lines)
{
    return {{QStringLiteral("title"), title}, {QStringLiteral("body"), lines.join('\n')}};
}
} // namespace

QVariantList DebugOverlay::Sections() const
{
    QVariantList sections;

    // ── Playback ──
    QString status;
    if (isIdle_)
    {
        status = QStringLiteral("Idle (no file)");
    }
    else if (isPaused_)
    {
        status = QStringLiteral("Paused");
    }
    else
    {
        status = QStringLiteral("Playing");
    }
    if (eofReached_)
    {
        status += QStringLiteral(" · EOF");
    }
    sections << Section(
        QStringLiteral("Playback"),
        {QStringLiteral("File   %1").arg(QString::fromStdString(title_.empty() ? filePath_ : title_)),
         QStringLiteral("State  %1   %2×").arg(status).arg(speed_, 0, 'f', 2),
         QStringLiteral("Time   %1 / %2  (%3%)")
             .arg(FormatClock(timePos_))
             .arg(FormatClock(duration_))
             .arg(percentPos_, 0, 'f', 0)}
    );

    // ── Video ──
    QString gfx = gfxBackend_.empty() ? QStringLiteral("unknown") : QString::fromStdString(gfxBackend_);
    if (gpuIsNvidia_)
    {
        gfx += QStringLiteral(" · NVIDIA");
    }
    sections << Section(
        QStringLiteral("Video"),
        {QStringLiteral("Size   %1×%2").arg(videoW_).arg(videoH_),
         QStringLiteral("HwDec  %1").arg(QString::fromStdString(hwDec_)), QStringLiteral("GFX    %1").arg(gfx)}
    );

    // ── Audio ──
    QString track = audioTrackCount_ > 0
                        ? QStringLiteral("%1 track(s)  %2")
                              .arg(audioTrackCount_)
                              .arg(audioSelLabel_.empty() ? QString() : QString::fromStdString(audioSelLabel_))
                        : QStringLiteral("none");
    sections << Section(
        QStringLiteral("Audio"),
        {QStringLiteral("Vol    %1%2   Norm %3")
             .arg(volume_, 0, 'f', 0)
             .arg(isMuted_ ? QStringLiteral(" (muted)") : QString())
             .arg(normalize_ ? QStringLiteral("on") : QStringLiteral("off")),
         QStringLiteral("Track  %1").arg(track),
         QStringLiteral("Out    %1  ·  %2")
             .arg(audioChannelMode_.empty() ? QStringLiteral("auto") : QString::fromStdString(audioChannelMode_))
             .arg(audioDevice_.empty() ? QStringLiteral("system default") : QString::fromStdString(audioDevice_))}
    );

    // ── Subtitles ──
    QString sub = subTrackCount_ > 0 ? QStringLiteral("%1 track(s)  %2")
                                           .arg(subTrackCount_)
                                           .arg(subSelLabel_.empty() ? QString() : QString::fromStdString(subSelLabel_))
                                     : QStringLiteral("none");
    sections << Section(
        QStringLiteral("Subtitles"),
        {QStringLiteral("State  %1").arg(subsEnabled_ ? QStringLiteral("on") : QStringLiteral("off")),
         QStringLiteral("Track  %1").arg(sub)}
    );

    // ── Decode & Cache ──
    sections << Section(
        QStringLiteral("Decode & Cache"),
        {QStringLiteral("Frames  drop %1  ·  mistime %2  ·  err %3").arg(dropped_).arg(mistimed_).arg(decodeErrors_),
         QStringLiteral("Cache   %1  ·  %2 hit / %3 miss").arg(FormatKiB(cacheUsed_)).arg(cacheHits_).arg(cacheMisses_)}
    );

    return sections;
}

namespace
{
const char* ChannelModeName(const AudioChannelMode mode)
{
    switch (mode)
    {
    case AudioChannelMode::Mono:
        return "mono";
    case AudioChannelMode::Stereo:
        return "stereo";
    case AudioChannelMode::Surround:
        return "surround";
    case AudioChannelMode::Auto:
        break;
    }
    return "auto";
}
} // namespace

void DebugOverlay::RequestRefresh()
{
    // Audio facet (synchronous getters + enumerations).
    if (auto* audio = ctx_ ? ctx_->GetService<IAudioControl>() : nullptr)
    {
        isMuted_ = audio->IsMuted();
        normalize_ = audio->IsNormalizeEnabled();
        const AudioPreferences prefs = audio->GetAudioPreferences();
        audioChannelMode_ = ChannelModeName(prefs.channelMode);
        audioDevice_ = prefs.outputDevice; // empty ⇒ system default

        struct AudioAcc
        {
            int count = 0;
            std::string sel;
        } acc;

        audio->EnumerateAudioTracks(
            [](const AudioTrack* t, void* ud)
            {
                auto* a = static_cast<AudioAcc*>(ud);
                ++a->count;
                if (t->selected)
                {
                    a->sel = t->label;
                }
            },
            &acc
        );
        audioTrackCount_ = acc.count;
        audioSelLabel_ = acc.sel;
    }

    // Subtitle facet.
    if (auto* subs = ctx_ ? ctx_->GetService<ISubtitleControl>() : nullptr)
    {
        subsEnabled_ = subs->IsSubtitlesEnabled();

        struct SubAcc
        {
            int count = 0;
            std::string sel;
        } acc;

        subs->EnumerateSubtitleTracks(
            [](const SubtitleTrack* t, void* ud)
            {
                auto* a = static_cast<SubAcc*>(ud);
                ++a->count;
                if (t->selected)
                {
                    a->sel = t->label;
                }
            },
            &acc
        );
        subTrackCount_ = acc.count;
        subSelLabel_ = acc.sel;
    }

    // Graphics backend (session-constant once the window exists).
    if (auto* gfx = ctx_ ? ctx_->GetService<IGraphicsInfo>() : nullptr)
    {
        char buf[64];
        gfx->GetBackendName(buf, sizeof(buf));
        gfxBackend_ = buf;
        gpuIsNvidia_ = gfx->HasNvidiaAdapter();
    }

    auto* player = ctx_ ? ctx_->GetService<IMediaProperties>() : nullptr;
    if (!player)
    {
        return;
    }

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
    player->GetDoubleAsync(PlayerProperty::Speed, dblCb, new DblField{&speed_, 1.0});
    player->GetDoubleAsync(PlayerProperty::PercentPos, dblCb, new DblField{&percentPos_, 0.0});
    player->GetInt64Async(PlayerProperty::DroppedFrames, i64Cb, new I64Field{&dropped_, 0});
    player->GetInt64Async(PlayerProperty::MistimedFrames, i64Cb, new I64Field{&mistimed_, 0});
    player->GetInt64Async(PlayerProperty::DecodeErrors, i64Cb, new I64Field{&decodeErrors_, 0});
    player->GetInt64Async(PlayerProperty::CacheUsed, i64Cb, new I64Field{&cacheUsed_, 0});
    player->GetInt64Async(PlayerProperty::CacheHits, i64Cb, new I64Field{&cacheHits_, 0});
    player->GetInt64Async(PlayerProperty::CacheMisses, i64Cb, new I64Field{&cacheMisses_, 0});
}
