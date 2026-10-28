#include "multiplayer_ui.h"

namespace kosmos::app
{
bool snap_to_ghost(Sim &sim, Ship &ship, const kosmos::Ghost &ghost, const TerrainSet &terrains)
{
    const int soi = ghost.state.soi;
    if (!ghost.have_state || soi < 0 || soi >= static_cast<int>(sim.universe.bodies.size()))
        return false;
    const kosmos::Body &planet = sim.universe.bodies[static_cast<std::size_t>(soi)];
    const sl::dvec3 p = ghost.shown_position();
    const double r = glm::length(p);
    if (r < 1.0)
        return false;
    const sl::dvec3 up = p / r;
    const sl::dvec3 right = ghost.shown * sl::dvec3(1.0, 0.0, 0.0);
    const sl::dvec3 nose = ghost.shown * sl::dvec3(0.0, 0.0, -1.0);
    const sl::Terrain *terrain = terrains.find(soi);
    const double ground = terrain ? terrain->height_at_direction(up) : 0.0;
    const sl::dvec3 surface_velocity = ghost.state.velocity - glm::cross(sim.universe.pole(soi) * planet.spin, p);
    if (r - planet.radius - ground < 30.0 && glm::length(surface_velocity) < 5.0)
    {
        const sl::dvec3 beside = glm::normalize(p + right * 60.0);
        const sl::dvec3 east = glm::normalize(glm::cross(sim.universe.pole(soi), beside));
        const sl::dvec3 north = glm::cross(beside, east);
        const double heading = std::atan2(glm::dot(nose, east), glm::dot(nose, north)) * 180.0 / 3.14159265358979;
        ship.place_on_surface(soi, beside, heading, terrain ? terrain->height_at_direction(beside) : 0.0);
        return true;
    }
    const sl::dvec3 offset = ghost.shown * sl::dvec3(60.0, 25.0, 140.0);
    ship.place_relative(soi, p + offset, ghost.state.velocity, ghost.shown);
    return true;
}

void copy_text(char *dest, std::size_t size, const std::string &text)
{
    std::snprintf(dest, size, "%s", text.c_str());
}

void draw_multiplayer_window(kosmos::NetPlay &net, NetUi &ui, bool &open, int system, const std::function<void(const kosmos::Ghost &)> &snap)
{
    ImGui::SetNextWindowPos(ImVec2(340.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520.0f, 560.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);
    if (!ImGui::Begin("Multiplayer [F12]", &open))
    {
        ImGui::End();
        return;
    }
    const bool playing = net.mode() != kosmos::NetPlay::Mode::none;
    ImGui::TextUnformatted(net.status().c_str());
    if (!net.error().empty())
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.45f, 1.0f), "%s", net.error().c_str());
    ImGui::TextDisabled("Everyone flies their own ship and sees the others as ghosts. Time warp is held at 1x while two or more play.");

    ImGui::BeginDisabled(playing);
    ImGui::InputText("Your name", ui.nick, sizeof ui.nick);
    ImGui::EndDisabled();

    ImGui::SeparatorText("Game");
    if (!playing)
    {
        ImGui::InputText("Port", ui.game_port, sizeof ui.game_port, ImGuiInputTextFlags_CharsDecimal);
        ImGui::InputText("Game title", ui.title, sizeof ui.title);
        ImGui::InputText("Public address (shown to others)", ui.advertise, sizeof ui.advertise);
        if (ImGui::Button("Host a game"))
        {
            const int port = std::atoi(ui.game_port);
            if (port >= 1 && port <= 65535)
                net.host(static_cast<std::uint16_t>(port), ui.nick, system);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("UDP port must be reachable (port forward or VPN).");
        ImGui::InputText("host:port", ui.join, sizeof ui.join);
        ImGui::SameLine();
        if (ImGui::Button("Join"))
        {
            std::string address;
            std::uint16_t port = kosmos::NetPlay::default_port;
            std::string text = ui.join;
            if (text.find(':') == std::string::npos)
                text += ":" + std::to_string(kosmos::NetPlay::default_port);
            if (lobby::split_endpoint(text, address, port))
                net.join(address, port, ui.nick, system);
        }
    }
    else
    {
        if (ImGui::Button("Leave game"))
            net.leave();
        for (const std::string &name : net.player_names())
            ImGui::BulletText("%s", name.c_str());
        for (const kosmos::Ghost &g : net.ghosts())
        {
            if (!g.have_state || g.age >= 3.0)
                continue;
            ImGui::PushID(static_cast<int>(g.id));
            if (ImGui::Button("Go to"))
                snap(g);
            ImGui::SameLine();
            ImGui::TextDisabled("%s: %s", g.nick.c_str(), g.state.ship.c_str());
            ImGui::PopID();
        }
    }

    ImGui::SeparatorText("Lobby (IRC)");
    const bool irc_active = net.irc.state() != lobby::IrcClient::State::disconnected;
    ImGui::BeginDisabled(irc_active);
    ImGui::InputText("IRC server", ui.irc_host, sizeof ui.irc_host);
    ImGui::InputText("IRC port", ui.irc_port, sizeof ui.irc_port, ImGuiInputTextFlags_CharsDecimal);
    ImGui::InputText("Channel", ui.channel, sizeof ui.channel);
    ImGui::EndDisabled();
    if (!irc_active)
    {
        if (ImGui::Button("Connect to lobby"))
        {
            const int port = std::atoi(ui.irc_port);
            if (port >= 1 && port <= 65535)
                net.lobby_connect(ui.irc_host, static_cast<std::uint16_t>(port), ui.nick, ui.channel);
        }
    }
    else if (ImGui::Button("Disconnect lobby"))
        net.lobby_disconnect();
    ImGui::SameLine();
    switch (net.irc.state())
    {
    case lobby::IrcClient::State::connecting: ImGui::TextUnformatted("connecting"); break;
    case lobby::IrcClient::State::registering: ImGui::TextUnformatted("registering"); break;
    case lobby::IrcClient::State::online: ImGui::Text("%s as %s, %d here", net.irc.channel().c_str(), net.irc.nick().c_str(), static_cast<int>(net.irc.users().size())); break;
    default: ImGui::TextDisabled("not connected"); break;
    }

    ImGui::Text("Games");
    ImGui::BeginChild("games", ImVec2(0.0f, 90.0f), true);
    const std::vector<lobby::GameListing> games = net.board.games();
    if (games.empty())
        ImGui::TextDisabled(irc_active ? "No games announced yet." : "Connect to the lobby to see games.");
    for (std::size_t i = 0; i < games.size(); ++i)
    {
        const lobby::GameListing &game = games[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginDisabled(playing);
        if (ImGui::Button("Join"))
            net.join(game.address, game.port, ui.nick, system);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("%s  by %s  (%s:%d)", game.title.c_str(), game.host_nick.c_str(), game.address.c_str(), static_cast<int>(game.port));
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::BeginChild("chat", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), true);
    for (const std::string &line : net.chat)
        ImGui::TextWrapped("%s", line.c_str());
    if (ui.scroll_chat)
    {
        ImGui::SetScrollHereY(1.0f);
        ui.scroll_chat = false;
    }
    ImGui::EndChild();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##chat", ui.chat, sizeof ui.chat, ImGuiInputTextFlags_EnterReturnsTrue) && ui.chat[0])
    {
        if (net.irc.say(ui.chat))
            net.chat.push_back(std::string("<") + net.irc.nick() + "> " + ui.chat);
        ui.chat[0] = 0;
        ui.scroll_chat = true;
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::TextDisabled("IRC is plain text: do not type anything private.");
    ImGui::End();
}

} // namespace kosmos::app
