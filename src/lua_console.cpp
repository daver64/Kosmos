#include "lua_console.h"

#include <imgui.h>
#include <json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>

namespace kosmos
{
    namespace
    {
        namespace fs = std::filesystem;

        const char *const trigger_labels[] = {"Manual (Run button)", "On scenario load / ship change", "Every frame", "Timer", "When a property changes"};

        std::string read_file(const fs::path &path)
        {
            std::ifstream in(path);
            return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }
    }

    std::string LuaConsole::trigger_name(Trigger trigger) const
    {
        static const char *const names[] = {"manual", "load", "frame", "timer", "property"};
        return names[static_cast<int>(trigger)];
    }

    void LuaConsole::print(const std::string &line)
    {
        std::istringstream lines(line);
        std::string one;
        while (std::getline(lines, one))
            output_.push_back(one);
        if (output_.size() > 500)
            output_.erase(output_.begin(), output_.begin() + (output_.size() - 500));
        scroll_output_ = true;
    }

    void LuaConsole::init(Sim &sim, const std::string &shipped_dir, const std::string &user_dir)
    {
        sim_ = &sim;
        user_dir_ = user_dir;
        sim.log_sink = [this](const std::string &text) { print(text); };
        editor_.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
        editor_.SetPalette(TextEditor::GetDarkPalette());
        editor_.SetShowWhitespaces(false);
        scan(shipped_dir, false);
        if (!user_dir.empty())
            scan(user_dir, true);
        if (!scripts_.empty())
            select(0);
    }

