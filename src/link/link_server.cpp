#include "link_server.h"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <thread>

namespace kosmos::link
{
    namespace
    {
        constexpr int max_frames_per_poll = 256;
        constexpr std::size_t max_subscriptions = 512;
        constexpr std::size_t max_draw_commands = 2000;
        constexpr double hello_timeout = 5.0;
        constexpr double mfd_redraw_interval = 0.05;
        constexpr std::size_t max_ui_widgets = 64, max_windows_per_client = 8, max_layers_per_client = 8, max_primitives = 2000;

        std::string json_text(const std::string &s) { return nlohmann::json(s).dump(); }

        bool path_ok(const std::string &path)
        {
            if (path.size() < 2 || path[0] != '/' || path.size() > 256 || path.find("..") != std::string::npos)
                return false;
            return std::none_of(path.begin(), path.end(), [](char c) { return static_cast<unsigned char>(c) < 32; });
        }

        bool id_ok(const std::string &id)
        {
            return !id.empty() && id.size() <= 48 && std::all_of(id.begin(), id.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; });
        }

        const char *const opt_in_events[] = {"pause", "warp", "scenario", "soi", "crash", "touchdown", "weather", "ships", "tick", "weapon"};

        bool opt_in(const std::string &name) { return std::find(std::begin(opt_in_events), std::end(opt_in_events), name) != std::end(opt_in_events); }

