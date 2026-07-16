pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root
    required property var viewModel
    anchors.fill: parent
    readonly property ContextMenuContent menuContent: menuLoader.item as ContextMenuContent

    function openMenu() {
        if (root.menuContent !== null) {
            root.menuContent.popup()
            return
        }
        menuLoader.setSource("ContextMenuContent.qml", { viewModel: root.viewModel })
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        propagateComposedEvents: true
        onClicked: root.openMenu()
    }

    Loader {
        id: menuLoader
        anchors.fill: parent
        onLoaded: root.menuContent.popup()
    }
}
