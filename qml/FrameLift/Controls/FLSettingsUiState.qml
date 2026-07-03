pragma Singleton
import QtQuick

// Shared, session-scoped UI state for the settings window. A singleton so any
// settings page (core, plugin-contributed, or the generic renderer) and the
// shared FLSettingRow control can read the same toggle without prop-drilling.
QtObject {
    // When true, every FLSettingRow reveals the setting's internal INI
    // identifier ("[section]/name"). Driven by the "Show keys" toggle in the
    // settings window's actions bar.
    property bool showKeys: false
}
