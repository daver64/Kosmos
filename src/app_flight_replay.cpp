#include "app_state.h"

#include <imgui.h>

namespace kosmos::app
{
    void App::start_flight_recording(const std::string &path)
    {
        if (flight_replay.playing || flight_replay.capturing || flight_replay.unsaved ||
            netplay.mode() != NetPlay::Mode::none || eva.active)
        {
            sim.log("Flight recording requires a local flight and no unsaved recording, replay or EVA");
            return;
        }
        std::error_code ec;
        if (std::filesystem::exists(path, ec) || ec)
        {
            sim.log("Flight recording: choose a new file name");
            return;
        }
        flight_replay.recording.frames.clear();
        flight_replay.estimated_bytes = 0;
        flight_replay.path = path;
        flight_replay.start = sim.universe.time();
        flight_replay.system = sim.system_index();
        flight_replay.capturing = true;
        update_flight_recording();
        if (flight_replay.capturing)
            sim.log("Flight recording started");
    }

    void App::update_flight_recording()
    {
        if (!flight_replay.capturing)
            return;
        const double elapsed = sim.universe.time() - flight_replay.start;
        if (sim.system_index() != flight_replay.system || netplay.mode() != NetPlay::Mode::none || eva.active ||
            elapsed < 0.0 || (!flight_replay.recording.frames.empty() &&
                              elapsed < flight_replay.recording.frames.back().elapsed))
        {
            sim.log("Flight recording stopped: flight, system or recording clock changed");
            stop_flight_recording();
            return;
        }
        if (!flight_replay.recording.frames.empty() &&
            elapsed - flight_replay.recording.frames.back().elapsed < 1.0)
            return;
        FlightRecordingFrame sample;
        sample.elapsed = elapsed;
        sample.scenario = capture_scenario(sim, "Flight recording");
        sample.throttle = std::clamp(sim.props.get_double("/controls/throttle"), -1.0, 1.0);
        sample.hover = std::clamp(sim.props.get_double("/controls/hover"), 0.0, 1.0);
        for (std::size_t i = 0; i < flight_recorded_axes.size(); ++i)
            sample.axes[i] = std::clamp(sim.props.get_double(std::string("/controls/") + flight_recorded_axes[i]), -1.0, 1.0);
        sample.sas = sim.props.get_string("/controls/sas", "stability");
        sample.camera_yaw = rig.cam_yaw;
        sample.camera_pitch = rig.cam_pitch;
        sample.camera_distance = rig.cam_distance;
        sample.cockpit = cockpit_view;
        sample.cockpit3d = cockpit3d;
        const std::size_t sample_bytes = scenario_to_json(sample.scenario).size() + 1024;
        if (flight_replay.estimated_bytes + sample_bytes > 100 * 1024 * 1024)
        {
            sim.log("Flight recording stopped: size limit reached");
            if (flight_replay.recording.frames.empty())
                flight_replay.capturing = false;
            else
                stop_flight_recording();
            return;
        }
        flight_replay.estimated_bytes += sample_bytes;
        flight_replay.recording.frames.push_back(std::move(sample));
        if (flight_replay.recording.frames.size() >= 3600)
        {
            sim.log("Flight recording stopped: frame limit reached");
            stop_flight_recording();
        }
    }

    void App::stop_flight_recording()
    {
        if (!flight_replay.capturing && !flight_replay.unsaved)
            return;
        flight_replay.capturing = false;
        std::string error;
        if (!save_flight_recording(flight_replay.path, flight_replay.recording, error))
        {
            flight_replay.unsaved = true;
            flight_replay.status = error;
            sim.log(error);
            return;
        }
        flight_replay.unsaved = false;
        sim.log("Flight recording saved: " + flight_replay.path);
        flight_replay.recording.frames.clear();
    }

