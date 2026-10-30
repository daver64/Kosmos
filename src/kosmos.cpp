// Kosmos: a space flight simulator. The first milestone flies a two-stage launcher from the pad to Earth orbit.
//
//   B: wheel brakes   F: cycle flaps (aircraft)   G or L: landing gear up/down (retractable gear)   N: galaxy map
//   Keypad +/-/0/.: hover engines (ships with a hover group)
//   Z/X: full / cut throttle   Shift/Ctrl: raise / lower throttle   Space: stage
//   W/S: pitch   A/D: yaw   Q/E: roll   Arrows: RCS translate   T: cycle attitude hold   P: ascent autopilot
//   , / . : time warp   [ / ] : zoom   Right-drag: look   O: orbit line   M: sound   R: reset   Esc: exit
//
// The application itself is the App struct (app_state.h); its parts are in the app_*.cpp files.

#include "app_state.h"

#ifdef _WIN32
#include <windows.h>
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
