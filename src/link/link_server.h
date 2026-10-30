#pragma once

#include "link_protocol.h"
#include "sl.h"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kosmos::link
{
    /** Weather at a place, in SI units (wind is the direction the air moves toward). */
    struct WeatherInfo
    {
        double wind_east = 0, wind_north = 0, wind_speed = 0, wind_from = 0, gust = 0;
        double rain = 0, thunder = 0, cloud_cover = 0, cloud_base = 0, cloud_top = 0;
        double visibility = 0, temperature = 0, pressure = 0;
    };

    struct ShipInfo
    {
        std::uint32_t index = 0;
        std::string name, body;
        double latitude = 0, longitude = 0, altitude = 0, heading = 0, speed = 0, vertical_speed = 0, distance = 0;
        std::uint8_t flags = 0; // 1 flown, 2 landed, 4 crashed, 8 spawned by a link addon
    };

    struct FacilityRunway
    {
        std::string name;
        double latitude = 0, longitude = 0, heading = 0, length = 0, width = 0;
        std::vector<double> ils; // frequencies in MHz
    };

    struct FacilityNavaid
    {
        std::string type, id, name;
        double latitude = 0, longitude = 0, frequency = 0, range = 0;
    };

    struct Facility
    {
        std::string id, name, icao, body;
        double latitude = 0, longitude = 0, elevation = 0, heading = 0, distance = 0;
        std::vector<FacilityRunway> runways;
        std::vector<FacilityNavaid> navaids;
    };

    /** One window a native program shows, kept by the server and drawn by the game. */
    struct UiWindow
    {
        std::string addon, id, title;
        std::vector<UiWidget> widgets;
        int owner = 0;
    };

    /** A set of 3D primitives a program draws, replaced wholesale on every update. */
    struct Layer3
    {
        std::string addon, id;
        std::vector<Primitive3> items;
        int owner = 0;
    };

    /** What the game does for an addon's requests that go beyond the property tree. Each returns an error message, or empty on success. */
    class LinkHost
    {
    public:
        virtual ~LinkHost() = default;
        virtual std::string sim_version() { return "unknown"; }
        /** Adds key actions from a keys.json document. */
        virtual std::string define_keys(const std::string &json) { (void)json; return "not supported"; }
        /** Spawns a ship next to the flown one, offset in the flown ship's axes (metres). */
        virtual std::string spawn_ship(const std::string &name, double x, double y, double z, std::uint32_t &handle) { (void)name; (void)x; (void)y; (void)z; handle = 0; return "not supported"; }
        virtual std::string release_ordnance(int hardpoint) { (void)hardpoint; return "not supported"; }
        /** Pulls (or releases) the trigger of a weapon group on ship `index`. */
        virtual std::string fire_weapons(std::uint32_t index, const std::string &group, bool on) { (void)index; (void)group; (void)on; return "not supported"; }
        virtual std::string give_equipment(const std::string &id, int count) { (void)id; (void)count; return "not supported"; }
        /** Adds an MFD page. The page's drawing arrives through LinkServer::page(). */
        virtual std::string define_mfd(const std::string &addon, const std::string &id, const std::string &title) { (void)addon; (void)id; (void)title; return "not supported"; }
        virtual void log(const std::string &addon, const std::string &text) { (void)addon; (void)text; }

        /** Shows a short notice on screen. */
        virtual void notify(const std::string &text, double seconds) { (void)text; (void)seconds; }
        /** Makes @p owner (a client id) the one that controls a ship; @p passthrough keeps the player's controls working alongside. */
        virtual std::string claim_ship(int owner, const std::string &addon, std::uint32_t index, bool passthrough) { (void)owner; (void)addon; (void)index; (void)passthrough; return "not supported"; }
        virtual std::string release_ship(int owner, std::uint32_t index) { (void)owner; (void)index; return "not supported"; }
        /** Releases everything @p owner claimed (it disconnected). */
        virtual void release_all(int owner) { (void)owner; }
        /** Publishes a value under the claimed ship's module properties; names starting "controls/" are the ship's controls. */
        virtual std::string ship_value(int owner, std::uint32_t index, const std::string &name, double value) { (void)owner; (void)index; (void)name; (void)value; return "not supported"; }
        /** Force (N) and torque (N m) in the ship's own axes, held until replaced. */
        virtual std::string ship_force(int owner, std::uint32_t index, const double force[3], const double torque[3]) { (void)owner; (void)index; (void)force; (void)torque; return "not supported"; }

        /** Weather at a place; @p body empty means the flown ship's body. */
        virtual std::string weather_at(const std::string &body, double lat, double lon, double alt, WeatherInfo &out, std::string &station, std::string &metar) { (void)body; (void)lat; (void)lon; (void)alt; (void)out; (void)station; (void)metar; return "not supported"; }
        /** @p mode is "metar" (@p arg is the report), "preset" (a preset id) or "auto". */
        virtual std::string set_weather(const std::string &mode, const std::string &arg) { (void)mode; (void)arg; return "not supported"; }
        /** Ships within @p radius metres of the flown one (0 for all). */
        virtual std::string list_ships(double radius, bool include_buildings, std::vector<ShipInfo> &out) { (void)radius; (void)include_buildings; (void)out; return "not supported"; }
        virtual std::string remove_ship(std::uint32_t index) { (void)index; return "not supported"; }
        virtual std::string place_ship(std::uint32_t index, const std::string &body, double lat, double lon, double alt, double heading, double speed) { (void)index; (void)body; (void)lat; (void)lon; (void)alt; (void)heading; (void)speed; return "not supported"; }
        /** Bases of @p kind ("airport", "base" or "" for all) near a point, nearest first; body empty means the flown ship's body. */
        virtual std::string list_facilities(const std::string &kind, const std::string &body, double lat, double lon, double radius_km, std::size_t max, std::vector<Facility> &out) { (void)kind; (void)body; (void)lat; (void)lon; (void)radius_km; (void)max; (void)out; return "not supported"; }
    };

    /** Decides what an addon may do: asked every poll so that a change the player makes takes effect at once. */
    class LinkPolicy
    {
    public:
        virtual ~LinkPolicy() = default;
        /** The capabilities to grant @p addon, which asked for @p requested; @p token is the one it presented (empty when none). */
        virtual std::uint32_t granted(const std::string &addon, const std::string &token, std::uint32_t requested) = 0;
        /** Called when an addon says hello, so a window can list it. */
        virtual void seen(const std::string &addon, const std::string &name, std::uint32_t requested) { (void)addon; (void)name; (void)requested; }
    };

    struct ClientInfo
    {
        int id = 0;
        std::string addon, name, version;
        std::uint32_t requested = 0, granted = 0;
        std::size_t subscriptions = 0;
        double idle = 0; // seconds since the program last sent anything
    };

    /** An MFD page an addon draws. */
    struct Page
    {
        std::string addon, id;
        std::vector<DrawCommand> commands;
        std::chrono::steady_clock::time_point wanted{}, notified{};
        bool drawn = false;
    };

    /**
     * The game side of KosmosLink: listens on the loopback interface, speaks the protocol of link_protocol.h to any number of
     * addon programs, and reads and writes the property tree on their behalf. Everything runs on the thread that calls poll().
     */
    class LinkServer
    {
    public:
        LinkServer(sl::PropertyTree &props, LinkHost &host, LinkPolicy *policy);
        ~LinkServer();

        /** Starts listening on 127.0.0.1:@p port (0 picks a free port). */
        bool start(std::uint16_t port);
        void stop();
        bool running() const;
        std::uint16_t port() const;

        /** Accepts connections, reads and answers requests, and tells clients whose grants changed. */
        void poll();
        /** Pushes changed subscribed properties and asks addons to redraw the pages that are on screen. */
        void flush();
        /** Sends an event: the always-on ones (ship, mfd, granted, shutdown) to every client, the others only to clients that subscribed. */
        void broadcast_event(const std::string &name, const std::string &arg);
        /** Whether any client subscribed to the opt-in event @p name. */
        bool wants_event(const std::string &name) const;

        std::vector<ClientInfo> clients() const;

        /** Seconds of silence after which a protocol 3 program is pinged and, one timeout later, dropped; 0 turns this off. */
        void set_timeout(double seconds) { timeout_ = seconds; }

        /**
         * Lockstep: when a program asked for it, tells it a step is due and waits (at most its limit) for step_done. Returns the
         * fixed step length the program asked for, or 0. Call once per frame before the simulation steps.
         */
        double lockstep_gate(double frame, double sim_time);
        bool lockstep_active() const;
        std::uint32_t lockstep_missed() const { return missed_; }

        std::vector<UiWindow> ui_windows() const;
        /** Tells the program that owns a window its widget changed (or the window was closed with widget empty). */
        void ui_event(const std::string &addon, const std::string &window, const std::string &widget, const std::string &value);
        /** The 3D layers of all connected programs. */
        std::vector<const Layer3 *> layers() const;
        /** The page an addon draws, or null. */
        Page *page(const std::string &addon, const std::string &id);
        /** Whether the addon that owns a page is still connected. */
        bool page_live(const std::string &addon) const;

    private:
        struct Client;
        void handle(Client &c, const Frame &f);
        void on_hello(Client &c, const Frame &f, Reader &r);
        void on_ping(Client &c, const Frame &f, Reader &r);
        void on_log(Client &c, const Frame &f, Reader &r);
        void on_get(Client &c, const Frame &f, Reader &r);
        void on_subscribe(Client &c, const Frame &f, Reader &r);
        void on_unsubscribe(Client &c, const Frame &f, Reader &r);
        void on_set(Client &c, const Frame &f, Reader &r);
        void on_define_key(Client &c, const Frame &f, Reader &r);
        void on_spawn_ship(Client &c, const Frame &f, Reader &r);
        void on_release(Client &c, const Frame &f, Reader &r);
        void on_fire(Client &c, const Frame &f, Reader &r);
        void on_give(Client &c, const Frame &f, Reader &r);
        void on_define_mfd(Client &c, const Frame &f, Reader &r);
        void on_draw(Client &c, const Frame &f, Reader &r);
        void on_weather_at(Client &c, const Frame &f, Reader &r);
        void on_set_weather(Client &c, const Frame &f, Reader &r);
        void on_event_subscription(Client &c, const Frame &f, Reader &r);
        void on_list_ships(Client &c, const Frame &f, Reader &r);
        void on_remove_ship(Client &c, const Frame &f, Reader &r);
        void on_place_ship(Client &c, const Frame &f, Reader &r);
        void on_list_facilities(Client &c, const Frame &f, Reader &r);
        void on_ui_window(Client &c, const Frame &f, Reader &r);
        void on_ui_close(Client &c, const Frame &f, Reader &r);
        void on_ui_notify(Client &c, const Frame &f, Reader &r);
        void on_lockstep(Client &c, const Frame &f, Reader &r);
        void on_step_done(Client &c, const Frame &f, Reader &r);
        void on_claim_ship(Client &c, const Frame &f, Reader &r);
        void on_release_ship(Client &c, const Frame &f, Reader &r);
        void on_ship_value(Client &c, const Frame &f, Reader &r);
        void on_ship_force(Client &c, const Frame &f, Reader &r);
        void on_draw3d(Client &c, const Frame &f, Reader &r);
        void reject(Client &c, const Frame &f);
        bool allowed(Client &c, const Frame &f, std::uint32_t cap, const char *what);
        void reply_error(Client &c, std::uint32_t request, ErrorCode code, const std::string &message);
        void reply_ok(Client &c, std::uint32_t request, std::uint32_t handle);
        void send(Client &c, const std::string &frame);
        Value read(const std::string &path) const;
        std::string write(const Client &c, const std::string &path, const Value &value);
        bool may_write(const Client &c, const std::string &path) const;

        sl::PropertyTree &props_;
        LinkHost &host_;
        LinkPolicy *policy_;
        sl::TcpListener listener_;
        std::vector<std::unique_ptr<Client>> clients_;
        std::map<std::string, Page> pages_;
        std::map<std::string, UiWindow> windows_;
        std::map<std::string, Layer3> layers_;
        double timeout_ = 0.0;
        int lockstep_owner_ = 0;
        double lockstep_wait_ = 0.1, lockstep_dt_ = 0.0;
        std::uint32_t step_frame_ = 0, step_acked_ = 0, missed_ = 0;
        void drop(Client &c);
        int next_id_ = 1;
    };
} // namespace kosmos::link
