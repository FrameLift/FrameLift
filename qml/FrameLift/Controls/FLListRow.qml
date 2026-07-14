import QtQuick

// Selectable list row shared by the Playlist and History panels. Encapsulates
// the highlight chrome (active/playing vs. keyboard-cursor) and the
// click-to-select / double-click-to-activate interaction. Declare the row
// content as children; wire selectRequested/activateRequested to the view model.
Rectangle {
    id: rowRoot
    property bool current: false   // active/playing entry — strong fill
    property bool selected: false  // keyboard/selection cursor — fill + outline
    default property alias content: holder.data
    signal selectRequested()
    signal activateRequested()

    width: ListView.view ? ListView.view.width : implicitWidth
    radius: 6
    color: rowRoot.current ? FLTheme.accentSoft
          : rowRoot.selected ? FLTheme.accentFaint
          : rowMouse.containsMouse ? FLTheme.hover : "transparent"
    border.width: rowRoot.selected && rowRoot.ListView.view
                  && rowRoot.ListView.view.activeFocus ? 1 : 0
    border.color: FLTheme.accent

    Item { id: holder; anchors.fill: parent }

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        width: 3
        height: Math.max(14, parent.height - 12)
        radius: 2
        color: FLTheme.accent
        visible: rowRoot.current
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: rowRoot.selectRequested()
        onDoubleClicked: rowRoot.activateRequested()
    }
}
