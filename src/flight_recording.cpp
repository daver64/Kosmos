#include "flight_recording.h"

#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace kosmos
{
    namespace
    {
        bool valid_frame(const FlightRecordingFrame &frame)
        {
            const bool valid_axes = std::all_of(frame.axes.begin(), frame.axes.end(),
                                                [](double value) { return std::isfinite(value) && value >= -1.0 && value <= 1.0; });
            return valid_axes && !frame.sas.empty() && std::isfinite(frame.elapsed) && frame.elapsed >= 0.0 &&
                   std::isfinite(frame.scenario.epoch_jd) && frame.scenario.epoch_jd > 0.0 &&
                   !frame.scenario.ships.empty() &&
                   std::isfinite(frame.throttle) && frame.throttle >= -1.0 && frame.throttle <= 1.0 &&
                   std::isfinite(frame.hover) && frame.hover >= 0.0 && frame.hover <= 1.0 &&
                   std::isfinite(frame.camera_yaw) && std::isfinite(frame.camera_pitch) &&
                   std::isfinite(frame.camera_distance) && frame.camera_distance >= 0.0;
        }

        bool validate(const FlightRecording &recording, std::string &error)
        {
            if (recording.frames.empty())
            {
                error = "flight recording has no frames";
                return false;
            }
            double elapsed = -1.0, date = 0.0;
            for (const FlightRecordingFrame &frame : recording.frames)
            {
                if (!valid_frame(frame) || frame.elapsed <= elapsed || frame.scenario.epoch_jd < date)
                {
                    error = "flight recording contains invalid or out-of-order frames";
                    return false;
                }
                if (frame.scenario.active < 0 ||
                    static_cast<std::size_t>(frame.scenario.active) >= frame.scenario.ships.size())
                {
                    error = "flight recording has an invalid active ship";
                    return false;
                }
                for (const ScenarioShip &ship : frame.scenario.ships)
                {
                    double orientation_norm = 0.0;
                    for (double value : ship.state_orientation)
                        orientation_norm += value * value;
                    const auto finite = [](double value) { return std::isfinite(value); };
                    if (!ship.has_state || !std::isfinite(orientation_norm) || orientation_norm < 1e-10 ||
                        !std::all_of(ship.state_position.begin(), ship.state_position.end(), finite) ||
                        !std::all_of(ship.state_velocity.begin(), ship.state_velocity.end(), finite) ||
                        !std::all_of(ship.state_angular_velocity.begin(), ship.state_angular_velocity.end(), finite))
                    {
                        error = "flight recording contains invalid vessel state";
                        return false;
                    }
                }
                elapsed = frame.elapsed;
                date = frame.scenario.epoch_jd;
            }
            return true;
        }
    }

    bool save_flight_recording(const std::string &path, const FlightRecording &recording, std::string &error)
    {
        if (!validate(recording, error))
            return false;
        nlohmann::json root = {{"format", "kosmos-flight-recording"}, {"version", 1}, {"frames", nlohmann::json::array()}};
        for (const FlightRecordingFrame &frame : recording.frames)
        {
            root["frames"].push_back({
                {"elapsed", frame.elapsed}, {"scenario", nlohmann::json::parse(scenario_to_json(frame.scenario))},
                {"throttle", frame.throttle}, {"hover", frame.hover},
                {"axes", frame.axes}, {"sas", frame.sas},
                {"camera", {{"yaw", frame.camera_yaw}, {"pitch", frame.camera_pitch}, {"distance", frame.camera_distance},
                            {"cockpit", frame.cockpit}, {"cockpit3d", frame.cockpit3d}}}});
        }
        const std::string text = root.dump();
        if (text.size() >= 128 * 1024 * 1024)
        {
            error = "flight recording exceeds 128 MiB";
            return false;
        }
        std::FILE *out = std::fopen(path.c_str(), "wbx");
        if (!out)
        {
            error = "cannot create flight recording (file may already exist): " + path;
            return false;
        }
        const bool written = std::fwrite(text.data(), 1, text.size(), out) == text.size() &&
                             std::fputc('\n', out) != EOF;
        const bool closed = std::fclose(out) == 0;
        if (!written || !closed)
        {
            error = "failed writing flight recording " + path;
            std::error_code ec;
            std::filesystem::remove(path, ec);
            if (ec)
                error += "; could not remove incomplete file: " + ec.message();
            return false;
        }
        return true;
    }

    bool load_flight_recording(const std::string &path, FlightRecording &recording, std::string &error)
    {
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec || size > 128 * 1024 * 1024)
        {
            error = "flight recording is unavailable or exceeds 128 MiB: " + path;
            return false;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            error = "cannot open flight recording " + path;
            return false;
        }
        try
        {
            const nlohmann::json root = nlohmann::json::parse(in);
            if (root.at("format") != "kosmos-flight-recording" || root.at("version") != 1 ||
                !root.at("frames").is_array() || root.at("frames").size() > 3600)
            {
                error = "unsupported or oversized flight recording " + path;
                return false;
            }
            FlightRecording parsed;
            for (const auto &item : root.at("frames"))
            {
                FlightRecordingFrame frame;
                frame.elapsed = item.at("elapsed").get<double>();
                frame.throttle = item.at("throttle").get<double>();
                frame.hover = item.at("hover").get<double>();
                frame.axes = item.at("axes").get<std::array<double, 8>>();
                frame.sas = item.at("sas").get<std::string>();
                const auto &camera = item.at("camera");
                frame.camera_yaw = camera.at("yaw").get<double>();
                frame.camera_pitch = camera.at("pitch").get<double>();
                frame.camera_distance = camera.at("distance").get<double>();
                frame.cockpit = camera.at("cockpit").get<bool>();
                frame.cockpit3d = camera.at("cockpit3d").get<bool>();
                const auto &scenario = item.at("scenario");
                const int active = scenario.at("active").get<int>();
                if (active < 0 || !scenario.at("ships").is_array() ||
                    static_cast<std::size_t>(active) >= scenario.at("ships").size())
                {
                    error = "flight recording has an invalid active ship";
                    return false;
                }
                if (!parse_scenario(scenario.dump(), path, frame.scenario, error))
                    return false;
                parsed.frames.push_back(std::move(frame));
            }
            if (!validate(parsed, error))
                return false;
            recording = std::move(parsed);
            return true;
        }
        catch (const nlohmann::json::exception &exception)
        {
            error = "invalid flight recording " + path + ": " + exception.what();
            return false;
        }
    }
}
