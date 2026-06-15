#include "App.h"
#include <framelift/Log.h>

#include <SDL3/SDL_main.h>

int main(int argc, char* argv[])
{
    Log::Init();
    App app("FrameLift", 1280, 720, argc, argv);
    return app.Run();
}