// GTA Online look for the YOU character outside the HUD proper: the pause menu header (title,
// player name, bank/cash instead of the story character's), and story-mode blips hidden from the
// map while YOU plays (restored when you go back to story mode).
#pragma once
#include <cstdint>
#include <sstream>
#include <string>

namespace online_ui {

void Frame(const std::string &playerName, int64_t wallet, int64_t bank);   // every frame while YOU is active
void PauseKey();                                                             // every frame while YOU (also while dead)
void Leave();                                                                // back to story mode: restore everything
bool HandleCommand(const std::string &v, std::istringstream &in);           // blips (dump), ui.* tests

}  // namespace online_ui
