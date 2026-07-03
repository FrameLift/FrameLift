pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import FrameLift.Controls
import FrameLift.Plugins.SettingsMenu

Item {
    id: root
    required property var viewModel

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Repeater {
            model: [
                { "label": "Debug", "prop": "showDebug" },
                { "label": "Info", "prop": "showInfo" },
                { "label": "Warnings", "prop": "showWarn" },
                { "label": "Errors", "prop": "showError" },
                { "label": "Performance only", "prop": "perfOnly" }
            ]
            delegate: FLSettingRow {
                required property var modelData
                title: modelData.label
                description: modelData.prop === "perfOnly"
                             ? "Show only performance log entries in the console."
                             : "Include this log level in the console output."
                keyName: "console." + modelData.prop
                FLSwitch {
                    checked: root.viewModel[modelData.prop]
                    onToggled: root.viewModel[modelData.prop] = checked
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
