import QtQuick
import QtQuick.Controls

// ListView for plugin panels. Up/Down/Return are handled natively by Qt's
// ListView whenever the list has keyboard focus, including its built-in,
// minimal scroll-to-current behaviour — so navigation never yanks an
// already-visible row. `active` is a convenience hook the panel binds to its
// open state; the panel's own onActiveChanged should call forceActiveFocus()
// (and seed currentIndex) so the arrows work immediately when it opens.
ListView {
    id: list
    clip: true
    spacing: 1
    keyNavigationEnabled: true
    property bool active: false

    ScrollBar.vertical: ScrollBar {
        policy: ScrollBar.AsNeeded
    }

    // Native navigation keeps the current row visible on its own; we only need
    // to re-assert it when the viewport is resized, and only when the row is
    // actually clipped (never scrolling a row that is already fully visible).
    function keepCurrentInView() {
        if (currentIndex < 0)
            return
        var item = itemAtIndex(currentIndex)
        if (!item) {
            positionViewAtIndex(currentIndex, ListView.Contain)
            return
        }
        if (item.y < contentY)
            positionViewAtIndex(currentIndex, ListView.Beginning)
        else if (item.y + item.height > contentY + height)
            positionViewAtIndex(currentIndex, ListView.End)
    }
    onHeightChanged: keepCurrentInView()
}
