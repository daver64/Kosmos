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
        std::function<void(const std::string &)> start_flight_recording;
        std::function<void()> stop_flight_recording;
        std::function<bool()> flight_recording;
        std::function<bool()> unsaved_flight_recording;
        std::function<void(const std::string &)> save_unsaved_flight_recording;
        std::function<void(const std::string &)> open_flight_recording;
        std::function<void()> toggle_eva;     // leave the ship on foot, or board the nearest one
        std::function<bool()> on_foot;
        std::function<bool()> in_building;
        std::function<std::string(const char *)> key_label; // the keys bound to a keyboard action, for menu hints
        std::function<bool()> multiplayer;    // true while hosting or joined; the clock cannot be skipped then
        std::function<void(int)> show_panel; // switch to the flown ship's 2D panel with this index
    };

    /** Capture ship locations and orbital states, current date, target and radio frequencies. */
    Scenario capture_scenario(Sim &sim, const std::string &name);

    /** The Autopilot menu's windows: aircraft modes (heading, altitude, speed, ILS) and spacecraft (ascent, attitude hold, throttle). */
    void draw_aircraft_autopilot(Sim &sim, bool &open);
    void draw_spacecraft_autopilot(Sim &sim, bool &open);

    /** The File menu's time-skip items: set the clock to just before the planned burn, and open the transfer windows dialog. */
    void file_menu_time_skip(Sim &sim, const MenuActions &actions, bool &windows_open);
    void draw_transfer_windows(Sim &sim, const MenuActions &actions, bool &open);

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
        void draw_toolbar(Sim &sim, FileDialog &dialog, const MenuActions &actions);
        void draw_scenario_fields();
        void draw_ship_fields(Sim &sim, ScenarioShip &s, const std::vector<std::string> &ships, const std::vector<std::string> &bodies);
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
        void tab_site(Sim &sim, Base &b, const std::function<void(double &, double &)> &ship_xz);
        void tab_objects(Sim &sim, Base &b, const std::function<void(double &, double &)> &ship_xz);
        void stock_model_row(Base &b);
        void blender_controls(Sim &sim, Base &b, const BaseObject &o);
        void object_fields(BaseObject &o, const std::function<void(double &, double &)> &ship_xz);
        void tab_runways(Sim &sim, Base &b, const std::function<void(double &, double &)> &ship_xz);
        void tab_navaids(Sim &sim, Base &b, const std::function<void(double &, double &)> &ship_xz);
        void tab_cameras(Sim &sim, Base &b, const std::function<void(double &, double &)> &ship_xz);
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
        std::vector<std::string> stock_models_; // listed once, the first time the base editor shows its stock models
        int stock_pick_ = 0;
        bool was_open_ = false;
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
        void menu_file(Sim &sim, const MenuActions &actions);
        void menu_edit(Sim &sim, std::vector<MenuWindow> &windows, const MenuActions &actions);
        void menu_view(Sim &sim, std::vector<MenuWindow> &windows, const MenuActions &actions);
        void draw_windows(Sim &sim, const MenuActions &actions);
        /** Escape: closes an open menu, the dialog, or the editor windows. Returns false when there was nothing to close. */
        bool dismiss(std::vector<MenuWindow> &windows);
        /** Whether a menu-owned or registered tool window needs the mouse pointer. */
        bool has_open_window(const std::vector<MenuWindow> &windows) const;

        void open_scenario_editor() { scenario_editor_open = true; }
        void open_base_editor() { base_editor_open = true; }
        /** Windows opened by the menus themselves; the program lists them in the View menu too. */
        bool scenario_editor_open = false, base_editor_open = false, help_open = false, about_open = false;
        bool aircraft_autopilot_open = false, spacecraft_autopilot_open = false, transfer_windows_open = false;
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
