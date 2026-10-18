// GTA Online-style HUD for the YOU character: bank and wallet balances in the top-right corner
// (shown for a few seconds after a change, or while Z / INPUT_MULTIPLAYER_INFO is held, with the
// "+$500" / "-$500" change lines), and the player list on Z. The story cash HUD is hidden while
// YOU is active so Michael/Franklin/Trevor's money is never shown for YOU.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hud {

struct PlayerRow { std::string name; bool self = false; int wanted = 0; int rank = 0; bool passive = false; int ped = 0; };

void Frame(int64_t wallet, int64_t bank, const std::vector<PlayerRow> &players, const std::string &status);
void OnMoneyChange(int64_t dWallet, int64_t dBank);   // from economy (local or server-confirmed)
void ShowMoney(int ms);
void Reset();
std::string Money(int64_t v);                         // "$1,234,567"

}  // namespace hud
