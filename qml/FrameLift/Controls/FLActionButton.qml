import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl

Button {
    id: control
    implicitHeight: 36
    padding: 10
    font.pixelSize: 13

    property color accentColor: FLTheme.accent

    // Tint monochrome SVG icons to match the label colour. IconLabel routes this
    // through IconImage, so `icon.source` buttons recolour without extra effects.
    icon.color: FLTheme.text
    icon.width: 18
    icon.height: 18

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        icon: control.icon
        text: control.text
        font: control.font
        color: FLTheme.text
    }

    background: Rectangle {
        radius: 8
        color: control.down ? Qt.darker(control.accentColor, 1.25)
                            : control.hovered ? Qt.lighter(control.accentColor, 1.08) : control.accentColor
        opacity: control.enabled ? 1 : 0.4
    }
}
