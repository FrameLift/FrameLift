#include "FFmpegPlayer.h"

#include "FFmpegPlayerInternal.h"

#include "FFmpegAudioOutput.h"
#include "FFmpegLetterbox.h"
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
#include "FFmpegVulkanDevice.h"
#endif

#include "IGraphicsBackend.h"

#include <cmath>
#include <mutex>
#include <utility>

// ── Rendering ─────────────────────────────────────────────────────────────────

void FFmpegPlayer::EnumerateAudioOutputDevices(void (*visit)(const AudioOutputDevice*, void*), void* ud) const noexcept
{
    audioOut_->EnumerateDevices(visit, ud);
}

void FFmpegPlayer::InitRender(void* graphicsBackend) noexcept
{
    auto* backend = static_cast<IGraphicsBackend*>(graphicsBackend);
    if (!backend)
    {
        return;
    }
    renderer_ = backend->CreateVideoRenderer();
    rendererReady_ = renderer_->Init(backend);
    if (!rendererReady_)
    {
        Log::Error("FFmpegPlayer: video renderer init failed; showing black");
    }

    // If the active backend is Vulkan and exposes a video-decode device, wrap it for
    // FFmpeg so we can decode straight onto the render device (#18). Non-fatal on
    // failure: vulkanZeroCopyAvailable_ stays false and PlayFile uses the readback /
    // CPU-RGBA8 paths.
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
    VulkanDeviceInfo vkInfo;
    if (backend->GetVulkanDeviceInfo(vkInfo) && vkInfo.supportsVideoDecode)
    {
        vkHwDevice_ = CreateVulkanHwDevice(vkInfo);
        vulkanZeroCopyAvailable_ = vkHwDevice_ != nullptr;
    }
#endif
}

void FFmpegPlayer::ReleaseRender() noexcept
{
    rendererReady_ = false;
    renderer_.reset();
#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
    vulkanZeroCopyAvailable_ = false;
    if (vkHwDevice_)
    {
        av_buffer_unref(&vkHwDevice_);
    }
#endif
}

void FFmpegPlayer::SetRenderUpdateCallback(void (*cb)(void*), void* ud) noexcept
{
    std::lock_guard lock(mutex_);
    renderCb_ = {cb, ud};
}

bool FFmpegPlayer::HasNewFrame() noexcept
{
    return frameGate_.HasNewFrame();
}

void FFmpegPlayer::RenderFrame(int w, int h) noexcept
{
    PrepareRenderFrame(w, h);
    DrawPreparedFrame(0, 0, w, h);
}

void FFmpegPlayer::PrepareRenderFrame(int w, int h) noexcept
{
    // Adopting a pending AVVkFrame releases the previously displayed one inside
    // the gate. The timeline semaphore (signalled by the renderer's sample
    // submit) keeps FFmpeg from reusing the image until our GPU read completes,
    // so dropping our ref there is safe even if a submit is still in flight.
    const VideoFrameGate::AcquireResult acq = frameGate_.Acquire();

    if (rendererReady_)
    {
        frameGate_.CommitDisplayChannel(acq);

#if FRAMELIFT_MODULE_GRAPHICS_VULKAN
        if (frameGate_.DisplayIsOpaque() && frameGate_.DisplayOpaque())
        {
            renderer_->UploadVulkanFrame(frameGate_.DisplayOpaque(), acq.w, acq.h);
        }
        else if (acq.newPixels)
#else
        if (acq.newPixels)
#endif
        {
            renderer_->UploadFrame(frameGate_.DisplayPixels().data(), acq.desc);
        }

        // Render the libass subtitle overlay at the on-screen video size so it stays
        // crisp regardless of the source resolution, then composite it in Draw.
        preparedOverlayActive_ = false;
        const int videoW = static_cast<int>(displayWidth_.load());
        const int videoH = static_cast<int>(displayHeight_.load());
        if (subtitlesEnabled_ && subtitles_ && subtitles_->Ok() && videoW > 0 && videoH > 0)
        {
            const LetterboxRect vp = ComputeLetterbox(w, h, videoW, videoH);
            const auto timeMs =
                static_cast<long long>(std::llround((GetSubtitleRenderClock() - subtitleDelay_.load()) * 1000.0));
            const FFmpegSubtitles::RenderResult res =
                subtitles_->RenderOverlay(vp.w, vp.h, videoW, videoH, timeMs, overlayScratch_);
            if (res == FFmpegSubtitles::RenderResult::Updated)
            {
                renderer_->UploadOverlay(overlayScratch_.data(), vp.w, vp.h);
                preparedOverlayActive_ = true;
            }
            else if (res == FFmpegSubtitles::RenderResult::Unchanged)
            {
                preparedOverlayActive_ = true; // reuse the already-uploaded overlay texture
            }
        }
    }
}

void FFmpegPlayer::DrawPreparedFrame(int fbX, int fbY, int fbW, int fbH) noexcept
{
    if (rendererReady_)
    {
        renderer_->Draw(fbX, fbY, fbW, fbH, preparedOverlayActive_);
    }
}
