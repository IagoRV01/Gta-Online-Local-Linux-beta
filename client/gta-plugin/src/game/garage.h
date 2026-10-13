// Personal vehicles on the client (Phase 8): the server keeps the list (vehicle::Personal JSON)
// and every state change; this module captures/applies a car's look, spawns and stores cars,
// reports destruction, and offers the "My vehicles" menu.
#pragma once
#include "protocol/protocol.h"

namespace garage {

void OnList(const proto::VehicleListMsg &m);
void OnResult(const proto::VehicleResultMsg &m);
void Frame();            // every frame while YOU is active and online
void Menu();             // blocking "My vehicles" menu (from F10)
void StoreAll();         // leaving YOU: every called-out car goes back to the garage
bool HandleCommand(const std::string &verb, std::istream &in);   // test commands (garage.*)

}  // namespace garage
