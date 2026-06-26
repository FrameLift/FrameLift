#include "Benchmark.h"

#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <chrono>
#include <cstdint>
#include <string>

#include "Version.h"
#include <framelift/platform.h>

std::vector<framelift::SettingsField> Benchmark::SettingsFields()
{
    return {{"limitDuration", &limitDuration_, false}, {"benchmarkDuration", &benchmarkDuration_, 30.0f}};
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
    if (auto* props = ctx.GetService<IMediaProperties>())
    {
        props->ObserveProperty(PlayerProperty::TimePos);
    }
    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(16);
    connect(
        refreshTimer_, &QTimer::timeout, this,
        [this]
        {
            if (!open_)
            {
                lastFrameTick_ = {};
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            if (lastFrameTick_.time_since_epoch().count() != 0)
            {
                const double dtMs = std::chrono::duration<double, std::milli>(now - lastFrameTick_).count();
                frameMsSmoothed_ = frameMsSmoothed_ <= 0.0 ? dtMs : frameMsSmoothed_ + (dtMs - frameMsSmoothed_) * 0.1;
                if (accumulating_ && !complete_ && !isIdle_)
                {
                    frameStat_.Add(dtMs);
                }
            }
            lastFrameTick_ = now;
            if (std::chrono::duration<double>(now - lastRefresh_).count() >= refreshInterval)
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
            Q_EMIT changed();
        }
    );
    refreshTimer_->start();
}

QString Benchmark::Summary() const
{
    const double fps = frameMsSmoothed_ > 0.0 ? 1000.0 / frameMsSmoothed_ : 0.0;
    QStringList rows;
    rows << QStringLiteral("UI  %1 fps  ·  %2 ms").arg(fps, 0, 'f', 0).arg(frameMsSmoothed_, 0, 'f', 1);
    rows << QStringLiteral("CPU  %1%   Memory  %2 MB")
                .arg(sys_.cpuPercent, 0, 'f', 0)
                .arg(static_cast<double>(sys_.memBytes) / (1024.0 * 1024.0), 0, 'f', 1);
    rows << QStringLiteral("GPU  %1").arg(sys_.gpuValid ? QString::number(sys_.gpuPercent, 'f', 0) + "%" : "N/A");
    rows << QStringLiteral("Decoder  %1").arg(QString::fromStdString(hwDec_));
    rows << QStringLiteral("Dropped %1  ·  Mistimed %2").arg(dropped_).arg(mistimed_);
    return rows.join('\n');
}

void Benchmark::chooseFile()
{
    if (auto* dialog = ctx_ ? ctx_->GetService<IFileDialog>() : nullptr)
    {
        dialog->OpenFile(
            [](const char* path, const bool ok, void* ud)
            {
                if (ok)
                {
                    static_cast<Benchmark*>(ud)->StartRun(path);
                    Q_EMIT static_cast<Benchmark*>(ud)->changed();
                }
            },
            this
        );
    }
}

void Benchmark::resetRun()
{
    accumulating_ = false;
    ResetStats();
    Q_EMIT changed();
}

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
    player->LoadFile(path, 0.0);
    player->SetPause(false);
}

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

void Benchmark::RequestRefresh()
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
