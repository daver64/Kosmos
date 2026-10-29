#include "joystick.h"

#include <gamepad.h>
#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace kosmos
{
    const std::vector<std::string> &joy_axis_actions(bool aircraft)
    {
        static const std::vector<std::string> air = {"pitch", "roll", "yaw", "throttle"};
        static const std::vector<std::string> space = {"pitch", "yaw", "roll", "throttle", "rcs-x", "rcs-y", "hover"};
        return aircraft ? air : space;
    }

    const std::vector<std::string> &joy_button_actions(bool aircraft)
    {
        static const std::vector<std::string> air = {"brake", "gear", "flaps", "autopilot", "throttle-zero", "throttle-full"};
        static const std::vector<std::string> space = {"stage", "sas", "autopilot", "throttle-zero", "throttle-full", "hover-on", "hover-off"};
        return aircraft ? air : space;
    }

    float joy_shape(float raw, const AxisBinding &binding, bool level)
    {
        float v = std::clamp(raw, -1.0f, 1.0f);
        if (binding.invert)
            v = -v;
        if (level)
            return std::clamp((v + 1.0f) * 0.5f, 0.0f, 1.0f);
        const float magnitude = std::fabs(v);
        if (magnitude <= binding.deadzone)
            return 0.0f;
        float m = (magnitude - binding.deadzone) / std::max(1.0f - binding.deadzone, 1.0e-3f);
        m = (1.0f - binding.expo) * m + binding.expo * m * m * m;
        return std::clamp(std::copysign(m, v) * binding.scale, -1.0f, 1.0f);
    }

    JoyProfile joy_default_profile(bool aircraft)
    {
        JoyProfile p;
        // Axes start unbound: axis numbering differs between sticks and a wrong guess would deflect the controls on its own.
        const auto button = [&](const char *action, int index) {
            ButtonBinding b;
            b.button = index;
            p.buttons[action] = b;
        };
        if (aircraft)
        {
            button("brake", 0);
            button("gear", 1);
            button("flaps", 2);
            button("throttle-zero", 3);
            button("throttle-full", 4);
            button("autopilot", 5);
        }
        else
        {
            button("stage", 0);
            button("sas", 1);
            button("autopilot", 2);
            button("throttle-zero", 3);
            button("throttle-full", 4);
        }
        return p;
    }

    namespace
    {
        nlohmann::json profile_json(const JoyProfile &p)
        {
            nlohmann::json j;
            for (const auto &[name, b] : p.axes)
                j["axes"][name] = {{"device", b.device}, {"axis", b.axis}, {"invert", b.invert}, {"deadzone", b.deadzone}, {"expo", b.expo}, {"scale", b.scale}};
            for (const auto &[name, b] : p.buttons)
                j["buttons"][name] = {{"device", b.device}, {"button", b.button}};
            if (!j.contains("axes"))
                j["axes"] = nlohmann::json::object();
            if (!j.contains("buttons"))
                j["buttons"] = nlohmann::json::object();
            return j;
        }

        void profile_from_json(const nlohmann::json &j, JoyProfile &p)
        {
            if (!j.is_object())
                return;
            // A saved profile replaces the defaults entirely so unbinding an action sticks.
            JoyProfile loaded;
            if (j.contains("axes") && j["axes"].is_object())
                for (const auto &[name, v] : j["axes"].items())
                {
                    AxisBinding b;
                    b.device = v.value("device", std::string());
                    b.axis = v.value("axis", -1);
                    b.invert = v.value("invert", false);
                    b.deadzone = std::clamp(v.value("deadzone", 0.08f), 0.0f, 0.9f);
                    b.expo = std::clamp(v.value("expo", 0.0f), 0.0f, 1.0f);
                    b.scale = std::clamp(v.value("scale", 1.0f), 0.1f, 2.0f);
                    loaded.axes[name] = b;
                }
            if (j.contains("buttons") && j["buttons"].is_object())
                for (const auto &[name, v] : j["buttons"].items())
                {
                    ButtonBinding b;
                    b.device = v.value("device", std::string());
                    b.button = v.value("button", -1);
                    loaded.buttons[name] = b;
                }
            p = loaded;
        }
    }

    bool joy_profiles_from_json(const std::string &text, JoyProfile &aircraft, JoyProfile &spacecraft, bool &enabled)
    {
        const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
        if (!j.is_object())
            return false;
        if (j.contains("enabled") && j["enabled"].is_boolean())
            enabled = j["enabled"].get<bool>();
        if (j.contains("aircraft"))
            profile_from_json(j["aircraft"], aircraft);
        if (j.contains("spacecraft"))
            profile_from_json(j["spacecraft"], spacecraft);
        return true;
    }

    std::string joy_profiles_to_json(const JoyProfile &aircraft, const JoyProfile &spacecraft, bool enabled)
    {
        nlohmann::json j;
        j["enabled"] = enabled;
        j["aircraft"] = profile_json(aircraft);
        j["spacecraft"] = profile_json(spacecraft);
        return j.dump(1) + "\n";
    }

    std::string joystick_path()
    {
        const char *xdg = std::getenv("XDG_CONFIG_HOME");
        const char *home = std::getenv("HOME");
        const char *appdata = std::getenv("APPDATA");
        const std::filesystem::path dir = xdg && *xdg ? std::filesystem::path(xdg) : home && *home ? std::filesystem::path(home) / ".config" : appdata ? std::filesystem::path(appdata) : std::filesystem::path();
        return dir.empty() ? std::string() : (dir / "kosmos" / "joystick.json").string();
    }

    Joystick::Joystick()
    {
        aircraft = joy_default_profile(true);
        spacecraft = joy_default_profile(false);
    }

    void Joystick::load()
    {
        const std::string path = joystick_path();
        std::ifstream in(path);
        if (path.empty() || !in)
            return;
        std::stringstream text;
        text << in.rdbuf();
        joy_profiles_from_json(text.str(), aircraft, spacecraft, enabled);
    }

    void Joystick::save() const
    {
        const std::string path = joystick_path();
        if (path.empty())
            return;
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        std::ofstream out(path);
        if (out)
            out << joy_profiles_to_json(aircraft, spacecraft, enabled);
    }

    int Joystick::device_count() const { return sl::gamepad_count(); }
    std::string Joystick::device_name(int index) const { return sl::gamepad_name(index); }
    int Joystick::axis_count(int index) const { return sl::joystick_axis_count(index); }
    int Joystick::button_count(int index) const { return sl::joystick_button_count(index); }
    float Joystick::axis_value(int index, int axis) const { return sl::joystick_axis_raw(index, axis); }
    bool Joystick::button_value(int index, int button) const { return sl::joystick_button_raw(index, button); }

    int Joystick::find_device(const std::string &name) const
    {
        const int count = device_count();
        if (count <= 0)
            return -1;
        if (name.empty())
            return 0;
        for (int i = 0; i < count; ++i)
            if (device_name(i) == name)
                return i;
        return -1;
    }

    JoyResult Joystick::update(bool aircraft_profile)
    {
        JoyResult result;
        if (!enabled || device_count() <= 0)
            return result;
        const JoyProfile &p = profile(aircraft_profile);
        for (const auto &[action, b] : p.axes)
        {
            const int device = find_device(b.device);
            if (device < 0 || b.axis < 0)
                continue;
            const float raw = axis_value(device, b.axis);
            const bool level = action == "throttle" || action == "hover";
            const float v = joy_shape(raw, b, level);
            if (level)
            {
                // A level only follows the stick once it has moved, so keys and buttons can still set it.
                const std::string key = (aircraft_profile ? "a:" : "s:") + action;
                const auto it = last_level_.find(key);
                if (it == last_level_.end())
                    last_level_[key] = v;
                else if (std::fabs(v - it->second) > 0.01f)
                {
                    it->second = v;
                    result.levels[action] = v;
                }
            }
            else
            {
                // A stick axis that first reads fully pinned is a device that has not reported its rest position yet; ignore it until it moves.
                const std::string key = (aircraft_profile ? "a:" : "s:") + action;
                const auto it = pinned_.find(key);
                if (it == pinned_.end())
                    pinned_[key] = std::fabs(raw) >= 0.95f ? raw : 2.0f;
                else if (it->second <= 1.0f && std::fabs(raw - it->second) > 0.05f)
                    it->second = 2.0f;
                if (pinned_[key] <= 1.0f)
                    continue;
                if (v != 0.0f)
                    result.axes[action] = v;
            }
        }
        for (const auto &[action, b] : p.buttons)
        {
            const int device = find_device(b.device);
            const bool down = device >= 0 && b.button >= 0 && button_value(device, b.button);
            const std::string key = (aircraft_profile ? "a:" : "s:") + action;
            const bool was = last_button_[key];
            last_button_[key] = down;
            if (action == "brake")
                result.brake = down;
            else if (down && !was)
                result.pressed.push_back(action);
        }
        return result;
    }

    std::map<std::pair<int, int>, float> Joystick::snapshot_axes() const
    {
        std::map<std::pair<int, int>, float> s;
        for (int d = 0; d < device_count(); ++d)
        {
            for (int a = 0; a < axis_count(d); ++a)
                s[{d, a}] = axis_value(d, a);
            for (int b = 0; b < button_count(d); ++b)
                s[{d, 1000 + b}] = button_value(d, b) ? 1.0f : 0.0f;
        }
        return s;
    }

    void Joystick::begin_learn(bool aircraft_profile, const std::string &action, bool axis)
    {
        learning_ = true;
        learn_axis_ = axis;
        learn_aircraft_ = aircraft_profile;
        learn_action_ = action;
        baseline_ = snapshot_axes();
    }

    bool Joystick::poll_learn()
    {
        if (!learning_)
            return false;
        for (const auto &[key, value] : snapshot_axes())
        {
            const auto base = baseline_.find(key);
            const float was = base == baseline_.end() ? 0.0f : base->second;
            const bool is_button = key.second >= 1000;
            if (is_button != !learn_axis_ || std::fabs(value - was) < (is_button ? 0.5f : 0.5f))
                continue;
            JoyProfile &p = profile(learn_aircraft_);
            const std::string name = device_name(key.first);
            if (learn_axis_)
            {
                AxisBinding b = p.axes.count(learn_action_) ? p.axes[learn_action_] : AxisBinding();
                b.device = name;
                b.axis = key.second;
                p.axes[learn_action_] = b;
            }
            else
            {
                ButtonBinding b;
                b.device = name;
                b.button = key.second - 1000;
                p.buttons[learn_action_] = b;
            }
            learning_ = false;
            return true;
        }
        return false;
    }
}
