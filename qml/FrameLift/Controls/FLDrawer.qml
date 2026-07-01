import QtQuick

FLGlassPanel {
    id: drawer
    property bool open: false
    property bool rightSide: false
    property real drawerWidth: 340
    signal visibleWidthChangedByAnimation(real width)

    // Overdraw 1px past every edge so the panel covers the sub-pixel seam that
    // otherwise lets the window/video show through where the flush edges land on
    // fractional device-pixel boundaries.
    readonly property int _bleed: 1

    // Suppresses the x animation until initial layout settles, so the drawer
    // doesn't slide across the screen when parent.width is still 0 at creation.
    // Latched true on the first frame the parent has a real width; never reset.
    property bool _ready: false
    onXChanged: if (!_ready && parent && parent.width > 0) _ready = true

    width: drawerWidth + drawer._bleed * 2
    height: (parent ? parent.height : 0) + drawer._bleed * 2
    y: -drawer._bleed
    x: rightSide
       ? (parent ? parent.width : 0) - (open ? drawerWidth + drawer._bleed : -drawer._bleed)
       : (open ? -drawer._bleed : -width)
    radius: 0

    Behavior on x {
        enabled: drawer._ready
        NumberAnimation {
            duration: 180
            easing.type: Easing.OutCubic
            onRunningChanged: drawer.visibleWidthChangedByAnimation(
                                  drawer.open ? drawer.width : 0)
        }
    }
}
