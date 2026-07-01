import QtQuick
import QtQuick.Controls

// Themed single-line text field matching the dark glass theme.
TextField {
    id: control
    implicitHeight: 32
    leftPadding: 10
    rightPadding: 10
    color: FLTheme.text
    placeholderTextColor: FLTheme.textMuted
    selectionColor: FLTheme.accent
    selectedTextColor: FLTheme.text
    font.pixelSize: 13

    background: Rectangle {
        radius: 8
        color: FLTheme.inputBg
        border.color: control.activeFocus ? FLTheme.accent : FLTheme.border
        border.width: 1
        opacity: control.enabled ? 1 : 0.4
    }
}
