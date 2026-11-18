// Shared population (protocol v15, 2026-11-18): the same NPCs and traffic for every player of a group.
//
// The server groups the players who are near each other (a co-op mission: its host and crew) and names one game of
// each group the authority (PopRole). The authority keeps GTA's own population and streams every ambient ped and
// vehicle near it ~8 times a second (a mission's host: its mission entities too). The other games of the group are
// viewers: they switch their own population off, remove what they had, and draw puppets of the authority's NPCs and
// cars. What a viewer does to a puppet goes back to the authority ("lo.pe"): damage, kills, a car it takes. What the
// authority's NPCs do to a viewer's player comes back to that viewer ("lo.nd"). Police stay each game's own.
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
uint16_t Authority();
bool IsPuppet(int entity);        // one of our population puppets
bool StreamsFor(int remotePed);   // we are the authority and this remote player's puppet is in our NPCs' reach
void ReportNpcDamage(uint16_t victim, int amount, uint32_t weapon);   // authority: our NPCs hurt that player's puppet
std::string StatusLine();         // "authority for 2 games, 54 peds 31 vehicles" / "viewer of Alex, 40 puppets"

}  // namespace population
