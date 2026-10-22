// ENet client for the Windows game server, driven from the script fiber (no extra threads):
// blocking calls wait with gta::Wait(0), so the game keeps rendering while they run.
#pragma once
#include <functional>
#include <string>
#include "protocol/protocol.h"

namespace net {

struct LoginResult {
    bool ok = false;
    std::string error;          // human readable when !ok
    std::string playerUuid;
    std::string newToken;       // set when the server registered this name just now (store it!)
    uint16_t netId = 0;
};

// Connect + Hello + Auth. timeoutMs covers the whole handshake.
LoginResult Login(const std::string &host, int port, const std::string &name, const std::string &token, int timeoutMs);
bool Connected();
uint16_t SelfNetId();       // our netId on the server (0 when offline)
uint32_t Logins();          // how many times we logged in (a restarted server can hand out the same netId again)
void Disconnect(int flushMs = 300);

// Blocking profile download; false on timeout/disconnect.
bool RequestProfile(proto::ProfileMsg &out, int timeoutMs);
// Fire-and-forget save; the answer arrives through the callback during Service().
bool SendProfileSave(uint32_t baseRevision, const std::string &json);
void OnProfileSaved(std::function<void(const proto::ProfileSavedMsg &)> cb);
// Blocking variant used at login/exit (waits for ProfileSaved).
bool SaveProfileAndWait(uint32_t baseRevision, const std::string &json, proto::ProfileSavedMsg &out, int timeoutMs);

// Game traffic (Snapshot, PlayerJoined/Left, PlayerAppearance) is handed to this callback with a
// reader positioned after the header.
void OnGameMessage(std::function<void(proto::MsgType, wire::Reader &)> cb);
void SendState(const proto::PlayerStateMsg &s);          // unreliable, channel 1
void SendAppearance(const std::string &json);            // reliable
void SendEvent(const wire::Writer &w);                   // reliable, channel 2 (combat events)

void Service();             // call every frame
unsigned RttMs();

}  // namespace net
