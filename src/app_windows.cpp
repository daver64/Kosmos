#include "app_state.h"
#include "ui_slider.h"

namespace kosmos::app
{
    void App::draw_systems_window()
    {
        if (windows.systems_window)
        {
            ImGui::SetNextWindowPos(ImVec2(330.0f, 60.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.9f);
            if (ImGui::Begin("Ship systems [F1]", &windows.systems_window, ImGuiWindowFlags_AlwaysAutoResize))
            {
                kosmos::Systems *sys = ship->systems();
                if (!sys)
                    ImGui::TextUnformatted("This ship has no power or life-support systems.");
                else
                {
                    int mode = static_cast<int>(sys->mode());
                    const char *modes[] = {"Off", "Warn", "Fatal"};
                    if (ImGui::Combo("Life support", &mode, modes, 3))
                        sys->set_mode(static_cast<kosmos::LifeSupportMode>(mode));
                    if (sys->vessel_count() > 0)
                    {
                        const bool on = sys->consumers_on();
                        if (ImGui::Button(on ? "Crew depart: systems and life support off" : "Crew arrive: systems and life support on"))
                        {
                            sys->set_consumers(!on);
                            for (int vi = 0; vi < sys->vessel_count(); ++vi)
                                sys->set_crew(vi, on ? 0 : std::max(sys->vessels()[static_cast<std::size_t>(vi)].spec.crew, 2));
                        }
                    }
                    if (sys->has_processing())
                    {
                        const bool mining = sys->processing_on();
                        if (ImGui::Button(mining ? "Resource processing: stop" : "Resource processing: start (needs the ground and power)"))
                            sys->set_processing(!mining);
                    }
                    ImGui::SeparatorText("Power");
                    ImGui::Text("Supply %.0f W   Demand %.0f W   %s", sys->supply_w(), sys->demand_w(), sys->powered() ? "" : "SHORTAGE");
                    const double cap = sys->battery_capacity_wh();
                    if (cap > 0.0)
                    {
                        char label[64];
                        std::snprintf(label, sizeof label, "Battery %.0f / %.0f Wh", sys->battery_wh(), cap);
                        ImGui::ProgressBar(static_cast<float>(sys->battery_wh() / cap), ImVec2(260.0f, 0.0f), label);
                    }
                    for (const kosmos::SystemComponent &c : sys->components())
                    {
                        if (!c.alive)
                            continue;
                        if (c.type == "tank" && c.capacity > 0.0)
                        {
                            char label[96];
                            std::snprintf(label, sizeof label, "%s tank %.2f / %.0f kg", c.gas.c_str(), c.amount, c.capacity);
                            ImGui::ProgressBar(static_cast<float>(c.amount / c.capacity), ImVec2(260.0f, 0.0f), label);
                        }
                        else if (c.output_w > 0.0)
                            ImGui::Text("%s  %.0f W", c.name.c_str(), c.output_w);
                        else if (c.type == "load" && c.watts > 0.0)
                        {
                            // Each load can be switched on its own, to shed instruments when the generators fade.
                            char label[128];
                            std::snprintf(label, sizeof label, "%s  %.1f W%s##load%p", c.name.c_str(), c.watts, c.enabled && !c.powered ? " (shed)" : "", static_cast<const void *>(&c));
                            ImGui::Checkbox(label, &const_cast<kosmos::SystemComponent &>(c).enabled);
                        }
                        else if ((c.type == "load" || c.type == "scrubber" || c.type == "regulator") && !c.enabled)
                            ImGui::TextDisabled("%s  off", c.name.c_str());
                        else if ((c.type == "load" || c.type == "scrubber" || c.type == "regulator") && c.enabled)
                            ImGui::TextColored(c.powered ? ImVec4(0.7f, 1.0f, 0.7f, 1.0f) : ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s  %.0f W %s", c.name.c_str(), c.watts, c.powered ? "" : "(shed)");
                    }
                    int &selected_vessel = memory.selected_vessel, &slice_y = memory.slice_y, &slice_gas = memory.slice_gas;
                    for (int vi = 0; vi < sys->vessel_count(); ++vi)
                    {
                        const kosmos::Systems::Vessel &v = sys->vessels()[static_cast<std::size_t>(vi)];
                        ImGui::SeparatorText(v.spec.name.c_str());
                        if (!v.alive)
                        {
                            ImGui::TextUnformatted("(separated)");
                            continue;
                        }
                        ImGui::Text("%.1f kPa   O2 %.1f   CO2 %.2f kPa   crew %d   health %.0f%%  %s", v.pressure_kpa(), v.ppo2_kpa(), v.ppco2_kpa(), v.spec.crew, v.health * 100.0, v.hazard.c_str());
                        ImGui::PushID(vi);
                        if (ImGui::SmallButton("Crew -"))
                            sys->set_crew(vi, v.spec.crew - 1);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Crew +"))
                            sys->set_crew(vi, v.spec.crew + 1);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Open 5 cm2 leak"))
                            sys->breach(vi, 5.0);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Show gas grid"))
                            selected_vessel = vi;
                        ImGui::PopID();
                    }
                    if (selected_vessel < sys->vessel_count() && sys->vessels()[static_cast<std::size_t>(selected_vessel)].alive)
                    {
                        ImGui::SeparatorText("Cabin gas grid");
                        ui::slider_int("Height", &slice_y, 1, 10);
                        ImGui::RadioButton("O2", &slice_gas, 0);
                        ImGui::SameLine();
                        ImGui::RadioButton("N2", &slice_gas, 1);
                        ImGui::SameLine();
                        ImGui::RadioButton("CO2", &slice_gas, 2);
                        const kosmos::Systems::Vessel &v = sys->vessels()[static_cast<std::size_t>(selected_vessel)];
                        // Colour each cell by its density relative to the well-mixed value; CO2 pockets stand out.
                        const float mixed = static_cast<float>((slice_gas == 0 ? v.o2 : slice_gas == 1 ? v.n2 : v.co2) / 1000.0);
                        const ImVec2 origin = ImGui::GetCursorScreenPos();
                        const float cell = 14.0f;
                        ImDrawList *draw = ImGui::GetWindowDrawList();
                        for (int z = 0; z < 12; ++z)
                            for (int x = 0; x < 12; ++x)
                            {
                                const float d = mixed > 0.0f ? sys->grid_cell(selected_vessel, x, slice_y, z, slice_gas) / mixed : 0.0f;
                                const float t = std::min(d / 2.0f, 1.0f);
                                const bool hull = x == 0 || z == 0 || x == 11 || z == 11;
                                const ImU32 colour = hull ? IM_COL32(70, 70, 80, 255) : IM_COL32(static_cast<int>(40 + 215 * t), static_cast<int>(60 + 120 * (1.0f - std::abs(t * 2.0f - 1.0f))), static_cast<int>(220 - 190 * t), 255);
                                draw->AddRectFilled(ImVec2(origin.x + x * cell, origin.y + z * cell), ImVec2(origin.x + (x + 1) * cell - 1.0f, origin.y + (z + 1) * cell - 1.0f), colour);
                            }
                        ImGui::Dummy(ImVec2(12 * cell, 12 * cell));
                    }
                }
            }
            ImGui::End();
        }
    }

    void App::draw_joystick_window()
    {
        if (windows.joystick_window)
        {
            ImGui::SetNextWindowPos(ImVec2(360.0f, 40.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.92f);
            if (ImGui::Begin("Joystick", &windows.joystick_window, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Checkbox("Joystick enabled", &joystick.enabled);
                const int devices = joystick.device_count();
                if (devices == 0)
                    ImGui::TextDisabled("No joystick connected");
                for (int d = 0; d < devices; ++d)
                {
                    if (ImGui::TreeNode(("device" + std::to_string(d)).c_str(), "%s", joystick.device_name(d).c_str()))
                    {
                        for (int a = 0; a < joystick.axis_count(d); ++a)
                            ImGui::ProgressBar(joystick.axis_value(d, a) * 0.5f + 0.5f, ImVec2(160.0f, 0.0f), ("axis " + std::to_string(a)).c_str());
                        std::string pressed;
                        for (int b = 0; b < joystick.button_count(d); ++b)
                            if (joystick.button_value(d, b))
                                pressed += " " + std::to_string(b);
                        ImGui::Text("Buttons down:%s", pressed.c_str());
                        ImGui::TreePop();
                    }
                }
                for (const bool air : {true, false})
                {
                    ImGui::PushID(air ? 1 : 2);
                    const bool current = ship->spec().aircraft() == air;
                    ImGui::SeparatorText(air ? (current ? "Aircraft profile (in use)" : "Aircraft profile") : (current ? "Spacecraft profile (in use)" : "Spacecraft profile"));
                    kosmos::JoyProfile &p = joystick.profile(air);
                    for (const std::string &action : kosmos::joy_axis_actions(air))
                    {
                        ImGui::PushID(action.c_str());
                        kosmos::AxisBinding &b = p.axes[action];
                        ImGui::Text("%-9s", action.c_str());
                        ImGui::SameLine(90.0f);
                        const bool waiting = joystick.learning() && joystick.learn_action() == action;
                        if (ImGui::Button(waiting ? "Move axis..." : b.axis < 0 ? "Bind" : ("Axis " + std::to_string(b.axis)).c_str(), ImVec2(90.0f, 0.0f)))
                            waiting ? joystick.cancel_learn() : joystick.begin_learn(air, action, true);
                        ImGui::SameLine();
                        ImGui::Checkbox("Invert", &b.invert);
                        if (action != "throttle" && action != "hover")
                        {
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(80.0f);
                            ui::slider_float("Dead", &b.deadzone, 0.0f, 0.5f, "%.2f");
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(80.0f);
                            ui::slider_float("Curve", &b.expo, 0.0f, 1.0f, "%.2f");
                        }
                        ImGui::PopID();
                    }
                    for (const std::string &action : kosmos::joy_button_actions(air))
                    {
                        ImGui::PushID(action.c_str());
                        kosmos::ButtonBinding &b = p.buttons[action];
                        ImGui::Text("%-13s", action.c_str());
                        ImGui::SameLine(120.0f);
                        const bool waiting = joystick.learning() && joystick.learn_action() == action;
                        if (ImGui::Button(waiting ? "Press..." : b.button < 0 ? "Bind" : ("Button " + std::to_string(b.button)).c_str(), ImVec2(90.0f, 0.0f)))
                            waiting ? joystick.cancel_learn() : joystick.begin_learn(air, action, false);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Clear"))
                            b.button = -1;
                        ImGui::PopID();
                    }
                    ImGui::PopID();
                }
                if (ImGui::Button("Save"))
                    joystick.save();
                ImGui::SameLine();
                if (ImGui::Button("Reset to defaults"))
                {
                    joystick.aircraft = kosmos::joy_default_profile(true);
                    joystick.spacecraft = kosmos::joy_default_profile(false);
                }
                ImGui::TextDisabled("Saved to joystick.json beside settings.json");
            }
            ImGui::End();
        }
    }

    void App::settings_graphics()
    {
        sl::PropertyTree &props = *props_ref;
        auto flag = [&](const char *label, const char *path, const char *tip)
        {
            bool v = props.get_bool(path);
            if (ImGui::Checkbox(label, &v))
                props.set_bool(path, v);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tip);
        };
            static const char *styles[] = {"Normal", "Flat shaded", "Wireframe"};
            int style = props.get_int("/sim/settings/gfx-style");
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::Combo("Render style", &style, styles, 3))
                props.set_int("/sim/settings/gfx-style", style);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Flat shaded and wireframe skip textures, clouds and haze: much lighter on slow or software graphics.");
            {
                static const char *aa[] = {"Off", "2x", "4x", "8x"};
                static const int samples[] = {1, 2, 4, 8};
                int cur = 0;
                for (int i = 0; i < 4; ++i)
                    if (samples[i] == props.get_int("/sim/settings/gfx-msaa", 4))
                        cur = i;
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::Combo("Anti-aliasing", &cur, aa, 4))
                    props.set_int("/sim/settings/gfx-msaa", samples[cur]);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Multisample edge smoothing of the 3D view (clamped to what the GPU supports). Textures are always mipmapped and anisotropically filtered.");
            }
            {
                static const char *ss[] = {"Bilinear", "Supersampled 2x", "Supersampled 4x"};
                static const int factors[] = {1, 2, 4};
                int cur = 1;
                for (int i = 0; i < 3; ++i)
                    if (factors[i] == props.get_int("/sim/settings/gfx-screen-ss", 2))
                        cur = i;
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::Combo("MFD / HUD filtering", &cur, ss, 3))
                    props.set_int("/sim/settings/gfx-screen-ss", factors[cur]);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("The MFD screens and cockpit HUD are textures. Supersampling draws them at 2x or 4x size and filters them down, which removes the shimmer when they are small on screen.");
            }
            flag("Clouds", "/sim/settings/gfx-clouds", "Cloud layers");
            int fps = props.get_int("/sim/settings/gfx-max-fps");
            ImGui::SetNextItemWidth(160.0f);
            if (ui::slider_int("Max frame rate", &fps, 10, 240))
                props.set_int("/sim/settings/gfx-max-fps", fps);
            int radius = props.get_int("/sim/settings/gfx-light-radius");
            ImGui::SetNextItemWidth(160.0f);
            if (ui::slider_int("Base light radius (m)", &radius, 500, 20000))
                props.set_int("/sim/settings/gfx-light-radius", radius);
            int weapon_lights = props.get_int("/sim/settings/gfx-weapon-lights");
            ImGui::SetNextItemWidth(160.0f);
            if (ui::slider_int("Weapon lights", &weapon_lights, 0, 8))
                props.set_int("/sim/settings/gfx-weapon-lights", weapon_lights);
            static const char *details[] = {"Ultra", "High", "Medium", "Low"};
            int detail = props.get_int("/sim/settings/gfx-terrain-detail");
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::Combo("Terrain detail", &detail, details, 4))
                props.set_int("/sim/settings/gfx-terrain-detail", detail);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Applies the next time a world's terrain is built (restart).");
            bool relief = props.get_bool("/sim/settings/gfx-earth-relief");
            if (ImGui::Checkbox("Earth relief from orbit", &relief))
                props.set_bool("/sim/settings/gfx-earth-relief", relief);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Terrain relief and sea glint from 300 km down, instead of the tiled sphere to 20 km. Slower.");
    }

    void App::settings_weather()
    {
        sl::PropertyTree &props = *props_ref;
        auto flag = [&](const char *label, const char *path, const char *tip)
        {
            bool v = props.get_bool(path);
            if (ImGui::Checkbox(label, &v))
                props.set_bool(path, v);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tip);
        };
            flag("Weather", "/sim/settings/weather", "Wind, rain, cloud and visibility on planets with an atmosphere");
            flag("Rain", "/sim/settings/weather-rain", "Draw falling rain");
            {
                static const char *styles[] = {"Simple (one deck)", "Layers (up to 4)", "Volumetric"};
                int style = std::clamp(props.get_int("/sim/settings/gfx-cloud-style"), 0, 2);
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::Combo("Cloud style", &style, styles, 3))
                    props.set_int("/sim/settings/gfx-cloud-style", style);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Simple: one cloud deck at the lowest base.\nLayers: every reported layer, up to four, with fog inside thick cloud.\nVolumetric: Layers with ray-marched 3D towers; heavy on the GPU.");
            }
            {
                const auto &presets = kosmos::Weather::presets();
                const std::string cur = props.get_string("/sim/settings/weather-preset", "auto");
                std::string label = cur == "auto" ? "Automatic" : cur == "custom" ? "Custom METAR" : cur;
                for (const auto &p : presets)
                    if (cur == p.id)
                        label = p.label;
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::BeginCombo("Weather preset", label.c_str()))
                {
                    if (ImGui::Selectable("Automatic", cur == "auto"))
                        props.set_string("/sim/settings/weather-preset", "auto");
                    for (const auto &p : presets)
                        if (ImGui::Selectable(p.label, cur == p.id))
                            props.set_string("/sim/settings/weather-preset", p.id);
                    if (ImGui::Selectable("Custom METAR", cur == "custom"))
                        props.set_string("/sim/settings/weather-preset", "custom");
                    ImGui::EndCombo();
                }
                if (cur == "custom")
                {
                    char (&metar)[256] = memory.metar;
                    if (!memory.metar_loaded)
                    {
                        std::snprintf(metar, sizeof metar, "%s", props.get_string("/sim/settings/weather-metar", "").c_str());
                        memory.metar_loaded = true;
                    }
                    ImGui::SetNextItemWidth(360.0f);
                    if (ImGui::InputText("METAR", metar, sizeof metar))
                        props.set_string("/sim/settings/weather-metar", metar);
                    if (!props.get_bool("/environment/metar-valid", true))
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Not a valid METAR (needs station and time, e.g. KOSM 011200Z 27015G25KT 3SM TS BKN010)");
                }
            }
            {
                const bool live_now = props.get_string("/sim/settings/weather-source", "procedural") == "live";
                if (ImGui::RadioButton("Procedural weather", !live_now))
                    props.set_string("/sim/settings/weather-source", "procedural");
                ImGui::SameLine();
                if (ImGui::RadioButton("Live (Earth, NOAA)", live_now))
                    props.set_string("/sim/settings/weather-source", "live");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Real METARs from aviationweather.gov, fetched at most hourly. Used only while the simulated date is within three hours of the real one.");
                if (live_now)
                    ImGui::TextDisabled("%s", sim.weather.live_status().c_str());
            }
    }

    void App::settings_clutter()
    {
        sl::PropertyTree &props = *props_ref;
            bool clutter_enabled = props.get_bool("/sim/settings/gfx-clutter", true);
            if (ImGui::Checkbox("Trees, buildings and rocks", &clutter_enabled))
                props.set_bool("/sim/settings/gfx-clutter", clutter_enabled);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Scatter over the ground near you: trees, shrubs and houses on Earth (from its land cover), boulders on rocky worlds, and outposts on airless ones.");
            if (clutter_enabled)
            {
                int clutter_density = props.get_int("/sim/settings/gfx-clutter-density", 100);
                ImGui::SetNextItemWidth(160.0f);
                if (ui::slider_int("Scatter density (%)", &clutter_density, 10, 200))
                    props.set_int("/sim/settings/gfx-clutter-density", clutter_density);
                int clutter_radius = props.get_int("/sim/settings/gfx-clutter-radius", 100);
                ImGui::SetNextItemWidth(160.0f);
                if (ui::slider_int("Draw distance (%)", &clutter_radius, 25, 200))
                    props.set_int("/sim/settings/gfx-clutter-radius", clutter_radius);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("How far away scenery is drawn, as a share of the default (mesh out to 150 m, billboards to 900 m for trees). Lower is faster.");
            }
    }

    void App::settings_worlds()
    {
        sl::PropertyTree &props = *props_ref;
            static const int widths[] = {0, 2048, 4096, 8192, 16384};
            static const char *names[] = {"Automatic", "2048 (small)", "4096", "8192 (large)", "16384 (huge)"};
            int current = 0;
            const int saved = props.get_int("/sim/settings/world-texture");
            for (int w = 0; w < 5; ++w)
                if (widths[w] == saved)
                    current = w;
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::Combo("World texture", &current, names, 5))
            {
                props.set_int("/sim/settings/world-texture", widths[current]);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Detail of the far-view image of invented worlds, applied as soon as it is made.\nEach size is made once and kept on disk: 4096 is about 35 MB, 8192 about 135 MB, 16384 about 540 MB per world,\nand needs that much video memory to show.");
    }

    void App::draw_settings_window()
    {
        sl::PropertyTree &props = *props_ref;
        if (windows.settings_window)
        {
            ImGui::SetNextWindowPos(ImVec2(330.0f, 12.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.9f);
            if (ImGui::Begin("Settings [F3]", &windows.settings_window, ImGuiWindowFlags_AlwaysAutoResize))
            {
                auto flag = [&](const char *label, const char *path, const char *tip)
                {
                    bool v = props.get_bool(path);
                    if (ImGui::Checkbox(label, &v))
                        props.set_bool(path, v);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", tip);
                };
                ImGui::SeparatorText("Mouse look");
                flag("Flip left-right", "/sim/settings/mouse-invert-x", "Reverse the horizontal mouse look in every view");
                flag("Flip up-down", "/sim/settings/mouse-invert-y", "Reverse the vertical mouse look in every view");
                ImGui::SeparatorText("Display");
                flag("Body labels", "/sim/settings/labels", "Name tags on planets and moons too small to recognise");
                flag("Ship labels", "/sim/settings/ship-labels", "Name and distance tags on other ships");
                flag("Orbit line", "/sim/settings/orbit-line", "Draw the flown ship's orbit (O)");
                flag("Key help text", "/sim/settings/help-text", "The key reminder lines under the info text");
                flag("Cockpit tooltips", "/sim/settings/tooltips", "Name the panel control or virtual cockpit switch under the mouse");
                flag("Warp and time", "/sim/settings/clock", "Warp factor, date and elapsed time along the bottom of the screen");
                ImGui::SeparatorText("Sound");
                flag("Sound on", "/sim/settings/sound", "Engine sound (M)");
                int volume = props.get_int("/sim/settings/volume");
                if (ui::slider_int("Volume", &volume, 0, 100, "%d%%"))
                    props.set_int("/sim/settings/volume", volume);
                ImGui::SeparatorText("Simulation");
                flag("Nonspherical gravity (J2)", "/sim/settings/nonspherical-gravity",
                     "Earth and Mars J2 gravity during physics flight.\nHigh-warp rails and time skips suspend it; orbit predictions remain Keplerian.");
                if (props.get_bool("/sim/settings/nonspherical-gravity") && ship && ship->on_rails())
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "J2 suspended on rails");
                int warp = props.get_int("/sim/warp-index");
                if (ui::slider_int("Time warp", &warp, 0, Sim::warp_levels - 1))
                    props.set_int("/sim/warp-index", warp);
                ImGui::TextDisabled("Settings are saved when you exit.");
                ImGui::TextDisabled("Graphics, weather, scenery and texture options are under Edit > Performance settings.");
            }
            ImGui::End();
        }
    }

    void App::draw_icbm_window()
    {
        if (!windows.icbm_window)
            return;
        sl::PropertyTree &props = *props_ref;
        ImGui::SetNextWindowPos(ImVec2(330.0f, 60.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.9f);
        if (ImGui::Begin("ICBM targeting", &windows.icbm_window, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (!props.exists("/icbm/target/lat"))
            {
                ImGui::TextDisabled("Fly the CVEL Sentinel ICBM (scenario 21) to use this.");
                ImGui::End();
                return;
            }
            const std::string phase = props.get_string("/icbm/phase", "idle");
            const bool armed = props.get_bool("/icbm/armed");
            const bool locked = armed || phase != "idle";
            const double range = props.get_double("/icbm/range-km");
            const bool in_range = range >= 300.0 && range <= 4900.0;
            ImGui::BeginDisabled(locked);
            double lat = props.get_double("/icbm/target/lat");
            double lon = props.get_double("/icbm/target/lon");
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::InputDouble("Target latitude", &lat, 0.1, 1.0, "%.4f"))
                props.set_double("/icbm/target/lat", std::max(-90.0, std::min(90.0, lat)));
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::InputDouble("Target longitude", &lon, 0.1, 1.0, "%.4f"))
                props.set_double("/icbm/target/lon", std::fmod(lon + 540.0, 360.0) - 180.0);
            bool air = props.get_string("/controls/ordnance/mode", "air") == "air";
            if (ImGui::RadioButton("Airburst", air))
                props.set_string("/controls/ordnance/mode", "air");
            ImGui::SameLine();
            if (ImGui::RadioButton("Ground burst", !air))
                props.set_string("/controls/ordnance/mode", "ground");
            float height = (float)props.get_double("/controls/ordnance/burst-height", 500.0);
            ImGui::BeginDisabled(!air);
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::SliderFloat("Burst height (m)", &height, 0.0f, 10000.0f, "%.0f"))
                props.set_double("/controls/ordnance/burst-height", height);
            ImGui::EndDisabled();
            float kt = (float)props.get_double("/controls/ordnance/yield-kt", 20.0);
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::SliderFloat("Yield (kt)", &kt, 20.0f, 1000.0f, "%.0f", ImGuiSliderFlags_Logarithmic))
                props.set_double("/controls/ordnance/yield-kt", kt);
            ImGui::EndDisabled();
            ImGui::Text("Range %.0f km (allowed 300 to 4900)%s", range, in_range ? "" : "  OUT OF RANGE");
            ImGui::Text("Status: %s", props.get_string("/icbm/status", "Safe").c_str());
            if (phase != "idle")
                ImGui::Text("Phase: %s   predicted miss %.1f km", phase.c_str(), props.get_double("/icbm/miss-km"));
            if (phase == "idle")
            {
                if (ImGui::Button(armed ? "Disarm" : "Arm"))
                {
                    if (armed)
                    {
                        props.set_bool("/icbm/armed", false);
                        props.set_string("/icbm/status", "Safe");
                    }
                    else if (in_range)
                    {
                        props.set_bool("/icbm/armed", true);
                        props.set_string("/icbm/status", "Armed - ready to launch");
                    }
                    else
                        props.set_string("/icbm/status", "Target out of range");
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!armed || !in_range);
                if (ImGui::Button("LAUNCH"))
                    props.set_bool("/icbm/launch", true);
                ImGui::EndDisabled();
            }
        }
        ImGui::End();
    }

    void App::draw_performance_window()
    {
        if (!windows.performance_window)
            return;
        sl::PropertyTree &props = *props_ref;
        ImGui::SetNextWindowPos(ImVec2(330.0f, 12.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.9f);
        if (ImGui::Begin("Performance settings", &windows.performance_window, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::SeparatorText("Graphics");
            settings_graphics();
            ImGui::SeparatorText("Weather");
            settings_weather();
            ImGui::SeparatorText("Scenery");
            settings_clutter();
            ImGui::SeparatorText("Textures");
            static const int sizes[] = {0, 2048, 4096, 8192, 16384};
            static const char *size_names[] = {"Default (16384)", "2048", "4096", "8192", "16384"};
            int largest = 0;
            const int saved = props.get_int("/sim/settings/max-texture");
            for (int i = 0; i < 5; ++i)
                if (sizes[i] == saved)
                    largest = i;
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::Combo("Largest texture", &largest, size_names, 5))
                props.set_int("/sim/settings/max-texture", sizes[largest]);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Hi-res planet images and clouds are scaled down to this width. Smaller needs less video memory.\nApplies the next time Kosmos starts (the launcher's --max-texture overrides it).");
            settings_worlds();
            ImGui::TextDisabled("Saved when you exit. The launcher's Graphics page sets the same options.");
        }
        ImGui::End();
    }

    void App::part_converted_mesh(kosmos::ShipSpec &fspec)
    {
        if (part_tool.part_edit.converted && !part_tool.part_edit.model.empty())
        {
            ImGui::Separator();
            ImGui::Text("Converted: %s", part_tool.part_edit.model.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Use in ship.json"))
            {
                std::string problem;
                part_tool.part_status = kosmos::write_part_model(fspec.source_file, part_tool.part_edit.stage, part_tool.part_edit.index, part_tool.part_edit.model, problem)
                                  ? "ship.json now uses " + part_tool.part_edit.model + " (original kept as ship.json.bak); effective on the next launch"
                                  : "Not written: " + problem;
            }
            ImGui::Separator();
        }
    }

    void App::part_paused_controls()
    {
        sl::PropertyTree &props = *props_ref;
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Editing %s in %s", part_tool.part_edit.file.filename().string().c_str(), part_tool.blender_edit.name().c_str());
        ImGui::TextWrapped("The simulation is paused. Save in %s (Ctrl+S) to see the change here; closing %s resumes.", part_tool.blender_edit.name().c_str(), part_tool.blender_edit.name().c_str());
        if (ImGui::Button(("Resume, keep " + part_tool.blender_edit.name() + " open").c_str()))
        {
            part_tool.edit_paused = false;
            props.set_bool("/sim/blender-edit", false);
            props.set_bool("/sim/paused", part_tool.edit_was_paused);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel and restore"))
        {
            part_tool.blender_edit.stop();
            if (part_tool.part_edit.converted && part_tool.part_edit.part)
                *part_tool.part_edit.part = part_tool.part_edit.original;
            part_tool.blender_edit.restore();
            part_tool.part_edit.model.clear();
            part_tool.part_edit.converted = false;
            part_tool.edit_paused = false;
            props.set_bool("/sim/blender-edit", false);
            props.set_bool("/sim/paused", part_tool.edit_was_paused);
            part_tool.part_status = "Edit cancelled; the original was restored";
        }
    }

    void App::part_live_controls(kosmos::Ship &flown, kosmos::ShipSpec &fspec)
    {
        sl::PropertyTree &props = *props_ref;
        ImGui::SeparatorText("Meshes of the flown ship");
        int stage_index = 0;
        for (kosmos::StageSpec &stage : fspec.stages)
        {
            int part_index = 0;
            for (kosmos::PartSpec &part : stage.parts)
            {
                ImGui::PushID(&part);
                const bool is_model = !part.model.empty();
                const std::string what = is_model ? std::filesystem::path(part.model).filename().string() : (part.prop.empty() ? std::string("profile") : part.prop);
                ImGui::Text("%s: %s", stage.name.c_str(), what.c_str());
                ImGui::SameLine(ImGui::GetWindowWidth() - 150.0f);
                if (ImGui::SmallButton(is_model ? (props.get_string("/sim/settings/mesh-editor", "blender") == "kosmoscad" ? "Edit in KosmosCAD" : "Edit in Blender") : "Convert + edit") && !netplay.shared())
                {
                    std::filesystem::path file;
                    std::string problem;
                    bool ok = true;
                    kosmos::PartSpec saved = part;
                    if (is_model)
                    {
                        file = std::filesystem::path(fspec.directory) / part.model;
                        std::string ext = file.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        const bool cad_editor = props.get_string("/sim/settings/mesh-editor", "blender") == "kosmoscad";
                        if (cad_editor && ext == ".gltf")
                        {
                            problem = "KosmosCAD saves .glb or .obj; use Blender for .gltf parts, or convert it first";
                            ok = false;
                        }
                        else if (ext != ".glb" && ext != ".gltf" && ext != ".obj")
                        {
                            problem = "Blender can edit .glb, .gltf and .obj parts; convert " + ext + " first";
                            ok = false;
                        }
                    }
                    else
                    {
                        sl::MeshData data;
                        ok = part.prop.empty() ? (data = make_lathe_data(part), !data.indices.empty()) : make_prop_data(part, data);
                        if (!ok)
                            problem = "this part has no geometry to export";
                        else
                        {
                            std::string slug = fspec.name + "_" + std::to_string(stage_index) + "_" + std::to_string(part_index);
                            for (char &c : slug)
                                if (!std::isalnum(static_cast<unsigned char>(c)))
                                    c = '_';
                            file = std::filesystem::path(fspec.directory) / "models" / (slug + ".glb");
                            std::error_code ec;
                            std::filesystem::create_directories(file.parent_path(), ec);
                            // An earlier conversion may hold edits: use it rather than rebuild the shape.
                            if (!std::filesystem::exists(file, ec))
                                ok = kosmos::write_glb(data, file, problem);
                            else
                                part_tool.part_status = "Reusing the existing " + file.filename().string();
                        }
                    }
                    if (ok && props.get_bool("/sim/blender-edit"))
                    {
                        ok = false;
                        part_tool.part_status = "Another mesh edit is already running";
                    }
                    if (ok && part_tool.blender_edit.start(kosmos::mesh_editor_choice(props.get_string("/sim/settings/mesh-editor", "blender"), props.get_string("/sim/settings/blender", "blender"), props.get_string("/sim/settings/kosmoscad", ""), std::filesystem::path(sim.root())), file, problem))
                    {
                        part_tool.part_edit.part = &part;
                        part_tool.part_edit.original = saved;
                        part_tool.part_edit.converted = !is_model;
                        part_tool.part_edit.file = file;
                        part_tool.part_edit.stage = stage_index;
                        part_tool.part_edit.index = part_index;
                        part_tool.part_edit.model.clear();
                        if (!is_model)
                        {
                            part.model = std::filesystem::relative(file, fspec.directory).generic_string();
                            part.prop.clear();
                            part_tool.part_edit.model = part.model;
                            const std::string snippet = "{\"model\": \"" + part.model + "\", \"offset\": [" + std::to_string(part.offset.x) + ", " + std::to_string(part.offset.y) + ", " + std::to_string(part.offset.z) + "]}";
                            ImGui::SetClipboardText(snippet.c_str());
                            sim.log("Copied part entry for ship.json: " + snippet);
                        }
                        part_tool.edit_was_paused = props.get_bool("/sim/paused");
                        props.set_bool("/sim/paused", true);
                        props.set_bool("/sim/blender-edit", true);
                        part_tool.edit_paused = true;
                        part_tool.part_status = part_tool.part_status.rfind("Reusing", 0) == 0 ? part_tool.part_status + "; " + part_tool.blender_edit.name() + " started" : part_tool.blender_edit.name() + " started";
                    }
                    else if (!problem.empty())
                        part_tool.part_status = problem;
                }
                ImGui::PopID();
                ++part_index;
            }
            ++stage_index;
        }
        if (fspec.stages.empty())
            ImGui::TextDisabled("This ship has no parts.");
    }

    void App::draw_part_window()
    {
        sl::PropertyTree &props = *props_ref;
        if (windows.part_window)
        {
            ImGui::SetNextWindowPos(ImVec2(330.0f, 80.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(520.0f, 360.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.92f);
            if (ImGui::Begin("Part editor", &windows.part_window))
            {
                kosmos::Ship &flown = *sim.active_ship();
                kosmos::ShipSpec &fspec = flown.editable_spec();
                if (part_tool.blender_buffer[0] == '\0')
                    copy_text(part_tool.blender_buffer, sizeof part_tool.blender_buffer, props.get_string("/sim/settings/blender", "blender"));
                {
                    const bool cad = props.get_string("/sim/settings/mesh-editor", "blender") == "kosmoscad";
                    ImGui::SetNextItemWidth(160.0f);
                    if (ImGui::BeginCombo("Mesh editor", cad ? "KosmosCAD" : "Blender"))
                    {
                        if (ImGui::Selectable("Blender", !cad))
                            props.set_string("/sim/settings/mesh-editor", "blender");
                        if (ImGui::Selectable("KosmosCAD", cad))
                            props.set_string("/sim/settings/mesh-editor", "kosmoscad");
                        ImGui::EndCombo();
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Blender has the full toolset. KosmosCAD is the simple editor that knows engines, habitats and systems.");
                    if (!cad)
                    {
                        ImGui::SetNextItemWidth(260.0f);
                        if (ImGui::InputText("Blender", part_tool.blender_buffer, sizeof part_tool.blender_buffer))
                            props.set_string("/sim/settings/blender", part_tool.blender_buffer);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("The Blender executable. Saved with the settings; --blender PATH overrides it.");
                    }
                }
                // Stays after Blender closes, so the converted mesh can still be written into the ship file.
                part_converted_mesh(fspec);
                if (part_tool.edit_paused)
                {
                    part_paused_controls();
                }
                else
                {
                    part_live_controls(flown, fspec);
                }
                if (!part_tool.part_status.empty())
                    ImGui::TextWrapped("%s", part_tool.part_status.c_str());
            }
            ImGui::End();
        }
    }

    // Window menu hints follow the player's key bindings.
    void App::refresh_menu_keys()
    {
        static const std::pair<const char *, const char *> window_actions[] = {{"Cameras", "window_cameras"}, {"Settings", "window_settings"}, {"Ship systems", "window_systems"},
                                                                              {"Properties", "window_properties"}, {"Lua console", "window_lua"}, {"Multiplayer", "window_multiplayer"}};
        for (kosmos::MenuWindow &w : menu_windows)
            for (const auto &[label, action] : window_actions)
                if (w.label == label)
                    w.key = keys.label_for(action);
    }

    void App::draw_keys_window()
    {
        if (!windows.keys_window)
            return;
        ImGui::SetNextWindowPos(ImVec2(380.0f, 50.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(560.0f, 520.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.94f);
        if (ImGui::Begin("Keyboard", &windows.keys_window))
        {
            ImGui::TextWrapped("Click a key to rebind it, then press the new key (Esc cancels). Right-click a key to clear it. A key already used by another action in the same context is taken from it.");
            ImGui::TextDisabled("Fixed: Esc, Alt+Enter, 1-9 (pack items), arrows and Enter in the galaxy map.");
            if (!keys_status.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", keys_status.c_str());
            if (ImGui::Button("Reset all to defaults"))
            {
                keys.reset_all();
                keys.save();
                keys_status.clear();
                refresh_menu_keys();
            }
            ImGui::Separator();
            if (ImGui::BeginChild("keylist"))
            {
                const auto groups = keys.action_groups();
                for (std::size_t group_index = 0; group_index < groups.size(); ++group_index)
                {
                    const auto &group = groups[group_index];
                    ImGui::PushID(static_cast<int>(group_index));
                    const bool open = ImGui::CollapsingHeader(group.name.c_str(), group.name == "Ship controls" ? ImGuiTreeNodeFlags_DefaultOpen : 0);
                    if (!open)
                    {
                        ImGui::PopID();
                        continue;
                    }
                    for (std::size_t action_index = 0; action_index < group.actions.size(); ++action_index)
                    {
                        const kosmos::KeyAction &a = *group.actions[action_index];
                        ImGui::PushID(static_cast<int>(action_index));
                        ImGui::TextUnformatted(a.label.c_str());
                        ImGui::SameLine(300.0f);
                        for (int slot = 0; slot < 2; ++slot)
                        {
                            ImGui::PushID(slot);
                            const bool waiting = keys.learning() && keys.learn_id() == a.id && keys.learn_slot() == slot;
                            const std::string text = waiting ? "Press a key..." : kosmos::KeyBindings::chord_name(a.chord[slot]);
                            if (ImGui::Button(text.c_str(), ImVec2(100.0f, 0.0f)))
                                waiting ? keys.cancel_learn() : keys.begin_learn(a.id, slot);
                            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                            {
                                keys.clear(a.id, slot);
                                keys.save();
                                refresh_menu_keys();
                            }
                            ImGui::PopID();
                            ImGui::SameLine();
                        }
                        if (!keys.is_default(a) && ImGui::SmallButton("Reset"))
                        {
                            keys.reset(a.id);
                            keys.save();
                            refresh_menu_keys();
                        }
                        ImGui::NewLine();
                        ImGui::PopID();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();
        // A rebind finishes in the event loop; keep the window hints current.
        if (!keys.learning())
            refresh_menu_keys();
    }

    void App::draw_quantum_window()
    {
        if (quantum.quantum_window)
        {
            // The portal console: pick a star in range and dial it from near the system's quantum portal.
            const double range_ly = 50.0;
            ImGui::SetNextWindowPos(ImVec2(330.0f, 120.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(420.0f, 400.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.9f);
            if (ImGui::Begin("Quantum portal", &quantum.quantum_window))
            {
                ImGui::Text("Position: %s", sim.galaxy.star(sim.system_index()).name.c_str());
                const kosmos::Ship *portal = portal_ship(sim);
                if (!portal)
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "No portal in this system.");
                else
                {
                    const double distance = glm::length(portal->body().position - ship->body().position);
                    ImGui::Text("Portal: %.1f %s away", distance >= 1.0e4 ? distance / 1000.0 : distance, distance >= 1.0e4 ? "km" : "m");
                    if (quantum.dialled >= 0)
                        ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), "%s: %s", sim.galaxy.star(quantum.dialled).name.c_str(), quantum.portal_open >= 1.0f ? "open, fly through the ring" : "dialling...");
                }
                ImGui::Separator();
                const std::vector<int> near = sim.galaxy.nearest(sim.system_index(), 64);
                if (ImGui::BeginChild("targets", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 3.0f)))
                {
                    if (ImGui::BeginTable("stars", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableSetupColumn("Star");
                        ImGui::TableSetupColumn("Class");
                        ImGui::TableSetupColumn("Planets");
                        ImGui::TableSetupColumn("Distance");
                        ImGui::TableHeadersRow();
                        for (const int id : near)
                        {
                            const double d = sim.galaxy.distance_ly(sim.system_index(), id);
                            if (d > range_ly)
                                break;
                            const kosmos::Star &st = sim.galaxy.star(id);
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            if (ImGui::Selectable((st.name + "##" + std::to_string(id)).c_str(), quantum.quantum_selected == id, ImGuiSelectableFlags_SpanAllColumns))
                                quantum.quantum_selected = id;
                            ImGui::TableNextColumn();
                            ImGui::Text("%c", st.spectral_class);
                            ImGui::TableNextColumn();
                            ImGui::Text("%d", sim.galaxy.planet_count(id));
                            ImGui::TableNextColumn();
                            ImGui::Text("%.2f ly", d);
                        }
                        ImGui::EndTable();
                    }
                }
                ImGui::EndChild();
                if (quantum.quantum_selected >= 0)
                {
                    const kosmos::Star &st = sim.galaxy.star(quantum.quantum_selected);
                    ImGui::Text("%s: %.1f solar masses, %.2f solar radii, %.0f K", st.name.c_str(), st.mass, st.radius, st.temperature);
                }
                else
                    ImGui::TextDisabled("Select a target star.");
                const bool ok = portal && quantum.quantum_selected >= 0 && quantum.quantum_selected != sim.system_index();
                if (!ok)
                    ImGui::BeginDisabled();
                if (ImGui::Button("Dial portal"))
                    dial_portal(quantum.quantum_selected);
                if (!ok)
                    ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("%s", quantum.status.empty() ? "Dial within 100 km of the ring." : quantum.status.c_str());
            }
            ImGui::End();
        }
    }

    void App::camera_editor_buttons(bool &changed, int count)
    {
    if (ImGui::Button("Add orbit"))
    {
        kosmos::UserCamera c;
        c.name = "Orbit " + std::to_string(count + 1);
        rig.user_cams.push_back(c);
        rig.camera_editor_pick = count;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add mounted"))
    {
        kosmos::UserCamera c;
        c.name = "Mounted " + std::to_string(count + 1);
        c.orbit = false;
        c.y = 2.0;
        rig.user_cams.push_back(c);
        rig.camera_editor_pick = count;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy") && count > 0)
    {
        kosmos::UserCamera c = rig.user_cams[static_cast<std::size_t>(rig.camera_editor_pick)];
        c.name += " copy";
        rig.user_cams.push_back(c);
        rig.camera_editor_pick = count;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && count > 0)
    {
        rig.user_cams.erase(rig.user_cams.begin() + rig.camera_editor_pick);
        if (rig.user_cam == rig.camera_editor_pick)
            rig.user_cam = -1;
        else if (rig.user_cam > rig.camera_editor_pick)
            --rig.user_cam;
        changed = true;
    }
    }

    void App::camera_editor_list()
    {
    if (ImGui::BeginListBox("##cams", ImVec2(-FLT_MIN, 100.0f)))
    {
        for (int ui = 0; ui < static_cast<int>(rig.user_cams.size()); ++ui)
        {
            ImGui::PushID(ui);
            if (ImGui::Selectable((rig.user_cams[static_cast<std::size_t>(ui)].name + (rig.user_cam == ui ? "  (viewing)" : "")).c_str(), rig.camera_editor_pick == ui))
                rig.camera_editor_pick = ui;
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
    }

    void App::camera_editor_fields(bool &changed)
    {
        kosmos::Universe &universe = *universe_ref;
        const int ui = rig.camera_editor_pick;
        kosmos::UserCamera &uc = rig.user_cams[static_cast<std::size_t>(ui)];
        if (ImGui::Button(rig.user_cam == ui ? "Back to the ship view" : "Look through"))
        {
            rig.user_cam = rig.user_cam == ui ? -1 : ui;
            cockpit_view = cockpit3d = false;
            rig.interior_cam = rig.base_view = -1;
            map_mode = galaxy_mode = false;
            rig.look_yaw = rig.look_pitch = 0.0f;
        }
        ImGui::Separator();
        char name[64];
        std::snprintf(name, sizeof(name), "%s", uc.name.c_str());
        if (ImGui::InputText("Name", name, sizeof(name)))
            uc.name = name, changed = true;
        int type = uc.orbit ? 0 : 1;
        if (ImGui::RadioButton("Orbits it, always looking at it", type == 0))
            uc.orbit = true, changed = true;
        if (ImGui::RadioButton("Mounted on it", type == 1))
            uc.orbit = false, changed = true;

        // A combo of the ships, or of the bodies, by name.
        auto pick_name = [&](const char *label, int kind, std::string &value)
        {
            const bool bodies = kind == kosmos::UserCamera::body;
            if (ImGui::BeginCombo(label, value.empty() ? "(choose)" : value.c_str()))
            {
                if (bodies)
                    for (const kosmos::Body &b : universe.bodies)
                    {
                        if (b.minor)
                            continue;
                        if (ImGui::Selectable(b.name.c_str(), b.name == value))
                            value = b.name, changed = true;
                    }
                else
                    for (int si = 0; si < sim.ship_count(); ++si)
                        if (ImGui::Selectable(sim.ship(si)->name.c_str(), sim.ship(si)->name == value))
                            value = sim.ship(si)->name, changed = true;
                ImGui::EndCombo();
            }
        };
        ImGui::Separator();
        ImGui::TextUnformatted("Attached to");
        if (ImGui::RadioButton("The ship I fly", uc.attach == kosmos::UserCamera::flown_ship))
            uc.attach = kosmos::UserCamera::flown_ship, changed = true;
        ImGui::SameLine();
        if (ImGui::RadioButton("A ship", uc.attach == kosmos::UserCamera::other_ship))
            uc.attach = kosmos::UserCamera::other_ship, changed = true;
        ImGui::SameLine();
        if (ImGui::RadioButton("A body", uc.attach == kosmos::UserCamera::body))
            uc.attach = kosmos::UserCamera::body, changed = true;
        if (uc.attach != kosmos::UserCamera::flown_ship)
            pick_name("##attach", uc.attach, uc.attach_name);
        ImGui::Separator();
        if (uc.orbit)
        {
            double d = uc.distance;
            if (ImGui::InputDouble("Distance (m, 0 frames it)", &d, 0.0, 0.0, "%.0f"))
                uc.distance = std::max(0.0, d), changed = true;
            float f = static_cast<float>(uc.yaw);
            if (ui::slider_float("Around", &f, -180.0f, 180.0f, "%.0f deg"))
                uc.yaw = f, changed = true;
            f = static_cast<float>(uc.pitch);
            if (ui::slider_float("Above", &f, -89.0f, 89.0f, "%.0f deg"))
                uc.pitch = f, changed = true;
            f = static_cast<float>(uc.spin);
            if (ui::slider_float("Spin", &f, -30.0f, 30.0f, "%.1f deg/s"))
                uc.spin = f, changed = true;
            ImGui::TextDisabled("Right-drag orbits, [ and ] zoom.");
        }
        else
        {
            float v[3] = {static_cast<float>(uc.x), static_cast<float>(uc.y), static_cast<float>(uc.z)};
            if (ImGui::InputFloat3("Right, up, back (m)", v))
                uc.x = v[0], uc.y = v[1], uc.z = v[2], changed = true;
            ImGui::TextUnformatted("Looks");
            const char *looks[] = {"The way the object faces", "At the ship I fly", "At a ship", "At a body"};
            int look = uc.look_at;
            if (ImGui::Combo("##look", &look, looks, 4))
                uc.look_at = look, changed = true;
            if (uc.look_at == kosmos::UserCamera::look_ahead)
            {
                float f = static_cast<float>(uc.look_yaw);
                if (ui::slider_float("Turned", &f, -180.0f, 180.0f, "%.0f deg"))
                    uc.look_yaw = f, changed = true;
                f = static_cast<float>(uc.look_pitch);
                if (ui::slider_float("Raised", &f, -89.0f, 89.0f, "%.0f deg"))
                    uc.look_pitch = f, changed = true;
            }
            else if (uc.look_at != kosmos::UserCamera::look_flown_ship)
                pick_name("##lookname", uc.look_at == kosmos::UserCamera::look_body ? kosmos::UserCamera::body : kosmos::UserCamera::other_ship, uc.look_name);
            ImGui::TextDisabled("On a ship: x right, y up, z back. On a body: east, up, south. Right-drag peeks around.");
        }
        float fov = static_cast<float>(uc.fov);
        if (ui::slider_float("Field of view", &fov, 10.0f, 110.0f, "%.0f deg"))
            uc.fov = fov, changed = true;
    }

    void App::draw_camera_editor_window()
    {
        if (windows.camera_editor_window)
        {
            ImGui::SetNextWindowPos(ImVec2(330.0f, 120.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(380.0f, 520.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.9f);
            if (ImGui::Begin("Camera editor", &windows.camera_editor_window))
            {
                bool changed = false;
                const int count = static_cast<int>(rig.user_cams.size());
                rig.camera_editor_pick = std::clamp(rig.camera_editor_pick, 0, std::max(0, count - 1));
                camera_editor_buttons(changed, count);
                camera_editor_list();
                if (!rig.user_cams.empty())
                    camera_editor_fields(changed);
                ImGui::Separator();
                ImGui::TextDisabled("C steps through these after the ship's own views.");
                if (changed)
                    kosmos::save_user_cameras(rig.user_cam_file, rig.user_cams);
            }
            ImGui::End();
        }
    }

    void App::draw_camera_window()
    {
        sl::PropertyTree &props = *props_ref;
        if (windows.camera_window)
        {
            ImGui::SetNextWindowPos(ImVec2(12.0f, 120.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(300.0f, 380.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.85f);
            if (ImGui::Begin("Cameras [F2]", &windows.camera_window))
            {
                const int active = props.get_int("/sim/active-ship");
                // Picking a ship's camera also hands you that ship's controls.
                auto pick = [&](int ship_index, bool cockpit, bool three_d, int interior, int base_index, int base_camera)
                {
                    if (ship_index >= 0 && ship_index != props.get_int("/sim/active-ship"))
                    {
                        props.set_bool("/autopilot/enabled", false);
                        props.set_string("/autopilot/status", "");
                        sim.activate(ship_index);
                        ship = sim.active_ship();
                        props.set_double("/controls/throttle", 0.0);
                        props.set_double("/controls/hover", 0.0);
                        props.set_string("/controls/sas", "stability");
                        sas_index = 0;
                        rig.cam_distance = ship->spec().camera_distance;
                    }
                    cockpit_view = cockpit;
                    cockpit3d = three_d;
                    rig.interior_cam = interior;
                    rig.user_cam = -1;
                    rig.base_view = base_index;
                    rig.base_view_cam = base_camera;
                    rig.look_yaw = rig.look_pitch = 0.0f;
                    map_mode = galaxy_mode = false;
                };
                const bool chase_now = !cockpit_view && rig.interior_cam < 0 && rig.base_view < 0 && rig.user_cam < 0;
                // Only the node holding the current camera starts unfolded; the others stay as the user leaves them.
                const bool in_ship_node = rig.base_view < 0 && rig.user_cam < 0;
                for (int si = 0; si < sim.ship_count(); ++si)
                {
                    const Ship &other = *sim.ship(si);
                    const kosmos::ShipSpec &spec = other.spec();
                    const bool mine = si == active;
                    ImGui::PushID(si);
                    ImGui::SetNextItemOpen(mine && in_ship_node, ImGuiCond_Appearing);
                    // Say where the vessel is, unless its name already does (the scenario labels read "Mir (Mars)").
                    std::string title = other.name;
                    const int soi = other.telemetry().soi;
                    if (soi >= 0 && soi < static_cast<int>(sim.universe.bodies.size()))
                    {
                        const std::string &where = sim.universe.bodies[static_cast<std::size_t>(soi)].name;
                        if (!where.empty() && title.find(where) == std::string::npos)
                            title += " (" + where + ")";
                    }
                    std::string partners;
                    for (const std::array<int, 4> &l : sim.nav.dock_links())
                    {
                        const int other_index = l[0] == si ? l[2] : l[2] == si ? l[0] : -1;
                        if (other_index >= 0 && other_index < sim.ship_count())
                            partners += (partners.empty() ? "" : ", ") + sim.ship(other_index)->name;
                    }
                    if (!partners.empty())
                        title += " - docked to " + partners;
                    if (ImGui::TreeNodeEx((title + (mine ? "  (flying)" : "")).c_str()))
                    {
                        if (ImGui::Selectable("External", mine && chase_now))
                            pick(si, false, false, -1, -1, 0);
                        if (spec.cockpit.present && ImGui::Selectable(spec.building() ? "Interface (2D panel)" : spec.cockpit.mfds || !spec.cockpit.panel.empty() ? "Cockpit (2D panel)" : "Cockpit", mine && cockpit_view && !cockpit3d && rig.interior_cam < 0 && rig.base_view < 0))
                            pick(si, true, false, -1, -1, 0);
                        if (spec.cockpit.present && spec.cockpit.mfds && ImGui::Selectable(spec.building() ? "Interface (3D)" : "Cockpit (3D, MFDs)", mine && cockpit_view && cockpit3d && rig.interior_cam < 0 && rig.base_view < 0))
                            pick(si, true, true, -1, -1, 0);
                        for (std::size_t ci = 0; ci < spec.cameras.size(); ++ci)
                            if (ImGui::Selectable(spec.cameras[ci].name.c_str(), mine && rig.interior_cam == static_cast<int>(ci) && rig.base_view < 0))
                                pick(si, false, false, static_cast<int>(ci), -1, 0);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                if (!rig.user_cams.empty())
                    ImGui::SetNextItemOpen(rig.user_cam >= 0, ImGuiCond_Appearing);
                if (!rig.user_cams.empty() && ImGui::TreeNodeEx("My cameras"))
                {
                    for (std::size_t ui = 0; ui < rig.user_cams.size(); ++ui)
                    {
                        ImGui::PushID(2000 + static_cast<int>(ui));
                        if (ImGui::Selectable(rig.user_cams[ui].name.c_str(), rig.user_cam == static_cast<int>(ui)))
                        {
                            pick(-1, false, false, -1, -1, 0);
                            rig.user_cam = static_cast<int>(ui);
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                // Base cameras, grouped by body. By default only the flown ship's body is listed, nearest base first.
                static bool all_bodies = false;
                struct BaseEntry { std::size_t index; double distance; };
                std::map<std::string, std::vector<BaseEntry>> by_body;
                const kosmos::Universe &uni = sim.universe;
                const Ship *flown = sim.active_ship();
                const int flown_soi = flown ? flown->telemetry().soi : -1;
                const std::string here = flown_soi >= 0 && flown_soi < static_cast<int>(uni.bodies.size()) ? uni.bodies[static_cast<std::size_t>(flown_soi)].name : std::string();
                for (std::size_t bi = 0; bi < sim.bases.size(); ++bi)
                {
                    const kosmos::Base &bs = sim.bases[bi];
                    if (bs.cameras.empty())
                        continue;
                    const bool current = rig.base_view == static_cast<int>(bi);
                    if (!all_bodies && bs.body != here && !current)
                        continue;
                    const int body = uni.find(bs.body);
                    double distance = 0.0;
                    if (body >= 0 && flown)
                        distance = glm::length(flown->origin() - (uni.position(body) + bs.direction * (uni.bodies[static_cast<std::size_t>(body)].radius + bs.height)));
                    by_body[bs.body].push_back({bi, distance});
                }
                std::vector<std::string> order;
                for (auto &entry : by_body)
                    order.push_back(entry.first);
                std::stable_sort(order.begin(), order.end(), [&](const std::string &x, const std::string &y) { return (x == here) > (y == here); });
                for (const std::string &body_name : order)
                {
                    std::vector<BaseEntry> &list = by_body[body_name];
                    std::sort(list.begin(), list.end(), [](const BaseEntry &x, const BaseEntry &y) { return x.distance < y.distance; });
                    const bool holds_current = std::any_of(list.begin(), list.end(), [&](const BaseEntry &e) { return rig.base_view == static_cast<int>(e.index); });
                    ImGui::PushID(body_name.c_str());
                    ImGui::SetNextItemOpen(holds_current, ImGuiCond_Appearing);
                    if (ImGui::TreeNodeEx(("Bases on " + body_name + " (" + std::to_string(list.size()) + ")").c_str()))
                    {
                        for (const BaseEntry &e : list)
                        {
                            const kosmos::Base &bs = sim.bases[e.index];
                            ImGui::PushID(1000 + static_cast<int>(e.index));
                            ImGui::SetNextItemOpen(rig.base_view == static_cast<int>(e.index), ImGuiCond_Appearing);
                            char dist[32] = "";
                            if (flown && body_name == here)
                                std::snprintf(dist, sizeof dist, e.distance >= 1.0e6 ? "  %.0f km" : "  %.1f km", e.distance / 1000.0);
                            if (ImGui::TreeNodeEx((bs.name + dist).c_str()))
                            {
                                for (std::size_t ci = 0; ci < bs.cameras.size(); ++ci)
                                    if (ImGui::Selectable(bs.cameras[ci].name.c_str(), rig.base_view == static_cast<int>(e.index) && rig.base_view_cam == static_cast<int>(ci)))
                                        pick(-1, false, false, -1, static_cast<int>(e.index), static_cast<int>(ci));
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::Checkbox("Bases on all bodies", &all_bodies);
                ImGui::Separator();
                ImGui::TextDisabled("C cycles the flown ship's views and your cameras. Right-drag looks around.");
                if (ImGui::Button("Camera editor..."))
                    windows.camera_editor_window = true;
            }
            ImGui::End();
        }
    }
} // namespace kosmos::app