        bool starts_with(const std::string &s, const std::string &prefix) { return s.compare(0, prefix.size(), prefix) == 0; }
    } // namespace

    struct LinkServer::Client
    {
        struct Subscription
        {
            std::string path;
            double interval = 0.0;
            Value last;
            bool sent = false;
            std::chrono::steady_clock::time_point at{};
        };
        int id = 0;
        sl::TcpSocket socket;
        std::string in;
        bool hello = false, dead = false;
        std::chrono::steady_clock::time_point connected = std::chrono::steady_clock::now();
        std::string addon, name, version, token;
        std::uint32_t requested = 0, granted = 0;
        std::vector<Subscription> subscriptions;
        std::vector<std::string> keys_defined;
        std::vector<std::string> events;
        std::uint16_t proto = 0;
        std::chrono::steady_clock::time_point last_rx = std::chrono::steady_clock::now();
        bool pinged = false;
    };

    LinkServer::LinkServer(sl::PropertyTree &props, LinkHost &host, LinkPolicy *policy) : props_(props), host_(host), policy_(policy) {}
    LinkServer::~LinkServer()
    {
        clients_.clear();
        listener_.close();
    }

    bool LinkServer::start(std::uint16_t port)
    {
        stop();
        return listener_.listen(port, 16, true);
    }

    void LinkServer::stop()
    {
        for (auto &c : clients_)
            drop(*c);
        clients_.clear();
        listener_.close();
    }

    bool LinkServer::running() const { return listener_.listening(); }
    std::uint16_t LinkServer::port() const { return listener_.port(); }

    void LinkServer::send(Client &c, const std::string &frame)
    {
        if (!c.socket.send(frame))
            c.dead = true;
    }

    void LinkServer::reply_error(Client &c, std::uint32_t request, ErrorCode code, const std::string &message)
    {
        Writer w(msg_error, request);
        w.u16(code).str(message);
        send(c, w.finish());
    }

    void LinkServer::reply_ok(Client &c, std::uint32_t request, std::uint32_t handle)
    {
        Writer w(msg_ok, request);
        w.u32(handle);
        send(c, w.finish());
    }

    Value LinkServer::read(const std::string &path) const
    {
        Value v;
        const sl::PropertyNode *node = props_.find(path);
        if (!node || !node->has_value())
            return v;
        switch (node->type())
        {
        case sl::PropertyNode::Type::boolean:
            v.tag = tag_bool;
            v.b = node->get_bool();
            break;
        case sl::PropertyNode::Type::integer:
            v.tag = tag_int;
            v.i = node->get_int();
            break;
        case sl::PropertyNode::Type::real:
            v.tag = tag_double;
            v.d = node->get_double();
            break;
        case sl::PropertyNode::Type::string:
            v.tag = tag_string;
            v.s = node->get_string();
            break;
        default:
            break;
        }
        return v;
    }

    bool LinkServer::may_write(const Client &c, const std::string &path) const
    {
        if (c.granted & cap_write)
            return true;
        return (c.granted & cap_controls) && (starts_with(path, "/controls/") || starts_with(path, "/addons/" + c.addon + "/"));
    }

    std::string LinkServer::write(const Client &c, const std::string &path, const Value &value)
    {
        if (!path_ok(path))
            return "bad property path";
        if (!may_write(c, path))
            return "this addon may not write " + path;
        if (value.tag == tag_none)
            return "no value";
        sl::PropertyNode *node = props_.node(path);
        if (!node)
            return "cannot create " + path;
        if (node->is_read_only())
            return path + " is read-only";
        const double number = value.tag == tag_bool ? (value.b ? 1.0 : 0.0) : value.tag == tag_int ? value.i : value.d;
        const std::string text = value.tag == tag_string ? value.s : value.tag == tag_bool ? (value.b ? "true" : "false") : value.tag == tag_int ? std::to_string(value.i) : std::to_string(value.d);
        switch (node->has_value() ? node->type() : sl::PropertyNode::Type::none)
        {
        case sl::PropertyNode::Type::boolean:
            node->set_bool(value.tag == tag_string ? (value.s == "true" || value.s == "1") : number != 0.0);
            break;
        case sl::PropertyNode::Type::integer:
            node->set_int(value.tag == tag_string ? std::atoi(value.s.c_str()) : static_cast<int>(number < 0 ? number - 0.5 : number + 0.5));
            break;
        case sl::PropertyNode::Type::real:
            node->set_double(value.tag == tag_string ? std::atof(value.s.c_str()) : number);
            break;
        case sl::PropertyNode::Type::string:
            node->set_string(text);
            break;
        default:
            if (value.tag == tag_bool)
                node->set_bool(value.b);
            else if (value.tag == tag_int)
                node->set_int(value.i);
            else if (value.tag == tag_double)
                node->set_double(value.d);
            else
                node->set_string(value.s);
        }
        return "";
    }

    void LinkServer::handle(Client &c, const Frame &f)
    {
        Reader r(f.payload.data(), f.payload.size());
        auto bad = [&] { reply_error(c, f.request, err_bad_request, "malformed message"); };
        auto need = [&](std::uint32_t cap, const char *what) {
            if (c.granted & cap)
                return true;
            reply_error(c, f.request, err_denied, std::string("not allowed: ") + what + " (the player can allow this addon in View > Native addons)");
            return false;
        };

        if (f.type == msg_hello)
        {
            const std::uint16_t version = r.u16();
            c.addon = r.str();
            c.name = r.str();
            c.version = r.str();
            c.token = r.str();
            c.requested = r.u32() & cap_all;
            if (!r.ok() || c.hello)
                return bad();
            if (version < min_protocol_version || version > protocol_version)
            {
                reply_error(c, f.request, err_version, "protocol version " + std::to_string(version) + " is not supported; Kosmos speaks " + std::to_string(protocol_version));
                c.dead = true;
                return;
            }
            if (!id_ok(c.addon))
            {
                reply_error(c, f.request, err_bad_request, "the addon id must be letters, digits, '-' and '_'");
                c.dead = true;
                return;
            }
            c.hello = true;
            c.proto = version;
            if (policy_)
                policy_->seen(c.addon, c.name.empty() ? c.addon : c.name, c.requested);
            c.granted = policy_ ? (policy_->granted(c.addon, c.token, c.requested) & c.requested) : c.requested;
            Writer w(msg_welcome, f.request);
            w.u16(protocol_version).str(host_.sim_version()).u32(c.granted);
            send(c, w.finish());
            return;
        }
        if (!c.hello)
        {
            reply_error(c, f.request, err_bad_request, "say HELLO first");
            c.dead = true;
            return;
        }

        switch (f.type)
        {
        case msg_ping:
            send(c, Writer(msg_pong, f.request).finish());
            break;
        case msg_log:
        {
            const std::string text = r.str();
            if (!r.ok())
                return bad();
            host_.log(c.addon, text);
            break;
        }
        case msg_get:
        {
            const std::string path = r.str();
            if (!r.ok() || !path_ok(path))
                return bad();
            if (!need(cap_read, "reading properties"))
                return;
            Writer w(msg_value, f.request);
            w.str(path).value(read(path));
            send(c, w.finish());
            break;
        }
        case msg_subscribe:
        {
            const std::string path = r.str();
            const std::uint16_t hz = r.u16();
            if (!r.ok() || !path_ok(path))
                return bad();
            if (!need(cap_read, "reading properties"))
                return;
            for (Client::Subscription &s : c.subscriptions)
                if (s.path == path)
                {
                    s.interval = hz ? 1.0 / hz : 0.0;
                    s.sent = false;
                    return;
                }
            if (c.subscriptions.size() >= max_subscriptions)
                return reply_error(c, f.request, err_failed, "too many subscriptions");
            Client::Subscription s;
            s.path = path;
            s.interval = hz ? 1.0 / hz : 0.0;
            c.subscriptions.push_back(std::move(s));
            break;
        }
        case msg_unsubscribe:
        {
            const std::string path = r.str();
            if (!r.ok())
                return bad();
            c.subscriptions.erase(std::remove_if(c.subscriptions.begin(), c.subscriptions.end(), [&](const Client::Subscription &s) { return s.path == path; }), c.subscriptions.end());
            break;
        }
        case msg_set:
        {
            const std::string path = r.str();
            const Value v = r.value();
            if (!r.ok())
                return bad();
            const std::string error = write(c, path, v);
            if (!error.empty())
                reply_error(c, f.request, error.find("may not") != std::string::npos ? err_denied : err_failed, error);
            else if (f.request)
                reply_ok(c, f.request, 0);
            break;
        }
        case msg_define_key:
        {
            const std::string id = r.str(), label = r.str(), group = r.str(), key = r.str();
            const bool shift = r.u8() != 0;
            const std::uint8_t mode = r.u8();
            if (!r.ok() || !id_ok(id))
                return bad();
            if (!need(cap_keys, "defining keys"))
                return;
            const std::string full = c.addon + "." + id;
            const std::string property = "/addons/" + c.addon + "/keys/" + id;
            const std::string doc = "{\"actions\":[{\"id\":" + json_text(full) + ",\"label\":" + json_text(label.empty() ? id : label) + ",\"group\":" + json_text(group.empty() ? c.name : group) + ",\"key\":" + json_text(key) +
                                    ",\"shift\":" + (shift ? "true" : "false") + ",\"mode\":\"" + (mode == 2 ? "hold" : "toggle") + "\",\"property\":" + json_text(property) + "}]}";
            const bool again = std::find(c.keys_defined.begin(), c.keys_defined.end(), id) != c.keys_defined.end();
            const std::string error = again ? "" : host_.define_keys(doc);
            if (!error.empty())
                return reply_error(c, f.request, err_failed, error);
            c.keys_defined.push_back(id);
            if (!props_.exists(property))
                props_.set_bool(property, false);
            reply_ok(c, f.request, 0);
            break;
        }
        case msg_spawn_ship:
        {
            const std::string name = r.str();
            const double x = r.f64(), y = r.f64(), z = r.f64();
            if (!r.ok())
                return bad();
            if (!need(cap_spawn, "spawning ships"))
                return;
            std::uint32_t handle = 0;
            const std::string error = host_.spawn_ship(name, x, y, z, handle);
            error.empty() ? reply_ok(c, f.request, handle) : reply_error(c, f.request, err_failed, error);
            break;
        }
        case msg_release:
        {
            const int hardpoint = static_cast<std::int32_t>(r.u32());
            if (!r.ok())
                return bad();
            if (!need(cap_spawn, "releasing ordnance"))
                return;
            const std::string error = host_.release_ordnance(hardpoint);
            error.empty() ? reply_ok(c, f.request, 0) : reply_error(c, f.request, err_failed, error);
            break;
        }
        case msg_fire:
        {
            const std::uint32_t index = r.u32();
            const std::string group = r.str();
            const bool on = r.u8() != 0;
            if (!r.ok() || group.empty() || group.size() > 32 || group.find_first_of("/[]") != std::string::npos)
                return bad();
            if (!need(cap_spawn, "firing weapons"))
                return;
            const std::string error = host_.fire_weapons(index, group, on);
            error.empty() ? reply_ok(c, f.request, 0) : reply_error(c, f.request, err_failed, error);
            break;
        }
        case msg_give:
        {
            const std::string id = r.str();
            const int count = static_cast<std::int32_t>(r.u32());
            if (!r.ok() || count < 1 || count > 1000)
                return bad();
            if (!need(cap_spawn, "giving equipment"))
                return;
            const std::string error = host_.give_equipment(id, count);
            error.empty() ? reply_ok(c, f.request, 0) : reply_error(c, f.request, err_failed, error);
            break;
        }
        case msg_define_mfd:
        {
            const std::string id = r.str(), title = r.str();
            if (!r.ok() || !id_ok(id))
                return bad();
            if (!need(cap_draw, "drawing on MFDs"))
                return;
            const std::string key = c.addon + "." + id;
            if (!pages_.count(key))
            {
                const std::string error = host_.define_mfd(c.addon, id, title.empty() ? id : title);
                if (!error.empty())
                    return reply_error(c, f.request, err_failed, error);
                Page &p = pages_[key];
                p.addon = c.addon;
                p.id = key;
            }
            reply_ok(c, f.request, 0);
            break;
        }
        case msg_draw:
        {
            const std::string id = r.str();
            const std::uint16_t count = r.u16();
            if (!r.ok() || count > max_draw_commands)
                return bad();
            if (!(c.granted & cap_draw))
                return;
            auto page = pages_.find(c.addon + "." + id);
            if (page == pages_.end())
                return reply_error(c, f.request, err_unknown, "define the MFD page first");
            std::vector<DrawCommand> commands;
            commands.reserve(count);
            for (int n = 0; n < count && r.ok(); ++n)
            {
                DrawCommand d;
                d.kind = r.u8();
                d.a = r.f32();
                d.b = r.f32();
                d.c = r.f32();
                d.d = r.f32();
                d.e = r.f32();
                d.f = r.f32();
                d.r = r.u8();
                d.g = r.u8();
                d.bl = r.u8();
                d.align = r.u8();
                d.text = r.str();
                commands.push_back(std::move(d));
            }
            if (!r.ok())
                return bad();
            page->second.commands = std::move(commands);
            page->second.drawn = true;
            break;
        }
        case msg_weather_at:
        {
            const std::string body = r.str();
            const double lat = r.f64(), lon = r.f64(), alt = r.f64();
            if (!r.ok() || body.size() > 64 || !std::isfinite(lat) || !std::isfinite(lon) || !std::isfinite(alt) || std::fabs(lat) > 90.0)
                return bad();
            if (!need(cap_read, "reading the weather"))
                return;
            WeatherInfo info;
            std::string station, metar;
            const std::string error = host_.weather_at(body, lat, lon, alt, info, station, metar);
            if (!error.empty())
                return reply_error(c, f.request, err_failed, error);
            Writer w(msg_weather, f.request);
            w.f64(info.wind_east).f64(info.wind_north).f64(info.wind_speed).f64(info.wind_from).f64(info.gust).f64(info.rain).f64(info.thunder);
            w.f64(info.cloud_cover).f64(info.cloud_base).f64(info.cloud_top).f64(info.visibility).f64(info.temperature).f64(info.pressure);
            w.str(station).str(metar);
            send(c, w.finish());
            break;
        }
        case msg_set_weather:
        {
            const std::string mode = r.str(), arg = r.str();
            if (!r.ok() || (mode != "metar" && mode != "preset" && mode != "auto") || arg.size() > 512)
                return bad();
            if (!need(cap_world, "changing the weather"))
                return;
            const std::string error = host_.set_weather(mode, arg);
            error.empty() ? reply_ok(c, f.request, 0) : reply_error(c, f.request, err_failed, error);
            break;
        }
        case msg_subscribe_event:
        case msg_unsubscribe_event:
        {
            const std::string name = r.str();
            if (!r.ok() || !opt_in(name))
                return bad();
            if (!need(cap_read, "receiving events"))
                return;
            auto it = std::find(c.events.begin(), c.events.end(), name);
            if (f.type == msg_subscribe_event && it == c.events.end())
                c.events.push_back(name);
            else if (f.type == msg_unsubscribe_event && it != c.events.end())
                c.events.erase(it);
            reply_ok(c, f.request, 0);
            break;
        }
        case msg_list_ships:
        {
            const double radius = r.f64();
            const std::uint8_t flags = r.u8();
            if (!r.ok() || !std::isfinite(radius) || radius < 0.0)
                return bad();
            if (!need(cap_read, "listing ships"))
                return;
            std::vector<ShipInfo> ships;
            const std::string error = host_.list_ships(radius, (flags & 1) != 0, ships);
            if (!error.empty())
                return reply_error(c, f.request, err_failed, error);
            if (ships.size() > 2000)
                ships.resize(2000);
            Writer w(msg_ship_list, f.request);
            w.u32(static_cast<std::uint32_t>(ships.size()));
            for (const ShipInfo &s : ships)
            {
                w.u32(s.index).str(s.name).str(s.body).f64(s.latitude).f64(s.longitude).f64(s.altitude).f64(s.heading).f64(s.speed).f64(s.vertical_speed).f64(s.distance).u8(s.flags);
            }
            send(c, w.finish());
            break;
        }
        case msg_remove_ship:
        {
            const std::uint32_t index = r.u32();
            if (!r.ok())
                return bad();
            if (!need(cap_world, "removing ships"))
                return;
            const std::string error = host_.remove_ship(index);
            error.empty() ? reply_ok(c, f.request, 0) : reply_error(c, f.request, err_failed, error);
            break;
        }
        case msg_place_ship:
        {
            const std::uint32_t index = r.u32();
            const std::string body = r.str();
            const double lat = r.f64(), lon = r.f64(), alt = r.f64(), heading = r.f64(), speed = r.f64();
            if (!r.ok() || body.size() > 64 || !std::isfinite(lat) || !std::isfinite(lon) || !std::isfinite(alt) || !std::isfinite(heading) || !std::isfinite(speed) || std::fabs(lat) > 90.0 || speed < 0.0)
                return bad();
            if (!need(cap_world, "placing ships"))
                return;
            const std::string error = host_.place_ship(index, body, lat, lon, alt, heading, speed);
            error.empty() ? reply_ok(c, f.request, 0) : reply_error(c, f.request, err_failed, error);
            break;
        }
        case msg_list_facilities:
        {
            const std::string kind = r.str(), body = r.str();
            const double lat = r.f64(), lon = r.f64(), radius = r.f64();
            const std::uint16_t max = r.u16();
            if (!r.ok() || kind.size() > 32 || body.size() > 64 || !std::isfinite(lat) || !std::isfinite(lon) || !std::isfinite(radius))
                return bad();
            if (!need(cap_read, "listing facilities"))
                return;
            std::vector<Facility> found;
            const std::size_t limit = std::clamp<std::size_t>(max ? max : 100, 1, 200);
            const std::string error = host_.list_facilities(kind, body, lat, lon, radius, limit, found);
            if (!error.empty())
                return reply_error(c, f.request, err_failed, error);
            if (found.size() > limit)
                found.resize(limit);
            Writer w(msg_facility_list, f.request);
            w.u32(static_cast<std::uint32_t>(found.size()));
            for (const Facility &b : found)
            {
                w.str(b.id).str(b.name).str(b.icao).str(b.body).f64(b.latitude).f64(b.longitude).f64(b.elevation).f64(b.heading).f64(b.distance);
                const std::size_t nr = std::min<std::size_t>(b.runways.size(), 32), nn = std::min<std::size_t>(b.navaids.size(), 64);
                w.u16(static_cast<std::uint16_t>(nr));
                for (std::size_t i = 0; i < nr; ++i)
                {
                    const FacilityRunway &rw = b.runways[i];
                    w.str(rw.name).f64(rw.latitude).f64(rw.longitude).f64(rw.heading).f64(rw.length).f64(rw.width);
                    const std::size_t ni = std::min<std::size_t>(rw.ils.size(), 4);
                    w.u8(static_cast<std::uint8_t>(ni));
                    for (std::size_t k = 0; k < ni; ++k)
                        w.f64(rw.ils[k]);
                }
                w.u16(static_cast<std::uint16_t>(nn));
                for (std::size_t i = 0; i < nn; ++i)
                {
                    const FacilityNavaid &n = b.navaids[i];
                    w.str(n.type).str(n.id).str(n.name).f64(n.latitude).f64(n.longitude).f64(n.frequency).f64(n.range);
                }
            }
            send(c, w.finish());
            break;
        }
        case msg_ui_window:
        {
            const std::string id = r.str(), title = r.str();
            const std::uint16_t count = r.u16();
            if (!r.ok() || !id_ok(id) || title.size() > 128 || count > max_ui_widgets)
                return bad();
            if (!need(cap_ui, "showing windows"))
                return;
            std::vector<UiWidget> widgets;
            for (int n = 0; n < count && r.ok(); ++n)
            {
                UiWidget w;
                w.kind = r.u8();
                w.id = r.str();
                w.label = r.str();
                w.text = r.str();
                w.value = r.f64();
                w.low = r.f64();
                w.high = r.f64();
                if (w.kind > 5 || w.id.size() > 48 || w.label.size() > 200 || w.text.size() > 200 || !std::isfinite(w.value) || !std::isfinite(w.low) || !std::isfinite(w.high))
                    return bad();
                widgets.push_back(std::move(w));
            }
            if (!r.ok())
                return bad();
            const std::string key = c.addon + "." + id;
            if (!windows_.count(key) && std::count_if(windows_.begin(), windows_.end(), [&](const auto &e) { return e.second.owner == c.id; }) >= static_cast<long>(max_windows_per_client))
                return reply_error(c, f.request, err_failed, "too many windows");
            UiWindow &win = windows_[key];
            win.addon = c.addon;
            win.id = id;
            win.title = title.empty() ? id : title;
            win.widgets = std::move(widgets);
            win.owner = c.id;
            break;
        }
        case msg_ui_close:
        {
            const std::string id = r.str();
            if (!r.ok())
                return bad();
            auto it = windows_.find(c.addon + "." + id);
            if (it != windows_.end() && it->second.owner == c.id)
                windows_.erase(it);
            break;
        }
        case msg_ui_notify:
        {
            const std::string text = r.str();
            const double seconds = r.f64();
            if (!r.ok() || text.size() > 200 || !std::isfinite(seconds))
                return bad();
            if (!need(cap_ui, "showing notices"))
                return;
            host_.notify(c.name.empty() ? text : c.name + ": " + text, std::clamp(seconds, 1.0, 30.0));
            break;
        }
        case msg_lockstep:
        {
            const bool on = r.u8() != 0;
            const std::uint32_t wait_ms = r.u32();
            const double dt = r.f64();
            if (!r.ok() || !std::isfinite(dt) || (dt != 0.0 && (dt < 0.001 || dt > 0.1)))
                return bad();
            if (!need(cap_controls, "lockstep"))
                return;
            if (on)
            {
                if (lockstep_owner_ && lockstep_owner_ != c.id)
                    return reply_error(c, f.request, err_failed, "another program already holds lockstep");
                lockstep_owner_ = c.id;
                lockstep_wait_ = std::clamp<double>(wait_ms ? wait_ms : 100, 1.0, 1000.0) / 1000.0;
                lockstep_dt_ = dt;
                step_acked_ = step_frame_;
                missed_ = 0;
            }
            else if (lockstep_owner_ == c.id)
                lockstep_owner_ = 0;
            if (f.request)
                reply_ok(c, f.request, 0);
            break;
        }
        case msg_step_done:
        {
            const std::uint32_t frame = r.u32();
            if (r.ok() && c.id == lockstep_owner_)
                step_acked_ = std::max(step_acked_, frame);
            break;
        }
        case msg_claim_ship:
        {
            const std::uint32_t index = r.u32();
            const bool passthrough = r.u8() != 0;
            if (!r.ok())
                return bad();
            if (!need(cap_vessel, "owning a ship"))
                return;
            const std::string error = host_.claim_ship(c.id, c.addon, index, passthrough);
            if (!error.empty())
                return reply_error(c, f.request, err_failed, error);
            reply_ok(c, f.request, 0);
            break;
        }
        case msg_release_ship:
        {
            const std::uint32_t index = r.u32();
            if (!r.ok())
                return bad();
            if (!need(cap_vessel, "owning a ship"))
                return;
            const std::string error = host_.release_ship(c.id, index);
            if (!error.empty())
                return reply_error(c, f.request, err_failed, error);
            reply_ok(c, f.request, 0);
            break;
        }
        case msg_ship_value:
        {
            const std::uint32_t index = r.u32();
            const std::string name = r.str();
            const double value = r.f64();
            if (!r.ok() || name.empty() || name.size() > 64 || !path_ok("/" + name) || !std::isfinite(value))
                return bad();
            if (!need(cap_vessel, "owning a ship"))
                return;
            const std::string error = host_.ship_value(c.id, index, name, value);
            if (!error.empty())
                reply_error(c, f.request, err_failed, error);
            break;
        }
        case msg_ship_force:
        {
            const std::uint32_t index = r.u32();
            double force[3], torque[3];
            for (double &v : force)
                v = r.f64();
            for (double &v : torque)
                v = r.f64();
            if (!r.ok() || !std::all_of(force, force + 3, [](double v) { return std::isfinite(v); }) || !std::all_of(torque, torque + 3, [](double v) { return std::isfinite(v); }))
                return bad();
            if (!need(cap_vessel, "owning a ship"))
                return;
            const std::string error = host_.ship_force(c.id, index, force, torque);
            if (!error.empty())
                reply_error(c, f.request, err_failed, error);
            break;
        }
        case msg_draw3d:
        {
            const std::string id = r.str();
            const std::uint16_t count = r.u16();
            if (!r.ok() || !id_ok(id) || count > max_primitives)
                return bad();
            if (!need(cap_draw3d, "drawing in the 3D view"))
                return;
            std::vector<Primitive3> items;
            items.reserve(count);
            for (int n = 0; n < count && r.ok(); ++n)
            {
                Primitive3 p;
                p.kind = r.u8();
                p.frame = r.u8();
                p.ship = r.u32();
                for (double &v : p.a)
                    v = r.f64();
                for (double &v : p.b)
                    v = r.f64();
                p.size = r.f64();
                p.r = r.u8();
                p.g = r.u8();
                p.bl = r.u8();
                p.alpha = r.u8();
                p.depth = r.u8();
                p.text = r.str();
                const bool finite = std::all_of(p.a, p.a + 3, [](double v) { return std::isfinite(v); }) && std::all_of(p.b, p.b + 3, [](double v) { return std::isfinite(v); }) && std::isfinite(p.size);
                if (p.kind > 4 || p.frame > 2 || p.text.size() > 120 || !finite)
                    return bad();
                items.push_back(std::move(p));
            }
            if (!r.ok())
                return bad();
            const std::string key = c.addon + "." + id;
            if (items.empty())
            {
                layers_.erase(key);
                break;
            }
            if (!layers_.count(key) && std::count_if(layers_.begin(), layers_.end(), [&](const auto &e) { return e.second.owner == c.id; }) >= static_cast<long>(max_layers_per_client))
                return reply_error(c, f.request, err_failed, "too many layers");
            Layer3 &layer = layers_[key];
            layer.addon = c.addon;
            layer.id = id;
            layer.owner = c.id;
            layer.items = std::move(items);
            break;
        }
        default:
            break; // unknown messages are ignored so newer addons still run
        }
    }

    void LinkServer::poll()
    {
        if (!listener_.listening())
            return;
        for (;;)
        {
            auto c = std::make_unique<Client>();
            if (!listener_.accept(c->socket))
                break;
            c->id = next_id_++;
            clients_.push_back(std::move(c));
        }
        const auto now = std::chrono::steady_clock::now();
        for (auto &c : clients_)
        {
            c->socket.update();
            std::string chunk;
            if (c->socket.receive(chunk) > 0)
            {
                c->in += chunk;
                c->last_rx = now;
                c->pinged = false;
            }
            if (c->socket.state() == sl::TcpState::closed)
                c->dead = true;
            for (int n = 0; n < max_frames_per_poll && !c->dead; ++n)
            {
                Frame f;
                const int got = take_frame(c->in, f);
                if (got < 0)
                    c->dead = true;
                if (got <= 0)
                    break;
                handle(*c, f);
            }
            if (!c->hello && std::chrono::duration<double>(now - c->connected).count() > hello_timeout)
                c->dead = true;
            if (c->hello && !c->dead && timeout_ > 0.0 && c->proto >= 3)
            {
                const double idle = std::chrono::duration<double>(now - c->last_rx).count();
                if (idle > 2.0 * timeout_)
                {
                    host_.log(c->addon, "no answer for " + std::to_string(static_cast<int>(idle)) + " s; disconnected");
                    c->dead = true;
                }
                else if (idle > timeout_ && !c->pinged)
                {
                    c->pinged = true;
                    send(*c, Writer(msg_ping, 0).finish());
                }
            }
            if (c->hello && !c->dead && policy_)
            {
                const std::uint32_t now_granted = policy_->granted(c->addon, c->token, c->requested) & c->requested;
                if (now_granted != c->granted)
                {
                    c->granted = now_granted;
                    Writer w(msg_event, 0);
                    w.str("granted").str(std::to_string(now_granted));
                    send(*c, w.finish());
                }
            }
        }
        for (auto &c : clients_)
            if (c->dead)
                drop(*c);
        clients_.erase(std::remove_if(clients_.begin(), clients_.end(), [](const std::unique_ptr<Client> &c) { return c->dead; }), clients_.end());
    }

    // Forgets what a program left behind: its windows and layers, its ships and its hold on lockstep.
    void LinkServer::drop(Client &c)
    {
        for (auto it = windows_.begin(); it != windows_.end();)
            it = it->second.owner == c.id ? windows_.erase(it) : std::next(it);
        for (auto it = layers_.begin(); it != layers_.end();)
            it = it->second.owner == c.id ? layers_.erase(it) : std::next(it);
        if (lockstep_owner_ == c.id)
            lockstep_owner_ = 0;
        if (c.hello)
            host_.release_all(c.id);
    }

    bool LinkServer::lockstep_active() const { return lockstep_owner_ != 0; }

    double LinkServer::lockstep_gate(double frame, double sim_time)
    {
        if (!lockstep_owner_ || !listener_.listening())
            return 0.0;
        flush();
        const double dt = lockstep_dt_ > 0.0 ? lockstep_dt_ : frame;
        const std::uint32_t step = ++step_frame_;
        const std::string arg = std::to_string(step) + " " + std::to_string(dt) + " " + std::to_string(sim_time);
        bool sent = false;
        for (auto &c : clients_)
            if (c->id == lockstep_owner_ && c->hello && !c->dead)
            {
                Writer w(msg_event, 0);
                w.str("step").str(arg);
                send(*c, w.finish());
                sent = true;
            }
        if (!sent)
        {
            lockstep_owner_ = 0;
            return 0.0;
        }
        const auto start = std::chrono::steady_clock::now();
        while (lockstep_owner_ && step_acked_ < step)
        {
            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() > lockstep_wait_)
            {
                ++missed_;
                break;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            poll();
        }
        return lockstep_owner_ ? lockstep_dt_ : 0.0;
    }

    std::vector<UiWindow> LinkServer::ui_windows() const
    {
        std::vector<UiWindow> out;
        for (const auto &e : windows_)
            out.push_back(e.second);
        return out;
    }

    void LinkServer::ui_event(const std::string &addon, const std::string &window, const std::string &widget, const std::string &value)
    {
        auto it = windows_.find(addon + "." + window);
        if (it == windows_.end())
            return;
        const bool closed = widget.empty() && value == "closed";
        for (UiWidget &w : it->second.widgets)
            if (!closed && w.id == widget)
            {
                if (w.kind == 4)
                    w.text = value;
                else
                    w.value = std::atof(value.c_str());
            }
        for (auto &c : clients_)
            if (c->id == it->second.owner && c->hello && !c->dead)
            {
                Writer w(msg_event, 0);
                w.str("ui").str(window + " " + widget + " " + value);
                send(*c, w.finish());
            }
        if (closed)
            windows_.erase(it);
    }

    std::vector<const Layer3 *> LinkServer::layers() const
    {
        std::vector<const Layer3 *> out;
        for (const auto &e : layers_)
            out.push_back(&e.second);
        return out;
    }

    void LinkServer::flush()
    {
        const auto now = std::chrono::steady_clock::now();
        for (auto &c : clients_)
        {
            if (!c->hello || c->dead)
                continue;
            std::string batch;
            for (Client::Subscription &s : c->subscriptions)
            {
                if (s.sent && s.interval > 0.0 && std::chrono::duration<double>(now - s.at).count() < s.interval)
                    continue;
                const Value v = read(s.path);
                if (s.sent && v == s.last)
                    continue;
                s.last = v;
                s.sent = true;
                s.at = now;
                Writer w(msg_value, 0);
                w.str(s.path).value(v);
                batch += w.finish();
            }
            if (!batch.empty())
                send(*c, batch);
        }
        for (auto &entry : pages_)
        {
            Page &p = entry.second;
            if (p.wanted.time_since_epoch().count() == 0 || std::chrono::duration<double>(now - p.wanted).count() > 1.0 || std::chrono::duration<double>(now - p.notified).count() < mfd_redraw_interval)
                continue;
            for (auto &c : clients_)
                if (c->hello && !c->dead && c->addon == p.addon)
                {
                    Writer w(msg_event, 0);
                    w.str("mfd").str(p.id.substr(p.addon.size() + 1));
                    send(*c, w.finish());
                }
            p.notified = now;
        }
    }

    bool LinkServer::wants_event(const std::string &name) const
    {
        return std::any_of(clients_.begin(), clients_.end(), [&](const std::unique_ptr<Client> &c) { return c->hello && !c->dead && std::find(c->events.begin(), c->events.end(), name) != c->events.end(); });
    }

    void LinkServer::broadcast_event(const std::string &name, const std::string &arg)
    {
        const bool optional = opt_in(name);
        for (auto &c : clients_)
            if (c->hello && !c->dead && (!optional || std::find(c->events.begin(), c->events.end(), name) != c->events.end()))
            {
                Writer w(msg_event, 0);
                w.str(name).str(arg);
                send(*c, w.finish());
            }
    }

    std::vector<ClientInfo> LinkServer::clients() const
    {
        std::vector<ClientInfo> out;
        for (const auto &c : clients_)
            if (c->hello)
                out.push_back({c->id, c->addon, c->name, c->version, c->requested, c->granted, c->subscriptions.size(), std::chrono::duration<double>(std::chrono::steady_clock::now() - c->last_rx).count()});
        return out;
    }

    Page *LinkServer::page(const std::string &addon, const std::string &id)
    {
        auto it = pages_.find(addon + "." + id);
        return it == pages_.end() ? nullptr : &it->second;
    }

    bool LinkServer::page_live(const std::string &addon) const
    {
        return std::any_of(clients_.begin(), clients_.end(), [&](const std::unique_ptr<Client> &c) { return c->hello && !c->dead && c->addon == addon; });
    }
} // namespace kosmos::link
