import QtQuick
import QtQuick.Controls

// Themed dropdown matching the dark glass theme. Use for enum-valued settings.
ComboBox {
    id: control
    implicitHeight: 32
    implicitWidth: 160
    font.pixelSize: 13

    contentItem: Text {
        leftPadding: 10
        rightPadding: control.indicator.width + 6
        text: control.displayText
        font: control.font
        color: FLTheme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: control.width - width - 8
        y: control.topPadding + (control.availableHeight - height) / 2
        text: "▾"
        color: FLTheme.textMuted
        font.pixelSize: 12
    }

    background: Rectangle {
        radius: 8
        color: FLTheme.inputBg
        border.color: control.activeFocus || control.popup.visible ? FLTheme.accent : FLTheme.border
        border.width: 1
        opacity: control.enabled ? 1 : 0.4
    }

    delegate: ItemDelegate {
        id: itemDelegate
        required property var modelData
        required property int index
        width: control.width
        height: 32

        contentItem: Text {
            text: itemDelegate.modelData
            color: FLTheme.text
            font.pixelSize: 13
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            color: control.currentIndex === itemDelegate.index ? FLTheme.accentSoft
                                                                : itemDelegate.hovered ? FLTheme.hover : "transparent"
        }
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 240)
        padding: 4

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            radius: 8
            color: FLTheme.surfaceStrong
            border.color: FLTheme.border
            border.width: 1
        }
    }
}
