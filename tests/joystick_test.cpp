#undef NDEBUG
#include "joystick.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace kosmos;

int main()
{
    AxisBinding b;
    b.deadzone = 0.1f;
    assert(joy_shape(0.05f, b, false) == 0.0f);
    assert(std::fabs(joy_shape(1.0f, b, false) - 1.0f) < 1e-5f);
    assert(std::fabs(joy_shape(-1.0f, b, false) + 1.0f) < 1e-5f);
    b.invert = true;
    assert(joy_shape(1.0f, b, false) < -0.99f);
    b.invert = false;
    b.expo = 1.0f;
    assert(std::fabs(joy_shape(0.55f, b, false)) < 0.2f);
    b.expo = 0.0f;
    b.scale = 0.5f;
    assert(std::fabs(joy_shape(1.0f, b, false) - 0.5f) < 1e-5f);

    // Levels (throttle) map the full travel to 0..1, and inversion flips it.
    AxisBinding t;
    assert(std::fabs(joy_shape(-1.0f, t, true)) < 1e-5f);
    assert(std::fabs(joy_shape(1.0f, t, true) - 1.0f) < 1e-5f);
    t.invert = true;
    assert(std::fabs(joy_shape(-1.0f, t, true) - 1.0f) < 1e-5f);

    // Profiles survive a round trip, including a custom binding and an unbound action.
    JoyProfile air = joy_default_profile(true), space = joy_default_profile(false);
    air.axes["pitch"].axis = 5;
    air.axes["pitch"].device = "Stick";
    air.axes["pitch"].expo = 0.4f;
    space.buttons["stage"].button = -1;
    bool enabled = false;
    JoyProfile air2, space2;
    assert(joy_profiles_from_json(joy_profiles_to_json(air, space, true), air2, space2, enabled));
    assert(enabled);
    assert(air2.axes["pitch"].axis == 5 && air2.axes["pitch"].device == "Stick");
    assert(std::fabs(air2.axes["pitch"].expo - 0.4f) < 1e-5f);
    assert(space2.buttons["stage"].button == -1);
    assert(air2.buttons["gear"].button == 1);
    assert(!joy_profiles_from_json("not json", air2, space2, enabled));

    // Each kind of vehicle has its own set of actions, and every default binding is one of them.
    for (const bool a : {true, false})
    {
        const JoyProfile p = joy_default_profile(a);
        for (const auto &[name, binding] : p.axes)
        {
            bool found = false;
            for (const std::string &n : joy_axis_actions(a))
                found = found || n == name;
            assert(found);
        }
        for (const auto &[name, binding] : p.buttons)
        {
            bool found = false;
            for (const std::string &n : joy_button_actions(a))
                found = found || n == name;
            assert(found);
        }
    }
    std::printf("joystick_test ok\n");
}
