// Phase 7: the server's clock, weather and blackout applied while YOU is online. The story's own
// clock and weather are captured on entering and put back on leaving, so story mode is unaffected.
#pragma once
#include "protocol/protocol.h"

namespace world_sync {

void OnWorldState(const proto::WorldStateMsg &m);   // network callback
void Begin();                                       // entering YOU: remember the story clock/weather
void Frame();                                       // every frame while YOU is active and online
void End();                                         // leaving YOU: restore the story clock/weather
// API v10: a job with its own time of day and weather (a race's lobby options) holds the server's world off while it
// lasts; released, the server's clock and weather come back
void SetHold(bool hold);

}  // namespace world_sync