    void LuaConsole::scan(const std::string &dir, bool user)
    {
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec))
            return;
        std::vector<fs::path> files;
        for (const auto &entry : fs::directory_iterator(dir, ec))
            if (entry.path().extension() == ".lua")
                files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        for (const fs::path &file : files)
        {
            Script script;
            script.name = file.stem().string();
            script.path = file.string();
            script.source = read_file(file);
            script.user = user;
            fs::path meta = file;
            meta.replace_extension(".json");
            if (fs::exists(meta, ec))
            {
                try
                {
                    const nlohmann::json j = nlohmann::json::parse(read_file(meta));
                    const std::string trigger = j.value("trigger", "manual");
                    for (int i = 0; i < 5; ++i)
                        if (trigger_name(static_cast<Trigger>(i)) == trigger)
                            script.trigger = static_cast<Trigger>(i);
                    script.interval = j.value("interval", 1.0f);
                    script.property = j.value("property", "");
                    script.armed = user && j.value("armed", false);
                }
                catch (const std::exception &)
                {
                }
            }
            // A user copy replaces a shipped script of the same name.
            auto same = std::find_if(scripts_.begin(), scripts_.end(), [&](const Script &s) { return s.name == script.name; });
            if (same != scripts_.end())
                *same = script;
            else
                scripts_.push_back(std::move(script));
        }
    }

    void LuaConsole::select(int index)
    {
        if (index < 0 || index >= static_cast<int>(scripts_.size()))
            return;
        selected_ = index;
        editor_.SetText(scripts_[static_cast<std::size_t>(index)].source);
        editor_.SetErrorMarkers({});
    }

    bool LuaConsole::save(Script &script)
    {
        if (user_dir_.empty())
        {
            status_ = "No user folder to save to";
            return false;
        }
        std::error_code ec;
        fs::create_directories(user_dir_, ec);
        const fs::path file = fs::path(user_dir_) / (script.name + ".lua");
        std::ofstream out(file);
        out << script.source;
        nlohmann::json j;
        j["trigger"] = trigger_name(script.trigger);
        j["interval"] = script.interval;
        j["property"] = script.property;
        j["armed"] = script.armed;
        fs::path meta = file;
        meta.replace_extension(".json");
        std::ofstream meta_out(meta);
        meta_out << j.dump(2) << "\n";
        if (!out || !meta_out)
        {
            status_ = "Could not write " + file.string();
            return false;
        }
        script.path = file.string();
        script.user = true;
        script.dirty = false;
        status_ = "Saved " + file.string();
        return true;
    }

    void LuaConsole::run(Script &script, const char *event, double dt, const std::string &path, const std::string &value)
    {
        sol::state &lua = sim_->lua.state();
        lua["event"] = std::string(event);
        lua["dt"] = dt;
        lua["event_path"] = path;
        lua["event_value"] = value;
        const sl::LuaScriptResult result = sim_->lua.execute(script.source);
        if (result.success)
            return;
        print("[" + script.name + "] error: " + result.error);
        script.armed = false;
        static const std::regex line_re(R"(:(\d+):)");
        std::smatch match;
        if (selected_ >= 0 && &scripts_[static_cast<std::size_t>(selected_)] == &script && std::regex_search(result.error, match, line_re))
            editor_.SetErrorMarkers({{std::stoi(match[1]), result.error}});
    }

    void LuaConsole::tick(double dt)
    {
        if (!sim_)
            return;
        const void *ship = sim_->active_ship();
        const bool ship_changed = ship != last_ship_;
        last_ship_ = ship;
        for (Script &script : scripts_)
        {
            if (!script.armed)
                continue;
            switch (script.trigger)
            {
            case Trigger::manual:
                break;
            case Trigger::load:
                if (ship_changed)
                    run(script, "load", dt);
                break;
            case Trigger::frame:
                run(script, "frame", dt);
                break;
            case Trigger::timer:
                script.accumulator += dt;
                if (script.accumulator >= std::max(0.05f, script.interval))
                {
                    const double elapsed = script.accumulator;
                    script.accumulator = 0.0;
                    run(script, "timer", elapsed);
                }
                break;
            case Trigger::property:
            {
                const sl::PropertyNode *node = sim_->props.find(script.property);
                if (!node || !node->has_value())
                    break;
                std::string value;
                switch (node->type())
                {
                case sl::PropertyNode::Type::boolean: value = node->get_bool() ? "true" : "false"; break;
                case sl::PropertyNode::Type::integer: value = std::to_string(node->get_int()); break;
                case sl::PropertyNode::Type::real: value = std::to_string(node->get_double()); break;
                default: value = node->get_string(); break;
                }
                if (script.has_last && value != script.last_value)
                    run(script, "property", dt, script.property, value);
                script.last_value = value;
                script.has_last = true;
                break;
            }
            }
        }
    }

    void LuaConsole::draw(bool &open)
    {
        if (!sim_)
            return;
        ImGui::SetNextWindowPos(ImVec2(160.0f, 60.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(820.0f, 560.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.94f);
        if (!ImGui::Begin("Lua console [F11]", &open))
        {
            ImGui::End();
            return;
        }

        ImGui::BeginChild("scripts", ImVec2(170.0f, 0.0f), true);
        for (int i = 0; i < static_cast<int>(scripts_.size()); ++i)
        {
            Script &script = scripts_[static_cast<std::size_t>(i)];
            std::string label = script.name + (script.dirty ? " *" : "") + (script.armed ? "  [on]" : "");
            if (ImGui::Selectable(label.c_str(), i == selected_))
                select(i);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", script.user ? "saved in your user folder" : "shipped example (Save makes a user copy)");
        }
        ImGui::Separator();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##new", "new script name", new_name_, sizeof(new_name_));
        if (ImGui::Button("New", ImVec2(-1.0f, 0.0f)) && new_name_[0])
        {
            std::string name;
            for (const char *p = new_name_; *p; ++p)
                name += (std::isalnum(static_cast<unsigned char>(*p)) || *p == '_' || *p == '-') ? *p : '_';
            Script script;
            script.name = name;
            script.source = "-- " + name + "\nprint(\"hello from " + name + "\")\n";
            script.dirty = true;
            script.user = true;
            scripts_.push_back(script);
            select(static_cast<int>(scripts_.size()) - 1);
            new_name_[0] = '\0';
        }
        ImGui::EndChild();
        ImGui::SameLine();

        ImGui::BeginGroup();
        if (selected_ >= 0)
        {
            Script &script = scripts_[static_cast<std::size_t>(selected_)];
            int trigger = static_cast<int>(script.trigger);
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::Combo("Run on", &trigger, trigger_labels, 5))
            {
                script.trigger = static_cast<Trigger>(trigger);
                script.has_last = false;
                script.dirty = true;
            }
            if (script.trigger == Trigger::timer)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::InputFloat("seconds", &script.interval, 0.0f, 0.0f, "%.2f"))
                    script.dirty = true;
            }
            else if (script.trigger == Trigger::property)
            {
                ImGui::SameLine();
                char path[160];
                std::snprintf(path, sizeof(path), "%s", script.property.c_str());
                ImGui::SetNextItemWidth(260.0f);
                if (ImGui::InputTextWithHint("##prop", "/controls/throttle", path, sizeof(path)))
                {
                    script.property = path;
                    script.has_last = false;
                    script.dirty = true;
                }
            }
            if (script.trigger != Trigger::manual)
            {
                ImGui::SameLine();
                if (ImGui::Checkbox("Armed", &script.armed))
                {
                    script.has_last = false;
                    script.accumulator = 0.0;
                    script.dirty = true;
                }
            }

            if (ImGui::Button("Run (Ctrl+Enter)"))
                run(script, "manual", 0.0);
            ImGui::SameLine();
            if (ImGui::Button("Save (Ctrl+S)"))
                save(script);
            ImGui::SameLine();
            if (ImGui::Button("Revert"))
            {
                script.source = script.path.empty() ? std::string() : read_file(script.path);
                script.dirty = false;
                editor_.SetText(script.source);
                editor_.SetErrorMarkers({});
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear output"))
                output_.clear();
            if (!status_.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", status_.c_str());
            }

            const float output_height = 130.0f;
            editor_.Render("##editor", ImVec2(0.0f, -output_height - ImGui::GetStyle().ItemSpacing.y), true);
            if (editor_.IsTextChanged())
            {
                script.source = editor_.GetText();
                script.dirty = true;
            }
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            {
                const ImGuiIO &io = ImGui::GetIO();
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter))
                    run(script, "manual", 0.0);
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
                    save(script);
            }
        }
        ImGui::BeginChild("output", ImVec2(0.0f, 0.0f), true);
        for (const std::string &line : output_)
            ImGui::TextUnformatted(line.c_str());
        if (scroll_output_)
            ImGui::SetScrollHereY(1.0f);
        scroll_output_ = false;
        ImGui::EndChild();
        ImGui::EndGroup();
        ImGui::End();
    }
}
