#include "app_state.h"

namespace kosmos::app
{
    // The menu bar and windows.
    void App::draw_gui()
    {
        sl::PropertyTree &props = *props_ref;
        if (sl::gui_available())
        {
            sl::gui_new_frame();
            draw_systems_window();
            draw_joystick_window();
            draw_keys_window();
            draw_link_window();
            draw_settings_window();
            menu_bar.draw_windows(sim, menu_actions);
            draw_part_window();
            if (lua_window)
                lua_console.draw(lua_window);
            if (net_window)
                draw_multiplayer_window(netplay, net_ui, net_window, sim.system_index(), [&](const kosmos::Ghost &g)
                                        {
                                            if (snap_to_ghost(sim, *ship, g, terrain_set))
                                                sim.log("Moved next to " + g.nick);
                                        });
            if (property_window)
                draw_property_browser(props, property_window, property_browser);
            draw_quantum_window();
            draw_camera_editor_window();
            draw_camera_window();
        }
    }
} // namespace kosmos::app
