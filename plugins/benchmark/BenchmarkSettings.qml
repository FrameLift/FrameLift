pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FrameLift.Controls
import FrameLift.Plugins.SettingsMenu

Item {
    id: root
    required property var viewModel

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        FLSettingRow {
            title: "Limit duration"
            description: "Stop benchmark accumulation after the configured playback duration."
            Switch { checked: root.viewModel.limitDuration; onToggled: root.viewModel.limitDuration = checked }
        }

        FLSettingRow {
            title: "Duration"
            description: "Benchmark run length in seconds."
            TextField {
                text: Number(root.viewModel.benchmarkDuration).toString()
                validator: DoubleValidator { bottom: 1.0 }
                implicitWidth: 180
                onEditingFinished: root.viewModel.benchmarkDuration = Number(text)
            }
        }

        Item { Layout.fillHeight: true }
    }
}
