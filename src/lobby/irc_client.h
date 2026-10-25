#pragma once

// A small IRC client for the exlobby example, built on sl::TcpSocket. It speaks plain text IRC (no TLS) and only
// covers what a lobby needs: register, join one channel, track who is in it, send and receive channel messages.

#include "net.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace lobby
{
    struct IrcMessage
    {
        std::string prefix;
        std::string command;
        std::vector<std::string> params;

        /** The nick part of a "nick!user@host" prefix. */
        std::string nick() const { return prefix.substr(0, prefix.find('!')); }
    };

    inline std::string lower(std::string text)
    {
        for (char &c : text)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        return text;
    }

    /** Remove control characters (IRC colour and formatting codes included) so text is safe to display. */
    inline std::string clean_text(const std::string &text)
    {
        std::string out;
        out.reserve(text.size());
        for (char c : text)
            if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7f)
                out.push_back(c);
        return out;
    }

    /** Parse one IRC line (RFC 1459 with optional IRCv3 tags). Returns false if the line has no command. */
    inline bool parse_irc_line(std::string line, IrcMessage &out)
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        out = IrcMessage();
        std::size_t pos = 0;
        const auto skip_spaces = [&]
        {
            while (pos < line.size() && line[pos] == ' ')
                ++pos;
        };
        if (pos < line.size() && line[pos] == '@')
        {
            pos = line.find(' ', pos);
            if (pos == std::string::npos)
                return false;
        }
        skip_spaces();
        if (pos < line.size() && line[pos] == ':')
        {
            const std::size_t end = line.find(' ', pos);
            if (end == std::string::npos)
                return false;
            out.prefix = line.substr(pos + 1, end - pos - 1);
            pos = end;
        }
        skip_spaces();
        std::size_t end = line.find(' ', pos);
        out.command = line.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        if (out.command.empty())
            return false;
        for (char &c : out.command)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        pos = end == std::string::npos ? line.size() : end;
        while (out.params.size() < 15)
        {
            skip_spaces();
            if (pos >= line.size())
                break;
            if (line[pos] == ':')
            {
                out.params.push_back(line.substr(pos + 1));
                break;
            }
            end = line.find(' ', pos);
            out.params.push_back(line.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
            if (end == std::string::npos)
                break;
            pos = end;
        }
        return true;
    }

    /** Reduce a requested nick to characters IRC accepts. */
    inline std::string make_nick(const std::string &wanted)
    {
        std::string nick;
        for (char c : wanted)
        {
            const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            const bool digit = c >= '0' && c <= '9';
            const bool special = std::string("[]\\`_^{|}-").find(c) != std::string::npos;
            if (letter || (!nick.empty() && (digit || special)) || (nick.empty() && special && c != '-'))
                nick.push_back(c);
        }
        if (nick.size() > 24)
            nick.resize(24);
        if (nick.empty())
            nick = "simlib" + std::to_string(1000 + std::rand() % 9000);
        return nick;
    }

    inline std::string make_channel(const std::string &wanted)
    {
        std::string channel;
        for (char c : wanted)
            if (static_cast<unsigned char>(c) > 0x20 && c != ',' && c != 0x7f)
                channel.push_back(c);
        if (channel.empty() || (channel[0] != '#' && channel[0] != '&'))
            channel.insert(channel.begin(), '#');
        if (channel.size() > 50)
            channel.resize(50);
        return channel;
    }

    class IrcClient
    {
    public:
        enum class State
        {
            disconnected,
            connecting,
            registering,
            online
        };

        /** Status lines for the user: connection progress, errors, joins and so on. */
        std::function<void(const std::string &)> on_status;
        /** A message (or /me action) in the channel. */
        std::function<void(const std::string &nick, const std::string &text, bool action)> on_message;
        /** Someone joined the channel. */
        std::function<void(const std::string &nick)> on_user_joined;
        /** Someone left the channel by parting, quitting, being kicked or renaming. */
        std::function<void(const std::string &nick)> on_user_left;

        State state() const { return state_; }
        const std::string &nick() const { return nick_; }
        const std::string &channel() const { return channel_; }
        const std::set<std::string> &users() const { return users_; }
        bool in_channel() const { return in_channel_; }

        bool connect(const std::string &host, std::uint16_t port, const std::string &nick, const std::string &channel)
        {
            disconnect("Reconnecting");
            nick_ = make_nick(nick);
            channel_ = make_channel(channel);
            status("Connecting to " + host + ":" + std::to_string(port) + " ...");
            if (!socket_.connect(host, port))
            {
                status(socket_.error().empty() ? "Unable to start the connection." : socket_.error());
                return false;
            }
            state_ = State::connecting;
            started_ = Clock::now();
            tokens_ = burst;
            last_refill_ = Clock::now();
            return true;
        }

        void disconnect(const std::string &reason = "Leaving")
        {
            if (state_ == State::registering || state_ == State::online)
            {
                socket_.send("QUIT :" + clean_text(reason) + "\r\n");
                socket_.update();
            }
            reset();
        }

        /** Send a channel message; long text is cut to fit an IRC line. Messages are rate limited. */
        bool say(const std::string &text)
        {
            std::string body = clean_text(text);
            if (state_ != State::online || !in_channel_ || body.empty())
                return false;
            if (body.size() > 400)
            {
                body.resize(400);
                // Do not end in the middle of a UTF-8 sequence.
                while (!body.empty() && (static_cast<unsigned char>(body.back()) & 0xC0) == 0x80)
                    body.pop_back();
                if (!body.empty() && static_cast<unsigned char>(body.back()) >= 0xC0)
                    body.pop_back();
            }
            if (outgoing_.size() >= 50)
                return false;
            outgoing_.push_back("PRIVMSG " + channel_ + " :" + body);
            return true;
        }

        void update()
        {
            if (state_ == State::disconnected)
                return;
            const sl::TcpState tcp = socket_.update();
            if (tcp == sl::TcpState::closed)
                return lost();
            if (state_ == State::connecting && tcp == sl::TcpState::connected)
            {
                status("Connected, registering as " + nick_ + " ...");
                send_raw("NICK " + nick_);
                send_raw("USER " + nick_ + " 0 * :simlib exlobby");
                state_ = State::registering;
            }
            if (tcp == sl::TcpState::connected)
            {
                std::string data;
                socket_.receive(data);
                input_ += data;
                process_input();
                if (state_ != State::disconnected && socket_.state() == sl::TcpState::closed)
                    return lost();
            }
            if (state_ == State::registering &&
                std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - started_).count() > 90)
            {
                status("The server did not complete registration.");
                reset();
                return;
            }
            drain_outgoing();
        }

    private:
        using Clock = std::chrono::steady_clock;
        static constexpr double burst = 4.0;

        void status(const std::string &text)
        {
            if (on_status)
                on_status(text);
        }

        void send_raw(std::string line)
        {
            line.erase(std::remove_if(line.begin(), line.end(), [](char c) { return c == '\r' || c == '\n' || c == '\0'; }),
                       line.end());
            if (line.size() > 510)
                line.resize(510);
            socket_.send(line + "\r\n");
        }

        void drain_outgoing()
        {
            const Clock::time_point now = Clock::now();
            tokens_ = std::min(burst, tokens_ + std::chrono::duration<double>(now - last_refill_).count());
            last_refill_ = now;
            while (!outgoing_.empty() && tokens_ >= 1.0 && state_ == State::online)
            {
                send_raw(outgoing_.front());
                outgoing_.pop_front();
                tokens_ -= 1.0;
            }
        }

        void reset()
        {
            socket_.close();
            state_ = State::disconnected;
            in_channel_ = false;
            users_.clear();
            input_.clear();
            outgoing_.clear();
        }

        void lost()
        {
            const std::string error = socket_.error();
            status(error.empty() ? "Disconnected." : error);
            reset();
        }

        void process_input()
        {
            std::size_t start = 0;
            for (std::size_t newline; (newline = input_.find('\n', start)) != std::string::npos; start = newline + 1)
            {
                IrcMessage message;
                if (newline - start <= 8192 && parse_irc_line(input_.substr(start, newline - start), message))
                    handle(message);
                if (state_ == State::disconnected)
                    return;
            }
            input_.erase(0, start);
            if (input_.size() > 16384)
                input_.clear(); // a line this long is not IRC
        }

        static std::string strip_rank(std::string name)
        {
            while (!name.empty() && std::string("@+%&~!").find(name[0]) != std::string::npos)
                name.erase(name.begin());
            return name;
        }

        void handle(const IrcMessage &message)
        {
            const std::string &command = message.command;
            const auto param = [&](std::size_t index) -> std::string
            { return index < message.params.size() ? message.params[index] : std::string(); };

            if (command == "PING")
            {
                send_raw("PONG :" + param(0));
            }
            else if (command == "001")
            {
                state_ = State::online;
                status("Registered. Joining " + channel_ + " ...");
                send_raw("JOIN " + channel_);
            }
            else if (command == "433" && state_ == State::registering)
            {
                nick_ = nick_.size() < 24 ? nick_ + "_" : make_nick("simlib");
                status("Nick in use, trying " + nick_);
                send_raw("NICK " + nick_);
            }
            else if (command == "353")
            {
                const std::string names = param(3);
                std::size_t pos = 0;
                while (pos < names.size())
                {
                    std::size_t end = names.find(' ', pos);
                    if (end == std::string::npos)
                        end = names.size();
                    const std::string name = strip_rank(names.substr(pos, end - pos));
                    if (!name.empty())
                        users_.insert(name);
                    pos = end + 1;
                }
            }
            else if (command == "JOIN")
            {
                if (lower(param(0)) != lower(channel_))
                    return;
                if (message.nick() == nick_)
                {
                    in_channel_ = true;
                    users_.insert(nick_);
                    status("Joined " + channel_);
                }
                else
                {
                    users_.insert(message.nick());
                    if (on_user_joined)
                        on_user_joined(message.nick());
                }
            }
            else if (command == "PART" || command == "QUIT")
            {
                if (command == "PART" && lower(param(0)) != lower(channel_))
                    return;
                if (message.nick() == nick_)
                {
                    in_channel_ = false;
                    users_.clear();
                    return;
                }
                users_.erase(message.nick());
                if (on_user_left)
                    on_user_left(message.nick());
            }
            else if (command == "KICK")
            {
                if (lower(param(0)) != lower(channel_))
                    return;
                if (param(1) == nick_)
                {
                    in_channel_ = false;
                    users_.clear();
                    status("You were kicked from " + channel_ + ": " + clean_text(param(2)));
                }
                else
                {
                    users_.erase(param(1));
                    if (on_user_left)
                        on_user_left(param(1));
                }
            }
            else if (command == "NICK")
            {
                const std::string old_nick = message.nick();
                const bool mine = old_nick == nick_;
                if (users_.erase(old_nick) > 0)
                    users_.insert(param(0));
                if (mine)
                    nick_ = param(0);
                else if (on_user_left)
                    on_user_left(old_nick); // listings are keyed by nick, so a rename drops them until re-announced
            }
            else if (command == "PRIVMSG")
            {
                if (lower(param(0)) != lower(channel_) || message.nick().empty())
                    return;
                std::string text = param(1);
                bool action = false;
                if (!text.empty() && text.front() == '\x01')
                {
                    // CTCP: show /me actions, ignore (and never answer) everything else.
                    static const std::string prefix = "\x01" "ACTION ";
                    if (text.compare(0, prefix.size(), prefix) != 0)
                        return;
                    action = true;
                    text = text.substr(prefix.size());
                    if (!text.empty() && text.back() == '\x01')
                        text.pop_back();
                }
                if (on_message)
                    on_message(clean_text(message.nick()), clean_text(text), action);
            }
            else if (command == "ERROR")
            {
                status("Server error: " + clean_text(param(0)));
                reset();
            }
            else if (command.size() == 3 && (command[0] == '4' || command[0] == '5') && message.params.size() >= 2)
            {
                status(command + ": " + clean_text(message.params.back()));
            }
        }

        sl::TcpSocket socket_;
        State state_ = State::disconnected;
        std::string nick_;
        std::string channel_;
        std::string input_;
        std::set<std::string> users_;
        std::deque<std::string> outgoing_;
        bool in_channel_ = false;
        double tokens_ = burst;
        Clock::time_point last_refill_ = Clock::now();
        Clock::time_point started_ = Clock::now();
    };

} // namespace lobby
