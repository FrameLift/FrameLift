import QtQuick
import QtQuick.Controls

// Compact action used by overlay-panel headers and toolbars. It is neutral by
// default, gains an accent treatment while checked, and can opt into destructive
// colouring without turning every panel action into a primary button.
Button {
    id: control

    property bool destructive: false
    property string toolTipText: ""

    implicitHeight: 24
    implicitWidth: Math.max(24, contentItem.implicitWidth + leftPadding + rightPadding)
    leftPadding: 7
    rightPadding: 7
    topPadding: 3
    bottomPadding: 3
    spacing: 4
    font.pixelSize: 11
    opacity: enabled ? 1 : 0.4

    Accessible.name: text.length > 0 ? text : toolTipText

    contentItem: Text {
        text: control.text
        color: control.destructive ? FLTheme.danger : FLTheme.text
        font.family: control.font.family
        font.pixelSize: control.font.pixelSize
        font.weight: control.checked ? Font.DemiBold : Font.Normal
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 6
        color: {
            if (control.checked) {
                return control.destructive
                        ? Qt.rgba(FLTheme.danger.r, FLTheme.danger.g, FLTheme.danger.b, 0.24)
                        : FLTheme.accentSoft
            }
            return control.down || control.hovered ? FLTheme.hover : FLTheme.inputBg
        }
        border.width: 1
        border.color: control.checked
                      ? (control.destructive ? FLTheme.danger : FLTheme.accent)
                      : FLTheme.border
    }

    ToolTip.visible: toolTipText.length > 0 && hovered
    ToolTip.text: toolTipText
    ToolTip.delay: 500
}
