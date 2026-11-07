#include "data.h"

#include "json.hpp"

#include <algorithm>
#include <cstdlib>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace launcher
{
    namespace
    {
        nlohmann::json read_json(const fs::path &file)
        {
            std::ifstream in(file);
            if (!in)
                return nlohmann::json();
            try
            {
                return nlohmann::json::parse(in);
            }
            catch (const std::exception &)
            {
                return nlohmann::json();
            }
        }

        std::string str(const nlohmann::json &j, const char *key)
        {
            return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
        }

        fs::path existing(const fs::path &base, const std::string &name)
        {
            if (name.empty())
                return {};
            const fs::path p = base / name;
            std::error_code ec;
            return fs::is_regular_file(p, ec) ? p : fs::path();
        }

        fs::path guess_thumbnail(const fs::path &dir, const std::string &stem)
        {
            for (const char *ext : {".png", ".jpg"})
            {
                std::error_code ec;
                const fs::path p = dir / "thumbs" / (stem + ext);
                if (fs::is_regular_file(p, ec))
                    return p;
            }
            return {};
        }

        std::vector<fs::path> json_files(const fs::path &dir)
        {
            std::vector<fs::path> out;
            std::error_code ec;
            if (!fs::is_directory(dir, ec))
                return out;
            for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
            {
                if (ec)
                    break;
                if (it->is_regular_file(ec) && it->path().extension() == ".json")
                    out.push_back(it->path());
            }
            std::sort(out.begin(), out.end());
            return out;
        }

        void add_scenarios(const fs::path &dir, std::vector<ScenarioInfo> &out)
        {
            for (const fs::path &file : json_files(dir))
            {
                const nlohmann::json j = read_json(file);
                if (!j.is_object() || !j.contains("ships"))
                    continue;
                ScenarioInfo s;
                s.id = file.stem().string();
                s.name = j.value("name", s.id);
                s.description = str(j, "description");
                s.file = file;
                const fs::path rel = fs::relative(file.parent_path(), dir);
                s.category = rel == "." ? std::string() : rel.generic_string(); // the whole folder path, shown as a nested tree
                // Only the vessel the player starts in is listed; docked pods and other payloads are left to the description.
                const nlohmann::json &list = j["ships"];
                const std::size_t active = j.contains("active") && j["active"].is_number_integer() && j["active"].get<int>() >= 0 && static_cast<std::size_t>(j["active"].get<int>()) < list.size()
                                               ? static_cast<std::size_t>(j["active"].get<int>())
                                               : 0;
                for (std::size_t i = 0; i < list.size(); ++i)
                {
                    const nlohmann::json &ship = list[i];
                    if (i == active && ship.is_object() && ship.contains("ship") && ship["ship"].is_string())
                        s.ships.push_back(ship["ship"].get<std::string>());
                    if (s.body.empty())
                        s.body = str(ship, "body");
                }
                s.thumbnail = existing(file.parent_path(), str(j, "thumbnail"));
                if (s.thumbnail.empty())
                {
                    // Look in thumbs/ beside the file, then in each folder above it up to the scenarios root.
                    for (fs::path d = file.parent_path(); s.thumbnail.empty(); d = d.parent_path())
                    {
                        s.thumbnail = guess_thumbnail(d, s.id);
                        if (d == dir || d == d.parent_path())
                            break;
                    }
                }
                out.push_back(std::move(s));
            }
        }
    }

    fs::path user_dir()
    {
        const char *xdg = std::getenv("XDG_CONFIG_HOME");
        const char *home = std::getenv("HOME");
        const char *appdata = std::getenv("APPDATA");
        const fs::path dir = xdg && *xdg ? fs::path(xdg) : home && *home ? fs::path(home) / ".config" : appdata ? fs::path(appdata) : fs::path();
        return dir.empty() ? fs::path() : dir / "kosmos";
    }

    fs::path find_root(const fs::path &launcher_dir)
    {
        std::error_code ec;
        fs::path dir = launcher_dir;
        for (int up = 0; up < 4 && !dir.empty(); ++up, dir = dir.parent_path())
            if (fs::is_directory(dir / "scenarios", ec) && fs::is_directory(dir / "addons", ec))
                return dir;
        return launcher_dir;
    }

    fs::path find_kosmos(const fs::path &root)
    {
        std::error_code ec;
        for (const fs::path &p : {root / "kosmos", root / "kosmos.exe", root / "build" / "kosmos", root / "build" / "kosmos.exe"})
            if (fs::is_regular_file(p, ec))
                return p;
        return {};
    }

    fs::path find_addon_program(const fs::path &root, const std::string &addon, const std::string &program)
    {
        std::error_code ec;
        for (const char *ext : {"", ".exe"})
        {
            const fs::path p = root / "addons" / addon / "bin" / (program + ext);
            if (fs::is_regular_file(p, ec))
                return p;
        }
        return {};
    }

    std::string read_text(const fs::path &file)
    {
        std::ifstream in(file, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    std::string installed_version(const fs::path &root)
    {
        std::string text = read_text(root / "VERSION.txt");
        if (text.empty())
            text = read_text(root / "VERSION");
        const auto end = text.find_first_of(" \t\r\n");
        return end == std::string::npos ? text : text.substr(0, end);
    }

    std::vector<AddonInfo> scan_addons(const fs::path &root)
    {
        std::vector<AddonInfo> out;
        std::error_code ec;
        std::vector<std::string> disabled;
        const fs::path user = user_dir();
        if (!user.empty())
        {
            const nlohmann::json list = read_json(user / "addons.json");
            if (list.is_object() && list.contains("disabled") && list["disabled"].is_array())
                for (const nlohmann::json &id : list["disabled"])
                    if (id.is_string())
                        disabled.push_back(id.get<std::string>());
        }
        std::vector<fs::path> folders;
        for (const fs::directory_entry &entry : fs::directory_iterator(root / "addons", ec))
            if (entry.is_directory(ec) && fs::exists(entry.path() / "addon.json", ec))
                folders.push_back(entry.path());
        std::sort(folders.begin(), folders.end());
        for (const fs::path &folder : folders)
        {
            const nlohmann::json j = read_json(folder / "addon.json");
            if (!j.is_object())
                continue;
            AddonInfo a;
            a.directory = folder;
            a.id = j.value("id", folder.filename().string());
            a.name = j.value("name", a.id);
            a.version = j.contains("version") && j["version"].is_string() ? j["version"].get<std::string>() : "0";
            a.author = str(j, "author");
            a.description = str(j, "description");
            if (const fs::path longer = existing(folder, str(j, "long_description")); !longer.empty())
                a.long_description = read_text(longer);
            a.thumbnail = existing(folder, str(j, "thumbnail"));
            if (a.thumbnail.empty())
                a.thumbnail = guess_thumbnail(folder, "addon");
            if (j.contains("ships") && j["ships"].is_array())
            {
                for (const nlohmann::json &file : j["ships"])
                {
                    const nlohmann::json ship = file.is_string() ? read_json(folder / file.get<std::string>()) : nlohmann::json();
                    const std::string name = str(ship, "name");
                    if (!name.empty())
                        a.ships.push_back(name);
                }
            }
            a.enabled = std::find(disabled.begin(), disabled.end(), a.id) == disabled.end();
            out.push_back(std::move(a));
        }
        return out;
    }

    std::vector<ScenarioInfo> scan_scenarios(const fs::path &root, const std::vector<AddonInfo> &addons)
    {
        std::vector<ScenarioInfo> out;
        add_scenarios(root / "scenarios", out);
        for (const AddonInfo &a : addons)
        {
            if (!a.enabled)
                continue;
            const size_t first = out.size();
            add_scenarios(a.directory / "scenarios", out);
            for (size_t i = first; i < out.size(); ++i)
                out[i].category = "Addon: " + a.name;
        }
        const fs::path user = user_dir();
        if (!user.empty())
        {
            const size_t first = out.size();
            add_scenarios(user / "scenarios", out);
            for (size_t i = first; i < out.size(); ++i)
            {
                // A user scenario with the same id replaces the stock one, as Kosmos does.
                const auto same = std::find_if(out.begin(), out.begin() + first, [&](const ScenarioInfo &o) { return o.id == out[i].id; });
                out[i].category = "My scenarios";
                if (same != out.begin() + first)
                    *same = out[i];
            }
        }
        // Drop the duplicates that replaced a stock scenario.
        std::vector<ScenarioInfo> unique;
        for (ScenarioInfo &s : out)
        {
            const auto same = std::find_if(unique.begin(), unique.end(), [&](const ScenarioInfo &o) { return o.id == s.id; });
            if (same == unique.end())
                unique.push_back(std::move(s));
        }
        return unique;
    }

    std::vector<PackInfo> builtin_packs(const fs::path &root)
    {
        PackInfo hires;
        hires.id = "hires";
        hires.name = "High-resolution Earth, Moon and Mars";
        hires.description = "16k textures for the planets seen from space. Optional; Kosmos falls back to the standard images without them.";
        hires.file = "kosmos-hires.tar.gz";
        hires.marker = "data/textures-hires";
        std::error_code ec;
        hires.installed = fs::is_directory(root / hires.marker, ec);
        return {hires};
    }

    std::vector<PackInfo> parse_packs(const std::string &json_text, const fs::path &root)
    {
        std::vector<PackInfo> out;
        const nlohmann::json j = nlohmann::json::parse(json_text, nullptr, false);
        if (!j.is_object() || !j.contains("packs") || !j["packs"].is_array())
            return out;
        std::error_code ec;
        for (const nlohmann::json &p : j["packs"])
        {
            PackInfo info;
            info.id = str(p, "id");
            info.name = p.value("name", info.id);
            info.description = str(p, "description");
            info.file = str(p, "file");
            info.sha256 = str(p, "sha256");
            info.marker = str(p, "marker");
            info.size = p.value("size", 0LL);
            // Reject names that could write outside the install folder.
            if (info.id.empty() || info.file.empty() || info.file.find_first_of("/\\") != std::string::npos || info.marker.find("..") != std::string::npos)
                continue;
            info.installed = !info.marker.empty() && fs::exists(root / info.marker, ec);
            out.push_back(std::move(info));
        }
        return out;
    }

    Profile load_profile()
    {
        Profile p;
        const fs::path user = user_dir();
        if (user.empty())
            return p;
        const nlohmann::json j = read_json(user / "launcher.json");
        if (!j.is_object())
            return p;
        p.backend = j.value("backend", p.backend);
        p.fullscreen = j.value("fullscreen", p.fullscreen);
        p.max_texture = j.value("max_texture", p.max_texture);
        p.world_texture = j.value("world_texture", p.world_texture);
        p.extra_args = j.value("extra_args", p.extra_args);
        p.last_scenario = j.value("last_scenario", p.last_scenario);
        p.server = j.value("server", p.server);
        return p;
    }

    bool save_profile(const Profile &p)
    {
        const fs::path user = user_dir();
        if (user.empty())
            return false;
        std::error_code ec;
        fs::create_directories(user, ec);
        nlohmann::json j;
        j["backend"] = p.backend;
        j["fullscreen"] = p.fullscreen;
        j["max_texture"] = p.max_texture;
        j["world_texture"] = p.world_texture;
        j["extra_args"] = p.extra_args;
        j["last_scenario"] = p.last_scenario;
        j["server"] = p.server;
        std::ofstream out(user / "launcher.json");
        out << j.dump(2) << "\n";
        return static_cast<bool>(out);
    }

    bool save_disabled_addons(const std::vector<AddonInfo> &addons)
    {
        const fs::path user = user_dir();
        if (user.empty())
            return false;
        std::error_code ec;
        fs::create_directories(user, ec);
        // Keep ids of addons that are no longer installed, so reinstalling one restores its setting.
        nlohmann::json j = read_json(user / "addons.json");
        std::vector<std::string> disabled;
        if (j.is_object() && j.contains("disabled") && j["disabled"].is_array())
            for (const nlohmann::json &id : j["disabled"])
                if (id.is_string() && std::none_of(addons.begin(), addons.end(), [&](const AddonInfo &a) { return a.id == id.get<std::string>(); }))
                    disabled.push_back(id.get<std::string>());
        for (const AddonInfo &a : addons)
            if (!a.enabled)
                disabled.push_back(a.id);
        std::ofstream out(user / "addons.json");
        out << nlohmann::json{{"disabled", disabled}}.dump(2) << "\n";
        return static_cast<bool>(out);
    }

    std::vector<std::string> split_arguments(const std::string &text)
    {
        std::vector<std::string> out;
        std::string cur;
        bool quoted = false, any = false;
        for (char c : text)
        {
            if (c == '"')
            {
                quoted = !quoted;
                any = true;
            }
            else if ((c == ' ' || c == '\t' || c == '\n') && !quoted)
            {
                if (any)
                    out.push_back(cur);
                cur.clear();
                any = false;
            }
            else
            {
                cur += c;
                any = true;
            }
        }
        if (any)
            out.push_back(cur);
        return out;
    }

    std::vector<std::string> build_arguments(const Profile &p, const std::string &scenario_id)
    {
        std::vector<std::string> args;
        if (!scenario_id.empty())
            args = {"--scenario", scenario_id};
        if (p.backend == "gl" || p.backend == "vulkan" || p.backend == "d3d12")
            args.push_back("--" + p.backend);
        if (p.fullscreen)
            args.push_back("--fullscreen");
        if (p.max_texture > 0)
            args.insert(args.end(), {"--max-texture", std::to_string(p.max_texture)});
        if (p.world_texture > 0)
            args.insert(args.end(), {"--world-texture", std::to_string(p.world_texture)});
        for (const std::string &a : split_arguments(p.extra_args))
            args.push_back(a);
        return args;
    }

    bool version_newer(const std::string &latest, const std::string &current)
    {
        auto parts = [](const std::string &v)
        {
            std::vector<long> n;
            std::stringstream ss(v);
            std::string tok;
            while (std::getline(ss, tok, '.'))
                n.push_back(std::atol(tok.c_str()));
            return n;
        };
        std::vector<long> a = parts(latest), b = parts(current);
        const size_t len = std::max(a.size(), b.size());
        a.resize(len);
        b.resize(len);
        return a > b;
    }

    std::string find_checksum(const std::string &sums, const std::string &name)
    {
        std::stringstream ss(sums);
        std::string line;
        while (std::getline(ss, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const auto sep = line.find("  ");
            if (sep != std::string::npos && line.substr(sep + 2) == name)
                return line.substr(0, sep);
        }
        return {};
    }

    std::string sha256_file(const fs::path &file)
    {
        static const std::uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        std::ifstream in(file, std::ios::binary);
        if (!in)
            return {};
        auto rotr = [](std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
        auto block = [&](const std::uint8_t *p)
        {
            std::uint32_t w[64];
            for (int i = 0; i < 16; ++i)
                w[i] = std::uint32_t(p[4 * i]) << 24 | std::uint32_t(p[4 * i + 1]) << 16 | std::uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
            for (int i = 16; i < 64; ++i)
            {
                const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
                const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }
            std::uint32_t v[8];
            std::memcpy(v, h, sizeof v);
            for (int i = 0; i < 64; ++i)
            {
                const std::uint32_t t1 = v[7] + (rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) + K[i] + w[i];
                const std::uint32_t t2 = (rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
                v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1; v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
            }
            for (int i = 0; i < 8; ++i)
                h[i] += v[i];
        };
        std::vector<std::uint8_t> buf(1 << 20);
        std::uint64_t total = 0;
        std::uint8_t tail[128];
        size_t tail_len = 0;
        while (in)
        {
            in.read(reinterpret_cast<char *>(buf.data()), static_cast<std::streamsize>(buf.size()));
            size_t got = static_cast<size_t>(in.gcount()), pos = 0;
            total += got;
            if (tail_len)
            {
                const size_t take = std::min(got, 64 - tail_len);
                std::memcpy(tail + tail_len, buf.data(), take);
                tail_len += take;
                pos = take;
                if (tail_len == 64)
                {
                    block(tail);
                    tail_len = 0;
                }
            }
            for (; pos + 64 <= got; pos += 64)
                block(buf.data() + pos);
            if (pos < got)
            {
                std::memcpy(tail, buf.data() + pos, got - pos);
                tail_len = got - pos;
            }
        }
        tail[tail_len++] = 0x80;
        const size_t pad_to = tail_len > 56 ? 128 : 64;
        std::memset(tail + tail_len, 0, pad_to - tail_len);
        const std::uint64_t bits = total * 8;
        for (int i = 0; i < 8; ++i)
            tail[pad_to - 1 - i] = static_cast<std::uint8_t>(bits >> (8 * i));
        for (size_t off = 0; off < pad_to; off += 64)
            block(tail + off);
        char hex[65];
        for (int i = 0; i < 8; ++i)
            std::snprintf(hex + 8 * i, 9, "%08x", h[i]);
        return hex;
    }
}

namespace launcher
{
    namespace
    {
        int clamped(const nlohmann::json &j, const char *key, int fallback, int lo, int hi)
        {
            if (j.is_object() && j.contains(key))
            {
                if (j[key].is_number_integer())
                    return std::clamp(j[key].get<int>(), lo, hi);
                if (j[key].is_boolean())
                    return j[key].get<bool>() ? 1 : 0;
            }
            return fallback;
        }
    } // namespace

    GraphicsSettings load_graphics(const fs::path &settings_file)
    {
        const nlohmann::json j = read_json(settings_file);
        GraphicsSettings g;
        g.style = clamped(j, "gfx-style", g.style, 0, 2);
        g.clouds = clamped(j, "gfx-clouds", g.clouds, 0, 1);
        g.light_radius = clamped(j, "gfx-light-radius", g.light_radius, 500, 20000);
        g.max_fps = clamped(j, "gfx-max-fps", g.max_fps, 10, 240);
        g.terrain_detail = clamped(j, "gfx-terrain-detail", g.terrain_detail, 0, 3);
        g.earth_relief = clamped(j, "gfx-earth-relief", g.earth_relief, 0, 1);
        g.weather = clamped(j, "weather", g.weather, 0, 1);
        g.weather_rain = clamped(j, "weather-rain", g.weather_rain, 0, 1);
        if (j.is_object() && j.contains("weather-source") && j["weather-source"].is_string())
            g.weather_live = j["weather-source"].get<std::string>() == "live" ? 1 : 0;
        if (j.is_object() && j.contains("weather-preset") && j["weather-preset"].is_string())
            g.weather_preset = j["weather-preset"].get<std::string>();
        if (j.is_object() && j.contains("weather-metar") && j["weather-metar"].is_string())
            g.weather_metar = j["weather-metar"].get<std::string>();
        return g;
    }

    bool save_graphics(const fs::path &settings_file, const GraphicsSettings &g)
    {
        nlohmann::json j = read_json(settings_file);
        if (!j.is_object())
            j = nlohmann::json::object();
        j["gfx-style"] = g.style;
        j["gfx-clouds"] = g.clouds != 0;
        j["gfx-light-radius"] = g.light_radius;
        j["gfx-max-fps"] = g.max_fps;
        j["gfx-terrain-detail"] = g.terrain_detail;
        j["gfx-earth-relief"] = g.earth_relief != 0;
        j["weather"] = g.weather != 0;
        j["weather-rain"] = g.weather_rain != 0;
        j["weather-source"] = g.weather_live ? "live" : "procedural";
        j["weather-preset"] = g.weather_preset;
        j["weather-metar"] = g.weather_metar;
        std::error_code ec;
        fs::create_directories(settings_file.parent_path(), ec);
        std::ofstream out(settings_file);
        if (!out)
            return false;
        out << j.dump(1) << "\n";
        return true;
    }

    namespace
    {
        void replace_all(std::string &text, const std::string &from, const std::string &to)
        {
            if (from.empty())
                return;
            for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
                text.replace(at, from.size(), to);
        }

        // "/home/alice" or "C:\Users\alice" -> the part after the last separator.
        std::string account_name(const std::string &home)
        {
            const size_t cut = home.find_last_of("/\\");
            return cut == std::string::npos ? home : home.substr(cut + 1);
        }

        std::string server_base(const std::string &server)
        {
            std::string base = server;
            while (!base.empty() && base.back() == '/')
                base.pop_back();
            const std::string tail = "/download";
            if (base.size() >= tail.size() && base.compare(base.size() - tail.size(), tail.size(), tail) == 0)
                base.resize(base.size() - tail.size());
            return base;
        }
    } // namespace

    std::string scrub_paths(const std::string &text, const std::string &home)
    {
        std::string out = text;
        if (home.size() < 3)
            return out;
        replace_all(out, home, "~");
        std::string slashed = home;
        std::replace(slashed.begin(), slashed.end(), '\\', '/');
        replace_all(out, slashed, "~");
        const std::string name = account_name(home);
        if (name.size() >= 3)
        {
            replace_all(out, "/home/" + name, "~");
            replace_all(out, "\\Users\\" + name, "\\Users\\user");
            replace_all(out, "/Users/" + name, "/Users/user");
        }
        return out;
    }

    std::string log_tail(const std::string &text, size_t limit)
    {
        if (text.size() <= limit)
            return text;
        size_t start = text.size() - limit;
        while (start < text.size() && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80)
            ++start; // do not begin in the middle of a UTF-8 sequence
        return "[earlier output cut]\n" + text.substr(start);
    }

    std::string report_url(const std::string &server) { return server_base(server) + "/report/submit.php"; }

    std::string report_page_url(const std::string &server) { return server_base(server) + "/report/"; }

    std::string report_payload(const ReportContent &r)
    {
        nlohmann::json j;
        j["log"] = r.log;
        j["info"] = r.info;
        j["options"] = r.options;
        j["note"] = r.note;
        j["contact"] = r.contact;
        j["kosmos"] = r.kosmos;
        j["launcher"] = r.launcher;
        j["exit_code"] = r.exit_code;
        j["scenario"] = r.scenario;
        return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    std::string log_exit_code(const std::string &log)
    {
        const std::string tag = "Exit code ";
        if (log.compare(0, tag.size(), tag) != 0)
            return "";
        const size_t end = log.find('\n');
        return log.substr(tag.size(), (end == std::string::npos ? log.size() : end) - tag.size());
    }
} // namespace launcher
