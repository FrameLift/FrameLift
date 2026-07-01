pragma Singleton
import QtQuick

QtObject {
    readonly property color canvas: "#050509"
    readonly property color surface: "#D914141D"
    readonly property color surfaceStrong: "#F01B1B27"
    readonly property color border: "#2AFFFFFF"
    readonly property color text: "#F2F2F7"
    readonly property color textMuted: "#9A9AA8"
    readonly property color accent: "#8B5CF6"
    readonly property color danger: "#EF4444"

    // Input / interaction tokens (shared by the themed control set).
    readonly property color inputBg: "#26FFFFFF"     // field fill over the dark canvas
    readonly property color hover: "#18FFFFFF"        // subtle hover overlay
    readonly property color accentSoft: "#408B5CF6"   // selected / active highlight
    readonly property color accentFaint: "#208B5CF6"  // keyboard-cursor highlight

    readonly property int radius: 12
    readonly property int spacing: 12
}
