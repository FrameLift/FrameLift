#pragma once

#include <string>
#include <vector>

// ── Settings field table ───────────────────────────────────────────────────────
// X(section, name, type, default, desc)
// Adding a new setting: add ONE row here — nothing else to change.
// The ini key is derived automatically as "section.name". `desc` is written as a
// "# ..." comment above the key in settings.ini so the file is self-documenting.
// clang-format off
#define SETTINGS_FIELDS(X) \
    /* ── General ──────────────────────────────────────────────────────────── */ \
    X(general,  maxDisplayRatio,    float,       0.8f,  "Max fraction of the screen used when auto-sizing the window to the video (0.0-1.0).") \
    /* ── Playback ────────────────────────────────────────────────────────── */ \
    X(playback, hwdec,              bool,        true,  "Enable hardware video decoding.") \
    X(playback, hwdecMode,          std::string, "auto",  "Video acceleration mode: off, auto, vulkan-zero-copy, vulkan, cuda-zero-copy, cuda, d3d11va, dxva2, or vaapi.") \
    X(playback, hrSeek,             bool,        true,  "Use precise (high-resolution) seeking.") \
    X(playback, videoSync,          bool,        true,  "Synchronize video timing to the display refresh.") \
    X(playback, subAutoLoad,        bool,        true,  "Auto-load subtitle files matching the opened media.") \
    X(playback, audioFileAutoLoad,  bool,        true,  "Auto-load external audio files matching the opened media.") \
    /* ── Subtitles ────────────────────────────────────────────────────────── */ \
    X(subtitles, overrideStyle,     bool,        false, "Override the subtitle file's own styling with the settings below.") \
    X(subtitles, fontScale,         float,       1.0f,  "Subtitle font-size multiplier (1.0 = the file's default size).") \
    X(subtitles, fontFamily,        std::string, "",    "Subtitle font family; empty keeps the file's font.") \
    X(subtitles, textColor,         std::string, "#FFFFFF", "Subtitle text colour as #RRGGBB.") \
    X(subtitles, outlineColor,      std::string, "#000000", "Subtitle outline colour as #RRGGBB.") \
    X(subtitles, backColor,         std::string, "#000000", "Subtitle shadow / box background colour as #RRGGBB.") \
    X(subtitles, backOpacity,       float,       0.5f,  "Opacity of the shadow / box background (0.0-1.0).") \
    X(subtitles, edgeStyle,         int,         1,     "Edge style: 0 none, 1 outline, 2 drop shadow, 3 opaque box.") \
    X(subtitles, outlineWidth,      float,       2.0f,  "Outline thickness in pixels.") \
    X(subtitles, shadowDepth,       float,       0.0f,  "Drop-shadow offset in pixels.") \
    X(subtitles, alignment,         int,         2,     "Numpad alignment 1-9 (\\an); 0 keeps the file's alignment.") \
    X(subtitles, lineSpacing,       float,       0.0f,  "Extra space between subtitle lines, pixels.") \
    X(subtitles, letterSpacing,     float,       0.0f,  "Extra space between glyphs, pixels.") \
    X(subtitles, defaultLanguage,   std::string, "",    "Preferred subtitle language to auto-select (ISO 639 code, e.g. eng).") \
    X(subtitles, preferForced,      bool,        false, "Prefer a forced subtitle track when one is available.") \
    /* ── Cache ────────────────────────────────────────────────────────────── */ \
    X(cache,    readAheadEnabled,   bool,        true,  "Enable the memory-bounded demuxer read-ahead cache.") \
    X(cache,    readAheadSizeMB,    int,           64,  "Read-ahead demuxer cache size in MB (total across audio/video/subtitle).") \
    /* ── Graphics ─────────────────────────────────────────────────────────── */ \
    X(graphics, backend,            std::string, "vulkan",  "Video rendering backend: vulkan or gl (falls back to gl if Vulkan is unavailable). Takes effect on restart.") \
    /* ── UI ───────────────────────────────────────────────────────────────── */ \
    X(ui,       panelWidth,         float,     320.f,   "Width in pixels of the side panels (playlist, etc.).") \
    X(ui,       slideSpeed,         float,      18.f,   "Panel slide-in/out animation speed.") \
    /* ── Files ─────────────────────────────────────────────────────────────  */ \
    X(files,    videoExtensions,    std::string, "mp4;mkv;avi;mov;wmv;flv;webm;m4v;mpg;mpeg", "Semicolon-separated list of video file extensions.") \
    X(files,    imageExtensions,    std::string, "png;jpg;jpeg;gif;bmp;webp",                 "Semicolon-separated list of image file extensions.") \
    /* ── Audio ────────────────────────────────────────────────────────────── */ \
    X(audio,    defaultAudioLanguage,std::string, "",    "Preferred audio language to auto-select (ISO 639 code, e.g. eng).") \
    X(audio,    outputDevice,       std::string, "",    "Preferred audio output device name; empty uses the system default.") \
    X(audio,    defaultVolume,      int,          100,   "Default playback volume (0-100).") \
    X(audio,    syncOffsetMs,       int,            0,   "Audio sync offset in milliseconds; positive delays audio relative to video.") \
    X(audio,    channelMode,        int,            0,   "Audio channel mode: 0 auto, 1 mono, 2 stereo, 3 surround.") \
    X(audio,    duckingEnabled,     bool,        false,  "Reduce playback volume while app-owned transient audio is active.") \
    X(audio,    duckingLevel,       int,           50,   "Playback gain while ducked, as percent of current volume.") \
    X(audio,    normalizeEnabled,   bool,        false,  "Enable dynamic audio normalization by default.") \
    X(audio,    dynaudnormFrameLen, int,          100,  "Dynamic audio normalization: filter frame length in milliseconds.") \
    X(audio,    dynaudnormGaussSize,int,            5,  "Dynamic audio normalization: Gaussian filter window size (odd number).") \
    X(audio,    dynaudnormPeak,     float,        0.95f,"Dynamic audio normalization: target peak magnitude (0.0-1.0).") \
    X(audio,    dynaudnormMaxGain,  float,         5.0f,"Dynamic audio normalization: maximum gain factor.") \
    X(audio,    dynaudnormVolume,   float,         1.5f,"Dynamic audio normalization: target RMS volume factor.") \
    /* ── Theme ────────────────────────────────────────────────────────────── */ \
    X(theme,    preset,             std::string, "dark",    "UI color preset: dark|light|classic.") \
    X(theme,    accentColor,        std::string, "#4296FA", "Accent color as #RRGGBB.") \
    X(theme,    fontFile,           std::string, "",        "Absolute path to a TTF font; empty = bundled Roboto.") \
    X(theme,    fontSize,           float,        16.0f,    "UI font size in points.") \
    /* ── Keybinds (core — used directly by App.cpp) ──────────────────────── */ \
    X(keybinds, togglePause,        std::string, "Space",       "Key combo to play/pause.") \
    X(keybinds, toggleFullscreen,   std::string, "F",           "Key combo to toggle fullscreen.") \
    X(keybinds, quit,               std::string, "Ctrl+Q",      "Key combo to quit the application.") \
    X(keybinds, volumeUp,           std::string, "Up",          "Key combo to raise the volume.") \
    X(keybinds, volumeDown,         std::string, "Down",        "Key combo to lower the volume.") \
    X(keybinds, toggleMute,         std::string, "M",           "Key combo to mute/unmute.") \
    X(keybinds, seekForward,        std::string, "Right",       "Key combo to seek forward (short step).") \
    X(keybinds, seekBack,           std::string, "Left",        "Key combo to seek backward (short step).") \
    X(keybinds, seekForwardLong,    std::string, "Shift+Right", "Key combo to seek forward (long step).") \
    X(keybinds, seekBackLong,       std::string, "Shift+Left",  "Key combo to seek backward (long step).") \
    X(keybinds, toggleNormalize,    std::string, "Shift+N",     "Key combo to toggle audio normalization.") \
    X(keybinds, toggleSubtitles,    std::string, "Shift+W",     "Key combo to toggle subtitle visibility.") \
    X(keybinds, openFileDialog,     std::string, "Ctrl+F",      "Key combo to open the file picker.")
// clang-format on

// ── Application settings (persisted as key=value text) ────────────────────────
struct Settings
{
    static constexpr const char* InterfaceId = "framelift.Settings";
#define X(section, name, type, def, desc) type name = def;
    SETTINGS_FIELDS(X)
#undef X

    // List of plugin base names to load from the plugins/ directory.
    // Serialized under [plugins] enabled=Name1;Name2;... in the ini file.
    std::vector<std::string> enabledPlugins = {"Playlist",     "History",   "Overlay",      "DebugOverlay",
                                               "SettingsMenu", "Benchmark", "RemoteStream", "Updater"};

    // Read settings from an ini-style "section.name=value" file at path.
    // Missing keys are left at their defaults; unknown keys are silently ignored.
    void Load(const std::string& path);
    // Write all settings to path synchronously, merging around sections and
    // keys owned by plugins so their data is preserved.
    void Save(const std::string& path) const;
};
