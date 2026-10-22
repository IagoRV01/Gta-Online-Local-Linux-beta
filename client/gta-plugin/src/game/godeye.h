// Phase 10: GodEye's actions in this player's world (protocol v12 GodEyeAction). All fiction inside the game:
// wanted stars, a police unit sent to the player, a roadblock ahead, and the traffic lights round the player
// (ALL RED / ALL GREEN / RANDOM / NORMAL). GTA's AI ignores what a traffic light prop shows, so the lights are
// overridden for the eye and the drivers are steered by our own layer: brakes before a red light, a driving
// style without "stop at lights" for a green one.
#pragma once
#include "protocol/protocol.h"

namespace godeye {

void OnAction(const proto::GodEyeActionMsg &m);   // network callback: queued, carried out in Frame
void Frame(int me);                               // every frame while YOU is active and online
void End();                                       // leaving YOU or going offline: units released, traffic normal
const char *TrafficModeName();                    // for the log and the F10 status

}  // namespace godeye
