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

void DebugOverlay::OnInstall(IModuleContext&)
{
    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(1000);
    connect(
        refreshTimer_, &QTimer::timeout, this,
        [this]
        {
            if (open_)
            {
                RequestRefresh();
                Q_EMIT changed();
            }
        }
    );
    refreshTimer_->start();
}

void DebugOverlay::HandleMediaEvent(const MediaEvent& event)
{
    if (event.type != MediaEventType::PropertyChange)
    {
        return;
    }

    const auto& [prop, type, value] = event.property;
    Q_EMIT changed();

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
    }
}

QString DebugOverlay::Summary() const
{
    QStringList rows;
    rows << QStringLiteral("File  %1").arg(QString::fromStdString(title_.empty() ? filePath_ : title_));
    rows << QStringLiteral("Time  %1 / %2 s").arg(timePos_, 0, 'f', 1).arg(duration_, 0, 'f', 1);
    rows << QStringLiteral("Video  %1×%2  ·  %3").arg(videoW_).arg(videoH_).arg(QString::fromStdString(hwDec_));
    rows << QStringLiteral("Graphics  %1").arg(QString::fromStdString(gfxBackend_));
    rows << QStringLiteral("Dropped %1  ·  Mistimed %2  ·  Errors %3").arg(dropped_).arg(mistimed_).arg(decodeErrors_);
    rows << QStringLiteral("Cache  %1 bytes  ·  %2 hits / %3 misses").arg(cacheUsed_).arg(cacheHits_).arg(cacheMisses_);
    return rows.join('\n');
}

void DebugOverlay::RequestRefresh()
{
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
    player->GetInt64Async(PlayerProperty::DroppedFrames, i64Cb, new I64Field{&dropped_, 0});
    player->GetInt64Async(PlayerProperty::MistimedFrames, i64Cb, new I64Field{&mistimed_, 0});
    player->GetInt64Async(PlayerProperty::DecodeErrors, i64Cb, new I64Field{&decodeErrors_, 0});
    player->GetInt64Async(PlayerProperty::CacheUsed, i64Cb, new I64Field{&cacheUsed_, 0});
    player->GetInt64Async(PlayerProperty::CacheHits, i64Cb, new I64Field{&cacheHits_, 0});
    player->GetInt64Async(PlayerProperty::CacheMisses, i64Cb, new I64Field{&cacheMisses_, 0});
}
