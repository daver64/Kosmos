#pragma once

#include "TextEditor.h"
#include "sim.h"

#include <string>
#include <vector>

namespace kosmos
{
    /**
     * Lua script editor (F11), modelled on FlightGear's Nasal console. Scripts are kept as .lua files with a .json
     * sidecar holding the event that runs them. They run in the simulation's Lua state, which has the `props` table.
     */
    class LuaConsole
    {
    public:
        enum class Trigger
        {
            manual,
            load,
            frame,
            timer,
            property
        };

        struct Script
        {
            std::string name;
            std::string path;
            std::string source;
            Trigger trigger = Trigger::manual;
            float interval = 1.0f;
            std::string property;
            bool armed = false;
            bool dirty = false;
            bool user = false;
            double accumulator = 0.0;
            std::string last_value;
            bool has_last = false;
        };

        /** Scan the shipped script folder and the user's, and route Lua output here. */
        void init(Sim &sim, const std::string &shipped_dir, const std::string &user_dir);
        /** Draw the window; `open` is cleared when it is closed. */
        void draw(bool &open);
        /** Run armed scripts for this frame (call after Sim::step). */
        void tick(double dt);

    private:
        void scan(const std::string &dir, bool user);
        void select(int index);
        void run(Script &script, const char *event, double dt, const std::string &path = {}, const std::string &value = {});
        bool save(Script &script);
        void print(const std::string &line);
        std::string trigger_name(Trigger trigger) const;

        Sim *sim_ = nullptr;
        std::string user_dir_;
        std::vector<Script> scripts_;
        int selected_ = -1;
        TextEditor editor_;
        std::vector<std::string> output_;
        bool scroll_output_ = false;
        const void *last_ship_ = nullptr;
        char new_name_[64] = "";
        std::string status_;
    };
}
