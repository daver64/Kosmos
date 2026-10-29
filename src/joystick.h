#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace kosmos
{
    /** An analogue input bound to a flight action. An empty device means the first connected one. */
    struct AxisBinding
    {
        std::string device;
        int axis = -1;
        bool invert = false;
        float deadzone = 0.08f;
        /** 0 is linear; towards 1 the centre of the stick gets finer. */
        float expo = 0.0f;
        float scale = 1.0f;
    };

    struct ButtonBinding
    {
        std::string device;
        int button = -1;
    };

    /** The bindings used while flying one kind of vehicle. */
    struct JoyProfile
    {
        std::map<std::string, AxisBinding> axes;
        std::map<std::string, ButtonBinding> buttons;
    };

    /** Axis actions: pitch, roll, yaw, rcs-x, rcs-y, hover and throttle. The last two are levels (0 to 1) and only act when the axis moves. */
    const std::vector<std::string> &joy_axis_actions(bool aircraft);
    /** Button actions: gear, flaps, sas, stage, autopilot, throttle-zero, throttle-full, hover-on, hover-off and brake (held). */
    const std::vector<std::string> &joy_button_actions(bool aircraft);
    /** A stick position after dead zone, curve, scale and inversion. Level actions map -1..1 to 0..1. */
    float joy_shape(float raw, const AxisBinding &binding, bool level);
    JoyProfile joy_default_profile(bool aircraft);
    /** Reads profiles from JSON text; missing parts keep their defaults. Returns false when the text is not a JSON object. */
    bool joy_profiles_from_json(const std::string &text, JoyProfile &aircraft, JoyProfile &spacecraft, bool &enabled);
    std::string joy_profiles_to_json(const JoyProfile &aircraft, const JoyProfile &spacecraft, bool enabled);

    /** What the joystick asks for this frame. */
    struct JoyResult
    {
        std::map<std::string, float> axes; // signed actions that are deflected
        std::map<std::string, float> levels; // throttle and hover, only when their axis moved
        std::vector<std::string> pressed;    // button actions that went down this frame
        bool brake = false;
        bool active() const { return !axes.empty() || !levels.empty() || !pressed.empty() || brake; }
    };

    class Joystick
    {
    public:
        Joystick();
        bool enabled = true;
        JoyProfile aircraft, spacecraft;

        void load();
        void save() const;
        JoyProfile &profile(bool aircraft_profile) { return aircraft_profile ? aircraft : spacecraft; }

        /** Samples the devices; call once per frame. */
        JoyResult update(bool aircraft_profile);

        int device_count() const;
        std::string device_name(int index) const;
        int axis_count(int index) const;
        int button_count(int index) const;
        float axis_value(int index, int axis) const;
        bool button_value(int index, int button) const;

        /** Waits for the next stick movement or button press and stores it as the binding for @p action. */
        void begin_learn(bool aircraft_profile, const std::string &action, bool axis);
        void cancel_learn() { learning_ = false; }
        bool learning() const { return learning_; }
        const std::string &learn_action() const { return learn_action_; }
        /** Checks for the input being learned; true when the binding was set. */
        bool poll_learn();

    private:
        int find_device(const std::string &name) const;
        std::map<std::pair<int, int>, float> snapshot_axes() const;
        bool learning_ = false, learn_axis_ = true, learn_aircraft_ = true;
        std::string learn_action_;
        std::map<std::pair<int, int>, float> baseline_;
        std::map<std::string, float> last_level_;
        std::map<std::string, float> pinned_; // raw value an axis was stuck at since start (2 = not stuck)
        std::map<std::string, bool> last_button_;
    };

    std::string joystick_path();
}
