#include "game/net_client.h"
#include <enet/enet.h>
#include <deque>
#include "common/log.h"
#include "engine/gta.h"

namespace net {

using namespace proto;

static bool g_init = false;
static ENetHost *g_host = nullptr;
static ENetPeer *g_peer = nullptr;
static bool g_connected = false;
static bool g_manual = false;   // Login/Disconnect drive ENet themselves: the Wait hook must not
static uint16_t g_selfNetId = 0;
static std::function<void(const ProfileSavedMsg &)> g_onSaved;
static std::function<void(MsgType, wire::Reader &)> g_onGame;
static std::deque<std::vector<uint8_t>> g_inbox;   // packets kept for a blocking waiter

static void Send(const wire::Writer &w, uint8_t ch, bool reliable) {
    if (!g_peer) return;
    enet_peer_send(g_peer, ch, enet_packet_create(w.Bytes().data(), w.Size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0));
    enet_host_flush(g_host);
}

// Pumps ENet once. Control packets are queued in g_inbox (ProfileSaved is dispatched directly).
static void Pump() {
    if (!g_host || g_manual) return;
    ENetEvent ev;
    while (enet_host_service(g_host, &ev, 0) > 0) {
        if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
            LOGW("net", "-", "disconnected from server (reason %u)", ev.data);
            g_connected = false;
            g_peer = nullptr;
        } else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
            std::vector<uint8_t> pkt(ev.packet->data, ev.packet->data + ev.packet->dataLength);
            enet_packet_destroy(ev.packet);
            wire::Reader r(pkt.data(), pkt.size());
            Header h;
            if (!GetHeader(r, h)) continue;
            if (h.type == MsgType::ProfileSaved && g_onSaved) {
                ProfileSavedMsg m;
                if (Get(r, m)) g_onSaved(m);
                continue;
            }
            if (h.type == MsgType::Snapshot || h.type == MsgType::PlayerJoined || h.type == MsgType::PlayerLeft || h.type == MsgType::PlayerAppearance ||
                h.type == MsgType::ShotRelay || h.type == MsgType::DamageApply || h.type == MsgType::KillFeed || h.type == MsgType::WorldState || h.type == MsgType::MoneyResult || h.type == MsgType::Ledger || h.type == MsgType::VehicleList || h.type == MsgType::VehicleResult || h.type == MsgType::JobList || h.type == MsgType::JobState || h.type == MsgType::RaceStandings) {
                if (g_onGame) g_onGame(h.type, r);
                continue;
            }
            g_inbox.push_back(std::move(pkt));
            if (g_inbox.size() > 64) g_inbox.pop_front();
        }
    }
}

// Waits for a packet of `type` (or Reject). Returns the reader positioned after the header.
static bool WaitFor(MsgType type, std::vector<uint8_t> &out, int timeoutMs, RejectMsg *reject = nullptr) {
    unsigned long long end = GetTickCount64() + (unsigned)timeoutMs;
    while (GetTickCount64() < end) {
        Pump();
        for (auto it = g_inbox.begin(); it != g_inbox.end(); ++it) {
            wire::Reader r(it->data(), it->size());
            Header h;
            GetHeader(r, h);
            if (h.type == type || h.type == MsgType::Reject) {
                out = std::move(*it);
                g_inbox.erase(it);
                if (h.type == MsgType::Reject) {
                    wire::Reader rr(out.data(), out.size());
                    GetHeader(rr, h);
                    if (reject) Get(rr, *reject);
                    return false;
                }
                return true;
            }
        }
        if (!g_peer) return false;
        gta::Wait(0);
    }
    return false;
}

