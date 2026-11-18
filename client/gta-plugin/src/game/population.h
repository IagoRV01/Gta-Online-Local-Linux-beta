// Shared population (protocol v15, v16 per-entity owners, 2026-11-18): the same NPCs and traffic for every player.
//
// As in GTA Online, every NPC and vehicle has ONE owner game: the one whose player it is nearest to. The server groups
// the players who are near each other (a co-op mission or survival: its host and crew) and tells each game its peers
// (PopRole). Each game keeps GTA's own population in its part of the world (nearer its player than any peer's, at its
// usual density), streams what it owns ~8 times a second, and draws puppets of what its peers own. An entity that ends
// up clearly nearer another player is handed over ("lo.pm"): that game's puppet becomes the real one and ours becomes the
// puppet, so nothing pops. What we do to a puppet goes to its owner ("lo.pe": damage, kills, a car we take); what an
// owner's NPCs do to our player comes back to us ("lo.nd"). A mission's host owns the mission's entities; police are
// streamed by the game whose wanted level brought them. Puppets copy cover, crouching, reloading, climbing, jumping,
// hands up, cowering and walking to a car. A camera client only draws.
#pragma once
#include <cstdint>
#include <string>
#include "protocol/protocol.h"

namespace population {

void OnRole(const proto::PopRoleMsg &m);                  // network callbacks
void OnRelay(const proto::PopRelayMsg &m);
void OnPlayerMessage(uint16_t from, const std::string &kind, const std::string &text);   // lo.pe / lo.nd
void Frame();                     // every frame while online (YOU, or the camera client)
void Clear();                     // offline / leaving YOU: puppets deleted, our own population back
int Mode();                       // proto::PopMode
uint16_t Authority();             // the mission host whose game owns the mission's entities (0: none)
bool IsPuppet(int entity);        // one of our population puppets
bool StreamsFor(int remotePed);   // we are the authority and this remote player's puppet is in our NPCs' reach
void ReportNpcDamage(uint16_t victim, int amount, uint32_t weapon);   // authority: our NPCs hurt that player's puppet
// v16 cars taken between players: an owner gives the car a remote player got into to remote.cpp as that player's car
int ClaimVehicle(uint16_t netId, uint32_t model, float x, float y, float z);   // the car to use for that player (0: spawn one)
bool KeepVehicle(int veh, uint16_t netId);   // remote.cpp would delete that player's car: true = a mission car we keep
// for Gori (LocalOnline API v16)
int GoriMode();                   // 0 own world, 1 we own the job's NPCs for the crew, 2 the host's game owns them (draw only)
int Hostiles();                   // hostile mission puppets alive here (a member's "enemies left")
int MissionKills();               // mission puppets our player killed (counter)
bool Shown(int entity);           // one of ours that the peers already draw (Gori's traffic must not swap it)
std::string StatusLine();
void DevCommand(const std::string &args);   // LocalOnline.cmd "pop ..." (tests)         // "authority for 2 games, 54 peds 31 vehicles" / "viewer of Alex, 40 puppets"

}  // namespace population
