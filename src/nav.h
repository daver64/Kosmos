#pragma once

#include "ship.h"
#include "universe.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace kosmos
{
    /** A ground transmitter fixed to a body's surface: a VOR, an NDB, an ILS localizer/glideslope, or a VTOL pad beacon. */
    struct Transmitter
    {
        std::string type; // "vor", "ndb", "ils" or "vtol"
        std::string id, name;
        /** Short code for the displays, like "EGCP VOR"; the name is used when empty. */
        std::string ident;
        std::string body;
        double latitude = 0.0, longitude = 0.0;
        /** MHz (an NDB is 0.19-0.535, i.e. kHz / 1000). A receiver locks on when tuned within 0.025 of this, or 0.0015 for an NDB. */
        double frequency = 0.0;
        /** Metres; the beacon is also hidden when the ship is below its horizon. */
        double range = 0.0;
        /** ils: the approach course to fly (degrees true). vtol: the pad's heading. */
        double heading = 0.0;
        /** ils: glideslope angle in degrees. */
        double glide = 3.0;
    };

    /** Reads {"navaids": [...]}. Appends to `out`. */
    bool load_navaids(const std::string &file, std::vector<Transmitter> &out, std::string &error);

    /** Unit vector from a body's centre to a latitude/longitude at the current time (matches ship telemetry). */
    sl::dvec3 surface_direction(const Universe &universe, int body, double latitude, double longitude);
    /** The inverse of surface_direction. */
    void surface_lat_lon(const Universe &universe, int body, const sl::dvec3 &direction, double &latitude, double &longitude);

    /**
     * Radio navigation and docking. Two NAV receivers on the active ship tune to transmitters by frequency (ground
     * VOR/ILS/VTOL beacons, other ships' docking ports (IDS) and transponders). Docking links are rigid: ships joined
     * through a pair of ports move as one, with the active ship (or the lowest-numbered member) leading.
     *
     * Properties: /nav/nav[0..1]/{frequency, obs, valid, type, name, distance, bearing, radial, ...}, /nav/item[k]/...,
     * /dock/{receiver, own-port, undock, valid, range, x, y, z, vx, vy, vz, yaw, pitch, roll, docked, partner, ...}.
     */
    class NavSystem
    {
    public:
        static constexpr int receivers = 2;

        void init(sl::PropertyTree &props);
        void set_beacons(std::vector<Transmitter> beacons) { beacons_ = std::move(beacons); }
        /** Adds a beacon, replacing any with the same id. */
        void add_beacon(const Transmitter &beacon);
        const std::vector<Transmitter> &beacons() const { return beacons_; }
        void clear_links();
        /** Beacons are added when bases are set up; a system change drops the ones past `count`. */
        std::size_t beacon_count() const { return beacons_.size(); }
        void truncate_beacons(std::size_t count) { if (beacons_.size() > count) beacons_.resize(count); }

        /** After the world has stepped: make new dock captures and move docked ships rigidly with their leader. */
        void couple(Universe &universe, const std::vector<std::unique_ptr<Ship>> &ships, int active, double dt);
        /** Moves propellant between docked ships by /dock/transfer: "auto" (tank pods top up what they are docked to), "off", "give" (flown ship to its partners) or "take". */
        void transfer_propellant(const std::vector<std::unique_ptr<Ship>> &ships, int active, double dt);
        /** After telemetry is refreshed: tune the receivers and publish /nav and /dock. */
        void update(Universe &universe, const std::vector<std::unique_ptr<Ship>> &ships, int active);
        /** Release every dock of a ship (or just one port) and push the pair gently apart. */
        bool undock(const std::vector<std::unique_ptr<Ship>> &ships, int ship, int port = -1);
        bool docked(int ship) const;
        /** The current docks as {ship a, port on a, ship b, port on b}, for saving a session. */
        std::vector<std::array<int, 4>> dock_links() const
        {
            std::vector<std::array<int, 4>> out;
            for (const Link &l : links_)
                out.push_back({l.a, l.pa, l.b, l.pb});
            return out;
        }
        /** Joins two ships through the given ports without needing a capture (scenarios that start docked). */
        void link(int a, int port_a, int b, int port_b) { links_.push_back({a, port_a, b, port_b}); }

    private:
        struct Link
        {
            int a, pa, b, pb;
        };
        struct Lock
        {
            int a, b;
            double until;
        };
        struct Frame
        {
            sl::dvec3 position{0.0}, velocity{0.0}, up{0.0, 1.0, 0.0}, east{1.0, 0.0, 0.0}, north{0.0, 0.0, 1.0};
        };
        struct Contact
        {
            std::string type, name, ident;
            double frequency = 0.0, distance = 0.0;
            int ship = -1, port = -1;
            const Transmitter *beacon = nullptr;
            Frame frame;
        };

        std::vector<Contact> gather(const Universe &universe, const std::vector<std::unique_ptr<Ship>> &ships, int active) const;
        void publish_receiver(int index, const Contact *contact, const Universe &universe, const std::vector<std::unique_ptr<Ship>> &ships, Ship &me);
        void publish_docking(const Contact *contact, const std::vector<std::unique_ptr<Ship>> &ships, int active);
        bool port_busy(int ship, int port) const;
        bool locked(int a, int b) const;
        void place_follower(Ship &leader, int leader_port, Ship &follower, int follower_port);

        sl::PropertyTree *props_ = nullptr;
        std::vector<Transmitter> beacons_;
        std::vector<Link> links_;
        std::vector<Lock> locks_;
        double time_ = 0.0;
    };
} // namespace kosmos
