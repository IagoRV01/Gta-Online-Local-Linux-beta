// Remote players (Phase 4): one puppet freemode ped per other player, driven from server snapshots
// through an interpolation buffer (100 ms behind, dead reckoning up to 250 ms). Peds walk/run with
// tasks so animations look natural; large errors snap. Each puppet has a named blip and a name tag.
#pragma once
#include <string>
#include <vector>
#include "protocol/protocol.h"

namespace remote {

void Init();                       // installs the net::OnGameMessage handler
void SetSelf(uint16_t netId);      // our own netId (never drawn)
void Frame();                      // every frame while YOU is active and online
void Clear();                      // deletes every puppet (leaving YOU / disconnect)
int Count();
// Spectator camera that follows a remote player (name prefix, "" = first). false if none found.
bool Spectate(const std::string &namePrefix);
void StopSpectate();
uint16_t NetIdOfPed(int ped);      // remote player driving/owning this ped, 0 if none
std::string NameOf(uint16_t netId); // account name of a remote player ("player N" if unknown)
double ServerNowMs();                // the server's clock (ms) as estimated from the snapshots, -1 before the first one
int PedOf(const std::string &namePrefix);
int PedOfNetId(uint16_t netId);    // the puppet of that player (0 if not spawned)
int VehOfNetId(uint16_t netId);    // the vehicle that player drives (0 if none)
void SeeAllSessions(bool on);      // camera clients (Phase 12): draw the players of every session
struct Row { uint16_t netId; std::string name; int wanted; int rank; bool passive; int ped; };
std::vector<Row> Rows();           // for the player list (HUD): the players in our session only
// v11 player info: our rank and InfoFlags (from Gori through the exports), sent when they change;
// the session the server put us in (players of other sessions are not drawn).
void SetMyInfo(int rank, int flags);
void Highlight(uint16_t netId, int ms);   // "Highlight Player": the blip flashes for a while
int MyRank();
int MyFlags();
uint32_t MySession();
std::string PlayersText();         // "netId|name|rank|flags|session" per line, every known player (Gori's menus)

}  // namespace remote
