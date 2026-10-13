// Phase 7: the server's clock, weather and blackout applied while YOU is online. The story's own
// clock and weather are captured on entering and put back on leaving, so story mode is unaffected.
#pragma once
#include "protocol/protocol.h"

namespace world_sync {

void OnWorldState(const proto::WorldStateMsg &m);   // network callback
void Begin();                                       // entering YOU: remember the story clock/weather
void Frame();                                       // every frame while YOU is active and online
void End();                                         // leaving YOU: restore the story clock/weather

}  // namespace world_sync
