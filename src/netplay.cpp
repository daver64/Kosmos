#include "netplay.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace kosmos
{
    namespace
    {
        constexpr double send_interval = 1.0 / 20.0;
        constexpr double max_extrapolation = 0.3;
        constexpr double correction_time = 0.15;
        constexpr double ghost_timeout = 6.0;
        constexpr int announce_seconds = 30;

        bool finite3(const sl::dvec3 &v, double limit)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && glm::length(v) < limit;
        }
    } // namespace

    sl::dvec3 Ghost::shown_position() const
    {
        return state.position + state.velocity * std::min(age, max_extrapolation) + offset * std::exp(-age / correction_time);
    }

    std::string NetPlay::clean_nick(const std::string &text)
    {
        std::string out;
        for (char c : text)
        {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
            if (ok && out.size() < 16)
                out.push_back(c);
        }
        return out.empty() ? "pilot" : out;
    }

    void NetPlay::write_state(sl::PacketWriter &out, const GhostState &s)
    {
        out.write_string(s.ship);
        out.write<std::int32_t>(s.soi);
        out.write<std::uint8_t>(s.stage);
        out.write<std::uint8_t>(s.engine ? 1 : 0);
        out.write<float>(s.throttle);
        out.write<std::uint8_t>(s.fire);
        for (int i = 0; i < 3; ++i)
            out.write<double>(s.position[i]);
        for (int i = 0; i < 3; ++i)
            out.write<double>(s.velocity[i]);
        out.write<double>(s.orientation.w);
        out.write<double>(s.orientation.x);
        out.write<double>(s.orientation.y);
        out.write<double>(s.orientation.z);
    }

    bool NetPlay::read_state(sl::PacketReader &in, GhostState &s)
    {
        std::uint8_t engine = 0;
        double q[4] = {1, 0, 0, 0};
        bool ok = in.read_string(s.ship) && in.read(s.soi) && in.read(s.stage) && in.read(engine) && in.read(s.throttle) && in.read(s.fire);
        for (int i = 0; ok && i < 3; ++i)
            ok = in.read(s.position[i]);
        for (int i = 0; ok && i < 3; ++i)
            ok = in.read(s.velocity[i]);
        for (int i = 0; ok && i < 4; ++i)
            ok = in.read(q[i]);
        if (!ok || s.ship.size() > 64 || s.soi < 0 || s.soi > 100000 || !finite3(s.position, 1.0e16) || !finite3(s.velocity, 1.0e9) ||
            !std::isfinite(s.throttle))
            return false;
        const double norm = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        if (!std::isfinite(norm) || norm < 1.0e-6)
            return false;
        s.orientation = sl::dquat(q[0] / norm, q[1] / norm, q[2] / norm, q[3] / norm);
        s.engine = engine != 0;
        s.fire &= 3;
        s.throttle = std::clamp(s.throttle, 0.0f, 1.0f);
        return true;
    }

    void NetPlay::add_chat(const std::string &line)
    {
        chat.push_back(line);
        if (chat.size() > 200)
            chat.erase(chat.begin(), chat.begin() + 50);
    }

    bool NetPlay::host(std::uint16_t port, const std::string &nick, int system)
    {
        leave();
        error_.clear();
        if (!server_.start(port, max_players - 1, 2))
        {
            error_ = "Unable to listen on UDP port " + std::to_string(port);
            return false;
        }
        mode_ = Mode::host;
        nick_ = clean_nick(nick);
        system_ = system;
        my_id_ = 0;
        next_id_ = 1;
        roster_.assign(1, Member{0, nick_});
        announce_now_ = true;
        return true;
    }

    bool NetPlay::join(const std::string &address, std::uint16_t port, const std::string &nick, int system)
    {
        leave();
        error_.clear();
        if (!client_.connect(address, port, 2))
        {
            error_ = "Unable to connect to " + address + ":" + std::to_string(port);
            return false;
        }
        mode_ = Mode::client;
        nick_ = clean_nick(nick);
        system_ = system;
        return true;
    }

    void NetPlay::leave()
    {
        if (mode_ == Mode::host && irc.state() == lobby::IrcClient::State::online)
            irc.say("!close");
        server_.stop();
        client_.disconnect();
        mode_ = Mode::none;
        my_id_ = invalid_id;
        peers_.clear();
        roster_.clear();
        ghosts_.clear();
        send_timer_ = 0.0;
    }

    void NetPlay::publish(const GhostState &state)
    {
        if (!joined() || roster_.size() < 2 || send_timer_ < send_interval)
            return;
        send_timer_ = 0.0;
        sl::PacketWriter out;
        if (mode_ == Mode::host)
        {
            out.write(Msg::relay);
            out.write<std::uint32_t>(0);
            write_state(out, state);
            for (const Peer &p : peers_)
                if (p.greeted)
                    server_.send(p.peer, out.bytes(), 1, false);
        }
        else
        {
            out.write(Msg::state);
            write_state(out, state);
            client_.send(out.bytes(), 1, false);
        }
    }

    void NetPlay::update(double dt)
    {
        send_timer_ += dt;
        if (mode_ == Mode::host)
            update_host(dt);
        else if (mode_ == Mode::client)
            update_client(dt);
        advance_ghosts(dt);
    }

    void NetPlay::update_host(double)
    {
        sl::NetEvent event;
        while (server_.poll(event))
        {
            if (event.type == sl::NetEventType::connected)
            {
                peers_.push_back(Peer{event.peer, invalid_id, {}, false});
            }
            else if (event.type == sl::NetEventType::disconnected)
            {
                const auto it = std::find_if(peers_.begin(), peers_.end(), [&](const Peer &p) { return p.peer == event.peer; });
                if (it == peers_.end())
                    continue;
                if (it->greeted)
                {
                    add_chat("* " + it->nick + " left the game");
                    roster_.erase(std::remove_if(roster_.begin(), roster_.end(), [&](const Member &m) { return m.id == it->id; }), roster_.end());
                    peers_.erase(it);
                    send_roster();
                    rebuild_ghosts_from_roster();
                }
                else
                    peers_.erase(it);
            }
            else if (event.type == sl::NetEventType::received)
            {
                const auto it = std::find_if(peers_.begin(), peers_.end(), [&](const Peer &p) { return p.peer == event.peer; });
                if (it == peers_.end())
                    continue;
                sl::PacketReader reader(event.data);
                Msg msg{};
                if (!reader.read(msg))
                    continue;
                if (msg == Msg::hello && !it->greeted)
                {
                    std::uint16_t version = 0;
                    std::int32_t system = 0;
                    std::string nick;
                    if (!reader.read(version) || !reader.read(system) || !reader.read_string(nick))
                        continue;
                    const char *refuse = nullptr;
                    if (version != protocol)
                        refuse = "Different Kosmos network version";
                    else if (system != system_)
                        refuse = "Host is in a different star system";
                    else if (static_cast<int>(roster_.size()) >= max_players)
                        refuse = "Game is full";
                    if (refuse)
                    {
                        sl::PacketWriter out;
                        out.write(Msg::reject);
                        out.write_string(refuse);
                        server_.send(it->peer, out.bytes(), 0, true);
                        server_.disconnect(it->peer);
                        continue;
                    }
                    it->nick = clean_nick(nick);
                    for (const Member &m : roster_)
                        if (m.nick == it->nick)
                            it->nick += "_" + std::to_string(next_id_);
                    it->id = next_id_++;
                    it->greeted = true;
                    roster_.push_back(Member{it->id, it->nick});
                    sl::PacketWriter out;
                    out.write(Msg::welcome);
                    out.write<std::uint32_t>(it->id);
                    server_.send(it->peer, out.bytes(), 0, true);
                    add_chat("* " + it->nick + " joined the game");
                    send_roster();
                    rebuild_ghosts_from_roster();
                }
                else if (msg == Msg::state && it->greeted)
                {
                    GhostState state;
                    if (!read_state(reader, state))
                        continue;
                    apply_state(it->id, state);
                    sl::PacketWriter out;
                    out.write(Msg::relay);
                    out.write<std::uint32_t>(it->id);
                    write_state(out, state);
                    for (const Peer &other : peers_)
                        if (other.greeted && other.peer != it->peer)
                            server_.send(other.peer, out.bytes(), 1, false);
                }
            }
        }
    }

    void NetPlay::update_client(double)
    {
        sl::NetEvent event;
        while (client_.poll(event))
        {
            if (event.type == sl::NetEventType::connected)
            {
                sl::PacketWriter out;
                out.write(Msg::hello);
                out.write<std::uint16_t>(protocol);
                out.write<std::int32_t>(system_);
                out.write_string(nick_);
                client_.send(out.bytes(), 0, true);
            }
            else if (event.type == sl::NetEventType::disconnected)
            {
                const bool was_joined = joined();
                if (error_.empty())
                    error_ = was_joined ? "Disconnected from the host" : "Unable to connect to the host";
                const std::string keep = error_;
                leave();
                error_ = keep;
                return;
            }
            else if (event.type == sl::NetEventType::received)
            {
                sl::PacketReader reader(event.data);
                Msg msg{};
                if (!reader.read(msg))
                    continue;
                if (msg == Msg::welcome)
                {
                    std::uint32_t id = 0;
                    if (reader.read(id))
                        my_id_ = id;
                }
                else if (msg == Msg::reject)
                {
                    std::string reason;
                    reader.read_string(reason);
                    error_ = reason.empty() ? "The host refused the connection" : reason.substr(0, 80);
                    client_.disconnect();
                    mode_ = Mode::none;
                    my_id_ = invalid_id;
                    return;
                }
                else if (msg == Msg::roster && joined())
                {
                    std::uint8_t count = 0;
                    if (!reader.read(count) || count > max_players)
                        continue;
                    std::vector<Member> roster;
                    for (int i = 0; i < count; ++i)
                    {
                        Member m;
                        if (!reader.read(m.id) || !reader.read_string(m.nick))
                            break;
                        m.nick = clean_nick(m.nick);
                        roster.push_back(m);
                    }
                    if (static_cast<int>(roster.size()) == count)
                    {
                        roster_ = roster;
                        rebuild_ghosts_from_roster();
                    }
                }
                else if (msg == Msg::relay && joined())
                {
                    std::uint32_t id = 0;
                    GhostState state;
                    if (reader.read(id) && id != my_id_ && read_state(reader, state))
                        apply_state(id, state);
                }
            }
        }
    }

    void NetPlay::send_roster()
    {
        sl::PacketWriter out;
        out.write(Msg::roster);
        out.write<std::uint8_t>(static_cast<std::uint8_t>(roster_.size()));
        for (const Member &m : roster_)
        {
            out.write<std::uint32_t>(m.id);
            out.write_string(m.nick);
        }
        for (const Peer &p : peers_)
            if (p.greeted)
                server_.send(p.peer, out.bytes(), 0, true);
    }

    void NetPlay::rebuild_ghosts_from_roster()
    {
        std::vector<Ghost> next;
        for (const Member &m : roster_)
        {
            if (m.id == my_id_)
                continue;
            const auto it = std::find_if(ghosts_.begin(), ghosts_.end(), [&](const Ghost &g) { return g.id == m.id; });
            Ghost g;
            if (it != ghosts_.end())
                g = *it;
            g.id = m.id;
            g.nick = m.nick;
            next.push_back(g);
        }
        ghosts_ = next;
    }

    void NetPlay::apply_state(std::uint32_t id, const GhostState &state)
    {
        auto it = std::find_if(ghosts_.begin(), ghosts_.end(), [&](const Ghost &g) { return g.id == id; });
        if (it == ghosts_.end())
        {
            Ghost g;
            g.id = id;
            g.nick = "player";
            for (const Member &m : roster_)
                if (m.id == id)
                    g.nick = m.nick;
            ghosts_.push_back(g);
            it = ghosts_.end() - 1;
        }
        Ghost &g = *it;
        if (g.have_state && g.state.soi == state.soi && g.age < 2.0)
            g.offset = g.shown_position() - state.position;
        else
        {
            g.offset = sl::dvec3(0.0);
            g.shown = state.orientation;
        }
        g.state = state;
        g.have_state = true;
        g.age = 0.0;
    }

    void NetPlay::advance_ghosts(double dt)
    {
        for (Ghost &g : ghosts_)
        {
            g.age += dt;
            if (g.have_state)
                g.shown = glm::slerp(g.shown, g.state.orientation, std::min(1.0, dt * 12.0));
        }
    }

    std::string NetPlay::status() const
    {
        switch (mode_)
        {
        case Mode::none: return "Offline";
        case Mode::host: return "Hosting on UDP " + std::to_string(server_.port()) + ": " + std::to_string(roster_.size()) + " of " + std::to_string(max_players) + " players";
        case Mode::client:
            if (!joined())
                return "Connecting ...";
            return "Connected, " + std::to_string(roster_.size()) + " players, ping " + std::to_string(client_.ping_ms()) + " ms";
        }
        return {};
    }

    std::vector<std::string> NetPlay::player_names() const
    {
        std::vector<std::string> names;
        if (!joined())
            return names;
        for (const Member &m : roster_)
            if (m.id == my_id_)
                names.insert(names.begin(), m.nick);
            else
                names.push_back(m.nick);
        return names;
    }

    void NetPlay::lobby_connect(const std::string &host, std::uint16_t port, const std::string &nick, const std::string &channel)
    {
        irc.on_status = [this](const std::string &text) { add_chat("* " + text); };
        irc.on_message = [this](const std::string &from, const std::string &text, bool action)
        {
            bool wants_announce = false;
            if (!action && board.handle_message(from, text, wants_announce))
            {
                if (wants_announce)
                    announce_now_ = true;
                return;
            }
            add_chat(action ? "* " + from + " " + text : "<" + from + "> " + text);
        };
        irc.on_user_joined = [this](const std::string &) { announce_now_ = true; };
        irc.on_user_left = [this](const std::string &from) { board.remove(from); };
        board.clear();
        announced_once_ = false;
        irc.connect(host, port, clean_nick(nick), channel);
    }

    void NetPlay::lobby_disconnect()
    {
        if (mode_ == Mode::host && irc.state() == lobby::IrcClient::State::online)
            irc.say("!close");
        irc.disconnect();
        board.clear();
    }

    void NetPlay::lobby_update(const std::string &announce_address, const std::string &title)
    {
        irc.update();
        board.expire();
        if (irc.state() != lobby::IrcClient::State::online || !irc.in_channel())
            return;
        if (!announced_once_)
        {
            announced_once_ = true;
            irc.say("!games");
        }
        if (mode_ == Mode::host && joined() && lobby::valid_address(announce_address))
        {
            const auto now = Clock::now();
            if (announce_now_ || now - announced_ > std::chrono::seconds(announce_seconds))
            {
                announce_now_ = false;
                announced_ = now;
                irc.say(lobby::make_host_command(announce_address, server_.port(), max_players, title.empty() ? "Kosmos" : title));
            }
        }
    }
} // namespace kosmos
