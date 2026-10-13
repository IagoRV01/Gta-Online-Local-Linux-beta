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
struct Row { uint16_t netId; std::string name; int wanted; };
std::vector<Row> Rows();           // for the player list (HUD)   // puppet ped of a remote player (tests), 0 if none

}  // namespace remote
