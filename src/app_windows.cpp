#include "app_state.h"
#include "ui_slider.h"

namespace kosmos::app
{
    void App::draw_systems_window()
    {
        if (systems_window)
        {
            ImGui::SetNextWindowPos(ImVec2(330.0f, 60.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.9f);
            if (ImGui::Begin("Ship systems [F1]", &systems_window, ImGuiWindowFlags_AlwaysAutoResize))
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
        if (joystick_window)
        {
            ImGui::SetNextWindowPos(ImVec2(360.0f, 40.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.92f);
            if (ImGui::Begin("Joystick", &joystick_window, ImGuiWindowFlags_AlwaysAutoResize))
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
        if (settings_window)
        {
            ImGui::SetNextWindowPos(ImVec2(330.0f, 12.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.9f);
            if (ImGui::Begin("Settings [F3]", &settings_window, ImGuiWindowFlags_AlwaysAutoResize))
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
        if (!icbm_window)
            return;
        sl::PropertyTree &props = *props_ref;
        ImGui::SetNextWindowPos(ImVec2(330.0f, 60.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.9f);
        if (ImGui::Begin("ICBM targeting", &icbm_window, ImGuiWindowFlags_AlwaysAutoResize))
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
        if (!performance_window)
            return;
        sl::PropertyTree &props = *props_ref;
        ImGui::SetNextWindowPos(ImVec2(330.0f, 12.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.9f);
        if (ImGui::Begin("Performance settings", &performance_window, ImGuiWindowFlags_AlwaysAutoResize))
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
        if (part_edit.converted && !part_edit.model.empty())
        {
            ImGui::Separator();
            ImGui::Text("Converted: %s", part_edit.model.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Use in ship.json"))
            {
                std::string problem;
                part_status = kosmos::write_part_model(fspec.source_file, part_edit.stage, part_edit.index, part_edit.model, problem)
                                  ? "ship.json now uses " + part_edit.model + " (original kept as ship.json.bak); effective on the next launch"
                                  : "Not written: " + problem;
            }
            ImGui::Separator();
        }
    }

    void App::part_paused_controls()
    {
        sl::PropertyTree &props = *props_ref;
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Editing %s in Blender", part_edit.file.filename().string().c_str());
        ImGui::TextWrapped("The simulation is paused. Save in Blender (Ctrl+S) to see the change here; closing Blender resumes.");
        if (ImGui::Button("Resume, keep Blender open"))
        {
            edit_paused = false;
            props.set_bool("/sim/blender-edit", false);
            props.set_bool("/sim/paused", edit_was_paused);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel and restore"))
        {
            blender_edit.stop();
            if (part_edit.converted && part_edit.part)
                *part_edit.part = part_edit.original;
            blender_edit.restore();
            part_edit.model.clear();
            part_edit.converted = false;
            edit_paused = false;
            props.set_bool("/sim/blender-edit", false);
            props.set_bool("/sim/paused", edit_was_paused);
            part_status = "Edit cancelled; the original was restored";
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
                if (ImGui::SmallButton(is_model ? "Edit in Blender" : "Convert + edit") && !netplay.shared())
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
                        if (ext != ".glb" && ext != ".gltf" && ext != ".obj")
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
                                part_status = "Reusing the existing " + file.filename().string();
                        }
                    }
                    if (ok && props.get_bool("/sim/blender-edit"))
                    {
                        ok = false;
                        part_status = "Another Blender edit is already running";
                    }
                    if (ok && blender_edit.start(props.get_string("/sim/settings/blender", "blender"), std::filesystem::path(sim.root()) / "data" / "blender" / "kosmos_edit.py", file, problem))
                    {
                        part_edit.part = &part;
                        part_edit.original = saved;
                        part_edit.converted = !is_model;
                        part_edit.file = file;
                        part_edit.stage = stage_index;
                        part_edit.index = part_index;
                        part_edit.model.clear();
                        if (!is_model)
                        {
                            part.model = std::filesystem::relative(file, fspec.directory).generic_string();
                            part.prop.clear();
                            part_edit.model = part.model;
                            const std::string snippet = "{\"model\": \"" + part.model + "\", \"offset\": [" + std::to_string(part.offset.x) + ", " + std::to_string(part.offset.y) + ", " + std::to_string(part.offset.z) + "]}";
                            ImGui::SetClipboardText(snippet.c_str());
                            sim.log("Copied part entry for ship.json: " + snippet);
                        }
                        edit_was_paused = props.get_bool("/sim/paused");
                        props.set_bool("/sim/paused", true);
                        props.set_bool("/sim/blender-edit", true);
                        edit_paused = true;
                        part_status = part_status.rfind("Reusing", 0) == 0 ? part_status + "; Blender started" : "Blender started";
                    }
                    else if (!problem.empty())
                        part_status = problem;
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
        if (part_window)
        {
            ImGui::SetNextWindowPos(ImVec2(330.0f, 80.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(520.0f, 360.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.92f);
            if (ImGui::Begin("Part editor", &part_window))
            {
                kosmos::Ship &flown = *sim.active_ship();
                kosmos::ShipSpec &fspec = flown.editable_spec();
                if (blender_buffer[0] == '\0')
                    copy_text(blender_buffer, sizeof blender_buffer, props.get_string("/sim/settings/blender", "blender"));
                ImGui::SetNextItemWidth(260.0f);
                if (ImGui::InputText("Blender", blender_buffer, sizeof blender_buffer))
                    props.set_string("/sim/settings/blender", blender_buffer);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("The Blender executable. Saved with the settings; --blender PATH overrides it.");
                // Stays after Blender closes, so the converted mesh can still be written into the ship file.
                part_converted_mesh(fspec);
                if (edit_paused)
                {
                    part_paused_controls();
                }
                else
                {
                    part_live_controls(flown, fspec);
                }
                if (!part_status.empty())
                    ImGui::TextWrapped("%s", part_status.c_str());
            }
            ImGui::End();
        }
    }

    // Window menu hints follow the player's key bindings.
    void App::refresh_menu_keys()
    {
        static const std::pair<const char *, const char *> windows[] = {{"Cameras", "window_cameras"}, {"Settings", "window_settings"}, {"Ship systems", "window_systems"},
                                                                       {"Properties", "window_properties"}, {"Lua console", "window_lua"}, {"Multiplayer", "window_multiplayer"}};
        for (kosmos::MenuWindow &w : menu_windows)
            for (const auto &[label, action] : windows)
                if (w.label == label)
                    w.key = keys.label_for(action);
    }

    void App::draw_keys_window()
    {
        if (!keys_window)
            return;
        ImGui::SetNextWindowPos(ImVec2(380.0f, 50.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(560.0f, 520.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.94f);
        if (ImGui::Begin("Keyboard", &keys_window))
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
                std::string group;
                bool open = false;
                for (const kosmos::KeyAction &a : keys.actions())
                {
                    if (a.group != group)
                    {
                        group = a.group;
                        open = ImGui::CollapsingHeader(group.c_str(), group == "Ship controls" ? ImGuiTreeNodeFlags_DefaultOpen : 0);
                    }
                    if (!open)
                        continue;
                    ImGui::PushID(a.id.c_str());
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
        if (quantum_window)
        {
            // The quantum drive: pick a star within range and arrive 1 AU above its surface.
            double range_ly = 0.0;
            for (const kosmos::StageSpec &st : ship->spec().stages)
                for (const kosmos::EngineSpec &e : st.engines)
                    if (e.type == kosmos::EngineType::hyperjump)
                        range_ly = std::max(range_ly, e.range_ly);
            ImGui::SetNextWindowPos(ImVec2(330.0f, 120.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(420.0f, 400.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.9f);
            if (ImGui::Begin("Quantum navigation", &quantum_window))
            {
                ImGui::Text("Ship: %s", ship->spec().name.c_str());
                ImGui::Text("Position: %s", sim.galaxy.star(sim.system_index()).name.c_str());
                if (range_ly <= 0.0)
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "This ship has no quantum jump drive.");
                else
                {
                    ImGui::Text("Drive range: %.0f light years", range_ly);
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
                                if (ImGui::Selectable((st.name + "##" + std::to_string(id)).c_str(), quantum_selected == id, ImGuiSelectableFlags_SpanAllColumns))
                                {
                                    quantum_selected = id;
                                    quantum_body.clear();
                                }
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
                    const bool ok = quantum_selected >= 0 && quantum_selected != sim.system_index() && sim.galaxy.distance_ly(sim.system_index(), quantum_selected) <= range_ly;
                    if (quantum_selected >= 0)
                    {
                        const kosmos::Star &st = sim.galaxy.star(quantum_selected);
                        ImGui::Text("%s: %.1f solar masses, %.2f solar radii, %.0f K", st.name.c_str(), st.mass, st.radius, st.temperature);
                    }
                    else
                        ImGui::TextDisabled("Select a target star.");
                    if (quantum_selected >= 0 && quantum_bodies_for != quantum_selected)
                    {
                        // The planets and moons of the chosen system, read from its description without building it.
                        quantum_bodies.clear();
                        quantum_bodies_for = quantum_selected;
                        std::string text = quantum_selected == 0 ? std::string{} : sim.galaxy.system_json(quantum_selected);
                        if (quantum_selected == 0)
                        {
                            std::ifstream in(sim.root() + "/data/sol.json");
                            text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                        }
                        const nlohmann::json system = nlohmann::json::parse(text, nullptr, false);
                        if (system.is_object() && system.contains("bodies"))
                            for (const nlohmann::json &b : system["bodies"])
                            {
                                const std::string kind = b.value("kind", "");
                                if (kind == "planet" || kind == "moon")
                                    quantum_bodies.emplace_back(b.value("name", ""), (kind == "moon" ? "moon of " : "planet of ") + b.value("parent", ""));
                            }
                    }
                    if (quantum_selected >= 0)
                    {
                        const std::string label = quantum_body.empty() ? "Best world (habitable first)" : quantum_body;
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::BeginCombo("Arrive at", label.c_str()))
                        {
                            if (ImGui::Selectable("Best world (habitable first)", quantum_body.empty()))
                                quantum_body.clear();
                            for (const auto &entry : quantum_bodies)
                                if (ImGui::Selectable((entry.first + "  (" + entry.second + ")").c_str(), quantum_body == entry.first))
                                    quantum_body = entry.first;
                            ImGui::EndCombo();
                        }
                    }
                    if (!ok)
                        ImGui::BeginDisabled();
                    if (ImGui::Button("Engage quantum jump"))
                    {
                        jump_to = quantum_selected;
                        jump_to_star_orbit = true;
                        jump_to_body = quantum_body;
                    }
                    if (!ok)
                        ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::TextDisabled("Arrives in orbit of the chosen world.");
                }
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
        user_cams.push_back(c);
        camera_editor_pick = count;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add mounted"))
    {
        kosmos::UserCamera c;
        c.name = "Mounted " + std::to_string(count + 1);
        c.orbit = false;
        c.y = 2.0;
        user_cams.push_back(c);
        camera_editor_pick = count;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy") && count > 0)
    {
        kosmos::UserCamera c = user_cams[static_cast<std::size_t>(camera_editor_pick)];
        c.name += " copy";
        user_cams.push_back(c);
        camera_editor_pick = count;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && count > 0)
    {
        user_cams.erase(user_cams.begin() + camera_editor_pick);
        if (user_cam == camera_editor_pick)
            user_cam = -1;
        else if (user_cam > camera_editor_pick)
            --user_cam;
        changed = true;
    }
    }

    void App::camera_editor_list()
    {
    if (ImGui::BeginListBox("##cams", ImVec2(-FLT_MIN, 100.0f)))
    {
        for (int ui = 0; ui < static_cast<int>(user_cams.size()); ++ui)
        {
            ImGui::PushID(ui);
            if (ImGui::Selectable((user_cams[static_cast<std::size_t>(ui)].name + (user_cam == ui ? "  (viewing)" : "")).c_str(), camera_editor_pick == ui))
                camera_editor_pick = ui;
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
    }

    void App::camera_editor_fields(bool &changed)
    {
        kosmos::Universe &universe = *universe_ref;
        const int ui = camera_editor_pick;
        kosmos::UserCamera &uc = user_cams[static_cast<std::size_t>(ui)];
        if (ImGui::Button(user_cam == ui ? "Back to the ship view" : "Look through"))
        {
            user_cam = user_cam == ui ? -1 : ui;
            cockpit_view = cockpit3d = false;
            interior_cam = base_view = -1;
            map_mode = galaxy_mode = false;
            look_yaw = look_pitch = 0.0f;
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
        kosmos::Universe &universe = *universe_ref;
        if (camera_editor_window)
        {
            ImGui::SetNextWindowPos(ImVec2(330.0f, 120.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(380.0f, 520.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.9f);
            if (ImGui::Begin("Camera editor", &camera_editor_window))
            {
                bool changed = false;
                const int count = static_cast<int>(user_cams.size());
                camera_editor_pick = std::clamp(camera_editor_pick, 0, std::max(0, count - 1));
                camera_editor_buttons(changed, count);
                camera_editor_list();
                if (!user_cams.empty())
                    camera_editor_fields(changed);
                ImGui::Separator();
                ImGui::TextDisabled("C steps through these after the ship's own views.");
                if (changed)
                    kosmos::save_user_cameras(user_cam_file, user_cams);
            }
            ImGui::End();
        }
    }

    void App::draw_camera_window()
    {
        sl::PropertyTree &props = *props_ref;
        if (camera_window)
        {
            ImGui::SetNextWindowPos(ImVec2(12.0f, 120.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(300.0f, 380.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.85f);
            if (ImGui::Begin("Cameras [F2]", &camera_window))
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
                        cam_distance = ship->spec().camera_distance;
                    }
                    cockpit_view = cockpit;
                    cockpit3d = three_d;
                    interior_cam = interior;
                    user_cam = -1;
                    base_view = base_index;
                    base_view_cam = base_camera;
                    look_yaw = look_pitch = 0.0f;
                    map_mode = galaxy_mode = false;
                };
                const bool chase_now = !cockpit_view && interior_cam < 0 && base_view < 0 && user_cam < 0;
                for (int si = 0; si < sim.ship_count(); ++si)
                {
                    const Ship &other = *sim.ship(si);
                    const kosmos::ShipSpec &spec = other.spec();
                    const bool mine = si == active;
                    ImGui::PushID(si);
                    if (ImGui::TreeNodeEx((other.name + (mine ? "  (flying)" : "")).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        if (ImGui::Selectable("External", mine && chase_now))
                            pick(si, false, false, -1, -1, 0);
                        if (spec.cockpit.present && ImGui::Selectable(spec.building() ? "Interface (2D panel)" : spec.cockpit.mfds || !spec.cockpit.panel.empty() ? "Cockpit (2D panel)" : "Cockpit", mine && cockpit_view && !cockpit3d && interior_cam < 0 && base_view < 0))
                            pick(si, true, false, -1, -1, 0);
                        if (spec.cockpit.present && spec.cockpit.mfds && ImGui::Selectable(spec.building() ? "Interface (3D)" : "Cockpit (3D, MFDs)", mine && cockpit_view && cockpit3d && interior_cam < 0 && base_view < 0))
                            pick(si, true, true, -1, -1, 0);
                        for (std::size_t ci = 0; ci < spec.cameras.size(); ++ci)
                            if (ImGui::Selectable(spec.cameras[ci].name.c_str(), mine && interior_cam == static_cast<int>(ci) && base_view < 0))
                                pick(si, false, false, static_cast<int>(ci), -1, 0);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                if (!user_cams.empty() && ImGui::TreeNodeEx("My cameras", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    for (std::size_t ui = 0; ui < user_cams.size(); ++ui)
                    {
                        ImGui::PushID(2000 + static_cast<int>(ui));
                        if (ImGui::Selectable(user_cams[ui].name.c_str(), user_cam == static_cast<int>(ui)))
                        {
                            pick(-1, false, false, -1, -1, 0);
                            user_cam = static_cast<int>(ui);
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                for (std::size_t bi = 0; bi < sim.bases.size(); ++bi)
                {
                    const kosmos::Base &bs = sim.bases[bi];
                    if (bs.cameras.empty())
                        continue;
                    ImGui::PushID(1000 + static_cast<int>(bi));
                    if (ImGui::TreeNodeEx((bs.name + " base").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        for (std::size_t ci = 0; ci < bs.cameras.size(); ++ci)
                            if (ImGui::Selectable(bs.cameras[ci].name.c_str(), base_view == static_cast<int>(bi) && base_view_cam == static_cast<int>(ci)))
                                pick(-1, false, false, -1, static_cast<int>(bi), static_cast<int>(ci));
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::Separator();
                ImGui::TextDisabled("C cycles the flown ship's views and your cameras. Right-drag looks around.");
                if (ImGui::Button("Camera editor..."))
                    camera_editor_window = true;
            }
            ImGui::End();
        }
    }
} // namespace kosmos::app
