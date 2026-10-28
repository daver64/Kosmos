#pragma once

#include "app.h"
#include "scenario_start.h"

// The multiplayer window (F12) and the helpers it uses.
namespace kosmos::app
{
struct NetUi
{
    char irc_host[128] = "", irc_port[8] = "6667", channel[64] = "#kosmos", nick[32] = "", advertise[128] = "", game_port[8] = "7777", join[160] = "", title[48] = "Kosmos", chat[300] = "";
    bool scroll_chat = false;
};


void copy_text(char *dest, std::size_t size, const std::string &text);

// Move the player's ship to the close vicinity of another player.
bool snap_to_ghost(Sim &sim, Ship &ship, const kosmos::Ghost &ghost, const TerrainSet &terrains);

void draw_multiplayer_window(kosmos::NetPlay &net, NetUi &ui, bool &open, int system, const std::function<void(const kosmos::Ghost &)> &snap);

} // namespace kosmos::app
