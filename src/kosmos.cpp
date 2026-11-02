// Kosmos: a space flight simulator. The first milestone flies a two-stage launcher from the pad to Earth orbit.
//
//   B: wheel brakes   F: cycle flaps (aircraft)   G or L: landing gear up/down (retractable gear)   N: galaxy map
//   Keypad 0/. : hover thrust up/down   Home/End: hover on/off (ships with a hover group)
//   Keypad +/*: full / cut throttle   Ctrl+Keypad +/-: throttle up/down   Space: stage   X/Z: field of view
//   Keypad 2/8 1/3 4/6: pitch, yaw, bank   Keypad /: RCS rotation/linear   Keypad 5: SAS   T: cycle attitude hold   P: ascent autopilot
//   , / . : time warp   [ / ] : zoom   Right-drag: look   O: orbit line   M: sound   R: reset   Esc: exit
//
// The application itself is the App struct (app_state.h); its parts are in the app_*.cpp files.

#include "app_state.h"

#ifdef _WIN32
#include <windows.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace
{
    // kosmos.exe is a GUI program, so it opens no console of its own. Started from a terminal it writes there; with
    // --console it opens a console window when there is no terminal. Output that is already redirected is left alone.
    void connect_console(int argc, char **argv)
    {
        const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (out && out != INVALID_HANDLE_VALUE)
            return;
        bool want = false;
        for (int i = 1; i < argc; ++i)
            want = want || !std::strcmp(argv[i], "--console");
        if (!AttachConsole(ATTACH_PARENT_PROCESS) && !(want && AllocConsole()))
            return;
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
        std::freopen("CONIN$", "r", stdin);
    }
} // namespace
#endif

int main(int argc, char **argv)
{
#ifdef _WIN32
    connect_console(argc, argv);
#endif
    kosmos::app::App app;
    return app.run(argc, argv);
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return main(__argc, __argv);
}
#endif
