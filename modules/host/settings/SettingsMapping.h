#pragma once

// Umbrella header. The pure Settings→player-option mappers now live in their
// owning modules; this header just re-exports them so host call sites keep one
// include. Each mapper takes its module's settings sub-struct.
//
//   ffmpeg     : ToAudioNormalizeParams, ToPlaybackOptions, ToVideoDecodeMode,
//                ToAudioPreferences, ToSubtitleStyle
//   read-ahead : ToReadAheadCacheOptions

#include "CacheSettings.h"          // host/read-ahead  (ToReadAheadCacheOptions)
#include "FFmpegSettingsMapping.h"  // media/ffmpeg