LoginResult Login(const std::string &hostName, int port, const std::string &name, const std::string &token, int timeoutMs) {
    LoginResult res;
    if (!g_init) {
        if (enet_initialize() != 0) { res.error = "network init failed"; return res; }
        g_init = true;
    }
    Disconnect(0);
    g_host = enet_host_create(nullptr, 1, kNumChannels, 0, 0);
    if (!g_host) { res.error = "cannot create socket"; return res; }
    ENetAddress addr{};
    if (enet_address_set_host(&addr, hostName.c_str()) != 0) { res.error = "bad server address " + hostName; return res; }
    addr.port = (enet_uint16)port;
    g_peer = enet_host_connect(g_host, &addr, kNumChannels, 0);
    enet_peer_timeout(g_peer, 0, 15000, 30000);   // the BC-250 can freeze for seconds while swapping
    unsigned long long end = GetTickCount64() + (unsigned)timeoutMs;
    ENetEvent ev;
    bool up = false;
    g_manual = true;
    while (!up && GetTickCount64() < end) {
        while (enet_host_service(g_host, &ev, 0) > 0)
            if (ev.type == ENET_EVENT_TYPE_CONNECT) up = true;
        if (!up) gta::Wait(0);
    }
    g_manual = false;
    if (!up) { res.error = "server not reachable (" + hostName + ":" + std::to_string(port) + ")"; Disconnect(0); return res; }
    g_connected = true;

    wire::Writer hello;
    Put(hello, HelloMsg{3889, "LocalOnline-0.2"});
    Send(hello, kChanControl, true);
    std::vector<uint8_t> pkt;
    RejectMsg rej;
    int left = (int)(end - GetTickCount64());
    if (!WaitFor(MsgType::Welcome, pkt, left > 500 ? left : 500, &rej)) {
        res.error = rej.reason.empty() ? "no answer to hello" : rej.reason;
        Disconnect(0);
        return res;
    }
    wire::Writer auth;
    Put(auth, AuthMsg{name, token});
    Send(auth, kChanControl, true);
    left = (int)(end - GetTickCount64());
    if (!WaitFor(MsgType::AuthResult, pkt, left > 1000 ? left : 1000, &rej)) {
        res.error = rej.reason.empty() ? "no answer to auth" : rej.reason;
        Disconnect(0);
        return res;
    }
    wire::Reader r(pkt.data(), pkt.size());
    Header h;
    GetHeader(r, h);
    AuthResultMsg ar;
    if (!Get(r, ar) || !ar.ok) { res.error = ar.reason.empty() ? "auth refused" : ar.reason; Disconnect(0); return res; }
    res.ok = true;
    res.playerUuid = ar.playerUuid;
    res.newToken = ar.newToken;
    res.netId = ar.netId;
    g_selfNetId = ar.netId;
    LOGI("net", name.c_str(), "logged in to %s:%d as netId %u (rtt %u ms)%s", hostName.c_str(), port, ar.netId, g_peer->roundTripTime,
         ar.newToken.empty() ? "" : ", new account");
    return res;
}

bool Connected() { return g_connected && g_peer; }
uint16_t SelfNetId() { return Connected() ? g_selfNetId : 0; }

void Disconnect(int flushMs) {
    g_manual = true;
    if (g_peer) {
        enet_peer_disconnect(g_peer, 0);
        unsigned long long end = GetTickCount64() + (unsigned)flushMs;
        ENetEvent ev;
        while (GetTickCount64() < end) {
            bool done = false;
            while (enet_host_service(g_host, &ev, 0) > 0) {
                if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
                if (ev.type == ENET_EVENT_TYPE_DISCONNECT) done = true;
            }
            if (done) break;
            gta::Wait(0);
        }
        enet_peer_reset(g_peer);
    }
    if (g_host) enet_host_destroy(g_host);
    g_host = nullptr;
    g_peer = nullptr;
    g_connected = false;
    g_inbox.clear();
    g_manual = false;
}

bool RequestProfile(ProfileMsg &out, int timeoutMs) {
    if (!Connected()) return false;
    wire::Writer w;
    PutProfileRequest(w);
    Send(w, kChanControl, true);
    std::vector<uint8_t> pkt;
    if (!WaitFor(MsgType::Profile, pkt, timeoutMs)) return false;
    wire::Reader r(pkt.data(), pkt.size());
    Header h;
    GetHeader(r, h);
    return Get(r, out);
}

bool SendProfileSave(uint32_t base, const std::string &json) {
    if (!Connected() || json.size() > kMaxProfileJson) return false;
    wire::Writer w;
    Put(w, ProfileSaveMsg{base, json});
    Send(w, kChanControl, true);
    return true;
}

void OnProfileSaved(std::function<void(const ProfileSavedMsg &)> cb) { g_onSaved = std::move(cb); }

bool SaveProfileAndWait(uint32_t base, const std::string &json, ProfileSavedMsg &out, int timeoutMs) {
    bool got = false;
    auto prev = g_onSaved;
    g_onSaved = [&](const ProfileSavedMsg &m) { out = m; got = true; };
    bool sent = SendProfileSave(base, json);
    unsigned long long end = GetTickCount64() + (unsigned)timeoutMs;
    while (sent && !got && Connected() && GetTickCount64() < end) { Pump(); if (!got) gta::Wait(0); }
    g_onSaved = prev;
    return got;
}

void Service() { Pump(); }

void OnGameMessage(std::function<void(MsgType, wire::Reader &)> cb) { g_onGame = std::move(cb); }

void SendState(const PlayerStateMsg &s) {
    if (!Connected()) return;
    wire::Writer w;
    PutPlayerState(w, s);
    enet_peer_send(g_peer, kChanState, enet_packet_create(w.Bytes().data(), w.Size(), 0));
}

void SendEvent(const wire::Writer &w) {
    if (!Connected()) return;
    enet_peer_send(g_peer, kChanEvents, enet_packet_create(w.Bytes().data(), w.Size(), ENET_PACKET_FLAG_RELIABLE));
}

void SendAppearance(const std::string &json) {
    if (!Connected() || json.size() > kMaxProfileJson) return;
    wire::Writer w;
    Put(w, AppearanceUpdateMsg{json});
    Send(w, kChanControl, true);
}

unsigned RttMs() { return g_peer ? g_peer->roundTripTime : 0; }

}  // namespace net
