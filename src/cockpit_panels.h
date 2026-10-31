#pragma once

#include "app.h"

namespace kosmos::app
{
    // A 3D cockpit's two MFD screens sit on a console in the ship frame (-Z is forward): a position, and a tilt back from the pilot.
    struct CockpitScreen
    {
        glm::vec3 centre;
        float width, height;
    };

    constexpr float screen_tilt = 0.436f;

    constexpr int mfd_target_size = 512;

    constexpr float mfd_target_frame = 400.0f;

    kosmos::MfdLayout mfd_target_layout(bool no_buttons = false);

    CockpitScreen cockpit_screen(const kosmos::Cockpit &cp, int side);

    glm::mat3 cockpit_screen_basis();

    // An instrument panel (Cockpit::instruments) is one flat board. In the 3D cockpit it is drawn into a texture at panel_target_width
    // pixels across; on the 2D screen it is drawn straight onto the display. Either way `scale` converts panel units to pixels.
    constexpr int panel_target_width = 2048;

    kosmos::MfdLayout panel_layout(const kosmos::Cockpit::PanelInstrument &pi, float ox, float oy, float scale);

    bool control_is_numeric(const std::string &v);

    bool control_lit(kosmos::Sim &sim, const kosmos::Cockpit::PanelControl &c);

    // Lamps, slider handles and the like are drawn over the panel bitmap, which carries the artwork.
    void draw_panel_control(kosmos::Sim &sim, const kosmos::Cockpit::PanelControl &c, float ox, float oy, float scale);

    // Returns the slider under (px, py) so the caller can keep dragging it, or -1.
    int panel_slider_at(const kosmos::Cockpit &cp, float ox, float oy, float scale, float px, float py);

    void panel_slider_set(kosmos::Sim &sim, const kosmos::Cockpit::PanelControl &c, float oy, float scale, float py);

    void panel_control_press(kosmos::Sim &sim, const kosmos::Cockpit::PanelControl &c);

    void draw_panel(kosmos::Sim &sim, const kosmos::Cockpit &cp, const std::string &image, float ox, float oy, float scale);

extern int panel_slider;

    /** What a panel control is called: its label, else a name made from its type and property. */
    std::string control_tooltip(const kosmos::Cockpit::PanelControl &c);

    /** The tooltip of the clickable control (or MFD button) under a point on a 2D panel; empty when there is none. */
    std::string panel_tooltip(kosmos::Sim &sim, const kosmos::Cockpit &cp, float ox, float oy, float scale, float px, float py);

    bool panel_click(kosmos::Sim &sim, const kosmos::Cockpit &cp, float ox, float oy, float scale, float px, float py);

    glm::mat3 panel_basis(const kosmos::Cockpit &cp);

    // An upright building looks out along +X with its roof at -Z; this maps a cockpit frame (nose -Z, up +Y) onto that.
    glm::mat3 interface_frame(const kosmos::ShipSpec &spec);

    // The cockpit with "{ship}" in its control properties replaced by the ship's property path.
    kosmos::Cockpit resolved_cockpit(const kosmos::Cockpit &cp, int ship_index);

    sl::Mesh *make_screen_quad();
} // namespace kosmos::app
