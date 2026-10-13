// Local side of combat (Phase 6): what our own ped does.
//  * aim point and shot events for the other players (visual only)
//  * damage reported by the server (another player hit us) is applied to our ped here
//  * our death is reported with the last attacker; the kill feed is shown
// Puppet-side combat (other players' peds) lives in remote.cpp.
#pragma once
#include <cstdint>
#include "protocol/protocol.h"

namespace combat {

proto::Vec3 AimPoint();                 // world point under the crosshair (camera ray, 80 m)
void Frame(int me);                     // every frame while YOU is active and online
void OnDamageApply(const proto::DamageApplyMsg &m);
void OnKillFeed(const proto::KillFeedMsg &m);
void OnLocalDeath();                    // called once when our ped dies
void Reset();                           // leaving YOU / disconnect
// Weapon of our most recent shot (hits are detected a few frames later, possibly after we died and
// respawned unarmed); falls back to the weapon in hand when we have not fired for 3 s.
uint32_t AttackWeapon(int me);

}  // namespace combat
