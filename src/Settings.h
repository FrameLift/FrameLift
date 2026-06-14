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
    X(playback, hrSeek,             bool,        true,  "Use precise (high-resolution) seeking.") \
    X(playback, videoSync,          bool,        true,  "Synchronize video timing to the display refresh.") \
    X(playback, subAutoLoad,        bool,        true,  "Auto-load subtitle files matching the opened media.") \
    X(playback, audioFileAutoLoad,  bool,        true,  "Auto-load external audio files matching the opened media.") \
    /* ── UI ───────────────────────────────────────────────────────────────── */ \
    X(ui,       panelWidth,         float,     320.f,   "Width in pixels of the side panels (playlist, etc.).") \
    X(ui,       slideSpeed,         float,      18.f,   "Panel slide-in/out animation speed.") \
    /* ── Files ─────────────────────────────────────────────────────────────  */ \
    X(files,    videoExtensions,    std::string, "mp4;mkv;avi;mov;wmv;flv;webm;m4v;mpg;mpeg", "Semicolon-separated list of video file extensions.") \
    X(files,    imageExtensions,    std::string, "png;jpg;jpeg;gif;bmp;webp",                 "Semicolon-separated list of image file extensions.") \
    /* ── Audio ────────────────────────────────────────────────────────────── */ \
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