// CREATE CHARACTER screen: Gender / Parents / Face / Skin / Hair / Eyes / Clothing / Name / [CREATE].
// Edits a Character and shows every change live on the player's freemode ped, with a script camera.
#pragma once
#include <functional>
#include "common/character.h"

namespace creator {

// swapGender(female) must replace the player's ped with the other freemode model and return it.
// Returns true when the player pressed CREATE/SAVE, false when they backed out (c is then unchanged).
bool Run(character::Character &c, bool firstTime, const std::function<int(bool female)> &swapGender);

}  // namespace creator
