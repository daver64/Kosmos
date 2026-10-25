#pragma once

// Multiplayer for Kosmos: "ghost ships". Every player flies their own simulation and broadcasts where their ship is; the
// others see it as a moving ghost. There is no interaction between ships.
//
// One player hosts (a sl::NetServer, UDP) and relays everybody's state; the others join (a sl::NetClient). Games are found
// in an IRC channel (see lobby/lobby.h) or by typing host:port. Channel 0 is reliable (hello, welcome, roster) and channel 1
// is unreliable (ship state, about 20 per second).
//
// A ship's state is sent relative to the body whose sphere of influence holds it, so it stays accurate far from the origin
// and does not depend on the players' clocks agreeing.

#include "lobby/irc_client.h"
#include "lobby/lobby.h"
#include "net.h"
#include "flight.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace kosmos
{
    /** What is sent about a ship. Positions are relative to the centre of body `soi`, in world axes. */
    struct GhostState
    {
        std::string ship; // the ship spec's name
        std::int32_t soi = 0;
        std::uint8_t stage = 0;
        bool engine = false;
        float throttle = 0.0f;
        sl::dvec3 position{0.0}, velocity{0.0};
        sl::dquat orientation{1.0, 0.0, 0.0, 0.0};
    };

    /** Another player's ship as this player sees it. */
    struct Ghost
    {
        std::uint32_t id = 0;
        std::string nick;
        GhostState state;
        bool have_state = false;
        /** Seconds since the last packet arrived. */
        double age = 0.0;
        /** Shown pose: it follows the extrapolated packet state and glides over corrections. */
        sl::dvec3 offset{0.0};
        sl::dquat shown{1.0, 0.0, 0.0, 0.0};
        /** The position relative to body `state.soi` to draw now. */
        sl::dvec3 shown_position() const;
    };

    class NetPlay
    {
    public:
        enum class Mode
        {
            none,
            host,
            client
        };
        static constexpr std::uint16_t protocol = 1;
        static constexpr std::uint16_t default_port = 7777;
        static constexpr int max_players = 4;

        Mode mode() const { return mode_; }
        /** True when hosting, or once a joined client has received the host's welcome. */
        bool joined() const { return mode_ == Mode::host || (mode_ == Mode::client && my_id_ != invalid_id); }
        /** Players including this one; 1 when offline. */
        int player_count() const { return joined() ? static_cast<int>(roster_.size()) : 1; }
        /** True while others are in the game, which is when the time warp is held at 1x. */
        bool shared() const { return player_count() >= 2; }
        const std::vector<Ghost> &ghosts() const { return ghosts_; }
        const std::string &error() const { return error_; }
        const std::string &nick() const { return nick_; }
        std::uint16_t port() const { return server_.port(); }
        std::uint32_t ping_ms() const { return mode_ == Mode::client ? client_.ping_ms() : 0; }

        bool host(std::uint16_t port, const std::string &nick, int system);
        bool join(const std::string &address, std::uint16_t port, const std::string &nick, int system);
        void leave();

        /** Service the network. Call every frame with the frame time. */
        void update(double frame_seconds);
        /** Offer the local ship's state; it is sent at most 20 times a second. */
        void publish(const GhostState &state);
        /** Text for the HUD and window, e.g. "Hosting on 7777: 2 players". */
        std::string status() const;
        /** Names of the players, this one first. */
        std::vector<std::string> player_names() const;

        // ---- the IRC lobby ----
        lobby::IrcClient irc;
        lobby::GameBoard board;
        std::vector<std::string> chat;
        /** Connect to the IRC server and channel to find or announce games. */
        void lobby_connect(const std::string &host, std::uint16_t port, const std::string &nick, const std::string &channel);
        void lobby_disconnect();
        /** Announce the game now and then, expire old listings and read IRC. Call every frame. */
        void lobby_update(const std::string &announce_address, const std::string &title);

        static std::string clean_nick(const std::string &text);

    private:
        enum class Msg : std::uint8_t
        {
            hello = 1,
            state = 2,
            welcome = 10,
            reject = 11,
            roster = 12,
            relay = 13
        };
        static constexpr std::uint32_t invalid_id = 0xFFFFFFFFu;
        using Clock = std::chrono::steady_clock;

        struct Peer
        {
            std::uint32_t peer = 0, id = 0;
            std::string nick;
            bool greeted = false;
        };

        void update_host(double dt);
        void update_client(double dt);
        void handle_state(std::uint32_t from, sl::PacketReader &reader);
        void apply_state(std::uint32_t id, const GhostState &state);
        void send_roster();
        void rebuild_ghosts_from_roster();
        void advance_ghosts(double dt);
        static void write_state(sl::PacketWriter &out, const GhostState &state);
        static bool read_state(sl::PacketReader &in, GhostState &state);
        void add_chat(const std::string &line);

        Mode mode_ = Mode::none;
        sl::NetServer server_;
        sl::NetClient client_;
        std::vector<Peer> peers_;
        struct Member
        {
            std::uint32_t id = 0;
            std::string nick;
        };
        std::vector<Member> roster_; // everybody, including this player
        std::vector<Ghost> ghosts_;
        std::uint32_t my_id_ = invalid_id, next_id_ = 1;
        std::string nick_, error_;
        int system_ = 0;
        double send_timer_ = 0.0;
        Clock::time_point announced_ = Clock::now() - std::chrono::hours(1);
        bool announce_now_ = false;
        bool announced_once_ = false;
    };
} // namespace kosmos
