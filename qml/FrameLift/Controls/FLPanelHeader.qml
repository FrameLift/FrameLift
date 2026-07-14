import QtQuick
import QtQuick.Layouts

// Shared header shell for slide-in panels. Instance children are inserted into
// the action row immediately before the always-present close affordance.
RowLayout {
    id: root

    property string title: ""
    property string countText: ""
    property string closeToolTip: "Close panel"
    default property alias actions: actionRow.data
    signal closeRequested()

    spacing: 5
    implicitHeight: 26

    Text {
        text: root.title
        color: FLTheme.text
        font.pixelSize: 15
        font.weight: Font.DemiBold
        elide: Text.ElideRight
        Layout.fillWidth: true
    }

    Rectangle {
        visible: root.countText.length > 0
        radius: 6
        color: FLTheme.inputBg
        border.width: 1
        border.color: FLTheme.border
        implicitWidth: countLabel.implicitWidth + 10
        implicitHeight: 20

        Text {
            id: countLabel
            anchors.centerIn: parent
            text: root.countText
            color: FLTheme.textMuted
            font.pixelSize: 10
            font.weight: Font.DemiBold
        }
    }

    RowLayout {
        id: actionRow
        spacing: 3
    }

    FLPanelAction {
        text: "×"
        toolTipText: root.closeToolTip
        font.pixelSize: 15
        leftPadding: 6
        rightPadding: 6
        onClicked: root.closeRequested()
    }
}
