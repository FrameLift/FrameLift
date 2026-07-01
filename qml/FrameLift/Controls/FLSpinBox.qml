import QtQuick
import QtQuick.Controls

// Themed integer spin box matching the dark glass theme.
SpinBox {
    id: control
    implicitHeight: 32
    editable: true
    font.pixelSize: 13

    contentItem: TextInput {
        text: control.displayText
        font: control.font
        color: FLTheme.text
        selectionColor: FLTheme.accent
        selectedTextColor: FLTheme.text
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
    }

    background: Rectangle {
        implicitWidth: 120
        radius: 8
        color: FLTheme.inputBg
        border.color: control.activeFocus ? FLTheme.accent : FLTheme.border
        border.width: 1
        opacity: control.enabled ? 1 : 0.4
    }

    up.indicator: Rectangle {
        x: control.width - width
        height: parent.height
        implicitWidth: 26
        radius: 8
        color: control.up.pressed ? FLTheme.accentSoft : control.up.hovered ? FLTheme.hover : "transparent"
        Text {
            anchors.centerIn: parent
            text: "+"
            color: FLTheme.text
            font.pixelSize: 16
        }
    }

    down.indicator: Rectangle {
        height: parent.height
        implicitWidth: 26
        radius: 8
        color: control.down.pressed ? FLTheme.accentSoft : control.down.hovered ? FLTheme.hover : "transparent"
        Text {
            anchors.centerIn: parent
            text: "−"
            color: FLTheme.text
            font.pixelSize: 16
        }
    }
}
