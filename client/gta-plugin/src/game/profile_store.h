// Local profile cache: <GTA>\LocalOnline\cache\character.json (+ .bak of the previous save).
// Phase 1: the only copy. Phase 2: a cache of the server's copy (the server stays authoritative).
// Writes are atomic (temp file + replace), so a crash mid-save never leaves a half-written profile.
#pragma once
#include <string>
#include "common/character.h"

namespace profile_store {

void Init(const std::string &cacheDir);
bool Exists();
// false + err when missing or invalid; a corrupt file falls back to the .bak copy automatically.
bool Load(character::Character &out, std::string &err);
bool Save(character::Character &c);   // sets c.updatedAt; c.revision is the server's (sync.cpp)
std::string Path();

// Sync bookkeeping (cache\sync.json): the server revision the cache is based on, and whether the
// cache holds changes the server has not acknowledged yet.
struct SyncState { uint32_t serverRevision = 0; bool dirty = true; bool known = false; };
SyncState LoadSync();
void SaveSync(const SyncState &s);
// Per-player secret issued by the server on first login (cache\token, never in client.toml).
std::string LoadToken();
void SaveToken(const std::string &token);
void Delete();   // removes character.json + .bak + sync.json (tests: "server returns the profile")

}  // namespace profile_store
