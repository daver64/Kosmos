#pragma once

#include "blender_edit.h"
#include "sim.h"

#include <functional>
#include <string>
#include <vector>

namespace kosmos
{
    /** A window the View menu can show or hide. */
    struct MenuWindow
    {
        std::string label, key;
        bool *open = nullptr;
    };

    /** What the menus ask the program to do. */
    struct MenuActions
    {
        std::function<void(const Scenario &)> start_scenario; // run this scenario now
        std::function<void()> restart;
        std::function<void()> quit;
        std::function<void()> screenshot;
        std::function<void()> toggle_recording;
        std::function<bool()> recording;
        std::function<void(int)> show_panel; // switch to the flown ship's 2D panel with this index
    };

    /** The scenario as it stands in the sim: each ship's place, captured from its live state. Orbits are saved as circular at the current altitude. */
    Scenario capture_scenario(Sim &sim, const std::string &name);

    /** The Autopilot menu's windows: aircraft modes (heading, altitude, speed, ILS) and spacecraft (ascent, attitude hold, throttle). */
    void draw_aircraft_autopilot(Sim &sim, bool &open);
    void draw_spacecraft_autopilot(Sim &sim, bool &open);

    /** A small file browser window: pick an existing .json file or type a name to save. */
    class FileDialog
    {
    public:
        enum class Mode
        {
            open,
            save
        };
        void show(Mode mode, const std::string &title, const std::string &folder, const std::string &name, std::function<void(const std::string &)> accept);
        void draw();
        bool active() const { return active_; }
        void close() { active_ = false; }

    private:
        bool active_ = false;
        Mode mode_ = Mode::open;
        std::string title_, folder_, name_, error_;
        std::function<void(const std::string &)> accept_;
    };

    /** Edits a Scenario in memory: ships, where each one starts, the target and radio frequencies. Undo and redo keep JSON snapshots. */
    class ScenarioEditor
    {
    public:
        void draw(bool &open, Sim &sim, FileDialog &dialog, const MenuActions &actions);
        void set(const Scenario &scenario);
        bool undo();
        bool redo();

    private:
        void snapshot_changes();
        Scenario scenario_;
        bool has_ = false;
        int selected_ = 0;
        std::string status_, committed_;
        std::vector<std::string> undo_, redo_;
    };

    /** Edits the bases in the sim live: objects, runways, navaids and cameras. Saving writes the base to the user folder. */
    class BaseEditor
    {
    public:
        void draw(bool &open, Sim &sim, FileDialog &dialog);
        bool undo(Sim &sim);
        bool redo(Sim &sim);

    private:
        void snapshot_changes(Sim &sim);
        void restore(Sim &sim, const std::string &text);
        void tick_blender(Sim &sim);
        bool begin_blender(Sim &sim, Base &base, const std::vector<int> &indices);
        void end_blender(Sim &sim);
        BlenderEdit blender_;
        bool blender_paused_ = false, blender_was_paused_ = false;
        int blender_base_ = -1;
        std::vector<BaseObject> blender_saved_;
        std::vector<char> picked_;
        std::string blender_status_;
        int base_ = 0, object_ = 0, runway_ = 0, navaid_ = 0, camera_ = 0;
        std::string status_, committed_;
        int committed_base_ = -1;
        std::vector<std::string> undo_, redo_;
    };

    /**
     * The File / Edit / View / Tools / Help bar. It stays off screen until the mouse reaches the top edge, then slides down,
     * and slides away again once the mouse leaves it and no menu is open.
     */
    class MenuBar
    {
    public:
        void draw(Sim &sim, std::vector<MenuWindow> &windows, const MenuActions &actions);
        /** Draw the editors and dialogs; call every frame, even while the bar is hidden. */
        void draw_windows(Sim &sim, const MenuActions &actions);
        /** Escape: closes an open menu, the dialog, or the editor windows. Returns false when there was nothing to close. */
        bool dismiss(std::vector<MenuWindow> &windows);

        void open_scenario_editor() { scenario_editor_open = true; }
        void open_base_editor() { base_editor_open = true; }
        /** Windows opened by the menus themselves; the program lists them in the View menu too. */
        bool scenario_editor_open = false, base_editor_open = false, help_open = false, about_open = false;
        bool aircraft_autopilot_open = false, spacecraft_autopilot_open = false;
        bool keep_visible = true;
        bool visible() const { return slide_ > 0.01f; }

        ScenarioEditor scenario_editor;
        BaseEditor base_editor;
        FileDialog dialog;
        double scenario_scan_time = -10.0;
        /** One level of the Open scenario menu: sub-menus for the folders under `prefix`, then its own scenarios. */
        void scenario_menu(const std::vector<Scenario> &all, const std::string &prefix, const Scenario *&chosen);

    private:
        float slide_ = 0.0f;
        bool menu_open_ = false;
        bool close_menus_ = false;
    };
} // namespace kosmos
