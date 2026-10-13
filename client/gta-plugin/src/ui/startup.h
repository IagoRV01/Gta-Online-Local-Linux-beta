// Start screen shown once when the game finishes loading: STORY MODE or GTA LOCAL ONLINE.
// Replaces Rockstar's landing page (which runs before ScriptHookV scripts exist and whose GTA Online
// entry leads to the official service). There is deliberately no official-Online option here.
// Background: a slow aerial orbit over Los Santos. Keyboard, mouse and gamepad all work.
#pragma once
#include <string>

namespace startup {

enum class Choice { Story, LocalOnline };

struct Info {
    std::string characterName;   // empty = no character yet ("Create your character")
    std::string serverText;      // e.g. "LAN server 192.168.1.10:4499 (Phase 2)"
};

// Blocks until the player chooses. Leaves the screen faded OUT when LocalOnline is chosen (the
// session switch fades in itself) and faded IN for Story.
Choice Run(const Info &info);

}  // namespace startup
