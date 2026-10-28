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
        virtual std::string give_equipment(const std::string &id, int count) { (void)id; (void)count; return "not supported"; }
        /** Adds an MFD page. The page's drawing arrives through LinkServer::page(). */
        virtual std::string define_mfd(const std::string &addon, const std::string &id, const std::string &title) { (void)addon; (void)id; (void)title; return "not supported"; }
        virtual void log(const std::string &addon, const std::string &text) { (void)addon; (void)text; }

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
        /** The page an addon draws, or null. */
        Page *page(const std::string &addon, const std::string &id);
        /** Whether the addon that owns a page is still connected. */
        bool page_live(const std::string &addon) const;

    private:
        struct Client;
        void handle(Client &c, const Frame &f);
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
        int next_id_ = 1;
    };
} // namespace kosmos::link
