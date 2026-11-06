#undef NDEBUG
#include "keybindings.h"
#include <imgui.h>

#include <cassert>
#include <cstdio>
#include <set>

using namespace kosmos;
using K = sl::Key;

static void check_imgui_ids(const KeyBindings &bindings)
{
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1000, 100000);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    const auto groups = bindings.action_groups();
    assert(groups.size() < 16);
    for (unsigned int mask = 0; mask < (1u << groups.size()); ++mask)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(900, 99000));
        ImGui::Begin("Keyboard test");
        std::set<ImGuiID> ids;
        for (std::size_t group_index = 0; group_index < groups.size(); ++group_index)
        {
            const auto &group = groups[group_index];
            ImGui::PushID(static_cast<int>(group_index));
            assert(ids.insert(ImGui::GetID(group.name.c_str())).second);
            ImGui::SetNextItemOpen((mask & (1u << group_index)) != 0, ImGuiCond_Always);
            const bool open = ImGui::CollapsingHeader(group.name.c_str());
            assert(open == ((mask & (1u << group_index)) != 0));
            if (open)
                for (std::size_t action_index = 0; action_index < group.actions.size(); ++action_index)
                {
                    ImGui::PushID(static_cast<int>(action_index));
                    for (int slot = 0; slot < 2; ++slot)
                    {
                        ImGui::PushID(slot);
                        const std::string label = KeyBindings::chord_name(group.actions[action_index]->chord[slot]);
                        assert(ids.insert(ImGui::GetID(label.c_str())).second);
                        ImGui::Button(label.c_str());
                        ImGui::PopID();
                    }
                    ImGui::PopID();
                }
            ImGui::PopID();
        }
        ImGui::End();
        ImGui::Render();
    }
    ImGui::DestroyContext();
}

int main()
{
    KeyBindings keys;

    const auto check_groups = [](const KeyBindings &bindings) {
        const auto groups = bindings.action_groups();
        std::set<std::string> names, ids;
        for (const auto &group : groups)
        {
            assert(names.insert(group.name).second);
            assert(!group.actions.empty());
            for (const KeyAction *action : group.actions)
            {
                assert(action->group == group.name);
                assert(ids.insert(action->id).second);
            }
        }
        assert(ids.size() == bindings.actions().size());
        for (const auto &action : bindings.actions())
            assert(ids.count(action.id) == 1);
        return groups;
    };
    const auto groups = check_groups(keys);
    assert(groups.size() >= 3 && groups[0].name == "Windows" && groups[1].name == "View and time" && groups[2].name == "Ship controls");
    bool found_hover_off = false, found_rcs_mode = false;
    for (const KeyAction *action : groups[2].actions)
    {
        found_hover_off |= action->id == "hover_off";
        found_rcs_mode |= action->id == "rcs_mode";
    }
    assert(found_hover_off && found_rcs_mode);
    KeyBindings grouped_addons;
    assert(grouped_addons.add_from_json(R"({"actions":[{"id":"test.one","group":"Ship controls","property":"/one"},{"id":"test.two","group":"New category","property":"/two"},{"id":"test.three","group":"Ship controls","property":"/three"}]})", "test").empty());
    assert(grouped_addons.add_from_json(R"({"actions":[{"id":"test.four","group":"First###shared","property":"/four"},{"id":"test.five","group":"Second###shared","property":"/five"}]})", "test").empty());
    const auto addon_groups = check_groups(grouped_addons);
    assert(addon_groups[2].actions[addon_groups[2].actions.size() - 2]->id == "test.one");
    assert(addon_groups[2].actions.back()->id == "test.three");
    assert(addon_groups[addon_groups.size() - 3].name == "New category");
    check_imgui_ids(grouped_addons);

    // Defaults: plain and Shift variants of one key are different actions.
    assert(keys.pressed("bomb", K::letter_k, {false}));
    assert(!keys.pressed("bomb", K::letter_k, {true}));
    assert(keys.pressed("bomb_special", K::letter_k, {true}));
    assert(keys.pressed("gear", K::letter_g, {false}));
    // A plain key still fires with Shift held when nothing claims the Shift variant.
    assert(keys.pressed("throttle_full", K::keypad_plus, {true}));
    assert(!keys.pressed("nothing", K::keypad_plus, {false}));

    // Ctrl and Alt are part of the chord: Ctrl+D undocks, plain D does not, and Ctrl+Numpad+ is the held throttle.
    assert(keys.pressed("undock", K::letter_d, {false, true, false}));
    assert(!keys.pressed("undock", K::letter_d, {false, false, false}));
    assert(!keys.pressed("hud_colour", K::letter_h, {true, false, false}) && keys.pressed("hud_colour", K::letter_h, {false, false, true}));
    assert(keys.pressed("throttle_full", K::keypad_plus, {false, false, false}) && !keys.pressed("throttle_full", K::keypad_plus, {false, true, false}));
    assert(KeyBindings::chord_name({K::letter_x, false, true}) == "Ctrl+X");

    // Rebinding takes the key from a clashing action in the same context, not from one in another context.
    assert(keys.assign("bomb", 0, {K::letter_g, false}) == "Landing gear up / down");
    assert(!keys.pressed("gear", K::letter_g, {false}));
    assert(keys.pressed("bomb", K::letter_g, {false}));
    assert(keys.assign("walk_forward", 0, {K::letter_s, false}).empty() == false);
    assert(keys.pressed("pitch_up", K::keypad_2, {false}));
    assert(keys.find("walk_forward")->chord[0].key == K::letter_s);

    // Clearing and resetting.
    keys.clear("enter_exit", 1);
    assert(keys.find("enter_exit")->chord[1].empty());
    keys.reset("enter_exit");
    assert(keys.pressed("enter_exit", K::return_key, {false}) && keys.pressed("enter_exit", K::keypad_enter, {false}));

    // Learning ignores a bare Shift for plain actions and takes the chord on the next key.
    keys.begin_learn("lamps", 0);
    bool done = true;
    keys.learn_key(K::left_shift, {true}, done);
    assert(!done && keys.learning());
    keys.learn_key(K::letter_u, {true}, done);
    assert(done && !keys.learning());
    assert(keys.pressed("lamps", K::letter_u, {true}));
    keys.begin_learn("lamps", 0);
    keys.learn_key(K::escape, {false}, done);
    assert(done && keys.pressed("lamps", K::letter_u, {true}));
    // A held action may itself be a modifier.
    keys.begin_learn("throttle_up", 0);
    keys.learn_key(K::right_shift, {true}, done);
    assert(done && keys.find("throttle_up")->chord[0].key == K::right_shift && !keys.find("throttle_up")->chord[0].shift);

    // Only changed actions are saved, and they round-trip.
    const std::string saved = keys.to_json();
    KeyBindings again;
    again.from_json(saved);
    assert(again.pressed("lamps", K::letter_u, {true}));
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
    assert(keys.press_custom(props, K::letter_b, {false}) && props.get_bool("/controls/spot"));
    assert(keys.press_custom(props, K::letter_b, {false}) && !props.get_bool("/controls/spot"));
    assert(!keys.press_custom(props, K::letter_c, {false}));

    assert(KeyBindings::chord_name({K::letter_k, true}) == "Shift+K");
    assert(KeyBindings::chord_name({}) == "-");
    assert(keys.label_for("gear") == "G");
    std::puts("keybindings test passed");
    return 0;
}
