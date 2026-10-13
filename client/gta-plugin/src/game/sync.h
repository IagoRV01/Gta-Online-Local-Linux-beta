// Profile sync between the local cache and the Windows server (Phase 2).
//
// Rules (the server is the authority, the cache is never the only copy once uploaded):
//  * Online at "Play as YOU": the server copy wins. Exception: the cache has unacknowledged local
//    changes made on top of exactly the server's current revision (offline play) -> they are uploaded.
//  * Server has no character but the cache does (Phase 1 profile) -> the cache is uploaded (migration).
//  * Server unreachable -> play from the cache ("offline"), changes are kept dirty and uploaded on
//    the next successful connection; reconnection is retried every 30 s while playing.
//  * Every save writes the cache first, then sends ProfileSave(base = last acknowledged revision).
#pragma once
#include <string>
#include "common/character.h"

namespace sync {

struct Settings {
    bool enabled = true;
    std::string host;
    int port = 4499;
    std::string playerName;
};

enum class Begin { Existing, NeedsCreator, Failed };

void Init(const Settings &s);
Begin Start(character::Character &c, std::string &status);   // blocks up to ~4 s while connecting
void Save(character::Character &c, const char *why);           // cache + async upload
void Finish(character::Character &c);                            // final save, wait for the ack, disconnect
void Tick(character::Character &c, bool active);                 // every frame
bool Online();
std::string StatusLine();                                        // for menus: "online 192.168.1.10 (1 ms)"

}  // namespace sync
