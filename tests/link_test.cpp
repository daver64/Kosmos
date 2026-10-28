// KosmosLink: the game-side server against the SDK's C client, in one process. The client runs on its own thread
// (as an addon program would) while this thread polls the server the way the frame loop does.
#include "link/link_server.h"
#include "link/native_addons.h"

extern "C"
{
#include "kosmos_link.h"
}

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace kosmos::link;

static int failures = 0;
#define CHECK(cond)                                                              \
    do                                                                           \
    {                                                                            \
        if (!(cond))                                                             \
        {                                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

namespace
{
    struct FakeHost : LinkHost
    {
        std::string keys_json, last_ship, last_equipment;
        int released = -2;
        std::string fired;
        std::string mfd;
        std::string sim_version() override { return "test-1"; }
        std::string define_keys(const std::string &json) override { keys_json = json; return ""; }
        std::string spawn_ship(const std::string &name, double, double, double, std::uint32_t &handle) override
        {
            if (name == "nothing")
                return "no ship called nothing";
            last_ship = name;
            handle = 7;
            return "";
        }
        std::string release_ordnance(int hardpoint) override { released = hardpoint; return ""; }
        std::string fire_weapons(std::uint32_t index, const std::string &group, bool on) override { fired = std::to_string(index) + group + (on ? "+" : "-"); return ""; }
        std::string give_equipment(const std::string &id, int) override { last_equipment = id; return ""; }
        std::string define_mfd(const std::string &addon, const std::string &id, const std::string &) override { mfd = addon + "/" + id; return ""; }
    };

    struct FakePolicy : LinkPolicy
    {
        std::uint32_t caps = cap_all;
        std::string seen_addon;
        std::uint32_t granted(const std::string &, const std::string &, std::uint32_t requested) override { return requested & caps; }
        void seen(const std::string &addon, const std::string &, std::uint32_t) override { seen_addon = addon; }
    };

    std::atomic<int> double_changes{0};
    std::atomic<bool> saw_granted_event{false}, saw_mfd_event{false}, saw_denied{false};
    double last_altitude = -1.0;

    void on_value(const char *path, const kl_value *v, void *)
    {
        if (std::string(path) == "/position/altitude-m" && v->type == KL_DOUBLE)
        {
            last_altitude = v->real;
            ++double_changes;
        }
    }
    void on_event(const char *name, const char *, void *)
    {
        if (std::string(name) == "granted")
            saw_granted_event = true;
        if (std::string(name) == "mfd")
            saw_mfd_event = true;
    }
    void on_error(int code, const char *, void *)
    {
        if (code == KL_ERR_DENIED)
            saw_denied = true;
    }

    void run_client(int port, std::atomic<bool> &done)
    {
        kl_options o{};
        o.host = "127.0.0.1";
        o.port = port;
        o.addon_id = "test-addon";
        o.name = "Test";
        o.capabilities = KL_CAP_READ | KL_CAP_CONTROLS | KL_CAP_SPAWN | KL_CAP_DRAW | KL_CAP_KEYS;
        char err[256];
        kl_client *c = kl_connect(&o, err, sizeof(err));
        CHECK(c != nullptr);
        if (!c)
        {
            std::printf("connect: %s\n", err);
            done = true;
            return;
        }
        CHECK(std::string(kl_sim_version(c)) == "test-1");
        CHECK(kl_granted(c) == (KL_CAP_READ | KL_CAP_CONTROLS | KL_CAP_SPAWN | KL_CAP_DRAW | KL_CAP_KEYS));
        kl_set_value_handler(c, on_value, nullptr);
        kl_set_event_handler(c, on_event, nullptr);
        kl_set_error_handler(c, on_error, nullptr);

        kl_value v;
        CHECK(kl_get(c, "/position/altitude-m", &v) == KL_OK && v.type == KL_DOUBLE && v.real == 1234.5);
        CHECK(kl_get(c, "/no/such/thing", &v) == KL_OK && v.type == KL_NONE);
        CHECK(kl_ping(c) == KL_OK);

        // A subscription delivers the value at once and again when it changes.
        CHECK(kl_subscribe(c, "/position/altitude-m", 0) == KL_OK);
        for (int n = 0; n < 100 && double_changes < 1; ++n)
            kl_poll(c, 10);
        CHECK(last_altitude == 1234.5);
        kl_set_double(c, "/controls/flight/elevator", 0.25);
        kl_set_double(c, "/position/altitude-m", 5.0); // not allowed: not a controls path
        for (int n = 0; n < 100 && !saw_denied; ++n)
            kl_poll(c, 10);
        CHECK(saw_denied);
        CHECK(kl_get(c, "/controls/flight/elevator", &v) == KL_OK && v.type == KL_DOUBLE && v.real == 0.25);
        CHECK(kl_get(c, "/position/altitude-m", &v) == KL_OK && v.real == 1234.5);
        // The addon's own subtree is writable and creates properties.
        kl_set_int(c, "/addons/test-addon/count", 3);
        CHECK(kl_get(c, "/addons/test-addon/count", &v) == KL_OK && v.type == KL_INT && v.integer == 3);

        CHECK(kl_define_key(c, "fire", "Fire it", "Test", "J", 0, 0) == KL_OK);
        unsigned handle = 0;
        CHECK(kl_spawn_ship(c, "Skylark", 1, 2, 3, &handle) == KL_OK && handle == 7);
        CHECK(kl_spawn_ship(c, "nothing", 0, 0, 0, &handle) == KL_ERR_FAILED);
        CHECK(std::string(kl_error(c)).find("nothing") != std::string::npos);
        CHECK(kl_release(c, 1) == KL_OK);
        CHECK(kl_fire(c, 2, "secondary", 1) == KL_OK);
        CHECK(kl_fire(c, -1, "bad/group", 1) != KL_OK);
        CHECK(kl_give(c, "pistol", 2) == KL_OK);
        CHECK(kl_define_mfd(c, "sight", "Bomb sight") == KL_OK);
        kl_draw_begin(c);
        kl_draw_colour(c, 255, 0, 0);
        kl_draw_line(c, 0, 0, 100, 100);
        kl_draw_text(c, 200, 20, "HELLO", 1);
        CHECK(kl_draw_end(c, "sight") == KL_OK);
        CHECK(kl_ping(c) == KL_OK);

        // Withdrawing the grant is announced, and later requests are refused.
        for (int n = 0; n < 300 && !saw_granted_event; ++n)
            kl_poll(c, 10);
        CHECK(saw_granted_event);
        CHECK(kl_get(c, "/position/altitude-m", &v) == KL_ERR_DENIED);
        for (int n = 0; n < 300 && !saw_mfd_event; ++n)
            kl_poll(c, 10);
        CHECK(saw_mfd_event);

        kl_close(c);
        done = true;
    }

    void wait_for(LinkServer &server, std::atomic<bool> &done, FakePolicy *policy, FakeHost &host)
    {
        const auto start = std::chrono::steady_clock::now();
        bool revoked = false;
        while (!done && std::chrono::steady_clock::now() - start < std::chrono::seconds(20))
        {
            server.poll();
            server.flush();
            if (!revoked && host.mfd == "test-addon/sight")
            {
                Page *p = server.page("test-addon", "sight");
                if (p && p->drawn)
                {
                    CHECK(p->commands.size() == 2 && p->commands[1].text == "HELLO" && p->commands[0].r == 255);
                    p->wanted = std::chrono::steady_clock::now();
                    revoked = true;
                    policy->caps = 0;
                }
            }
            if (revoked)
                server.page("test-addon", "sight")->wanted = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
} // namespace

static void test_protocol_edges()
{
    Writer w(msg_value, 9);
    Value in;
    in.tag = tag_string;
    in.s = "abc";
    w.str("/x").value(in);
    std::string stream = w.finish();
    stream += std::string("\x03\x00", 2); // the start of another frame
    Frame f;
    CHECK(take_frame(stream, f) == 1 && f.type == msg_value && f.request == 9);
    Reader r(f.payload.data(), f.payload.size());
    CHECK(r.str() == "/x" && r.value() == in && r.ok());
    CHECK(take_frame(stream, f) == 0);
    std::string junk("\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00", 10);
    CHECK(take_frame(junk, f) == -1);
    Reader past("\x05", 1);
    past.u32();
    CHECK(!past.ok());
}

static void test_manifest()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "kosmos_link_test_addon";
    fs::create_directories(dir);
    {
        std::ofstream out(dir / "addon.json");
        out << R"({"id":"pilot","native":{"run":{"linux":"bin/pilot","windows":"bin/pilot.exe"},"capabilities":["read","controls"],"autostart":false}})";
    }
    NativeAddons native;
    native.set_state_file((dir / "state.json").string());
    native.scan({{"pilot", "Pilot", dir.string()}, {"plain", "Plain", (dir / "missing").string()}});
    CHECK(native.entries().size() == 1);
    CHECK(native.entries()[0].caps == (cap_read | cap_controls));
    CHECK(!native.entries()[0].autostart);
    CHECK(native.granted("pilot", "", cap_all) == 0);
    native.set_allowed("pilot", true);
    CHECK(native.granted("pilot", "", cap_all) == (cap_read | cap_controls));
    NativeAddons again;
    again.set_state_file((dir / "state.json").string());
    again.scan({{"pilot", "Pilot", dir.string()}});
    again.load();
    CHECK(again.entries()[0].allowed);
    // An unknown program that connects is listed and gets nothing until allowed.
    again.seen("hardware", "Hardware", cap_read);
    CHECK(again.granted("hardware", "", cap_read) == 0);
    again.set_allowed("hardware", true);
    CHECK(again.granted("hardware", "", cap_read | cap_write) == cap_read);
    fs::remove_all(dir);
}

namespace
{
    struct WorldHost : LinkHost
    {
        std::string weather_mode, weather_arg, placed, removed;
        std::string weather_at(const std::string &body, double lat, double, double alt, WeatherInfo &out, std::string &station, std::string &metar) override
        {
            if (body == "Mars")
                return "that body has no weather";
            out.wind_speed = 6.0;
            out.wind_from = 270.0;
            out.temperature = lat;
            out.cloud_base = alt;
            station = "EGLL";
            metar = "EGLL 011200Z 27012KT";
            return "";
        }
        std::string set_weather(const std::string &mode, const std::string &arg) override { weather_mode = mode; weather_arg = arg; return ""; }
        std::string list_ships(double radius, bool, std::vector<ShipInfo> &out) override
        {
            ShipInfo a, b;
            a.index = 0;
            a.name = "Flown";
            a.body = "Earth";
            a.flags = 1;
            b.index = 1;
            b.name = "Tanker";
            b.body = "Earth";
            b.distance = 900.0;
            b.flags = 8;
            out.push_back(a);
            if (radius == 0.0 || radius > 1000.0)
                out.push_back(b);
            return "";
        }
        std::string remove_ship(std::uint32_t index) override
        {
            if (index == 0)
                return "the flown ship cannot be removed";
            removed = std::to_string(index);
            return "";
        }
        std::string place_ship(std::uint32_t index, const std::string &body, double lat, double lon, double alt, double heading, double speed) override
        {
            placed = std::to_string(index) + body + ":" + std::to_string(static_cast<int>(lat)) + "," + std::to_string(static_cast<int>(lon)) + "," + std::to_string(static_cast<int>(alt)) + "," + std::to_string(static_cast<int>(heading)) + "," + std::to_string(static_cast<int>(speed));
            return "";
        }
        std::string list_facilities(const std::string &kind, const std::string &, double, double, double, std::size_t max, std::vector<Facility> &out) override
        {
            for (std::size_t i = 0; i < 3 && i < max; ++i)
            {
                Facility f;
                f.id = "base" + std::to_string(i);
                f.name = kind;
                f.icao = "ABCD";
                f.distance = 1000.0 * static_cast<double>(i);
                f.runways.push_back({"09/27", 1.0, 2.0, 90.0, 3000.0, 45.0, {110.3, 110.9}});
                f.navaids.push_back({"vor", "VOR", "Test VOR", 1.0, 2.0, 113.4, 100000.0});
                out.push_back(f);
            }
            return "";
        }
    };

    struct EventCounts
    {
        std::atomic<int> tick{0}, pause{0};
    };

    void count_events(const char *name, const char *, void *user)
    {
        EventCounts *e = static_cast<EventCounts *>(user);
        if (std::string(name) == "tick")
            ++e->tick;
        if (std::string(name) == "pause")
            ++e->pause;
    }

    kl_client *open_client(int port, const char *id, unsigned caps)
    {
        kl_options o{};
        o.host = "127.0.0.1";
        o.port = port;
        o.addon_id = id;
        o.name = id;
        o.capabilities = caps;
        char err[256];
        return kl_connect(&o, err, sizeof err);
    }

    void run_world_client(int port, std::atomic<bool> &subscribed, std::atomic<bool> &done)
    {
        kl_client *c = open_client(port, "world-addon", KL_CAP_READ | KL_CAP_WORLD);
        kl_client *quiet_client = open_client(port, "quiet-addon", KL_CAP_READ);
        CHECK(c && quiet_client);
        if (!c || !quiet_client)
        {
            done = true;
            return;
        }
        EventCounts quiet, wanted;
        kl_set_event_handler(quiet_client, count_events, &quiet);
        kl_set_event_handler(c, count_events, &wanted);

        kl_weather w;
        CHECK(kl_weather_at(c, "Earth", 12.0, 3.0, 450.0, &w) == KL_OK);
        CHECK(w.wind_speed == 6.0 && w.wind_from == 270.0 && w.temperature == 12.0 && w.cloud_base == 450.0);
        CHECK(std::string(w.station) == "EGLL" && std::string(w.metar) == "EGLL 011200Z 27012KT");
        CHECK(kl_weather_at(c, "Mars", 0, 0, 0, &w) == KL_ERR_FAILED);
        CHECK(kl_set_weather(c, "bogus", "") == KL_ERR_BAD_REQUEST);
        CHECK(kl_set_weather(c, "preset", "storm") == KL_OK);
        CHECK(kl_subscribe_event(c, "nonsense") == KL_ERR_BAD_REQUEST);

        kl_ship_info ships[4];
        int n = 0;
        CHECK(kl_list_ships(c, 0.0, 0, ships, 4, &n) == KL_OK && n == 2);
        CHECK(std::string(ships[0].name) == "Flown" && (ships[0].flags & KL_SHIP_FLOWN) && std::string(ships[1].name) == "Tanker" && (ships[1].flags & KL_SHIP_FROM_LINK));
        CHECK(kl_list_ships(c, 100.0, 0, ships, 4, &n) == KL_OK && n == 1);
        CHECK(kl_list_ships(c, 0.0, 0, ships, 1, &n) == KL_OK && n == 1);
        CHECK(kl_remove_ship(c, 1) == KL_OK);
        CHECK(kl_remove_ship(c, 0) == KL_ERR_FAILED);
        CHECK(kl_place_ship(c, 1, "Earth", 51.0, -1.0, 3000.0, 270.0, 120.0) == KL_OK);

        kl_facility fac[5];
        CHECK(kl_list_facilities(c, "airport", "", 0, 0, 0, fac, 5, &n) == KL_OK && n == 3);
        CHECK(std::string(fac[1].id) == "base1" && std::string(fac[1].icao) == "ABCD" && fac[1].distance == 1000.0);
        CHECK(fac[0].runway_count == 1 && std::string(fac[0].runways[0].name) == "09/27" && fac[0].runways[0].ils_count == 2 && fac[0].runways[0].ils[1] == 110.9);
        CHECK(fac[0].navaid_count == 1 && fac[0].navaids[0].frequency == 113.4);
        CHECK(kl_list_facilities(c, "", "", 0, 0, 0, fac, 2, &n) == KL_OK && n == 2);

        // Only subscribers get opt-in events.
        CHECK(kl_subscribe_event(c, "tick") == KL_OK);
        subscribed = true;
        for (int i = 0; i < 500 && wanted.tick < 1; ++i)
        {
            kl_poll(c, 10);
            kl_poll(quiet_client, 0);
        }
        CHECK(wanted.tick >= 1 && wanted.pause == 0);
        CHECK(quiet.tick == 0 && quiet.pause == 0);

        // Without the world capability the changing requests are refused; reading still works.
        CHECK(kl_set_weather(quiet_client, "auto", "") == KL_ERR_DENIED);
        CHECK(kl_remove_ship(quiet_client, 1) == KL_ERR_DENIED);
        CHECK(kl_place_ship(quiet_client, 1, "", 0, 0, 0, 0, 0) == KL_ERR_DENIED);
        CHECK(kl_weather_at(quiet_client, "Earth", 0, 0, 0, &w) == KL_OK);

        kl_close(quiet_client);
        kl_close(c);
        done = true;
    }
} // namespace

static void test_protocol_two()
{
    sl::PropertyTree props;
    WorldHost host;
    FakePolicy policy;
    LinkServer server(props, host, &policy);
    CHECK(server.start(0));

    std::atomic<bool> subscribed{false}, done{false};
    std::thread client(run_world_client, static_cast<int>(server.port()), std::ref(subscribed), std::ref(done));
    const auto start = std::chrono::steady_clock::now();
    while (!done && std::chrono::steady_clock::now() - start < std::chrono::seconds(20))
    {
        server.poll();
        server.flush();
        if (subscribed)
        {
            server.broadcast_event("pause", "1");
            server.broadcast_event("tick", "5");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    client.join();
    CHECK(host.weather_mode == "preset" && host.weather_arg == "storm");
    CHECK(host.removed == "1");
    CHECK(host.placed == "1Earth:51,-1,3000,270,120");

    // A protocol 1 client is still welcome.
    sl::TcpSocket raw;
    raw.connect("127.0.0.1", server.port());
    for (int i = 0; i < 200 && !raw.connected(); ++i)
    {
        raw.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Writer hello(msg_hello, 1);
    hello.u16(1).str("old-addon").str("Old").str("1.0").str("").u32(cap_read);
    raw.send(hello.finish());
    std::string received;
    Frame f;
    int got = 0;
    for (int i = 0; i < 200 && got == 0; ++i)
    {
        server.poll();
        raw.receive(received);
        got = take_frame(received, f);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(got == 1 && f.type == msg_welcome);
}


namespace
{
    struct VesselHost : LinkHost
    {
        std::string notice, claimed, value, forces;
        int released_owner = -1;
        std::string notify_text;
        void notify(const std::string &text, double) override { notice = text; }
        std::string claim_ship(int owner, const std::string &addon, std::uint32_t index, bool passthrough) override
        {
            if (index == 9)
                return "no such ship";
            claimed = addon + ":" + std::to_string(index) + (passthrough ? "p" : "");
            (void)owner;
            return "";
        }
        std::string release_ship(int, std::uint32_t) override { return ""; }
        void release_all(int owner) override { released_owner = owner; }
        std::string ship_value(int, std::uint32_t index, const std::string &name, double v) override { value = std::to_string(index) + name + "=" + std::to_string(static_cast<int>(v)); return ""; }
        std::string ship_force(int, std::uint32_t index, const double f[3], const double t[3]) override { forces = std::to_string(index) + ":" + std::to_string(static_cast<int>(f[1])) + "," + std::to_string(static_cast<int>(t[2])); return ""; }
        void log(const std::string &, const std::string &text) override { notify_text = text; }
    };

    struct Raw
    {
        sl::TcpSocket socket;
        std::string received;
        explicit Raw(LinkServer &server, std::uint32_t caps, std::uint16_t version = 3)
        {
            socket.connect("127.0.0.1", server.port());
            for (int i = 0; i < 200 && !socket.connected(); ++i)
            {
                socket.update();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            Writer hello(msg_hello, 1);
            hello.u16(version).str("p3").str("Protocol 3").str("1.0").str("").u32(caps);
            socket.send(hello.finish());
        }
        void send(const std::string &frame) { socket.send(frame); }
        // Pumps the server until a frame of the type arrives; returns false on timeout.
        bool wait(LinkServer &server, std::uint16_t type, Frame &out, int ms = 1000)
        {
            for (int i = 0; i < ms / 5; ++i)
            {
                server.poll();
                server.flush();
                socket.update();
                std::string chunk;
                if (socket.receive(chunk) > 0)
                    received += chunk;
                int got;
                while ((got = take_frame(received, out)) == 1)
                    if (out.type == type)
                        return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return false;
        }
        void pump(LinkServer &server, int ms = 100)
        {
            for (int i = 0; i < ms / 5; ++i)
            {
                server.poll();
                socket.update();
                std::string chunk;
                socket.receive(chunk);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    };
} // namespace

static void test_protocol_three()
{
    sl::PropertyTree props;
    VesselHost host;
    FakePolicy policy;
    LinkServer server(props, host, &policy);
    CHECK(server.start(0));
    Frame f;

    // Windows, notices, claims, forces and 3D layers.
    {
        Raw a(server, cap_ui | cap_vessel | cap_draw3d | cap_controls);
        CHECK(a.wait(server, msg_welcome, f));
        Writer win(msg_ui_window, 0);
        win.str("panel").str("Panel").u16(2);
        win.u8(1).str("go").str("Go").str("").f64(0).f64(0).f64(1);
        win.u8(3).str("level").str("Level").str("").f64(2).f64(0).f64(10);
        a.send(win.finish());
        a.send(Writer(msg_ui_notify, 0).str("hello").f64(3.0).finish());
        Writer claim(msg_claim_ship, 5);
        claim.u32(2).u8(1);
        a.send(claim.finish());
        CHECK(a.wait(server, msg_ok, f));
        Writer bad(msg_claim_ship, 6);
        bad.u32(9).u8(0);
        a.send(bad.finish());
        CHECK(a.wait(server, msg_error, f));
        a.send(Writer(msg_ship_value, 0).u32(2).str("controls/throttle").f64(1).finish());
        a.send(Writer(msg_ship_force, 0).u32(2).f64(0).f64(500).f64(0).f64(0).f64(0).f64(7).finish());
        Writer layer(msg_draw3d, 0);
        layer.str("path").u16(1);
        layer.u8(0).u8(0).u32(0).f64(1).f64(2).f64(3).f64(4).f64(5).f64(6).f64(0).u8(1).u8(2).u8(3).u8(255).u8(1).str("");
        a.send(layer.finish());
        a.pump(server);
        CHECK(server.ui_windows().size() == 1 && server.ui_windows()[0].widgets.size() == 2);
        CHECK(host.notice.find("hello") != std::string::npos);
        CHECK(host.claimed == "p3:2p");
        CHECK(host.value == "2controls/throttle=1");
        CHECK(host.forces == "2:500,7");
        CHECK(server.layers().size() == 1 && server.layers()[0]->items.size() == 1 && server.layers()[0]->items[0].b[2] == 6);

        server.ui_event("p3", "panel", "level", "5.5");
        CHECK(a.wait(server, msg_event, f));
        Reader r(f.payload.data(), f.payload.size());
        CHECK(r.str() == "ui" && r.str() == "panel level 5.5");
        CHECK(server.ui_windows()[0].widgets[1].value == 5.5);
        server.ui_event("p3", "panel", "", "closed");
        CHECK(server.ui_windows().empty());

        a.send(win.finish());
        a.pump(server);
        CHECK(server.ui_windows().size() == 1);
    }
    // Disconnecting takes the windows and layers away and releases the ships.
    Raw gone(server, cap_read);
    gone.pump(server, 300);
    CHECK(server.ui_windows().empty() && server.layers().empty());
    CHECK(host.released_owner > 0);

    // Without the capabilities these are refused.
    {
        Raw b(server, cap_read);
        CHECK(b.wait(server, msg_welcome, f));
        Writer claim(msg_claim_ship, 3);
        claim.u32(0).u8(0);
        b.send(claim.finish());
        CHECK(b.wait(server, msg_error, f));
        Writer ls(msg_lockstep, 4);
        ls.u8(1).u32(50).f64(0);
        b.send(ls.finish());
        CHECK(b.wait(server, msg_error, f));
        CHECK(!server.lockstep_active());
    }

    // Lockstep: the gate waits for step_done, times out on a silent program and is released by a disconnect.
    {
        Raw c(server, cap_controls);
        CHECK(c.wait(server, msg_welcome, f));
        Writer on(msg_lockstep, 8);
        on.u8(1).u32(60).f64(0.02);
        c.send(on.finish());
        CHECK(c.wait(server, msg_ok, f));
        CHECK(server.lockstep_active());
        Raw second(server, cap_controls);
        CHECK(second.wait(server, msg_welcome, f));
        Writer on2(msg_lockstep, 9);
        on2.u8(1).u32(60).f64(0);
        second.send(on2.finish());
        CHECK(second.wait(server, msg_error, f));

        std::atomic<bool> answer{true}, stop{false};
        std::thread driver([&] {
            // The program side: answer each step as soon as it is seen.
            std::string buffer;
            while (!stop)
            {
                c.socket.update();
                std::string chunk;
                if (c.socket.receive(chunk) > 0)
                    buffer += chunk;
                Frame g;
                while (take_frame(buffer, g) == 1)
                    if (g.type == msg_event && answer)
                    {
                        Reader r(g.payload.data(), g.payload.size());
                        if (r.str() == "step")
                        {
                            const std::string arg = r.str();
                            c.socket.send(Writer(msg_step_done, 0).u32(static_cast<std::uint32_t>(std::atoi(arg.c_str()))).finish());
                        }
                    }
                std::this_thread::sleep_for(std::chrono::microseconds(300));
            }
        });
        const auto t0 = std::chrono::steady_clock::now();
        const double dt = server.lockstep_gate(0.016, 1.0);
        const auto answered = std::chrono::steady_clock::now() - t0;
        CHECK(dt == 0.02);
        CHECK(server.lockstep_missed() == 0 && answered < std::chrono::milliseconds(50));
        answer = false;
        server.lockstep_gate(0.016, 2.0);
        CHECK(server.lockstep_missed() == 1);
        stop = true;
        driver.join();
    }
    for (int i = 0; i < 20 && server.lockstep_active(); ++i)
        server.poll(), std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(!server.lockstep_active());
    CHECK(server.lockstep_gate(0.016, 3.0) == 0.0);

    // A protocol 3 program that goes silent is pinged, then dropped; one that answers stays.
    {
        server.set_timeout(0.1);
        Raw quiet(server, cap_read), lively(server, cap_read);
        CHECK(quiet.wait(server, msg_welcome, f) && lively.wait(server, msg_welcome, f));
        bool pinged = false;
        const auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(600))
        {
            server.poll();
            lively.socket.update();
            std::string chunk, chunk2;
            lively.socket.receive(chunk);
            lively.received += chunk;
            Frame g;
            while (take_frame(lively.received, g) == 1)
                if (g.type == msg_ping)
                {
                    pinged = true;
                    lively.send(Writer(msg_ping, 0).finish());
                }
            quiet.socket.update();
            quiet.socket.receive(chunk2);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(pinged);
        CHECK(server.clients().size() == 1);
        // A protocol 2 program is never timed out.
        Raw old(server, cap_read, 2);
        CHECK(old.wait(server, msg_welcome, f));
        old.pump(server, 500);
        // The idle protocol 3 program is gone by now; the protocol 2 one remains.
        CHECK(server.clients().size() == 1);
        server.set_timeout(0.0);
    }
}

int main()
{
    test_protocol_two();
    test_protocol_three();
    test_protocol_edges();
    test_manifest();

    sl::PropertyTree props;
    props.set_double("/position/altitude-m", 1234.5);
    props.set_double("/controls/flight/elevator", 0.0);
    FakeHost host;
    FakePolicy policy;
    LinkServer server(props, host, &policy);
    CHECK(server.start(0));
    std::atomic<bool> done{false};
    std::thread client(run_client, static_cast<int>(server.port()), std::ref(done));
    wait_for(server, done, &policy, host);
    client.join();
    CHECK(done);
    CHECK(policy.seen_addon == "test-addon");
    CHECK(host.keys_json.find("\"test-addon.fire\"") != std::string::npos && host.keys_json.find("/addons/test-addon/keys/fire") != std::string::npos);
    CHECK(host.last_ship == "Skylark");
    CHECK(host.released == 1);
    CHECK(host.fired == "2secondary+");
    CHECK(host.last_equipment == "pistol");
    CHECK(props.get_double("/controls/flight/elevator") == 0.25);
    CHECK(props.get_double("/position/altitude-m") == 1234.5);

    // Garbage and an unsupported protocol version close the connection.
    sl::TcpSocket raw;
    raw.connect("127.0.0.1", server.port());
    for (int n = 0; n < 200 && !raw.connected(); ++n)
    {
        raw.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    raw.send(std::string("\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00", 10));
    for (int n = 0; n < 200 && raw.state() == sl::TcpState::connected; ++n)
    {
        server.poll();
        std::string sink;
        raw.receive(sink);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(raw.state() == sl::TcpState::closed);
    CHECK(server.clients().empty());

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("link_test: all passed\n");
    return 0;
}