    void App::open_flight_recording(const std::string &path)
    {
        if (flight_replay.capturing || flight_replay.unsaved || netplay.mode() != NetPlay::Mode::none || eva.active)
        {
            sim.log("Save or stop flight recording and leave multiplayer or EVA before playback");
            return;
        }
        FlightRecording loaded;
        std::string error;
        if (!load_flight_recording(path, loaded, error))
        {
            sim.log(error);
            return;
        }
        const std::string system = sim.system_index() == 0 ? "" : sim.galaxy.star(sim.system_index()).name;
        for (const FlightRecordingFrame &sample : loaded.frames)
        {
            if (sample.scenario.system != system)
            {
                sim.log("Flight playback requires the recording's current star system");
                return;
            }
            for (const ScenarioShip &entry : sample.scenario.ships)
            {
                const ShipSpec *spec = sim.find_ship_spec(entry.ship);
                if (!spec || sim.universe.find(entry.body) < 0 || !entry.has_state || !entry.carry_state)
                {
                    sim.log("Flight playback requires the recorded ships and bodies");
                    return;
                }
                const ShipCarryState &state = *entry.carry_state;
                if (state.active_stage < 0 || state.active_stage >= static_cast<int>(spec->stages.size()) ||
                    state.stage_propellant.size() != spec->stages.size() ||
                    state.animation_positions.size() != spec->animation_controls.size() ||
                    state.has_systems != !spec->systems.empty() ||
                    (state.has_systems &&
                     (state.systems.components.size() != spec->systems.components.size() ||
                      state.systems.vessels.size() != spec->systems.vessels.size())))
                {
                    sim.log("Flight playback rejected: recorded ship state does not match installed addon");
                    return;
                }
            }
        }
        if (!flight_replay.playing)
        {
            flight_replay.original = capture_scenario(sim, "Before flight playback");
            flight_replay.original_valid = true;
        }
        flight_replay.recording = std::move(loaded);
        flight_replay.playing = true;
        flight_replay.paused = true;
        flight_replay.playback_time = 0.0;
        show_flight_frame(0);
        if (flight_replay.playing)
            sim.log("Flight playback loaded; simulation is paused");
    }

    void App::show_flight_frame(std::size_t index)
    {
        if (!flight_replay.playing || index >= flight_replay.recording.frames.size())
            return;
        const FlightRecordingFrame &sample = flight_replay.recording.frames[index];
        const bool was_running = running;
        flight_replay.applying = true;
        const bool restored = menu_start_scenario(sample.scenario);
        flight_replay.applying = false;
        if (!restored)
        {
            sim.log("Unable to restore flight recording frame; restoring previous flight");
            stop_flight_playback();
            if (was_running && !flight_replay.original_valid && ship)
                running = true;
            return;
        }
        sim.props.set_bool("/sim/paused", true);
        sim.props.set_double("/controls/throttle", sample.throttle);
        sim.props.set_double("/controls/hover", sample.hover);
        for (std::size_t i = 0; i < flight_recorded_axes.size(); ++i)
            sim.props.set_double(std::string("/controls/") + flight_recorded_axes[i], sample.axes[i]);
        sim.props.set_string("/controls/sas", sample.sas);
        sim.refresh_navigation();
        cockpit_view = sample.cockpit;
        cockpit3d = sample.cockpit3d;
        rig.cam_yaw = static_cast<float>(sample.camera_yaw);
        rig.cam_pitch = static_cast<float>(sample.camera_pitch);
        rig.cam_distance = sample.camera_distance;
        flight_replay.frame = index;
    }

    void App::update_flight_playback()
    {
        if (!flight_replay.playing || flight_replay.paused)
            return;
        flight_replay.playback_time += frame;
        const auto &frames = flight_replay.recording.frames;
        std::size_t next = flight_replay.frame;
        while (next + 1 < frames.size() && frames[next + 1].elapsed <= flight_replay.playback_time)
            ++next;
        if (next != flight_replay.frame)
            show_flight_frame(next);
        if (!flight_replay.playing)
            return;
        if (flight_replay.frame + 1 >= frames.size())
            flight_replay.paused = true;
    }

    void App::stop_flight_playback()
    {
        if (!flight_replay.playing)
            return;
        flight_replay.playing = false;
        if (flight_replay.original_valid)
        {
            flight_replay.applying = true;
            const bool restored = menu_start_scenario(flight_replay.original);
            flight_replay.applying = false;
            if (!restored)
            {
                sim.log("Unable to restore the previous flight after playback");
                running = false;
                return;
            }
            flight_replay.original_valid = false;
            flight_replay.original = {};
        }
        sim.props.set_bool("/sim/paused", true);
        sim.props.set_double("/controls/throttle", 0.0);
        sim.props.set_double("/controls/hover", 0.0);
        for (const char *axis : flight_recorded_axes)
            sim.props.set_double(std::string("/controls/") + axis, 0.0);
        flight_replay.recording.frames.clear();
        sim.refresh_navigation();
        sim.log("Flight playback stopped; previous flight restored and paused");
    }

