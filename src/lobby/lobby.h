#pragma once

// The game board for exlobby. Hosts announce games in the IRC channel with plain text commands:
//   !host <address>:<port> <max players> <title>   announce or update a game
//   !close                                         remove the announcement
//   !games                                         ask hosts to announce again
// Everything received from IRC is untrusted, so announcements are validated before they are listed.

#include "irc_client.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace lobby
{
    struct GameListing
    {
        std::string host_nick;
        std::string address;
        std::uint16_t port = 0;
        int max_players = 2;
        std::string title;
        std::chrono::steady_clock::time_point last_seen;
    };

    inline bool valid_address(const std::string &address)
    {
        if (address.empty() || address.size() > 253)
            return false;
        for (char c : address)
        {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                            c == '-' || c == ':';
            if (!ok)
                return false;
        }
        return true;
    }

    /** Parse "address:port" (the last colon separates the port, so bare IPv6 is not supported). */
    inline bool split_endpoint(const std::string &text, std::string &address, std::uint16_t &port)
    {
        const std::size_t colon = text.rfind(':');
        if (colon == std::string::npos || colon == 0 || colon + 1 >= text.size() || colon + 6 < text.size())
            return false;
        int value = 0;
        for (std::size_t i = colon + 1; i < text.size(); ++i)
        {
            if (text[i] < '0' || text[i] > '9')
                return false;
            value = value * 10 + (text[i] - '0');
        }
        address = text.substr(0, colon);
        if (value < 1 || value > 65535 || !valid_address(address) || address.find(':') != std::string::npos)
            return false;
        port = static_cast<std::uint16_t>(value);
        return true;
    }

    inline std::string make_host_command(const std::string &address, std::uint16_t port, int max_players,
                                         const std::string &title)
    {
        return "!host " + address + ":" + std::to_string(port) + " " + std::to_string(max_players) + " " + title;
    }

    class GameBoard
    {
    public:
        static constexpr int expiry_seconds = 90;

        /** Handle a channel message. Returns true if it was a lobby command (so the UI can hide it from chat). */
        bool handle_message(const std::string &nick, const std::string &text, bool &wants_announce)
        {
            wants_announce = false;
            if (text.empty() || text[0] != '!')
                return false;
            std::istringstream words(text);
            std::string command;
            words >> command;
            command = lower(command);
            if (command == "!games")
            {
                wants_announce = true;
                return true;
            }
            if (command == "!close")
            {
                remove(nick);
                return true;
            }
            if (command != "!host")
                return false;
            std::string endpoint;
            int max_players = 0;
            words >> endpoint >> max_players;
            std::string title;
            std::getline(words, title);
            const std::size_t first = title.find_first_not_of(' ');
            title = first == std::string::npos ? std::string() : title.substr(first);
            GameListing game;
            {
                if (split_endpoint(endpoint, game.address, game.port) && max_players >= 2 && max_players <= 16 &&
                    !title.empty() && title.size() <= 40)
                {
                    game.host_nick = nick;
                    game.max_players = max_players;
                    game.title = clean_text(title);
                    game.last_seen = std::chrono::steady_clock::now();
                    games_[lower(nick)] = game;
                }
            }
            return true;
        }

        void remove(const std::string &nick) { games_.erase(lower(nick)); }

        void clear() { games_.clear(); }

        /** Drop listings that have not been announced recently. */
        void expire(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now())
        {
            for (auto it = games_.begin(); it != games_.end();)
                it = (now - it->second.last_seen > std::chrono::seconds(expiry_seconds)) ? games_.erase(it) : std::next(it);
        }

        std::vector<GameListing> games() const
        {
            std::vector<GameListing> list;
            for (const auto &entry : games_)
                list.push_back(entry.second);
            return list;
        }

    private:
        std::map<std::string, GameListing> games_;
    };

} // namespace lobby
