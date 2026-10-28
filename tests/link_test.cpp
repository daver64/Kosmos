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

int main()
{
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
