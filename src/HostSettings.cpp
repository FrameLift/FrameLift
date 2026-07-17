#include "HostSettings.h"

#include "AudioSettings.h"
#include "CacheSettings.h"
#include "CoreSettings.h"
#include "PlaybackSettings.h"
#include "SubtitleSettings.h"
#include "ThemeSettings.h"
#include "UISettings.h"
#include "VideoDecodeMode.h"

#include <cstdlib>

HostSettings::HostSettings()
{
    RegisterSection<PlaybackSettings>(RegisterPlaybackSettings);
    RegisterSection<SubtitleSettings>(RegisterSubtitleSettings);
    RegisterSection<CacheSettings>(RegisterCacheSettings);
    RegisterSection<UISettings>(RegisterUISettings);
    RegisterSection<FilesSettings>(RegisterFilesSettings);
    RegisterSection<AudioSettings>(RegisterAudioSettings);
    RegisterSection<ThemeSettings>(RegisterThemeSettings);
    RegisterSection<KeybindSettings>(RegisterKeybindSettings);
}

void HostSettings::ApplyLaunchEnvironmentOverrides()
{
    if (const char* modeEnv = std::getenv("FL_ACCEL_MODE"); modeEnv && modeEnv[0])
    {
        PlaybackSettings& playback = Get<PlaybackSettings>();
        playback.hwdecMode = VideoDecodeModeName(VideoDecodeModeFromString(modeEnv));
    }
}
