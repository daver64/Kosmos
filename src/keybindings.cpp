#include "keybindings.h"

#include "json.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace kosmos
{
    namespace
    {
        struct KeyName
        {
            sl::Key key;
            const char *id;
            const char *display;
        };

        const KeyName key_names[] = {
#include "key_names.inc"
        };

        const KeyName *name_of(sl::Key key)
        {
            for (const KeyName &n : key_names)
                if (n.key == key)
                    return &n;
            return nullptr;
        }

        bool is_modifier(sl::Key key)
        {
            return key == sl::Key::left_shift || key == sl::Key::right_shift || key == sl::Key::left_control || key == sl::Key::right_control || key == sl::Key::left_alt ||
                   key == sl::Key::right_alt || key == sl::Key::left_gui || key == sl::Key::right_gui;
        }

        bool shift_key_down() { return sl::key_down(sl::Key::left_shift) || sl::key_down(sl::Key::right_shift); }

        // A modifier chord counts for either side of the keyboard.
        bool key_held(sl::Key key)
        {
            switch (key)
            {
            case sl::Key::left_shift:
            case sl::Key::right_shift:
                return shift_key_down();
            case sl::Key::left_control:
            case sl::Key::right_control:
                return sl::key_down(sl::Key::left_control) || sl::key_down(sl::Key::right_control);
            case sl::Key::left_alt:
            case sl::Key::right_alt:
                return sl::key_down(sl::Key::left_alt) || sl::key_down(sl::Key::right_alt);
            default:
                return sl::key_down(key);
            }
        }

        std::string chord_to_text(const KeyChord &c)
        {
            const KeyName *n = name_of(c.key);
            if (!n)
                return "";
            return std::string(c.ctrl ? "ctrl+" : "") + (c.alt ? "alt+" : "") + (c.shift ? "shift+" : "") + n->id;
        }

        KeyChord chord_from_text(std::string text)
        {
            KeyChord c;
            for (bool more = true; more;)
            {
                more = false;
                for (const auto &[prefix, flag] : {std::pair<const char *, bool KeyChord::*>{"shift+", &KeyChord::shift}, {"ctrl+", &KeyChord::ctrl}, {"alt+", &KeyChord::alt}})
                    if (text.rfind(prefix, 0) == 0)
                    {
                        c.*flag = true;
                        text = text.substr(std::string(prefix).size());
                        more = true;
                    }
            }
            for (const KeyName &n : key_names)
                if (text == n.id)
                {
                    c.key = n.key;
                    return c;
                }
            return KeyChord();
        }

        bool ctrl_down() { return sl::key_down(sl::Key::left_control) || sl::key_down(sl::Key::right_control); }
        bool alt_down() { return sl::key_down(sl::Key::left_alt) || sl::key_down(sl::Key::right_alt); }

        // A held chord with Ctrl or Alt needs them down; other held chords ignore modifiers.
        bool chord_mods_held(const KeyChord &c) { return (!c.ctrl || ctrl_down()) && (!c.alt || alt_down()); }

        bool contexts_clash(KeyContext a, KeyContext b) { return a == KeyContext::anywhere || b == KeyContext::anywhere || a == b; }

        using K = sl::Key;
    } // namespace

    std::string keybindings_path()
    {
        const char *xdg = std::getenv("XDG_CONFIG_HOME");
        const char *home = std::getenv("HOME");
        const char *appdata = std::getenv("APPDATA");
        const std::filesystem::path dir = xdg && *xdg ? std::filesystem::path(xdg) : home && *home ? std::filesystem::path(home) / ".config" : appdata ? std::filesystem::path(appdata) : std::filesystem::path();
        return dir.empty() ? std::string() : (dir / "kosmos" / "keys.json").string();
    }

    KeyBindings::KeyBindings()
    {
        auto add = [this](const char *id, const char *label, const char *group, KeyContext context, bool held, KeyChord a, KeyChord b = {}) {
            KeyAction action;
            action.id = id;
            action.label = label;
            action.group = group;
            action.context = context;
            action.held = held;
            action.defaults[0] = action.chord[0] = a;
            action.defaults[1] = action.chord[1] = b;
            actions_.push_back(std::move(action));
        };
        constexpr KeyContext any = KeyContext::anywhere, fly = KeyContext::flying, foot = KeyContext::on_foot;
        const char *win = "Windows", *gen = "View and time", *ship = "Ship controls", *hold = "Ship controls (held)", *pods = "Engine pods", *gear = "Lights, gear and ordnance", *walk = "On foot (held)";

        add("window_systems", "Ship systems", win, any, false, {K::f1});
        add("window_cameras", "Cameras", win, any, false, {K::f2});
        add("window_settings", "Settings", win, any, false, {K::f3});
        add("window_properties", "Properties", win, any, false, {K::f4});
        add("window_lua", "Lua console", win, any, false, {K::f11});
        add("window_multiplayer", "Multiplayer", win, any, false, {K::f12});
        add("mfd_display", "Cycle MFD display", win, any, false, {K::f5});
        add("mfd_left_mode", "Left MFD mode", win, any, false, {K::f6});
        add("mfd_right_mode", "Right MFD mode", win, any, false, {K::f7});
        add("mfd_left_button", "Left MFD button", win, any, false, {K::f8});
        add("mfd_right_button", "Right MFD button", win, any, false, {K::f9});
        add("next_scenario", "Next scenario", win, any, false, {K::f10});

        add("free_pointer", "Free the mouse pointer", gen, any, false, {K::backquote});
        add("restart", "Restart the scenario", gen, any, false, {K::letter_r, true});
        add("map", "System map", gen, any, false, {K::letter_m, false, true}, {K::tab});
        add("galaxy_map", "Galaxy map", gen, any, false, {K::letter_n});
        add("cycle_camera", "Cycle cameras / cockpit views", gen, any, false, {K::letter_c});
        add("next_ship", "Switch to the next ship", gen, any, false, {K::letter_v});
        add("jump_player", "Jump to the nearest other player", gen, any, false, {K::letter_j});
        add("info", "Info overlay", gen, any, false, {K::letter_i, false, true});
        add("sound", "Sound on / off", gen, any, false, {K::letter_m});
        add("pause", "Pause / resume", gen, any, false, {K::letter_p, false, true});
        add("record", "Start / stop video recording", gen, any, false, {K::letter_c, false, true});
        add("quit", "Quit", gen, any, false, {K::letter_q, false, true});
        add("fov_up", "Widen field of view", gen, any, true, {K::letter_x});
        add("fov_down", "Narrow field of view", gen, any, true, {K::letter_z});
        add("fov_step_up", "Widen field of view by a step", gen, any, false, {K::letter_x, false, true});
        add("fov_step_down", "Narrow field of view by a step", gen, any, false, {K::letter_z, false, true});
        add("orbit_line", "Orbit line on / off", gen, any, false, {K::letter_o});
        add("warp_up", "Faster time warp", gen, any, false, {K::letter_t});
        add("warp_down", "Slower time warp", gen, any, false, {K::letter_r});
        add("enter_exit", "Leave / board ship, enter building", gen, any, false, {K::return_key}, {K::keypad_enter});
        add("zoom_in", "Zoom in", gen, any, true, {K::equals});
        add("zoom_out", "Zoom out", gen, any, true, {K::minus});

        add("throttle_full", "Full throttle", ship, fly, false, {K::keypad_plus});
        add("throttle_zero", "Cut throttle", ship, fly, false, {K::keypad_multiply});
        add("stage", "Stage", ship, fly, false, {K::space});
        add("chute_deploy", "Deploy parachute", ship, fly, false, {K::delete_key});
        add("chute_cut", "Cut parachutes away", ship, fly, false, {K::delete_key, true});
        add("autopilot", "Autopilot on / off", ship, fly, false, {K::letter_p});
        add("sas", "SAS: kill rotation", ship, fly, false, {K::keypad_5});
        add("sas_prograde", "SAS: hold prograde", ship, fly, false, {K::left_bracket});
        add("sas_retrograde", "SAS: hold retrograde", ship, fly, false, {K::right_bracket});
        add("sas_normal", "SAS: hold normal", ship, fly, false, {K::semicolon});
        add("sas_antinormal", "SAS: hold anti-normal", ship, fly, false, {K::quote});
        add("gear", "Landing gear up / down", ship, fly, false, {K::letter_g});
        add("flaps", "Flaps (map: focus; on foot: pack)", ship, any, false, {K::letter_f});
        add("undock", "Undock", ship, fly, false, {K::letter_d, false, true});
        add("hover_on", "Hover engines full", ship, fly, false, {K::home});
        add("hover_off", "Hover engines off", ship, fly, false, {K::end});
        add("hud_mode", "Next HUD mode", ship, any, false, {K::letter_h});
        add("hud_colour", "Next HUD colour", ship, any, false, {K::letter_h, false, false, true});

        add("fire_primary", "Fire primary weapons", hold, fly, true, {K::keypad_divide});
        add("fire_secondary", "Fire secondary weapons", hold, fly, true, {K::keypad_7});
        add("throttle_up", "Throttle up", hold, fly, true, {K::keypad_plus, false, true});
        add("throttle_down", "Throttle down", hold, fly, true, {K::keypad_minus, false, true});
        add("hover_up", "Hover thrust up", hold, fly, true, {K::keypad_0});
        add("hover_down", "Hover thrust down", hold, fly, true, {K::keypad_period});
        add("pitch_up", "Pitch up", hold, fly, true, {K::keypad_2});
        add("pitch_down", "Pitch down", hold, fly, true, {K::keypad_8});
        add("yaw_left", "Yaw left", hold, fly, true, {K::keypad_1});
        add("yaw_right", "Yaw right", hold, fly, true, {K::keypad_3});
        add("roll_left", "Roll left", hold, fly, true, {K::keypad_4});
        add("roll_right", "Roll right", hold, fly, true, {K::keypad_6});
        add("brake", "Wheel brakes", hold, fly, true, {K::comma});
        add("rcs_left", "Translate left (cockpit: head left)", hold, fly, true, {K::arrow_left});
        add("rcs_right", "Translate right (cockpit: head right)", hold, fly, true, {K::arrow_right});
        add("rcs_up", "Translate up (cockpit: head forward)", hold, fly, true, {K::arrow_up});
        add("rcs_down", "Translate down (cockpit: head back)", hold, fly, true, {K::arrow_down});
        add("head_up", "Cockpit head up", hold, fly, true, {K::page_up});
        add("head_down", "Cockpit head down", hold, fly, true, {K::page_down});

        add("pod_thrust_up", "Pod thrust up", pods, fly, true, {K::digit_9});
        add("pod_thrust_down", "Pod thrust down", pods, fly, true, {K::digit_3});
        add("pod_tilt_up", "Pods tilt forward", pods, fly, true, {K::digit_8});
        add("pod_tilt_down", "Pods tilt back", pods, fly, true, {K::digit_2});
        add("pod_retro", "Pods to retro", pods, fly, true, {K::digit_4});
        add("pod_hover", "Pods to hover", pods, fly, true, {K::digit_5});
        add("pod_forward", "Pods to forward", pods, fly, true, {K::digit_6});

        add("nav_lights", "Navigation lights and strobes", gear, any, false, {K::letter_y});
        add("lamps", "Landing and nose lights", gear, any, false, {K::letter_y, true});
        add("bomb", "Drop the next bomb", gear, fly, false, {K::letter_k});
        add("bomb_special", "Drop the special store", gear, fly, false, {K::letter_k, true});

        add("walk_forward", "Walk forward", walk, foot, true, {K::letter_w});
        add("walk_back", "Walk back", walk, foot, true, {K::letter_s});
        add("walk_left", "Strafe left", walk, foot, true, {K::letter_a});
        add("walk_right", "Strafe right", walk, foot, true, {K::letter_d});
        add("jump", "Jump", walk, foot, true, {K::space});
        add("crouch", "Crouch", walk, foot, true, {K::left_control});
        add("run", "Boost", walk, foot, true, {K::left_shift});
        add("foot_roll_left", "Roll left (floating free)", walk, foot, true, {K::letter_q});
        add("foot_roll_right", "Roll right (floating free)", walk, foot, true, {K::letter_e});
        add("look_left", "Look left", walk, foot, true, {K::arrow_left});
        add("look_right", "Look right", walk, foot, true, {K::arrow_right});
        add("look_up", "Look up", walk, foot, true, {K::arrow_up});
        add("look_down", "Look down", walk, foot, true, {K::arrow_down});
    }

    const KeyAction *KeyBindings::find(const std::string &id) const
    {
        for (const KeyAction &a : actions_)
            if (a.id == id)
                return &a;
        return nullptr;
    }

    KeyAction *KeyBindings::find_mutable(const std::string &id)
    {
        for (KeyAction &a : actions_)
            if (a.id == id)
                return &a;
        return nullptr;
    }

    bool KeyBindings::pressed(const char *id, sl::Key key, KeyMods mods) const
    {
        const KeyAction *action = find(id);
        if (!action)
            return false;
        for (const KeyChord &c : action->chord)
        {
            if (c.empty() || c.key != key || c.ctrl != mods.ctrl || c.alt != mods.alt)
                continue;
            if (c.shift == mods.shift)
                return true;
            // A plain binding still fires with Shift held unless another action claims Shift+that key.
            if (!c.shift)
            {
                bool claimed = false;
                for (const KeyAction &other : actions_)
                    for (const KeyChord &oc : other.chord)
                        claimed = claimed || (oc.key == key && oc.shift && oc.ctrl == mods.ctrl && oc.alt == mods.alt && !other.held);
                if (!claimed)
                    return true;
            }
        }
        return false;
    }

    bool KeyBindings::down(const char *id) const
    {
        const KeyAction *action = find(id);
        if (!action)
            return false;
        for (const KeyChord &c : action->chord)
            if (!c.empty() && key_held(c.key) && chord_mods_held(c))
                return true;
        return false;
    }

    bool KeyBindings::press_custom(sl::PropertyTree &props, sl::Key key, KeyMods mods) const
    {
        bool fired = false;
        for (const KeyAction &a : actions_)
        {
            if (a.custom != CustomMode::toggle)
                continue;
            for (const KeyChord &c : a.chord)
                if (!c.empty() && c.key == key && c.shift == mods.shift && c.ctrl == mods.ctrl && c.alt == mods.alt)
                {
                    props.set_bool(a.property, !props.get_bool(a.property));
                    fired = true;
                }
        }
        return fired;
    }

    void KeyBindings::update_custom(sl::PropertyTree &props, bool typing) const
    {
        for (const KeyAction &a : actions_)
            if (a.custom == CustomMode::hold)
            {
                bool on = false;
                for (const KeyChord &c : a.chord)
                    on = on || (!typing && !c.empty() && key_held(c.key) && chord_mods_held(c));
                if (props.get_bool(a.property) != on)
                    props.set_bool(a.property, on);
            }
    }

    std::string KeyBindings::add_from_json(const std::string &text, const std::string &source)
    {
        try
        {
            const nlohmann::json j = nlohmann::json::parse(text);
            for (const nlohmann::json &ja : j.value("actions", nlohmann::json::array()))
            {
                KeyAction a;
                a.id = ja.value("id", "");
                a.label = ja.value("label", a.id);
                a.group = ja.value("group", "Addons");
                a.property = ja.value("property", "");
                const std::string mode = ja.value("mode", "toggle");
                if (a.id.empty() || a.property.empty() || find(a.id))
                    return source + ": each action needs a unique id and a property";
                a.custom = mode == "hold" ? CustomMode::hold : CustomMode::toggle;
                a.held = a.custom == CustomMode::hold;
                KeyChord c = chord_from_text(ja.value("key", ""));
                c.shift = c.shift || ja.value("shift", false);
                c.ctrl = c.ctrl || ja.value("ctrl", false);
                c.alt = c.alt || ja.value("alt", false);
                a.defaults[0] = a.chord[0] = c;
                actions_.push_back(std::move(a));
            }
        }
        catch (const std::exception &e)
        {
            return source + ": " + e.what();
        }
        return "";
    }

    std::string KeyBindings::assign(const std::string &id, int slot, KeyChord chord)
    {
        KeyAction *target = find_mutable(id);
        if (!target || slot < 0 || slot > 1)
            return "";
        std::string taken;
        if (!chord.empty())
        {
            if (target->held)
                chord.shift = false;
            for (KeyAction &other : actions_)
                for (int s = 0; s < 2; ++s)
                {
                    if (&other == target && s == slot)
                        continue;
                    KeyChord &oc = other.chord[s];
                    if (!oc.empty() && oc.key == chord.key && oc.ctrl == chord.ctrl && oc.alt == chord.alt && (oc.shift == chord.shift || other.held || target->held) && contexts_clash(other.context, target->context))
                    {
                        oc = KeyChord();
                        if (&other != target)
                            taken = other.label;
                    }
                }
        }
        target->chord[slot] = chord;
        return taken;
    }

    void KeyBindings::clear(const std::string &id, int slot)
    {
        if (KeyAction *a = find_mutable(id); a && slot >= 0 && slot <= 1)
            a->chord[slot] = KeyChord();
    }

    void KeyBindings::reset(const std::string &id)
    {
        if (KeyAction *a = find_mutable(id))
            a->chord[0] = a->defaults[0], a->chord[1] = a->defaults[1];
    }

    void KeyBindings::reset_all()
    {
        for (KeyAction &a : actions_)
            a.chord[0] = a.defaults[0], a.chord[1] = a.defaults[1];
    }

    bool KeyBindings::is_default(const KeyAction &a) const { return a.chord[0] == a.defaults[0] && a.chord[1] == a.defaults[1]; }

    void KeyBindings::begin_learn(const std::string &id, int slot)
    {
        learn_id_ = id;
        learn_slot_ = slot;
    }

    std::string KeyBindings::learn_key(sl::Key key, KeyMods mods, bool &done)
    {
        done = false;
        if (!learning())
            return "";
        if (key == sl::Key::escape)
        {
            cancel_learn();
            done = true;
            return "";
        }
        const KeyAction *a = find(learn_id_);
        // A plain action waits for the key after Shift, so Shift+K can be bound; a held action may be a modifier itself.
        if (!a || (is_modifier(key) && !a->held))
            return "";
        done = true;
        const bool bare = is_modifier(key);
        KeyChord chord{key, mods.shift && !bare, mods.ctrl && !bare, mods.alt && !bare};
        const std::string taken = assign(learn_id_, learn_slot_, chord);
        cancel_learn();
        return taken;
    }

    std::string KeyBindings::chord_name(const KeyChord &chord)
    {
        const KeyName *n = name_of(chord.key);
        if (!n)
            return "-";
        return std::string(chord.ctrl ? "Ctrl+" : "") + (chord.alt ? "Alt+" : "") + (chord.shift ? "Shift+" : "") + n->display;
    }

    std::string KeyBindings::label_for(const std::string &id) const
    {
        const KeyAction *a = find(id);
        std::string out;
        if (a)
            for (const KeyChord &c : a->chord)
                if (!c.empty())
                    out += (out.empty() ? "" : " / ") + chord_name(c);
        return out;
    }

    std::string KeyBindings::to_json() const
    {
        nlohmann::json j = nlohmann::json::object();
        for (const KeyAction &a : actions_)
            if (!is_default(a))
                j[a.id] = {chord_to_text(a.chord[0]), chord_to_text(a.chord[1])};
        return j.dump(1);
    }

    void KeyBindings::from_json(const std::string &text)
    {
        try
        {
            const nlohmann::json j = nlohmann::json::parse(text);
            for (KeyAction &a : actions_)
            {
                const auto it = j.find(a.id);
                if (it == j.end() || !it->is_array())
                    continue;
                for (std::size_t s = 0; s < 2 && s < it->size(); ++s)
                    a.chord[s] = chord_from_text((*it)[s].get<std::string>());
            }
        }
        catch (const std::exception &)
        {
        }
    }

    void KeyBindings::load()
    {
        const std::string path = keybindings_path();
        std::ifstream in(path);
        if (path.empty() || !in)
            return;
        std::stringstream text;
        text << in.rdbuf();
        from_json(text.str());
    }

    void KeyBindings::save() const
    {
        const std::string path = keybindings_path();
        if (path.empty())
            return;
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        std::ofstream out(path);
        if (out)
            out << to_json();
    }
} // namespace kosmos
