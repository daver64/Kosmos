#undef NDEBUG
#include "keybindings.h"

#include <cassert>
#include <cstdio>

using namespace kosmos;
using K = sl::Key;

int main()
{
    KeyBindings keys;

    // Defaults: plain and Shift variants of one key are different actions.
    assert(keys.pressed("bomb", K::letter_k, false));
    assert(!keys.pressed("bomb", K::letter_k, true));
    assert(keys.pressed("bomb_special", K::letter_k, true));
    assert(keys.pressed("gear", K::letter_g, false) && keys.pressed("gear", K::letter_l, false));
    // A plain key still fires with Shift held when nothing claims the Shift variant.
    assert(keys.pressed("throttle_full", K::letter_z, true));
    assert(!keys.pressed("nothing", K::letter_z, false));

    // Rebinding takes the key from a clashing action in the same context, not from one in another context.
    assert(keys.assign("bomb", 0, {K::letter_g, false}) == "Landing gear up / down");
    assert(!keys.pressed("gear", K::letter_g, false) && keys.pressed("gear", K::letter_l, false));
    assert(keys.pressed("bomb", K::letter_g, false));
    assert(keys.assign("walk_forward", 0, {K::letter_s, false}).empty() == false);
    assert(keys.pressed("pitch_up", K::letter_s, false)); // flying context kept S
    assert(keys.find("walk_forward")->chord[0].key == K::letter_s);

    // Clearing and resetting.
    keys.clear("gear", 1);
    assert(keys.find("gear")->chord[1].empty());
    keys.reset("gear");
    assert(keys.pressed("gear", K::letter_g, false) && keys.pressed("gear", K::letter_l, false));

    // Learning ignores a bare Shift for plain actions and takes the chord on the next key.
    keys.begin_learn("lamps", 0);
    bool done = true;
    keys.learn_key(K::left_shift, true, done);
    assert(!done && keys.learning());
    keys.learn_key(K::letter_u, true, done);
    assert(done && !keys.learning());
    assert(keys.pressed("lamps", K::letter_u, true));
    keys.begin_learn("lamps", 0);
    keys.learn_key(K::escape, false, done);
    assert(done && keys.pressed("lamps", K::letter_u, true));
    // A held action may itself be a modifier.
    keys.begin_learn("throttle_up", 0);
    keys.learn_key(K::right_shift, true, done);
    assert(done && keys.find("throttle_up")->chord[0].key == K::right_shift && !keys.find("throttle_up")->chord[0].shift);

    // Only changed actions are saved, and they round-trip.
    const std::string saved = keys.to_json();
    KeyBindings again;
    again.from_json(saved);
    assert(again.pressed("lamps", K::letter_u, true));
    assert(again.find("throttle_up")->chord[0].key == K::right_shift);
    assert(again.is_default(*again.find("stage")));
    assert(saved.find("\"stage\"") == std::string::npos);
    keys.reset_all();
    assert(KeyBindings().to_json() == keys.to_json());

    // Addon actions.
    sl::PropertyTree props;
    assert(keys.add_from_json(R"({"actions":[{"id":"mod.spot","label":"Spotlight","property":"/controls/spot","key":"letter_b","mode":"toggle"}]})", "test").empty());
    assert(!keys.add_from_json(R"({"actions":[{"id":"mod.spot","property":"/x"}]})", "test").empty());
    assert(!keys.add_from_json("{", "test").empty());
    assert(keys.press_custom(props, K::letter_b, false) && props.get_bool("/controls/spot"));
    assert(keys.press_custom(props, K::letter_b, false) && !props.get_bool("/controls/spot"));
    assert(!keys.press_custom(props, K::letter_c, false));

    assert(KeyBindings::chord_name({K::letter_k, true}) == "Shift+K");
    assert(KeyBindings::chord_name({}) == "-");
    assert(keys.label_for("gear") == "G / L");
    std::puts("keybindings test passed");
    return 0;
}
