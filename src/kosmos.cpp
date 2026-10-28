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

int main(int argc, char **argv)
{
    kosmos::app::App app;
    return app.run(argc, argv);
}
