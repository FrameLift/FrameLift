#include "App.h"
#include "GraphicsApi.h"
#include <framelift/Log.h>

#include <QtGui/QSurfaceFormat>
#include <QtQuick/QQuickWindow>
#include <QtWidgets/QApplication>
#include <exception>

int main(int argc, char* argv[])
{
    // Force the single-threaded "basic" scene-graph render loop so all GL work — Qt's
    // adopted context and our raw-GL video node — stays on the GUI thread (the GL video
    // renderer / FFmpeg RenderFrame assume single-threaded main-thread GL).
    qputenv("QSG_RENDER_LOOP", "basic");

    // Backend is chosen via FL_BACKEND before the Qt platform exists (see GraphicsApi.h).
    const GraphicsApi graphicsApi = GraphicsApiFromEnv();

    // Qt does not draw client-side window decorations for Vulkan-RHI windows on Wayland,
    // so a Vulkan window comes up with no title bar under GNOME/Mutter and other CSD
    // compositors. Run the Vulkan path through XWayland (xcb), where the compositor draws
    // server-side decorations. Only when the user hasn't pinned a platform themselves.
#if defined(__linux__)
    if (graphicsApi != GraphicsApi::OpenGL && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") &&
        !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
    {
        qputenv("QT_QPA_PLATFORM", "xcb");
    }
#endif

    // QApplication (not QGuiApplication) so the native QFileDialog open-file picker —
    // a QWidget — has the widgets application it requires. QApplication is a
    // QGuiApplication, so Qt Quick / raw PCM audio are unaffected.
    QApplication qtApp(argc, argv);
    QApplication::setOrganizationName("FrameLift");
    QApplication::setApplicationName("FrameLift");
    // We drive shutdown explicitly (window close → App quit flow), so don't let Qt quit
    // out from under the host when the last window closes.
    QApplication::setQuitOnLastWindowClosed(false);

    Log::Init();
    try
    {
        App app("FrameLift", 1280, 720, graphicsApi, argc, argv);
        return app.Run();
    }
    catch (const std::exception& e)
    {
        Log::Error("FrameLift startup failed: {}", e.what());
        return 1;
    }
}
