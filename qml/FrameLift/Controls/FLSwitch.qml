import QtQuick
import QtQuick.Controls

// Themed on/off switch matching the dark glass theme.
Switch {
    id: control
    padding: 0
    spacing: 0
    implicitWidth: 44
    implicitHeight: 24

    indicator: Rectangle {
        implicitWidth: 44
        implicitHeight: 24
        radius: height / 2
        color: control.checked ? FLTheme.accent : FLTheme.inputBg
        border.color: control.checked ? FLTheme.accent : FLTheme.border
        border.width: 1
        opacity: control.enabled ? 1 : 0.4

        Behavior on color {
            ColorAnimation { duration: 120 }
        }

        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            width: 18
            height: 18
            radius: height / 2
            color: FLTheme.text

            Behavior on x {
                NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
            }
        }
    }

    contentItem: Item {}
}
