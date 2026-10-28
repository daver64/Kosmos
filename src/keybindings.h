#pragma once

#include "sl.h"

#include <string>
#include <vector>

namespace kosmos
{
    /** A key, optionally with Shift held. Held (continuous) actions ignore the Shift flag. */
    struct KeyChord
    {
        sl::Key key = sl::Key::unknown;
        bool shift = false;
        bool empty() const { return key == sl::Key::unknown; }
        bool operator==(const KeyChord &o) const { return key == o.key && shift == o.shift; }
    };

    /** Where an action applies: two actions may share a chord when their contexts differ. */
    enum class KeyContext
    {
        anywhere,
        flying,
        on_foot
    };

    /** What an addon-defined action does with its property. */
    enum class CustomMode
    {
        none,
        toggle,
        hold
    };

    struct KeyAction
    {
        std::string id, label, group;
        KeyContext context = KeyContext::anywhere;
        bool held = false;
        KeyChord defaults[2], chord[2];
        /** Addon actions set a bool property when pressed (toggle) or while held. */
        CustomMode custom = CustomMode::none;
        std::string property;
    };

    /**
     * Keyboard bindings: every action has up to two chords, with defaults the player can change and save. The built-in
     * actions are listed in keybindings.cpp; addons add their own with a keys.json (see the modding guide).
     */
    class KeyBindings
    {
    public:
        KeyBindings();

        const std::vector<KeyAction> &actions() const { return actions_; }
        const KeyAction *find(const std::string &id) const;

        /** True when @p key (with @p shift held) was just pressed and is bound to @p id. */
        bool pressed(const char *id, sl::Key key, bool shift) const;
        /** True while any key bound to @p id is held. */
        bool down(const char *id) const;

        /** Addon actions pressed by this key; sets or toggles their properties. Returns true when one fired. */
        bool press_custom(sl::PropertyTree &props, sl::Key key, bool shift) const;
        /** Writes held addon actions' properties for this frame. */
        void update_custom(sl::PropertyTree &props, bool typing) const;

        /** Adds actions from a keys.json; returns an error message or empty. */
        std::string add_from_json(const std::string &text, const std::string &source);

        /** Assigns a chord, taking it from any other action it would clash with; returns the name of that action or empty. */
        std::string assign(const std::string &id, int slot, KeyChord chord);
        void clear(const std::string &id, int slot);
        void reset(const std::string &id);
        void reset_all();
        bool is_default(const KeyAction &action) const;

        /** Rebinding: the next key pressed goes to the action's slot. */
        void begin_learn(const std::string &id, int slot);
        void cancel_learn() { learn_id_.clear(); }
        bool learning() const { return !learn_id_.empty(); }
        const std::string &learn_id() const { return learn_id_; }
        int learn_slot() const { return learn_slot_; }
        /** Feed a key press while learning. Returns the name of an action the chord was taken from (or empty); sets @p done when finished. */
        std::string learn_key(sl::Key key, bool shift, bool &done);

        /** "Shift+K", "Keypad 9", or "-" when empty. */
        static std::string chord_name(const KeyChord &chord);
        /** Display names of an action's chords joined with " / ". */
        std::string label_for(const std::string &id) const;

        void load();
        void save() const;
        std::string to_json() const;
        void from_json(const std::string &text);

    private:
        KeyAction *find_mutable(const std::string &id);
        std::vector<KeyAction> actions_;
        std::string learn_id_;
        int learn_slot_ = 0;
    };

    std::string keybindings_path();
} // namespace kosmos
