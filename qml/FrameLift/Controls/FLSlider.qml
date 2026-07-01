import QtQuick
import QtQuick.Controls

// Themed slider for bounded numeric settings (volume, opacity, scale...).
Slider {
    id: control
    implicitWidth: 180
    implicitHeight: 24

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 4
        radius: 2
        color: FLTheme.inputBg

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 2
            color: FLTheme.accent
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 16
        height: 16
        radius: 8
        color: control.pressed ? Qt.lighter(FLTheme.accent, 1.15) : FLTheme.text
        border.color: FLTheme.accent
        border.width: 2
        opacity: control.enabled ? 1 : 0.4
    }
}
