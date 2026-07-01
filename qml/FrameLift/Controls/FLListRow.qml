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
    border.width: rowRoot.selected && !rowRoot.current ? 1 : 0
    border.color: FLTheme.accent

    Item { id: holder; anchors.fill: parent }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: rowRoot.selectRequested()
        onDoubleClicked: rowRoot.activateRequested()
    }
}
