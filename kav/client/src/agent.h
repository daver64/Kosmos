#pragma once
// The KosmosAV program's logic without any user interface: it talks to the KosmosAV server and watches the game through a GameSource, so
// the same code runs under the GTK window and under the test that plays a fake game.
#include <string>
#include <vector>

namespace kav
{
    struct GameState
    {
        bool connected = false;
        std::string ship_name; // the flown ship's name as the game knows it
        bool landed = false;
        std::string base_id;   // the base the flown ship is at (within a few km), when it is at one
    };

    struct GameSource
    {
        virtual ~GameSource() = default;
        virtual GameState read() = 0;
        virtual void notify(const std::string &) {}
    };

    struct Config
    {
        std::string server; // e.g. https://example.org/kosmos/kav/app
        std::string key;    // the pilot's API key (shown on the website's account page)
    };

    struct Job
    {
        long id = 0, qty = 0, pay = 0, deadline = 0;
        double dv = 0;
        std::string from, to, kind;
    };

    struct Me
    {
        std::string callsign, location, ship;
        long credits = 0;
        std::vector<Job> jobs;
    };

    struct ShipType { std::string id, name; long cargo = 0, seats = 0, max_dv = 0, fee = 0; };
    struct Location { std::string id, name, body; };

    class Agent
    {
    public:
        Agent(Config cfg, GameSource *game) : cfg_(std::move(cfg)), game_(game)
        {
            while (!cfg_.server.empty() && cfg_.server.back() == '/')
                cfg_.server.pop_back();
        }

        bool load_registry();
        bool fetch_me(Me &out);
        bool fetch_jobs(const std::string &from, std::vector<Job> &out);
        // Each returns an empty string when it worked, else why not.
        std::string accept(long id, const std::string &ship);
        std::string abandon(long id);

        // Call about once a second: reads the game, keeps the server told where the pilot is and completes jobs that have arrived.
        // Returns messages worth showing to the pilot.
        std::vector<std::string> tick(long now);

        const std::vector<ShipType> &ships() const { return ships_; }
        const std::vector<Location> &locations() const { return locations_; }
        std::string location_name(const std::string &id) const;
        std::string ship_name(const std::string &id) const;
        const Me &me() const { return me_; }
        const GameState &game_state() const { return state_; }
        const std::string &last_error() const { return error_; }
        bool server_ok() const { return ok_; }

    private:
        Config cfg_;
        GameSource *game_;
        std::vector<ShipType> ships_;
        std::vector<Location> locations_;
        Me me_;
        GameState state_;
        std::string error_, last_ship_, last_location_;
        long last_beat_ = 0, last_me_ = 0;
        std::vector<std::pair<long, long>> tried_; // job id, time of the last attempt to complete it
        std::string last_complete_error_;
        bool ok_ = false;

        std::string post(const std::string &path, const std::string &form, long *credits = nullptr);
    };
}