    void App::draw_flight_playback()
    {
        if (!flight_replay.playing)
            return;
        ImGui::SetNextWindowPos(ImVec2(20, 100), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Flight playback"))
        {
            const auto &frames = flight_replay.recording.frames;
            ImGui::Text("Sample %zu / %zu   %.1f / %.1f s", flight_replay.frame + 1, frames.size(),
                        frames[flight_replay.frame].elapsed, frames.back().elapsed);
            ImGui::TextDisabled("One-second snapshots; not a physics resimulation.");
            if (ImGui::Button(flight_replay.paused ? "Play" : "Pause"))
            {
                if (flight_replay.paused && flight_replay.frame + 1 == frames.size())
                {
                    flight_replay.playback_time = frames.front().elapsed;
                    show_flight_frame(0);
                }
                flight_replay.paused = !flight_replay.paused;
            }
            ImGui::SameLine();
            if (ImGui::Button("Rewind"))
            {
                flight_replay.paused = true;
                flight_replay.playback_time = frames.front().elapsed;
                show_flight_frame(0);
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop"))
                stop_flight_playback();
        }
        ImGui::End();
    }

    int App::test_flight_playback(const std::string &path)
    {
        int failures = 0;
        const auto check = [&](bool ok, const char *what) {
            std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
            failures += ok ? 0 : 1;
        };
        const std::string missing_path = path + ".missing/flight.json";
        const std::string recovered_path = path + ".recovered";
        if (std::filesystem::exists(path + ".missing") || std::filesystem::exists(recovered_path))
        {
            std::fprintf(stderr, "flight playback selftest: retry paths already exist\n");
            return 1;
        }
        start_flight_recording(missing_path);
        stop_flight_recording();
        check(flight_replay.unsaved && !flight_replay.recording.frames.empty(), "failed save retains recorded frames");
        start_flight_recording(path);
        check(flight_replay.unsaved && !flight_replay.capturing, "unsaved recording cannot be replaced");
        flight_replay.path = recovered_path;
        stop_flight_recording();
        check(!flight_replay.unsaved && std::filesystem::exists(recovered_path), "failed save can be retried at a new path");
        std::error_code cleanup_error;
        if (!flight_replay.unsaved)
            std::filesystem::remove(recovered_path, cleanup_error);
        check(!cleanup_error, "recovered recording test file removed");
        rig.cam_distance = 42.0;
        sim.props.set_double("/controls/throttle", 0.0);
        const sl::dvec3 first_position = ship->body().position -
                                         sim.universe.position(ship->telemetry().soi);
        start_flight_recording(path);
        check(flight_replay.capturing && flight_replay.recording.frames.size() == 1, "recording starts with initial sample");
        for (int i = 0; i < 20; ++i)
            sim.step(0.1);
        rig.cam_distance = 73.0;
        sim.props.set_double("/controls/pitch", 0.3);
        update_flight_recording();
        const sl::dvec3 last_position = ship->body().position -
                                        sim.universe.position(ship->telemetry().soi);
        const double last_jd = sim.universe.julian_date();
        const double last_fuel = ship->telemetry().stage_fuel;
        const std::size_t links = sim.nav.dock_links().size();
        check(flight_replay.recording.frames.size() == 2, "recording captures advanced vessel");
        stop_flight_recording();
        check(!flight_replay.capturing && std::filesystem::exists(path), "recording is persisted on stop");
        FlightRecording existing;
        std::string error;
        const bool loaded = load_flight_recording(path, existing, error);
        if (loaded)
        {
            const double throttle = existing.frames.front().throttle;
            existing.frames.front().throttle = throttle == 0.0 ? 0.5 : 0.0;
            const bool overwritten = save_flight_recording(path, existing, error);
            FlightRecording unchanged;
            check(!overwritten && load_flight_recording(path, unchanged, error) &&
                      unchanged.frames.front().throttle == throttle,
                  "recording refuses to overwrite an existing file");
        }
        else
            check(false, "recording refuses to overwrite an existing file");
        open_flight_recording(path);
        check(flight_replay.playing && flight_replay.paused && flight_replay.frame == 0 &&
                  glm::length(ship->body().position - sim.universe.position(ship->telemetry().soi) - first_position) < 0.01,
              "playback restores initial recorded vessel state");
        if (flight_replay.playing)
        {
            flight_replay.paused = false;
            frame = 3.0;
            update_flight_playback();
            check(flight_replay.paused && flight_replay.frame == 1 && rig.cam_distance == 73.0 &&
                      std::abs(sim.props.get_double("/controls/pitch") - 0.3) < 1e-9 &&
                      glm::length(ship->body().position - sim.universe.position(ship->telemetry().soi) - last_position) < 0.01 &&
                      std::abs(sim.universe.julian_date() - last_jd) < 1e-9 &&
                      std::abs(ship->telemetry().stage_fuel - last_fuel) < 0.01 &&
                      sim.nav.dock_links().size() == links,
                  "playback advances vessel, clock, fuel, docks and camera");
            show_flight_frame(0);
            check(flight_replay.frame == 0 &&
                      glm::length(ship->body().position - sim.universe.position(ship->telemetry().soi) - first_position) < 0.01,
                  "playback rewinds to initial state");
            stop_flight_playback();
            check(!flight_replay.playing && sim.props.get_bool("/sim/paused") &&
                      glm::length(ship->body().position - sim.universe.position(ship->telemetry().soi) - last_position) < 0.01 &&
                      sim.props.get_double("/controls/throttle") == 0.0,
                  "stopping playback leaves simulation paused with throttle cut");
        }
        open_flight_recording(path);
        if (flight_replay.playing)
        {
            toggle_eva();
            check(flight_replay.playing && !eva.active, "EVA is blocked during playback");
            auto &invalid_ship = flight_replay.recording.frames.front().scenario.ships.front();
            invalid_ship.landed = false;
            invalid_ship.escape_to = "invalid replay destination";
            show_flight_frame(0);
            check(!flight_replay.playing && !flight_replay.original_valid && ship &&
                      sim.props.get_bool("/sim/paused") &&
                      glm::length(ship->body().position - sim.universe.position(ship->telemetry().soi) - last_position) < 0.01,
                  "failed playback frame restores the previous flight");
        }
        else
            check(false, "failed playback frame restores the previous flight");
        const Scenario next = capture_scenario(sim, "Playback switch test");
        open_flight_recording(path);
        const bool changed = flight_replay.playing && menu_start_scenario(next);
        check(changed && !flight_replay.playing && ship &&
                  glm::length(ship->body().position - sim.universe.position(ship->telemetry().soi) - last_position) < 0.01,
              "switching scenarios stops playback and opens the requested flight");
        std::printf("flight playback selftest: %d failure(s)\n", failures);
        return failures ? 1 : 0;
    }

    int App::test_open_flight_playback(const std::string &path)
    {
        open_flight_recording(path);
        if (!flight_replay.playing || flight_replay.recording.frames.size() < 2)
        {
            std::fprintf(stderr, "flight playback reopen selftest: could not open recording\n");
            return 1;
        }
        const FlightRecordingFrame &last = flight_replay.recording.frames.back();
        const ScenarioShip &expected = last.scenario.ships[static_cast<std::size_t>(last.scenario.active)];
        const auto position = expected.state_position;
        const double jd = last.scenario.epoch_jd;
        show_flight_frame(flight_replay.recording.frames.size() - 1);
        const sl::dvec3 r = ship->body().position - sim.universe.position(ship->telemetry().soi);
        const bool ok = glm::length(r - sl::dvec3(position[0], position[1], position[2])) < 0.01 &&
                        std::abs(sim.universe.julian_date() - jd) < 1e-9;
        std::printf("flight playback reopen selftest: %d failure(s)\n", ok ? 0 : 1);
        stop_flight_playback();
        return ok ? 0 : 1;
    }
}
