#pragma once

#include "SettingsRegistry.h"

#include <string>

// Host-core settings with no narrower owning module — file-type lists and the
// core keybinds consumed directly by App.

struct FilesSettings
{
    std::string videoExtensions = "mp4;mkv;avi;mov;wmv;flv;webm;m4v;mpg;mpeg";
    std::string imageExtensions = "png;jpg;jpeg;gif;bmp;webp";
    bool rememberOpenDialogDirectory = true;
};

struct KeybindSettings
{
    std::string togglePause = "Space";
    std::string toggleFullscreen = "F";
    std::string quit = "Ctrl+Q";
    std::string volumeUp = "Up";
    std::string volumeDown = "Down";
    std::string toggleMute = "M";
    std::string seekForward = "Right";
    std::string seekBack = "Left";
    std::string seekForwardLong = "Shift+Right";
    std::string seekBackLong = "Shift+Left";
    std::string toggleNormalize = "Shift+N";
    std::string toggleSubtitles = "Shift+W";
    std::string openFileDialog = "Ctrl+F";
};

inline void RegisterFilesSettings(SettingsRegistry& reg, FilesSettings& s)
{
    reg.AddString("files.videoExtensions", s.videoExtensions, "Semicolon-separated list of video file extensions.");
    reg.AddString("files.imageExtensions", s.imageExtensions, "Semicolon-separated list of image file extensions.");
    reg.AddBool(
        "files.rememberOpenDialogDirectory", s.rememberOpenDialogDirectory,
        "Remember the last open-file dialog directory."
    );
}

inline void RegisterKeybindSettings(SettingsRegistry& reg, KeybindSettings& s)
{
    reg.AddString("keybinds.togglePause", s.togglePause, "Key combo to play/pause.");
    reg.AddString("keybinds.toggleFullscreen", s.toggleFullscreen, "Key combo to toggle fullscreen.");
    reg.AddString("keybinds.quit", s.quit, "Key combo to quit the application.");
    reg.AddString("keybinds.volumeUp", s.volumeUp, "Key combo to raise the volume.");
    reg.AddString("keybinds.volumeDown", s.volumeDown, "Key combo to lower the volume.");
    reg.AddString("keybinds.toggleMute", s.toggleMute, "Key combo to mute/unmute.");
    reg.AddString("keybinds.seekForward", s.seekForward, "Key combo to seek forward (short step).");
    reg.AddString("keybinds.seekBack", s.seekBack, "Key combo to seek backward (short step).");
    reg.AddString("keybinds.seekForwardLong", s.seekForwardLong, "Key combo to seek forward (long step).");
    reg.AddString("keybinds.seekBackLong", s.seekBackLong, "Key combo to seek backward (long step).");
    reg.AddString("keybinds.toggleNormalize", s.toggleNormalize, "Key combo to toggle audio normalization.");
    reg.AddString("keybinds.toggleSubtitles", s.toggleSubtitles, "Key combo to toggle subtitle visibility.");
    reg.AddString("keybinds.openFileDialog", s.openFileDialog, "Key combo to open the file picker.");
}
