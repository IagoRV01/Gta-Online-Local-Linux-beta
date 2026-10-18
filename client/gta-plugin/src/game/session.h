// The "YOU" mode: switches the player from the story character (Michael/Franklin/Trevor) to the
// custom freemode character and back, without touching the story character.
//
// How the story character is preserved: we never change its model. The freemode ped is a NEW ped
// and the player is moved into it with CHANGE_PLAYER_PED (what the game's own character switch
// does). The story ped is kept alive as a hidden, frozen mission entity where the player left it,
// with its clothes, weapons and health untouched, and the player is put back into it on exit.
// Fallback if a game script deleted it meanwhile: SET_PLAYER_MODEL + the captured outfit.
#pragma once
#include <string>
#include <windows.h>
#include "game/sync.h"

namespace session {

struct Config {
    int menuKey = VK_F10;
    int autosaveSeconds = 15;
    std::string cacheDir;      // <GTA>\LocalOnline\cache\  (profile)
    std::string backupDir;     // <GTA>\LocalOnline\backups\ (story save copies)
    sync::Settings server;     // Windows game server (Phase 2 persistence)
};

void Init(const Config &cfg);
void Tick();                         // every frame, on the script fiber
void OnKey(DWORD key);               // keyboard hook thread: only sets flags
bool InYou();
void EnterYou();                     // same as F10 -> Play as YOU (used by the start screen)
std::string CharacterName();         // "" when no character exists yet
std::string DisplayName();           // the name the others see (the account name, else the character's)
// YOU character's money (other mods use it through the exports in main.cpp). AddMoney clamps at 0
// and fails (false) when it would go negative or YOU is not active; it saves the profile.
// Money() is wallet + bank. AddMoney: earnings (> 0) go to the bank, costs (< 0) come from the
// wallet first, then the bank (GTA Online rules). Spend: preferBank for internet/phone purchases.
long long Money();
long long Wallet();
long long Bank();
bool AddMoney(long long delta, const char *reason);
bool Spend(long long amount, const char *reason, bool preferBank);
bool ChangeAccount(int account, long long delta, const char *reason);
// Other mods' per-character data (saved with the profile on the server). GetData returns the length
// (copies up to cap-1 bytes + NUL), -1 when absent or YOU is not active.
int GetData(const char *key, char *buf, int cap);
bool SetData(const char *key, const char *value);   // 0 wallet, 1 bank, 2 deposit (<0 withdraw)
bool HandleCommand(const std::string &line);   // remote test commands; false if unknown

}  // namespace session
