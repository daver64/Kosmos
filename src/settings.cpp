#include "settings.h"

#include "json.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace kosmos
{

    std::string settings_path()
    {
        const char *xdg = std::getenv("XDG_CONFIG_HOME");
        const char *home = std::getenv("HOME");
        const char *appdata = std::getenv("APPDATA");
        std::filesystem::path dir = xdg && *xdg ? std::filesystem::path(xdg) : home && *home ? std::filesystem::path(home) / ".config" : appdata ? std::filesystem::path(appdata) : std::filesystem::path();
        return dir.empty() ? std::string() : (dir / "kosmos" / "settings.json").string();
    }

    // --set path=value: numbers and true/false get their own types, anything else is a string.
    void apply_sets(sl::PropertyTree &props, const std::vector<std::string> &sets)
    {
        for (const std::string &assignment : sets)
        {
            const std::size_t eq = assignment.find('=');
            if (eq == std::string::npos)
                continue;
            const std::string path = assignment.substr(0, eq), value = assignment.substr(eq + 1);
            char *end = nullptr;
            const double number = std::strtod(value.c_str(), &end);
            if (value == "true" || value == "false")
                props.set_bool(path, value == "true");
            else if (end && *end == 0 && !value.empty())
                props.set_double(path, number);
            else
                props.set_string(path, value);
        }
    }

    // Settings live in the property tree under /sim/settings so scripts and the F4 browser see them too.
    void settings_defaults(sl::PropertyTree &props)
    {
        props.set_bool("/sim/settings/labels", false);
        props.set_bool("/sim/settings/ship-labels", false);
        props.set_bool("/sim/settings/help-text", true);
        props.set_bool("/sim/settings/tooltips", true);
        props.set_bool("/sim/settings/clock", true);
        props.set_bool("/sim/settings/orbit-line", true);
        props.set_bool("/sim/settings/sound", true);
        props.set_int("/sim/settings/volume", 20);
        props.set_string("/sim/settings/blender", "blender");
        props.set_int("/sim/settings/world-texture", 0);
        props.set_int("/sim/settings/gfx-style", 0);
        props.set_bool("/sim/settings/gfx-clouds", true);
        props.set_int("/sim/settings/gfx-cloud-style", 0);
        props.set_bool("/sim/settings/link-listen", false);
        props.set_bool("/sim/settings/mouse-invert-x", false);
        props.set_bool("/sim/settings/mouse-invert-y", false);
        props.set_bool("/sim/settings/weather", true);
        props.set_bool("/sim/settings/weather-rain", true);
        props.set_string("/sim/settings/weather-source", "procedural");
        props.set_string("/sim/settings/weather-preset", "auto");
        props.set_string("/sim/settings/weather-metar", "");
        props.set_int("/sim/settings/gfx-light-radius", 8000);
        props.set_int("/sim/settings/gfx-weapon-lights", 4);
        props.set_int("/sim/settings/gfx-max-fps", 60);
        props.set_int("/sim/settings/gfx-msaa", 4);
        props.set_int("/sim/settings/gfx-screen-ss", 2);
        props.set_int("/sim/settings/gfx-terrain-detail", 1);
        props.set_bool("/sim/settings/gfx-earth-relief", false);
        props.set_bool("/sim/settings/gfx-clutter", true);
        props.set_int("/sim/settings/gfx-clutter-density", 100);
        props.set_int("/sim/settings/gfx-clutter-radius", 100);
        props.set_int("/sim/settings/max-texture", 0);
        props.set_string("/sim/settings/camera-mode", "dynamic");
        props.set_int("/sim/settings/camera-strength", 100);
    }

    void settings_load(sl::PropertyTree &props)
    {
        const std::string path = settings_path();
        std::ifstream in(path);
        if (path.empty() || !in)
            return;
        nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
        if (!j.is_object())
            return;
        for (const char *key : {"labels", "ship-labels", "help-text", "tooltips", "orbit-line", "sound", "clock"})
            if (j.contains(key) && j[key].is_boolean())
                props.set_bool(std::string("/sim/settings/") + key, j[key].get<bool>());
        if (j.contains("volume") && j["volume"].is_number())
            props.set_int("/sim/settings/volume", std::clamp(j["volume"].get<int>(), 0, 100));
        if (j.contains("world-texture") && j["world-texture"].is_number_integer())
            props.set_int("/sim/settings/world-texture", std::clamp(j["world-texture"].get<int>(), 0, 16384));
        const auto load_int = [&](const char *key, int lo, int hi) {
            if (j.contains(key) && j[key].is_number_integer())
                props.set_int(std::string("/sim/settings/") + key, std::clamp(j[key].get<int>(), lo, hi));
        };
        load_int("gfx-style", 0, 2);
        load_int("gfx-light-radius", 500, 20000);
        load_int("gfx-weapon-lights", 0, 8);
        load_int("gfx-max-fps", 10, 240);
        load_int("gfx-msaa", 1, 8);
        load_int("gfx-screen-ss", 1, 4);
        load_int("gfx-terrain-detail", 0, 3);
        load_int("gfx-clutter-density", 10, 200);
        load_int("gfx-clutter-radius", 25, 200);
        load_int("max-texture", 0, 16384);
        load_int("camera-strength", 0, 200);
        if (j.contains("camera-mode") && j["camera-mode"].is_string())
            props.set_string("/sim/settings/camera-mode", j["camera-mode"].get<std::string>() == "standard" ? "standard" : "dynamic");
        if (j.contains("gfx-clutter") && j["gfx-clutter"].is_boolean())
            props.set_bool("/sim/settings/gfx-clutter", j["gfx-clutter"].get<bool>());
        if (j.contains("gfx-earth-relief") && j["gfx-earth-relief"].is_boolean())
            props.set_bool("/sim/settings/gfx-earth-relief", j["gfx-earth-relief"].get<bool>());
        if (j.contains("weather") && j["weather"].is_boolean())
            props.set_bool("/sim/settings/weather", j["weather"].get<bool>());
        if (j.contains("weather-rain") && j["weather-rain"].is_boolean())
            props.set_bool("/sim/settings/weather-rain", j["weather-rain"].get<bool>());
        if (j.contains("weather-preset") && j["weather-preset"].is_string())
            props.set_string("/sim/settings/weather-preset", j["weather-preset"].get<std::string>());
        if (j.contains("weather-metar") && j["weather-metar"].is_string())
            props.set_string("/sim/settings/weather-metar", j["weather-metar"].get<std::string>());
        if (j.contains("weather-source") && j["weather-source"].is_string())
            props.set_string("/sim/settings/weather-source", j["weather-source"].get<std::string>() == "live" ? "live" : "procedural");
        if (j.contains("gfx-cloud-style") && j["gfx-cloud-style"].is_number_integer())
            props.set_int("/sim/settings/gfx-cloud-style", std::clamp(j["gfx-cloud-style"].get<int>(), 0, 2));
        if (j.contains("gfx-clouds") && j["gfx-clouds"].is_boolean())
            props.set_bool("/sim/settings/gfx-clouds", j["gfx-clouds"].get<bool>());
        for (const char *name : {"mouse-invert-x", "mouse-invert-y"})
            if (j.contains(name) && j[name].is_boolean())
                props.set_bool(std::string("/sim/settings/") + name, j[name].get<bool>());
        if (j.contains("link-listen") && j["link-listen"].is_boolean())
            props.set_bool("/sim/settings/link-listen", j["link-listen"].get<bool>());
        if (j.contains("blender") && j["blender"].is_string() && !j["blender"].get<std::string>().empty())
            props.set_string("/sim/settings/blender", j["blender"].get<std::string>());
    }

    void settings_save(sl::PropertyTree &props)
    {
        const std::string path = settings_path();
        if (path.empty())
            return;
        nlohmann::json j;
        for (const char *key : {"labels", "ship-labels", "help-text", "tooltips", "orbit-line", "sound", "clock"})
            j[key] = props.get_bool(std::string("/sim/settings/") + key);
        j["volume"] = props.get_int("/sim/settings/volume");
        j["blender"] = props.get_string("/sim/settings/blender", "blender");
        j["world-texture"] = props.get_int("/sim/settings/world-texture");
        j["max-texture"] = props.get_int("/sim/settings/max-texture");
        for (const char *key : {"gfx-style", "gfx-light-radius", "gfx-weapon-lights", "gfx-max-fps", "gfx-msaa", "gfx-terrain-detail", "gfx-cloud-style"})
            j[key] = props.get_int(std::string("/sim/settings/") + key);
        j["gfx-clouds"] = props.get_bool("/sim/settings/gfx-clouds");
        j["mouse-invert-x"] = props.get_bool("/sim/settings/mouse-invert-x");
        j["mouse-invert-y"] = props.get_bool("/sim/settings/mouse-invert-y");
        j["link-listen"] = props.get_bool("/sim/settings/link-listen");
        j["weather"] = props.get_bool("/sim/settings/weather", true);
        j["weather-preset"] = props.get_string("/sim/settings/weather-preset", "auto");
        j["weather-metar"] = props.get_string("/sim/settings/weather-metar", "");
        j["weather-rain"] = props.get_bool("/sim/settings/weather-rain", true);
        j["weather-source"] = props.get_string("/sim/settings/weather-source", "procedural");
        j["gfx-earth-relief"] = props.get_bool("/sim/settings/gfx-earth-relief");
        j["gfx-clutter"] = props.get_bool("/sim/settings/gfx-clutter", true);
        j["gfx-clutter-density"] = props.get_int("/sim/settings/gfx-clutter-density", 100);
        j["gfx-clutter-radius"] = props.get_int("/sim/settings/gfx-clutter-radius", 100);
        j["camera-mode"] = props.get_string("/sim/settings/camera-mode", "dynamic");
        j["camera-strength"] = props.get_int("/sim/settings/camera-strength", 100);
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        std::ofstream out(path);
        if (out)
            out << j.dump(1) << "\n";
    }

    namespace
    {

        // One editable row for a node that has a value.
        void property_value_widget(sl::PropertyNode &node, PropertyBrowser &state)
        {
            ImGui::PushID(&node);
            ImGui::SetNextItemWidth(160.0f);
            switch (node.type())
            {
            case sl::PropertyNode::Type::boolean:
            {
                bool v = node.get_bool();
                if (ImGui::Checkbox("##v", &v))
                    node.set_bool(v);
                break;
            }
            case sl::PropertyNode::Type::integer:
            {
                int v = node.get_int();
                if (ImGui::InputInt("##v", &v, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue))
                    node.set_int(v);
                break;
            }
            case sl::PropertyNode::Type::real:
            {
                double v = node.get_double();
                if (ImGui::InputDouble("##v", &v, 0.0, 0.0, "%.6g", ImGuiInputTextFlags_EnterReturnsTrue))
                    node.set_double(v);
                break;
            }
            default:
            {
                std::string &text = state.edit[&node];
                if (!ImGui::IsItemActive() && !ImGui::IsAnyItemActive())
                    text = node.get_string();
                char buffer[256];
                std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());
                if (ImGui::InputText("##v", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
                    node.set_string(buffer);
                else
                    text = buffer;
                break;
            }
            }
            ImGui::PopID();
        }

        void property_tree_rows(sl::PropertyNode &node, PropertyBrowser &state)
        {
            for (sl::PropertyNode *child : node.children())
            {
                std::string label = child->name();
                if (child->index() > 0 || node.child_count(child->name()) > 1)
                    label += "[" + std::to_string(child->index()) + "]";
                ImGui::PushID(child);
                if (child->has_children())
                {
                    const bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
                    if (child->has_value())
                    {
                        ImGui::SameLine();
                        property_value_widget(*child, state);
                    }
                    if (open)
                    {
                        property_tree_rows(*child, state);
                        ImGui::TreePop();
                    }
                }
                else
                {
                    ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
                    if (ImGui::IsItemClicked())
                    {
                        std::snprintf(state.path, sizeof(state.path), "%s", child->path().c_str());
                        std::snprintf(state.value, sizeof(state.value), "%s", child->get_string().c_str());
                    }
                    ImGui::SameLine(190.0f);
                    if (child->has_value())
                        property_value_widget(*child, state);
                    else
                        ImGui::TextDisabled("(empty)");
                }
                ImGui::PopID();
            }
        }

        // Flat list of the leaves whose path contains the filter text.
        void property_search(sl::PropertyNode &node, const std::string &needle, PropertyBrowser &state, int &shown)
        {
            for (sl::PropertyNode *child : node.children())
            {
                if (shown >= 200)
                    return;
                if (child->has_value())
                {
                    const std::string path = child->path();
                    std::string lower = path;
                    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (lower.find(needle) != std::string::npos)
                    {
                        ++shown;
                        ImGui::Text("%s", path.c_str());
                        ImGui::SameLine(300.0f);
                        property_value_widget(*child, state);
                    }
                }
                property_search(*child, needle, state, shown);
            }
        }

    } // namespace

    void draw_property_browser(sl::PropertyTree &props, bool &open, PropertyBrowser &state)
    {
        ImGui::SetNextWindowPos(ImVec2(560.0f, 330.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(520.0f, 460.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.9f);
        if (ImGui::Begin("Property tree [F4]", &open))
        {
            ImGui::SetNextItemWidth(-60.0f);
            ImGui::InputTextWithHint("##filter", "filter paths, e.g. controls or altitude", state.filter, sizeof(state.filter));
            ImGui::SameLine();
            if (ImGui::Button("Clear"))
                state.filter[0] = '\0';
            ImGui::BeginChild("tree", ImVec2(0.0f, -64.0f), true);
            if (state.filter[0])
            {
                std::string needle = state.filter;
                std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                int shown = 0;
                property_search(props.root(), needle, state, shown);
                if (shown >= 200)
                    ImGui::TextDisabled("(first 200 matches)");
            }
            else
                property_tree_rows(props.root(), state);
            ImGui::EndChild();
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("path", state.path, sizeof(state.path));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            const bool enter = ImGui::InputText("value", state.value, sizeof(state.value), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("Get"))
            {
                if (const sl::PropertyNode *n = props.find(state.path))
                {
                    std::snprintf(state.value, sizeof(state.value), "%s", n->get_string().c_str());
                    state.status.clear();
                }
                else
                    state.status = "no such property";
            }
            ImGui::SameLine();
            if (ImGui::Button("Set") || enter)
            {
                if (state.path[0] == '/')
                {
                    props.node(state.path)->set_from_string(state.value);
                    state.status = "set " + std::string(state.path);
                }
                else
                    state.status = "path must start with /";
            }
            ImGui::TextDisabled("%s", state.status.c_str());
        }
        ImGui::End();
    }
} // namespace kosmos
