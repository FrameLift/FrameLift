import QtQuick

FLGlassPanel {
    id: drawer
    property bool open: false
    property bool rightSide: false
    property real drawerWidth: 340
    // Set drawerWidthRatio to opt into responsive sizing. A zero ratio preserves
    // the historical fixed-width behaviour for existing drawers.
    property real drawerWidthRatio: 0
    property real minimumDrawerWidth: drawerWidth
    property real maximumDrawerWidth: drawerWidth
    readonly property real effectiveDrawerWidth: {
        if (drawerWidthRatio <= 0 || !parent)
            return drawerWidth
        const responsiveWidth = parent.width * drawerWidthRatio
        const clampedWidth = Math.max(minimumDrawerWidth,
                                      Math.min(maximumDrawerWidth, responsiveWidth))
        return Math.min(parent.width, clampedWidth)
    }
    signal visibleWidthChangedByAnimation(real width)

    // Overdraw 1px past every edge so the panel covers the sub-pixel seam that
    // otherwise lets the window/video show through where the flush edges land on
    // fractional device-pixel boundaries.
    readonly property int _bleed: 1

    // Suppresses the x animation until initial layout settles, so the drawer
    // doesn't slide across the screen when parent.width is still 0 at creation.
    // Latched true on the first frame the parent has a real width; never reset.
    property bool _ready: false
    property real _revealProgress: open ? 1 : 0
    onXChanged: if (!_ready && parent && parent.width > 0) _ready = true

    width: effectiveDrawerWidth + drawer._bleed * 2
    height: (parent ? parent.height : 0) + drawer._bleed * 2
    y: -drawer._bleed
    // Animate only the reveal fraction. Parent and drawer geometry remain direct
    // inputs, so a window resize moves even a closed drawer to its new off-screen
    // endpoint immediately instead of animating it through the enlarged viewport.
    x: rightSide
       ? (parent ? parent.width : 0) + drawer._bleed - drawer._revealProgress * width
       : -width + drawer._revealProgress * (width - drawer._bleed)
    radius: 0

    Behavior on _revealProgress {
        enabled: drawer._ready
        NumberAnimation {
            duration: 180
            easing.type: Easing.OutCubic
            onRunningChanged: drawer.visibleWidthChangedByAnimation(
                                  drawer.open ? drawer.width : 0)
        }
    }
}
