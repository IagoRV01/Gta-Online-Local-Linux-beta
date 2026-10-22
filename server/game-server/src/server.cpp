#include "server.h"
#include <enet/enet.h>
#include <chrono>
#include <cctype>
#include <functional>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <thread>
#include "common/character.h"
#include "common/json.h"
#include "common/sha256.h"
#include "common/vehicle.h"
#include "common/log.h"

using namespace proto;

static long long WallMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
int Server::NowMs() const { return (int)(WallMs() - startMs_); }

static std::string PeerAddress(ENetPeer *p) {
    char ip[64] = "?";
    enet_address_get_host_ip(&p->address, ip, sizeof ip);
    return std::string(ip) + ":" + std::to_string(p->address.port);
}

bool Server::Start(const ServerConfig &cfg, Storage *storage) {
    cfg_ = cfg;
    db_ = storage;
    startMs_ = WallMs();
    if (enet_initialize() != 0) { LOGE("net", "-", "enet_initialize failed"); return false; }
    ENetAddress addr{};
    if (cfg.bind == "0.0.0.0" || cfg.bind.empty()) addr.host = ENET_HOST_ANY;
    else if (enet_address_set_host_ip(&addr, cfg.bind.c_str()) != 0) { LOGE("net", "-", "bad bind address %s", cfg.bind.c_str()); return false; }
    addr.port = (enet_uint16)cfg.port;
    host_ = enet_host_create(&addr, (size_t)cfg.maxPlayers + 2, kNumChannels, 0, 0);   // +2: room to send polite rejects
    if (!host_) { LOGE("net", "-", "cannot bind UDP %s:%d (port in use?)", cfg.bind.c_str(), cfg.port); return false; }
    host_->maximumPacketSize = kMaxPacket;
    LOGI("net", "-", "listening on UDP %s:%d, tick %d Hz, max %d players, protocol v%d", cfg.bind.c_str(), cfg.port, cfg.tickRate, cfg.maxPlayers, kProtocolVersion);
    // world state survives restarts
    if (auto v = db_->GetWorld("clock_minutes")) clock_.SetMinutes(std::atof(v->c_str()));
    if (auto v = db_->GetWorld("clock_frozen")) clock_.frozen = *v == "1";
    if (auto v = db_->GetWorld("weather")) weather_.Set(*v, WallMs());
    if (auto v = db_->GetWorld("blackout")) blackout_ = *v == "1";
    weather_.automatic = cfg.weatherAutomatic;
    lastWorldMs_ = WallMs();
    LOGI("world", "-", "clock %02d:%02d%s, weather %s%s", clock_.Hour(), clock_.Minute(), clock_.frozen ? " (frozen)" : "",
         weather_.Current().c_str(), blackout_ ? ", blackout" : "");
    AdminApiStart();   // a failure is logged; the game keeps running without GodEye
    return true;
}

void Server::Run(volatile bool &stop) {
    const int tickMs = 1000 / (cfg_.tickRate > 0 ? cfg_.tickRate : 20);
    long long next = WallMs();
    while (!stop) {
        ENetEvent ev;
        // service until the next tick is due
        int wait = (int)(next - WallMs());
        while (enet_host_service(host_, &ev, wait > 0 ? (enet_uint32)wait : 0) > 0) {
            switch (ev.type) {
                case ENET_EVENT_TYPE_CONNECT: OnConnect(ev.peer); break;
                case ENET_EVENT_TYPE_DISCONNECT: OnDisconnect(ev.peer); break;
                case ENET_EVENT_TYPE_RECEIVE:
                    OnPacket(ev.peer, ev.packet->data, ev.packet->dataLength, ev.channelID);
                    enet_packet_destroy(ev.packet);
                    break;
                default: break;
            }
            wait = (int)(next - WallMs());
            if (wait <= 0) break;
        }
        long long now = WallMs();
        if (now >= next) {
            ++tick_;
            WorldTick(now);
            BroadcastSnapshot();
            AdminApiPoll(now);
            next += tickMs;
            if (now - next > 1000) next = now;   // we fell far behind (debugger, sleep): don't spiral
        }
    }
}

void Server::Shutdown() {
    AdminApiStop();
    if (!host_) return;
    for (auto &kv : clients_) enet_peer_disconnect(kv.first, 0);
    enet_host_flush(host_);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    enet_host_destroy(host_);
    host_ = nullptr;
    enet_deinitialize();
    LOGI("net", "-", "server stopped");
}

void Server::OnConnect(ENetPeer *peer) {
    Client c;
    c.peer = peer;
    c.address = PeerAddress(peer);
    c.windowStartMs = NowMs();
    enet_peer_timeout(peer, 0, (enet_uint32)cfg_.timeoutMs / 2, (enet_uint32)cfg_.timeoutMs);
    clients_[peer] = c;
    LOGI("net", c.address.c_str(), "connected (%d peers)", (int)clients_.size());
}

void Server::OnDisconnect(ENetPeer *peer) {
    auto it = clients_.find(peer);
    if (it == clients_.end()) return;
    Client &c = it->second;
    LOGI("net", c.authed ? c.name.c_str() : c.address.c_str(), "disconnected (netId %u, dropped %d, malformed %d)", c.netId, c.dropped, c.malformed);
    if (c.sessionId) db_->EndSession(c.sessionId, "disconnect");
    if (c.authed) LeaveJobs(c.netId);
    if (c.authed) {
        db_->Event("leave", c.playerUuid, "");
        wire::Writer w;
        PutHeader(w, MsgType::PlayerLeft);
        w.U16(c.netId);
        w.U8(0);
        if (!c.camera)
            for (auto &kv : clients_) {
                if (kv.first == peer || !kv.second.authed) continue;
                Send(kv.first, w, kChanControl, true);
                if (kv.second.camera && kv.second.cam.target == c.netId && kv.second.cam.mode != kCamCctv) {
                    kv.second.cam = DefaultCamera();
                    SendCamera(kv.second);
                }
            }
    }
    clients_.erase(it);
}

void Server::Send(ENetPeer *peer, const wire::Writer &w, uint8_t channel, bool reliable) {
    ENetPacket *p = enet_packet_create(w.Bytes().data(), w.Size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    enet_peer_send(peer, channel, p);
}

void Server::Reject(Client &c, RejectCode code, const std::string &reason) {
    wire::Writer w;
    PutHeader(w, MsgType::Reject);
    w.U8((uint8_t)code);
    w.Str(reason, kMaxReason);
    Send(c.peer, w, kChanControl, true);
    enet_peer_disconnect_later(c.peer, (enet_uint32)code);
    LOGW("net", c.address.c_str(), "rejected (%d): %s", (int)code, reason.c_str());
}

void Server::OnPacket(ENetPeer *peer, const uint8_t *data, size_t size, uint8_t channel) {
    auto it = clients_.find(peer);
    if (it == clients_.end()) return;
    Client &c = it->second;
    const char *who = c.authed ? c.name.c_str() : c.address.c_str();

    // rate limit: a fixed one-second window per peer
    int now = NowMs();
    if (now - c.windowStartMs >= 1000) { c.windowStartMs = now; c.windowPackets = 0; }
    if (++c.windowPackets > cfg_.maxPacketsPerSecond) { if (++c.dropped % 100 == 1) LOGW("net", who, "rate limit: dropping packets (%d so far)", c.dropped); return; }

    if (size > kMaxPacket) { ++c.malformed; return; }
    wire::Reader r(data, size);
    Header h;
    if (!GetHeader(r, h)) {
        ++c.malformed;
        if (!c.hello) Reject(c, RejectCode::BadVersion, "protocol mismatch: server speaks v" + std::to_string(kProtocolVersion));
        return;
    }
    auto malformed = [&](const char *what) {
        if (++c.malformed % 50 == 1) LOGW("net", who, "malformed %s (%d so far)", what, c.malformed);
        if (c.malformed > 500) Reject(c, RejectCode::Malformed, "too many malformed packets");
    };

    switch (h.type) {
        case MsgType::Hello: {
            HelloMsg m;
            if (!Get(r, m)) return malformed("Hello");
            c.hello = true;
            LOGI("net", who, "hello: client %s, game build %u", m.clientVersion.c_str(), m.gameBuild);
            wire::Writer w;
            Put(w, WelcomeMsg{(uint16_t)cfg_.tickRate, (uint32_t)NowMs(), cfg_.name});
            Send(peer, w, kChanControl, true);
            break;
        }
        case MsgType::Auth: {
            AuthMsg m;
            if (!Get(r, m) || !c.hello) return malformed("Auth");
            if (c.authed) return malformed("Auth twice");
            HandleAuth(c, m);
            break;
        }
        case MsgType::ProfileRequest: {
            if (!r.Done()) return malformed("ProfileRequest");
            if (!c.authed) return malformed("ProfileRequest before auth");
            HandleProfileRequest(c);
            break;
        }
        case MsgType::ProfileSave: {
            ProfileSaveMsg m;
            if (!Get(r, m)) return malformed("ProfileSave");
            if (!c.authed) return malformed("ProfileSave before auth");
            HandleProfileSave(c, m);
            break;
        }
        case MsgType::AppearanceUpdate: {
            AppearanceUpdateMsg m;
            if (!Get(r, m)) return malformed("AppearanceUpdate");
            if (!c.authed) return malformed("AppearanceUpdate before auth");
            HandleAppearance(c, m);
            break;
        }
        case MsgType::PlayerInfo: {
            PlayerInfoMsg m;
            if (!Get(r, m)) return malformed("PlayerInfo");
            if (c.authed) HandlePlayerInfo(c, m);
            break;
        }
        case MsgType::Shot: {
            ShotMsg m;
            if (!Get(r, m)) return malformed("Shot");
            if (c.authed) HandleShot(c, m);
            break;
        }
        case MsgType::Damage: {
            DamageMsg m;
            if (!Get(r, m)) return malformed("Damage");
            if (c.authed) HandleDamage(c, m);
            break;
        }
        case MsgType::Death: {
            DeathMsg m;
            if (!Get(r, m)) return malformed("Death");
            if (c.authed) HandleDeath(c, m);
            break;
        }
        case MsgType::JobOpen: {
            JobOpenMsg m;
            if (!Get(r, m)) return malformed("JobOpen");
            if (c.authed) HandleJobOpen(c, m);
            break;
        }
        case MsgType::JobJoin: {
            JobJoinMsg m;
            if (!Get(r, m)) return malformed("JobJoin");
            if (c.authed) HandleJobJoin(c, m);
            break;
        }
        case MsgType::JobUpdate: {
            JobUpdateMsg m;
            if (!Get(r, m)) return malformed("JobUpdate");
            if (c.authed) HandleJobUpdate(c, m);
            break;
        }
        case MsgType::RaceProgress: {
            RaceProgressMsg m;
            if (!Get(r, m)) return malformed("RaceProgress");
            if (c.authed) HandleRaceProgress(c, m);
            break;
        }
        case MsgType::LedgerRequest: {
            LedgerRequestMsg m;
            if (!Get(r, m)) return malformed("LedgerRequest");
            if (c.authed) HandleLedger(c, m);
            break;
        }
        case MsgType::MoneyChange: {
            MoneyChangeMsg m;
            if (!Get(r, m)) return malformed("MoneyChange");
            if (c.authed) HandleMoney(c, m);
            break;
        }
        case MsgType::VehicleListRequest: {
            if (!r.Done()) return malformed("VehicleListRequest");
            if (c.authed) HandleVehicleList(c);
            break;
        }
        case MsgType::VehicleRegister: {
            VehicleRegisterMsg m;
            if (!Get(r, m)) return malformed("VehicleRegister");
            if (c.authed) HandleVehicleRegister(c, m);
            break;
        }
        case MsgType::VehicleUpdate: {
            VehicleUpdateMsg m;
            if (!Get(r, m)) return malformed("VehicleUpdate");
            if (c.authed) HandleVehicleUpdate(c, m);
            break;
        }
        case MsgType::PlayerState: {
            if (!c.authed) return;   // state before auth is ignored, not an error
            PlayerStateMsg s;
            if (!GetPlayerState(r, s)) return malformed("PlayerState");
            if (channel != kChanState) return malformed("PlayerState channel");
            if (c.haveState && !SeqNewer(s.seq, c.lastSeq)) return;   // late/duplicate: unreliable channel, just drop
            c.lastSeq = s.seq;
            c.state = s;
            c.haveState = true;
            break;
        }
        default:
            malformed("unknown type");
            break;
    }
}

void Server::BroadcastSnapshot() {
    std::vector<RemoteState> all;
    for (auto &kv : clients_) {
        const Client &c = kv.second;
        if (!c.authed || !c.haveState) continue;
        RemoteState s;
        s.netId = c.netId; s.seq = c.lastSeq;
        s.pos = c.state.pos; s.vel = c.state.vel; s.heading = c.state.heading;
        s.move = c.state.move; s.flags = c.state.flags;
        s.health = c.state.health; s.armour = c.state.armour; s.weapon = c.state.weapon; s.wanted = c.state.wanted;
        s.veh = c.state.veh;   // the driver's client is authoritative for its vehicle
        all.push_back(s);
    }
    for (auto &kv : clients_) {
        if (!kv.second.authed) continue;
        // each client gets everyone in its own session except itself (its own ped is local)
        std::vector<RemoteState> others;
        for (auto &s : all) {
            if (s.netId == kv.second.netId) continue;
            const Client *o = nullptr;
            for (auto &kv2 : clients_) if (kv2.second.netId == s.netId) o = &kv2.second;
            if (o && (kv.second.camera || o->session == kv.second.session)) others.push_back(s);
        }
        wire::Writer w;
        PutSnapshot(w, tick_, (uint32_t)NowMs(), others);
        Send(kv.first, w, kChanState, false);
    }
}

// ---------------------------------------------------------------- auth / profile

static std::string AddressJson(const std::string &addr) { return "{\"address\":\"" + addr + "\"}"; }

void Server::HandleAuth(Client &c, const AuthMsg &m) {
    if (!character::ValidName(m.name)) return Reject(c, RejectCode::BadAuth, "invalid player name");
    for (auto &kv : clients_)
        if (kv.second.authed && kv.second.name == m.name) return Reject(c, RejectCode::AlreadyConnected, m.name + " is already connected");
    int authed = 0, cameras = 0;
    for (auto &kv : clients_) { authed += kv.second.authed && !kv.second.camera; cameras += kv.second.authed && kv.second.camera; }
    if (m.role == kRoleCamera ? cameras >= cfg_.maxCameras : authed >= cfg_.maxPlayers) return Reject(c, RejectCode::ServerFull, "server full");

    AuthResultMsg res;
    auto player = db_->FindPlayer(m.name);
    if (player) {
        if (player->banned) return Reject(c, RejectCode::Banned, "banned");
        if (m.token.empty() || sha256::Hex(m.token) != player->tokenHash) {
            db_->Event("auth_failed", player->uuid, AddressJson(c.address));
            return Reject(c, RejectCode::BadAuth, "wrong token for " + m.name);
        }
    } else {
        if (!cfg_.allowRegistration) return Reject(c, RejectCode::BadAuth, "unknown player " + m.name);
        PlayerRow p;
        p.uuid = character::NewUuid();
        p.name = m.name;
        res.newToken = sha256::RandomToken();
        p.tokenHash = sha256::Hex(res.newToken);
        if (!db_->CreatePlayer(p)) return Reject(c, RejectCode::BadAuth, "could not create the account");
        db_->Event("register", p.uuid, AddressJson(c.address));
        LOGI("session", m.name.c_str(), "new account %s registered from %s", p.uuid.c_str(), c.address.c_str());
        player = p;
    }
    c.authed = true;
    c.camera = m.role == kRoleCamera;
    c.name = m.name;
    c.playerUuid = player->uuid;
    c.netId = nextNetId_++;
    c.sessionId = db_->StartSession(c.playerUuid, c.address);
    db_->TouchPlayer(c.playerUuid);
    db_->Event("join", c.playerUuid, AddressJson(c.address));
    res.ok = true;
    res.netId = c.netId;
    res.playerUuid = c.playerUuid;
    wire::Writer w;
    Put(w, res);
    Send(c.peer, w, kChanControl, true);
    LOGI("session", c.name.c_str(), "authenticated as %s %u from %s", c.camera ? "camera client" : "netId", c.netId, c.address.c_str());
    SendWorld(c.peer);
    if (trafficMode_ != kTrafficNormal) SendTraffic(c.peer);   // a GodEye traffic hack still running
    SendJobList(c.peer);   // the open co-op jobs
    for (auto &kv : clients_) {   // tell everyone (both ways) who is here; camera clients see the players, never the reverse
        if (kv.first == c.peer || !kv.second.authed) continue;
        if (kv.second.camera && c.camera) continue;
        if (!c.camera) {
            wire::Writer a;
            Put(a, PlayerJoinedMsg{c.netId, c.name});
            Send(kv.first, a, kChanControl, true);
        }
        if (kv.second.camera) continue;
        wire::Writer b;
        Put(b, PlayerJoinedMsg{kv.second.netId, kv.second.name});
        Send(c.peer, b, kChanControl, true);
        if (!kv.second.appearanceJson.empty()) {
            wire::Writer ap;
            Put(ap, PlayerAppearanceMsg{kv.second.netId, kv.second.appearanceJson});
            Send(c.peer, ap, kChanControl, true);
        }
        RelayInfo(kv.second, c.peer);
    }
    if (c.camera) { c.cam = DefaultCamera(); SendCamera(c); }
    else RelayInfo(c, nullptr);
}

// ---------------------------------------------------------------- player info and sessions (v11)
void Server::RelayInfo(const Client &c, ENetPeer *only) {
    wire::Writer w;
    Put(w, PlayerInfoRelayMsg{c.netId, c.rank, c.infoFlags, c.session});
    if (only) { Send(only, w, kChanControl, true); return; }
    for (auto &kv : clients_)
        if (kv.second.authed) Send(kv.first, w, kChanControl, true);
}

uint32_t Server::SessionOf(const Client &c) const {
    for (auto &kv : jobs_) {   // a running job: its own session (the lobby is still freemode)
        const Job &j = kv.second;
        if (j.status != kJobRunning) continue;
        if (j.host == c.netId || std::find(j.members.begin(), j.members.end(), c.netId) != j.members.end()) return j.id;
    }
    if (c.infoFlags & kInfoActivity) return kPrivateSession | c.netId;   // a solo activity: alone
    return 0;
}

void Server::RefreshSessions() {
    for (auto &kv : clients_) {
        Client &c = kv.second;
        if (!c.authed) continue;
        uint32_t s = SessionOf(c);
        if (s == c.session) continue;
        LOGI("session", c.name.c_str(), "session %u -> %u", c.session, s);
        c.session = s;
        RelayInfo(c, nullptr);
    }
}

void Server::HandlePlayerInfo(Client &c, const PlayerInfoMsg &m) {
    if (m.rank == c.rank && m.flags == c.infoFlags) return;
    bool passiveChanged = (m.flags ^ c.infoFlags) & kInfoPassive;
    c.rank = m.rank; c.infoFlags = m.flags;
    if (passiveChanged) LOGI("session", c.name.c_str(), "passive mode %s", (m.flags & kInfoPassive) ? "on" : "off");
    uint32_t s = SessionOf(c);
    if (s != c.session) LOGI("session", c.name.c_str(), "session %u -> %u", c.session, s);
    c.session = s;
    RelayInfo(c, nullptr);
}

void Server::HandleAppearance(Client &c, const AppearanceUpdateMsg &m) {
    json::Value v;
    std::string err;
    character::Character ch;
    if (!json::Parse(m.json, v, err) || !character::FromJson(v, ch, err)) {
        LOGW("profile", c.name.c_str(), "appearance rejected: %s", err.c_str());
        return;
    }
    // Re-serialise from the validated struct: others only ever get clean, look-only data.
    c.appearanceJson = json::Write(character::ToJson(character::AppearanceOnly(ch)), 0);
    LOGI("profile", c.name.c_str(), "appearance of %s (%s) relayed", ch.name.c_str(), ch.female ? "female" : "male");
    wire::Writer w;
    Put(w, PlayerAppearanceMsg{c.netId, c.appearanceJson});
    for (auto &kv : clients_)
        if (kv.first != c.peer && kv.second.authed) Send(kv.first, w, kChanControl, true);
}

void Server::HandleProfileRequest(Client &c) {
    ProfileMsg res;
    if (auto row = db_->LoadCharacter(c.playerUuid)) {
        res.exists = true;
        res.revision = row->revision;
        res.json = row->json;
        c.characterUuid = row->uuid;
        // the money column is authoritative: put it into the profile the client gets
        json::Value v;
        std::string err;
        character::Character ch;
        if (json::Parse(row->json, v, err) && character::FromJson(v, ch, err)) {
            long long money = db_->GetMoney(row->uuid), bank = db_->GetBank(row->uuid);
            if ((money >= 0 && money != ch.money) || (bank >= 0 && bank != ch.bank)) {
                if (money >= 0) ch.money = money;
                if (bank >= 0) ch.bank = bank;
                res.json = json::Write(character::ToJson(ch), 0);
            }
        }
    }
    wire::Writer w;
    Put(w, res);
    Send(c.peer, w, kChanControl, true);
    LOGI("profile", c.name.c_str(), "profile sent (%s, rev %u, %d bytes)", res.exists ? "exists" : "none", res.revision, (int)res.json.size());
}

void Server::HandleProfileSave(Client &c, const ProfileSaveMsg &m) {
    auto reply = [&](bool ok, uint32_t rev, const std::string &why) {
        ProfileSavedMsg res;
        res.ok = ok; res.revision = rev; res.reason = why;
        wire::Writer w;
        Put(w, res);
        Send(c.peer, w, kChanControl, true);
    };
    int now = NowMs();
    if (now - c.lastProfileSaveMs < cfg_.minProfileSaveIntervalMs) return reply(false, m.baseRevision, "too many saves");
    c.lastProfileSaveMs = now;

    // Validate exactly like the client does: parse, schema, clamp; never store what fails.
    json::Value v;
    std::string err;
    character::Character ch;
    if (!json::Parse(m.json, v, err) || !character::FromJson(v, ch, err)) {
        LOGW("profile", c.name.c_str(), "rejected invalid profile: %s", err.c_str());
        db_->Event("profile_rejected", c.playerUuid, "");
        return reply(false, m.baseRevision, "invalid profile: " + err);
    }
    auto existing = db_->LoadCharacter(c.playerUuid);
    if (existing && existing->uuid != ch.uuid) return reply(false, existing->revision, "this account already has another character");

    // Money is never taken from a profile save: existing characters keep the server balance (changed
    // only through MoneyChange); a new character may bring at most startingMoneyCap.
    if (existing) {
        long long money = db_->GetMoney(existing->uuid), bank = db_->GetBank(existing->uuid);
        if (money >= 0 && money != ch.money) { LOGD("economy", c.name.c_str(), "profile money %lld ignored (server %lld)", (long long)ch.money, money); ch.money = money; }
        if (bank >= 0 && bank != ch.bank) ch.bank = bank;
    } else {
        if (ch.money > cfg_.startingMoneyCap) {
            LOGW("economy", c.name.c_str(), "new character money %lld capped to %lld", (long long)ch.money, cfg_.startingMoneyCap);
            ch.money = cfg_.startingMoneyCap;
        }
        ch.bank = 0;
    }
    c.characterUuid = ch.uuid;
    CharacterRow row;
    row.uuid = ch.uuid;
    row.playerUuid = c.playerUuid;
    ch.revision = m.baseRevision + 1;   // the stored copy carries the revision it is stored under
    row.json = json::Write(character::ToJson(ch), 0);
    row.name = ch.name;
    row.model = ch.female ? "mp_f_freemode_01" : "mp_m_freemode_01";
    row.money = ch.money;
    row.bank = ch.bank;
    row.x = ch.pos.x; row.y = ch.pos.y; row.z = ch.pos.z; row.heading = ch.pos.heading;
    uint32_t newRev = 0;
    switch (db_->SaveCharacter(row, m.baseRevision, newRev, err)) {
        case SaveResult::Ok:
            LOGD("profile", c.name.c_str(), "saved rev %u (%.1f %.1f %.1f)", newRev, ch.pos.x, ch.pos.y, ch.pos.z);
            return reply(true, newRev, "");
        case SaveResult::Conflict:
            LOGW("profile", c.name.c_str(), "save conflict: %s", err.c_str());
            return reply(false, newRev, err);
        default:
            LOGE("profile", c.name.c_str(), "save failed: %s", err.c_str());
            return reply(false, m.baseRevision, err);
    }
}

// ---------------------------------------------------------------- combat (Phase 6)
// Every hit is reported by the shooter's client ("I hit netId N for X with weapon W"); the server
// checks it is plausible and forwards it to the victim, whose client applies it to its own ped.
// The victim reports its death (with the last attacker); the server announces it to everyone.

static float Dist(const Vec3 &a, const Vec3 &b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

Server::Client *Server::ByNetId(uint16_t id) {
    for (auto &kv : clients_)
        if (kv.second.authed && kv.second.netId == id) return &kv.second;
    return nullptr;
}

void Server::CombatWindow(Client &c) {
    int now = NowMs();
    if (now - c.combatWindowMs >= 1000) { c.combatWindowMs = now; c.shotsInWindow = 0; c.damageInWindow = 0; }
}

void Server::HandleShot(Client &c, const ShotMsg &m) {
    CombatWindow(c);
    if (++c.shotsInWindow > cfg_.maxShotsPerSecond) return;
    if (c.haveState && (c.state.flags & kFlagDead)) return;
    // the shot must start near the shooter and not end absurdly far away
    if (c.haveState) {
        Vec3 from = (c.state.flags & kFlagInVehicle) ? c.state.veh.pos : c.state.pos;
        if (Dist(m.origin, from) > 12.f) { LOGD("combat", c.name.c_str(), "shot origin %.1f m from the shooter: dropped", Dist(m.origin, from)); return; }
    }
    if (Dist(m.origin, m.target) > 1500.f) return;
    wire::Writer w;
    Put(w, ShotRelayMsg{c.netId, m.weapon, m.origin, m.target});
    for (auto &kv : clients_)
        if (kv.first != c.peer && kv.second.authed && kv.second.session == c.session) Send(kv.first, w, kChanEvents, true);
}

void Server::HandleDamage(Client &c, const DamageMsg &m) {
    const char *who = c.name.c_str();
    if (!cfg_.pvp) { LOGD("combat", who, "hit on netId %u ignored: pvp off", m.target); return; }
    Client *v = ByNetId(m.target);
    if (!v || v == &c) return;
    if (!c.haveState || !v->haveState) return;
    if ((c.state.flags & kFlagDead) || (v->state.flags & kFlagDead)) return;
    if (v->session != c.session) { LOGD("combat", who, "hit on %s refused: another session", v->name.c_str()); return; }
    if ((c.infoFlags | v->infoFlags) & kInfoPassive) { LOGD("combat", who, "hit on %s refused: passive mode", v->name.c_str()); return; }
    Vec3 a = (c.state.flags & kFlagInVehicle) ? c.state.veh.pos : c.state.pos;
    Vec3 b = (v->state.flags & kFlagInVehicle) ? v->state.veh.pos : v->state.pos;
    float d = Dist(a, b);
    if (d > cfg_.maxHitDistance) { LOGW("combat", who, "hit on %s refused: %.0f m away", v->name.c_str(), d); return; }
    CombatWindow(c);
    if ((c.damageInWindow += m.amount) > cfg_.maxDamagePerSecond) { LOGW("combat", who, "damage rate limit hit (%d/s)", c.damageInWindow); return; }
    LOGI("combat", who, "hit %s for %u (weapon 0x%08X, bone %u, %.0f m)", v->name.c_str(), m.amount, m.weapon, m.bone, d);
    wire::Writer w;
    Put(w, DamageApplyMsg{c.netId, m.amount, m.weapon, m.bone});
    Send(v->peer, w, kChanEvents, true);
}

void Server::HandleDeath(Client &c, const DeathMsg &m) {
    Client *k = m.killer ? ByNetId(m.killer) : nullptr;
    if (k == &c) k = nullptr;
    c.deaths++;
    if (k) k->kills++;
    LOGI("combat", c.name.c_str(), "died%s%s (weapon 0x%08X)", k ? ", killed by " : "", k ? k->name.c_str() : "", m.weapon);
    char data[160];
    std::snprintf(data, sizeof data, "{\"killer\":\"%s\",\"weapon\":%u}", k ? k->playerUuid.c_str() : "", m.weapon);
    db_->Event(k ? "kill" : "death", c.playerUuid, data);
    for (auto &kv : jobs_) {   // a deathmatch both are playing in: the score
        Job &j = kv.second;
        if (j.kind != kJobDeathmatch || j.status != kJobRunning || NowMs() < j.startAt) continue;
        auto vi = j.racers.find(c.netId);
        if (vi == j.racers.end()) continue;
        vi->second.finishMs++;   // deaths
        if (k) { auto ki = j.racers.find(k->netId); if (ki != j.racers.end()) { ki->second.checkpoints++; ki->second.atMs = NowMs(); } }
        SendRaceStandings(j);
    }
    wire::Writer w;
    Put(w, KillFeedMsg{c.netId, k ? k->netId : (uint16_t)0, m.weapon});
    for (auto &kv : clients_)
        if (kv.second.authed) Send(kv.first, w, kChanEvents, true);
}

// ---------------------------------------------------------------- world (Phase 7)

void Server::SendWorld(ENetPeer *only) {
    long long now = WallMs();
    WorldStateMsg m;
    m.hour = (uint8_t)clock_.Hour(); m.minute = (uint8_t)clock_.Minute(); m.second = (uint8_t)clock_.Second();
    m.flags = (clock_.frozen ? kWorldClockFrozen : 0) | (blackout_ ? kWorldBlackout : 0);
    m.weather = weather_.Current();
    m.nextWeather = weather_.Next();
    m.transition = weather_.Transition(now);
    wire::Writer w;
    Put(w, m);
    if (only) { Send(only, w, kChanControl, true); return; }
    for (auto &kv : clients_)
        if (kv.second.authed) Send(kv.first, w, kChanControl, true);
}

void Server::WorldTick(long long now) {
    clock_.Advance(now - lastWorldMs_);
    lastWorldMs_ = now;
    std::string before = weather_.Current() + ">" + weather_.Next();
    weather_.Advance(now);
    std::string after = weather_.Current() + ">" + weather_.Next();
    if (after != before) {
        LOGI("world", "-", "weather %s", weather_.Current() == weather_.Next() ? weather_.Current().c_str() : (weather_.Current() + " -> " + weather_.Next()).c_str());
        SendWorld(nullptr);
        lastWorldSentMs_ = now;
    }
    if (now - lastWorldSentMs_ >= 10000) { SendWorld(nullptr); lastWorldSentMs_ = now; }
    if (trafficUntilMs_ && now >= trafficUntilMs_) {   // a timed traffic hack ran out
        trafficMode_ = kTrafficNormal; trafficUntilMs_ = 0;
        LOGI("godeye", "-", "traffic back to normal");
        SendTraffic(nullptr);
    }
    if (now - lastPersistMs_ >= 60000) {
        lastPersistMs_ = now;
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.2f", clock_.Minutes());
        db_->SetWorld("clock_minutes", buf);
        db_->SetWorld("weather", weather_.Current());
    }
    if (!cfg_.adminCommandFile.empty() && now - lastCmdPollMs_ >= 1000) {
        lastCmdPollMs_ = now;
        FILE *f = std::fopen(cfg_.adminCommandFile.c_str(), "rb");
        if (f) {
            std::string text;
            char b[512];
            size_t n;
            while ((n = std::fread(b, 1, sizeof b, f)) > 0 && text.size() < 8192) text.append(b, n);
            std::fclose(f);
            if (!text.empty()) {
                if (FILE *t = std::fopen(cfg_.adminCommandFile.c_str(), "wb")) std::fclose(t);   // consume
                size_t start = 0;
                while (start < text.size()) {
                    size_t end = text.find('\n', start);
                    std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                    if (!line.empty()) AdminCommand(line);
                    if (end == std::string::npos) break;
                    start = end + 1;
                }
            }
        }
    }
}

static std::string JsonEsc(const std::string &s) {
    std::string o;
    for (unsigned char ch : s) {
        if (ch == '"' || ch == '\\') { o += '\\'; o += (char)ch; }
        else if (ch >= 0x20) o += (char)ch;
    }
    return o;
}

// GodEye actions (Phase 10): to one player's client, or the traffic mode to everyone
void Server::SendGodEye(Client &c, const GodEyeActionMsg &m) {
    wire::Writer w;
    Put(w, m);
    Send(c.peer, w, kChanControl, true);
}

void Server::SendTraffic(ENetPeer *only) {
    GodEyeActionMsg m;
    m.kind = kGeTraffic;
    m.value = trafficMode_;
    long long left = trafficUntilMs_ ? trafficUntilMs_ - WallMs() : 0;
    m.durationMs = left > 0 ? (uint32_t)left : 0;
    wire::Writer w;
    Put(w, m);
    if (only) { Send(only, w, kChanControl, true); return; }
    for (auto &kv : clients_)
        if (kv.second.authed) Send(kv.first, w, kChanControl, true);
}

// The GodEye commands that act on players: "wanted 2 +1", "alert 2 [text]", "roadblock 2", "notify 2|all text",
// "traffic red|green|random|normal [seconds]". false when the player is unknown or not in the world.
bool Server::GodEyeCommand(const std::string &v, const std::string &line) {
    char a1[32] = {0};
    int consumed = 0;
    std::sscanf(line.c_str(), "%*s %31s %n", a1, &consumed);
    std::string arg = a1, rest = consumed > 0 && consumed <= (int)line.size() ? line.substr(consumed) : "";
    if (rest.size() > kMaxGodEyeText) rest.resize(kMaxGodEyeText);
    if (v == "traffic") {
        static const char *modes[] = {"normal", "red", "green", "random"};
        int mode = -1;
        for (int i = 0; i < 4; ++i) if (arg == modes[i]) mode = i;
        if (mode < 0) return false;
        int secs = std::atoi(rest.c_str());
        if (secs < 0 || secs > 3600) return false;
        trafficMode_ = (uint8_t)mode;
        trafficUntilMs_ = mode != kTrafficNormal && secs > 0 ? WallMs() + secs * 1000LL : 0;
        SendTraffic(nullptr);
        return true;
    }
    if (v == "notify" && arg == "all") {
        if (rest.empty()) return false;
        for (auto &kv : clients_)
            if (kv.second.authed) SendGodEye(kv.second, GodEyeActionMsg{kGeNotice, 0, 0, rest});
        return true;
    }
    Client *c = ByNetId((uint16_t)std::atoi(arg.c_str()));
    if (!c || !c->haveState) return false;   // the actions need the player in the world (YOU, not story mode)
    GodEyeActionMsg m;
    if (v == "wanted") {
        int now = c->state.wanted, want;
        if (!rest.empty() && (rest[0] == '+' || rest[0] == '-')) want = now + std::atoi(rest.c_str());
        else if (!rest.empty() && std::isdigit((unsigned char)rest[0])) want = std::atoi(rest.c_str());
        else return false;
        m.kind = kGeWanted;
        m.value = (uint8_t)(want < 0 ? 0 : want > 5 ? 5 : want);
    } else if (v == "alert") {
        m.kind = kGePoliceAlert;
        m.text = rest.empty() ? "Dispatch has your description." : rest;
    } else if (v == "roadblock") m.kind = kGeRoadblock;
    else if (v == "notify") {
        if (rest.empty()) return false;
        m.kind = kGeNotice;
        m.text = rest;
    } else return false;
    SendGodEye(*c, m);
    return true;
}

// ---------------------------------------------------------------- camera clients (Phase 12)

CameraSetMsg Server::DefaultCamera() const {
    CameraSetMsg m;
    if (!cfg_.cctv.empty()) {
        const auto &k = cfg_.cctv.front();
        m.mode = kCamCctv; m.cameraId = k.id; m.pos = k.pos; m.rot = k.rot; m.fov = k.fov; m.label = k.id + " " + k.name;
    } else {
        m.mode = kCamFree; m.pos = {200, -900, 400}; m.rot = {-60, 0, 0}; m.fov = 60; m.label = "LOS SANTOS";
    }
    return m;
}

void Server::SendCamera(Client &c) {
    wire::Writer w;
    Put(w, c.cam);
    Send(c.peer, w, kChanControl, true);
}

// "camera <camera netId|all> cctv CAM_001" | "camera <id|all> follow|heli|drone|cinematic|third|chase <player netId>"
bool Server::CameraCommand(const std::string &line) {
    char who[16] = {0}, mode[16] = {0}, arg[32] = {0};
    if (std::sscanf(line.c_str(), "%*s %15s %15s %31s", who, mode, arg) < 3) return false;
    CameraSetMsg m;
    std::string md = mode;
    int mi = -1;
    for (int i = 0; i < 7; ++i) if (md == kCameraModeNames[i]) mi = i;
    if (mi < 0) return false;
    m.mode = (uint8_t)mi;
    if (m.mode == kCamCctv) {
        const ServerConfig::Cctv *k = nullptr;
        for (const auto &x : cfg_.cctv) if (x.id == arg) k = &x;
        if (!k) return false;
        m.cameraId = k->id; m.pos = k->pos; m.rot = k->rot; m.fov = k->fov; m.label = k->id + " " + k->name;
    } else {
        Client *t = ByNetId((uint16_t)std::atoi(arg));
        if (!t || t->camera || !t->haveState) return false;   // a player in the world
        m.target = t->netId;
        m.fov = m.mode == kCamHeli ? 40.f : m.mode == kCamCinematic ? 35.f : 55.f;
        std::string up = md;
        for (auto &ch : up) ch = (char)toupper((unsigned char)ch);
        m.label = up + " " + t->name;
    }
    bool all = std::string(who) == "all", any = false;
    for (auto &kv : clients_) {
        Client &c = kv.second;
        if (!c.authed || !c.camera || (!all && c.netId != (uint16_t)std::atoi(who))) continue;
        c.cam = m;
        SendCamera(c);
        any = true;
    }
    return any;
}

// Admin commands (server.cmd and the GodEye admin API). Every change is logged and stored, and pushed to the
// clients immediately.
bool Server::AdminCommand(const std::string &line) {
    char verb[32] = {0}, arg[64] = {0};
    int a = 0, b = 0;
    std::sscanf(line.c_str(), "%31s %63s", verb, arg);
    std::string v = verb, x = arg;
    if (v == "camera") {
        bool ok = CameraCommand(line);
        LOGI("admin", "-", "%s: %s", line.c_str(), ok ? "ok" : "rejected");
        return ok;
    }
    if (v == "wanted" || v == "alert" || v == "roadblock" || v == "notify" || v == "traffic") {
        bool ok = GodEyeCommand(v, line);
        LOGI("admin", "-", "%s: %s", line.c_str(), ok ? "ok" : "rejected");
        if (ok) db_->Event("godeye", "", "{\"command\":\"" + JsonEsc(line.substr(0, 160)) + "\"}");
        return ok;
    }
    bool ok = true;
    if (v == "time" && std::sscanf(line.c_str(), "%*s %d %d", &a, &b) >= 1) clock_.Set(a, b);
    else if (v == "freeze") { clock_.frozen = x == "on"; db_->SetWorld("clock_frozen", clock_.frozen ? "1" : "0"); }
    else if (v == "weather") {
        if (x == "auto") weather_.automatic = true;
        else { std::string up = x; for (auto &ch : up) ch = (char)toupper((unsigned char)ch); ok = weather_.Set(up, WallMs()); weather_.automatic = !ok ? weather_.automatic : false; }
    } else if (v == "blackout") { blackout_ = x == "on"; db_->SetWorld("blackout", blackout_ ? "1" : "0"); }
    else ok = false;
    LOGI("admin", "-", "%s: %s", line.c_str(), ok ? "ok" : "rejected");
    if (ok) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.2f", clock_.Minutes());
        db_->SetWorld("clock_minutes", buf);
        db_->SetWorld("weather", weather_.Current());
        db_->Event("admin", "", "{\"command\":\"" + JsonEsc(line.substr(0, 60)) + "\"}");
        SendWorld(nullptr);
    }
    return ok;
}

// ---------------------------------------------------------------- economy (Phase 8)

void Server::HandleMoney(Client &c, const MoneyChangeMsg &m) {
    MoneyResultMsg res;
    res.requestId = m.requestId;
    auto reply = [&](bool ok, long long wallet, long long bank, const std::string &why) {
        res.ok = ok; res.wallet = wallet; res.bank = bank; res.reason = why;
        wire::Writer w;
        Put(w, res);
        Send(c.peer, w, kChanEvents, true);
    };
    if (c.characterUuid.empty()) return reply(false, 0, 0, "no character loaded");
    long long curW = db_->GetMoney(c.characterUuid), curB = db_->GetBank(c.characterUuid);
    if (m.delta > cfg_.maxTransaction || m.delta < -cfg_.maxTransaction) {
        LOGW("economy", c.name.c_str(), "refused %+lld (%s): over the per-transaction limit", (long long)m.delta, m.reason.c_str());
        return reply(false, curW, curB, "transaction too large");
    }
    long long dW = 0, dB = 0;
    if (m.account == kWallet) dW = m.delta;
    else if (m.account == kBank) dB = m.delta;
    else { dW = -m.delta; dB = m.delta; }   // deposit (or withdraw when negative): no new money
    long long income = (dW > 0 ? dW : 0) + (dB > 0 ? dB : 0);
    if (m.account != kDeposit && income > 0) {
        long long now = NowMs();
        if (now - c.incomeWindowMs >= 60000) { c.incomeWindowMs = now; c.incomeInWindow = 0; }
        if (c.incomeInWindow + income > cfg_.maxIncomePerMinute) {
            LOGW("economy", c.name.c_str(), "refused %+lld (%s): income limit", (long long)m.delta, m.reason.c_str());
            return reply(false, curW, curB, "income limit reached, try later");
        }
        c.incomeInWindow += income;
    }
    long long wallet = 0, bank = 0;
    std::string err;
    std::string reason = m.reason.empty() ? "unspecified" : m.reason;
    if (!db_->AdjustAccounts(c.characterUuid, dW, dB, reason, wallet, bank, err)) {
        LOGI("economy", c.name.c_str(), "refused %+lld %s (%s): %s", (long long)m.delta, m.account == kWallet ? "wallet" : m.account == kBank ? "bank" : "deposit", reason.c_str(), err.c_str());
        return reply(false, wallet, bank, err);
    }
    LOGI("economy", c.name.c_str(), "%+lld %s (%s) -> wallet %lld, bank %lld", (long long)m.delta, m.account == kWallet ? "wallet" : m.account == kBank ? "bank" : "deposit",
         reason.c_str(), wallet, bank);
    reply(true, wallet, bank, "");
}

// The Maze Bank website's transaction log: the character's newest ledger rows.
void Server::HandleLedger(Client &c, const LedgerRequestMsg &m) {
    LedgerMsg res;
    res.requestId = m.requestId;
    if (!c.characterUuid.empty()) {
        for (const auto &row : db_->GetLedger(c.characterUuid, (int)kMaxLedgerRows)) {
            LedgerRow out;
            out.delta = row.delta;
            out.reason = row.reason;
            if (out.reason.rfind("bank: ", 0) == 0) { out.account = kBank; out.reason = out.reason.substr(6); }
            else if (out.reason.rfind("wallet: ", 0) == 0) { out.account = kWallet; out.reason = out.reason.substr(8); }
            out.at = row.at;
            res.rows.push_back(out);
        }
    }
    wire::Writer w;
    Put(w, res);
    Send(c.peer, w, kChanEvents, true);
}

// ---------------------------------------------------------------- personal vehicles (Phase 8)

static void VehicleReply(Server *, std::function<void(const wire::Writer &)> send, uint32_t id, bool ok, const std::string &uuid, const std::string &why) {
    wire::Writer w;
    Put(w, VehicleResultMsg{id, ok, uuid, why});
    send(w);
}

void Server::HandleVehicleList(Client &c) {
    VehicleListMsg m;
    if (!c.characterUuid.empty())
        for (auto &row : db_->ListVehicles(c.characterUuid)) m.vehicles.push_back(row.json);
    wire::Writer w;
    Put(w, m);
    Send(c.peer, w, kChanControl, true);
    LOGI("vehicles", c.name.c_str(), "list sent (%d)", (int)m.vehicles.size());
}

void Server::HandleVehicleRegister(Client &c, const VehicleRegisterMsg &m) {
    auto send = [&](const wire::Writer &w) { Send(c.peer, w, kChanControl, true); };
    if (c.characterUuid.empty()) return VehicleReply(this, send, m.requestId, false, "", "no character loaded");
    json::Value j;
    std::string err;
    vehicle::Personal v;
    if (!json::Parse(m.json, j, err) || !vehicle::FromJson(j, v, err)) return VehicleReply(this, send, m.requestId, false, "", "invalid vehicle: " + err);
    if ((int)db_->ListVehicles(c.characterUuid).size() >= vehicle::kMaxPerCharacter)
        return VehicleReply(this, send, m.requestId, false, "", "garage full (" + std::to_string(vehicle::kMaxPerCharacter) + " vehicles)");
    v.uuid = character::NewUuid();   // ids are always the server's
    v.state = vehicle::State::Active;
    std::string json = json::Write(vehicle::ToJson(v), 0);
    if (!db_->PutVehicle({v.uuid, c.characterUuid, json, "active"}, v.plate, v.model, v.x, v.y, v.z, v.heading, v.engineHealth))
        return VehicleReply(this, send, m.requestId, false, "", "database error");
    db_->Event("vehicle_register", c.playerUuid, "{\"vehicle\":\"" + v.uuid + "\",\"plate\":\"" + v.plate + "\"}");
    LOGI("vehicles", c.name.c_str(), "registered 0x%08X plate %s as %s", v.model, v.plate.c_str(), v.uuid.c_str());
    VehicleReply(this, send, m.requestId, true, v.uuid, "");
}

void Server::HandleVehicleUpdate(Client &c, const VehicleUpdateMsg &m) {
    auto send = [&](const wire::Writer &w) { Send(c.peer, w, kChanControl, true); };
    auto row = db_->GetVehicle(m.uuid);
    if (!row || row->ownerUuid != c.characterUuid) return VehicleReply(this, send, m.requestId, false, m.uuid, "not your vehicle");
    json::Value j;
    std::string err;
    vehicle::Personal v;
    if (!json::Parse(row->json, j, err) || !vehicle::FromJson(j, v, err)) return VehicleReply(this, send, m.requestId, false, m.uuid, "stored vehicle unreadable");
    auto to = (vehicle::State)m.state;
    if (!vehicle::CanTransition(v.state, to))
        return VehicleReply(this, send, m.requestId, false, m.uuid, std::string("cannot go from ") + vehicle::StateName(v.state) + " to " + vehicle::StateName(to));
    // leaving destroyed/impounded costs money (server-side, through the ledger)
    long long fee = v.state == vehicle::State::Destroyed ? cfg_.insuranceCost : v.state == vehicle::State::Impounded ? cfg_.impoundCost : 0;
    if (fee > 0) {
        long long wallet = 0, bank = 0;
        const char *why = v.state == vehicle::State::Destroyed ? "vehicle insurance" : "impound release";
        if (!db_->AdjustAccounts(c.characterUuid, 0, -fee, why, wallet, bank, err) && !db_->AdjustAccounts(c.characterUuid, -fee, 0, why, wallet, bank, err))
            return VehicleReply(this, send, m.requestId, false, m.uuid, err);
        wire::Writer mw;   // tell the client its new balances
        Put(mw, MoneyResultMsg{0, true, wallet, bank, ""});
        Send(c.peer, mw, kChanEvents, true);
    }
    v.state = to;
    if (to == vehicle::State::Active || to == vehicle::State::Stored || to == vehicle::State::Destroyed) {
        if (std::isfinite(m.pos.x) && (m.pos.x != 0 || m.pos.y != 0)) { v.x = m.pos.x; v.y = m.pos.y; v.z = m.pos.z; v.heading = m.heading; }
        v.engineHealth = m.engine;
        v.bodyHealth = m.body;
    }
    if (to == vehicle::State::Stored && fee > 0) { v.engineHealth = 1000; v.bodyHealth = 1000; }   // repaired by the insurer/impound
    vehicle::Validate(v, err);
    std::string json = json::Write(vehicle::ToJson(v), 0);
    if (!db_->PutVehicle({v.uuid, c.characterUuid, json, vehicle::StateName(v.state)}, v.plate, v.model, v.x, v.y, v.z, v.heading, v.engineHealth))
        return VehicleReply(this, send, m.requestId, false, m.uuid, "database error");
    LOGI("vehicles", c.name.c_str(), "%s %s -> %s%s", v.plate.c_str(), v.uuid.c_str(), vehicle::StateName(v.state), fee ? (" (paid " + std::to_string(fee) + ")").c_str() : "");
    VehicleReply(this, send, m.requestId, true, v.uuid, "");
}

// ---- co-op jobs (v9) -------------------------------------------------------------------------------------
void Server::SendJobList(ENetPeer *only) {
    JobListMsg m;
    for (const auto &kv : jobs_) {
        const Job &j = kv.second;
        JobInfo i;
        i.id = j.id; i.host = j.host; i.title = j.title; i.start = j.start; i.members = (uint8_t)j.members.size(); i.status = j.status; i.kind = j.kind;
        m.jobs.push_back(i);
    }
    wire::Writer w;
    Put(w, m);
    if (only) { Send(only, w, kChanEvents, true); return; }
    for (auto &kv : clients_) if (kv.second.authed) Send(kv.first, w, kChanEvents, true);
}

void Server::SendJobState(const Job &j) {
    JobStateMsg m;
    m.jobId = j.id; m.host = j.host; m.status = j.status; m.step = j.step; m.objective = j.objective; m.text = j.text; m.payout = j.payout; m.members = j.members;
    m.kind = j.kind; m.startAt = (uint32_t)j.startAt;
    wire::Writer w;
    Put(w, m);
    for (auto &kv : clients_) {
        const Client &c = kv.second;
        if (!c.authed) continue;
        if (c.netId == j.host || std::find(j.members.begin(), j.members.end(), c.netId) != j.members.end()) Send(kv.first, w, kChanEvents, true);
    }
}

void Server::CloseJob(uint32_t id, const char *why) {
    auto it = jobs_.find(id);
    if (it == jobs_.end()) return;
    it->second.status = kJobClosed;
    SendJobState(it->second);
    LOGI("jobs", "-", "job %u \"%s\" closed (%s)", id, it->second.title.c_str(), why);
    jobs_.erase(it);
    SendJobList(nullptr);
    RefreshSessions();
}

void Server::LeaveJobs(uint16_t netId) {
    std::vector<uint32_t> hosted;
    bool changed = false;
    for (auto &kv : jobs_) {
        Job &j = kv.second;
        if (j.host == netId) { hosted.push_back(j.id); continue; }
        auto m = std::find(j.members.begin(), j.members.end(), netId);
        if (m != j.members.end()) { j.members.erase(m); SendJobState(j); changed = true; }
    }
    for (uint32_t id : hosted) CloseJob(id, "host left");
    if (changed && hosted.empty()) SendJobList(nullptr);
    if (changed) RefreshSessions();
}

void Server::HandleJobOpen(Client &c, const JobOpenMsg &m) {
    LeaveJobs(c.netId);   // one job per player, hosting or joined
    Job j;
    j.id = nextJobId_++;
    j.host = c.netId;
    j.title = m.title;
    j.start = m.start;
    j.objective = m.start;
    j.payout = (uint32_t)std::min<long long>(m.payout, cfg_.maxJobPayout);
    j.startedMs = NowMs();
    j.kind = m.kind;
    jobs_[j.id] = j;
    LOGI("jobs", c.name.c_str(), "opened %s %u \"%s\" (payout %u)", j.kind == kJobRace ? "race" : j.kind == kJobDeathmatch ? "deathmatch" : j.kind == kJobSurvival ? "survival" : "job", j.id, j.title.c_str(), j.payout);
    SendJobState(jobs_[j.id]);
    SendJobList(nullptr);
}

void Server::HandleJobJoin(Client &c, const JobJoinMsg &m) {
    if (m.jobId == 0) { LeaveJobs(c.netId); return; }
    auto it = jobs_.find(m.jobId);
    if (it == jobs_.end() || it->second.host == c.netId) return;
    Job &j = it->second;
    if (std::find(j.members.begin(), j.members.end(), c.netId) != j.members.end()) { SendJobState(j); return; }
    if (j.members.size() + 1 >= kMaxJobMembers || j.status >= kJobPassed) return;
    bool racing = StartsTogether(j.kind) && j.status == kJobRunning;
    if (racing && NowMs() >= j.startAt) return;   // a race can be joined until its countdown ends
    LeaveJobs(c.netId);
    j.members.push_back(c.netId);
    if (racing) j.racers[c.netId] = {};
    LOGI("jobs", c.name.c_str(), "joined job %u \"%s\"", j.id, j.title.c_str());
    SendJobState(j);
    SendJobList(nullptr);
    RefreshSessions();
}

void Server::PayJob(const Job &j) {
    if (NowMs() - j.startedMs < cfg_.minJobMs) { LOGW("jobs", "-", "job %u \"%s\" passed too quickly: no payout", j.id, j.title.c_str()); return; }
    std::vector<std::pair<uint16_t, uint32_t>> pay;   // netId, amount
    if (j.kind == kJobDeathmatch) {   // everyone who played, by position
        static const double kShare[kMaxJobMembers] = {1.0, 0.75, 0.6, 0.5, 0.4, 0.35, 0.3, 0.25};
        std::vector<RaceEntry> order = RaceOrder(j);
        for (size_t i = 0; i < order.size(); ++i)
            if (order[i].checkpoints || order[i].finishMs) pay.push_back({order[i].netId, (uint32_t)(j.payout * kShare[i < kMaxJobMembers ? i : kMaxJobMembers - 1])});
    } else if (j.kind == kJobSurvival) {   // everyone who started it, by the waves they survived
        for (auto &kv : j.racers) {
            uint32_t waves = kv.second.finishMs & 0xFFFF;
            if (waves > kSurvivalWaves) waves = kSurvivalWaves;
            pay.push_back({kv.first, (uint32_t)((unsigned long long)j.payout * waves / kSurvivalWaves)});
        }
    } else if (j.kind == kJobRace) {
        static const double kShare[kMaxJobMembers] = {1.0, 0.75, 0.6, 0.5, 0.4, 0.35, 0.3, 0.25};
        std::vector<RaceEntry> order = RaceOrder(j);
        for (size_t i = 0; i < order.size(); ++i) {
            if (!order[i].checkpoints) continue;   // never left the grid
            double share = order[i].finishMs ? kShare[i < kMaxJobMembers ? i : kMaxJobMembers - 1] : 0.1;   // did not finish
            pay.push_back({order[i].netId, (uint32_t)(j.payout * share)});
        }
    } else for (uint16_t id : j.members) pay.push_back({id, j.payout});
    for (auto &pr : pay) {
        uint16_t id = pr.first;
        uint32_t amount = pr.second;
        Client *c = ByNetId(id);
        if (!c || c->characterUuid.empty() || amount == 0) continue;
        long long wallet = 0, bank = 0;
        std::string err, reason = (j.kind == kJobRace ? "Race: " : j.kind == kJobDeathmatch ? "Deathmatch: " : j.kind == kJobSurvival ? "Survival: " : "Job: ") + j.title;
        if (reason.size() > kMaxMoneyReason) reason.resize(kMaxMoneyReason);
        if (!db_->AdjustAccounts(c->characterUuid, 0, (long long)amount, reason, wallet, bank, err)) {
            LOGW("jobs", c->name.c_str(), "payout of job %u failed: %s", j.id, err.c_str());
            continue;
        }
        LOGI("jobs", c->name.c_str(), "paid %u for job %u \"%s\" -> bank %lld", amount, j.id, j.title.c_str(), bank);
        MoneyResultMsg res;   // unsolicited (requestId 0): the client takes the server's balances
        res.ok = true; res.wallet = wallet; res.bank = bank;
        wire::Writer w;
        Put(w, res);
        Send(c->peer, w, kChanEvents, true);
    }
}

// race order: finished first (by time), then by checkpoints passed, then by who got there first
std::vector<RaceEntry> Server::RaceOrder(const Job &j) const {
    std::vector<std::pair<RaceEntry, long long>> rows;
    for (const auto &kv : j.racers) rows.push_back({RaceEntry{kv.first, kv.second.checkpoints, kv.second.finishMs}, kv.second.atMs});
    if (j.kind == kJobSurvival)   // most kills, then the most waves, then who got there first
        std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
            if (a.first.checkpoints != b.first.checkpoints) return a.first.checkpoints > b.first.checkpoints;
            if ((a.first.finishMs & 0xFFFF) != (b.first.finishMs & 0xFFFF)) return (a.first.finishMs & 0xFFFF) > (b.first.finishMs & 0xFFFF);
            return a.second < b.second;
        });
    else if (j.kind == kJobDeathmatch)   // most kills, then fewest deaths, then who got there first
        std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
            if (a.first.checkpoints != b.first.checkpoints) return a.first.checkpoints > b.first.checkpoints;
            if (a.first.finishMs != b.first.finishMs) return a.first.finishMs < b.first.finishMs;
            return a.second < b.second;
        });
    else std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
        bool fa = a.first.finishMs != 0, fb = b.first.finishMs != 0;
        if (fa != fb) return fa;
        if (fa) return a.first.finishMs < b.first.finishMs;
        if (a.first.checkpoints != b.first.checkpoints) return a.first.checkpoints > b.first.checkpoints;
        return a.second < b.second;
    });
    std::vector<RaceEntry> out;
    for (auto &r : rows) out.push_back(r.first);
    return out;
}

void Server::SendRaceStandings(const Job &j) {
    RaceStandingsMsg m;
    m.jobId = j.id;
    m.entries = RaceOrder(j);
    wire::Writer w;
    Put(w, m);
    for (auto &kv : clients_) {
        const Client &c = kv.second;
        if (c.authed && j.racers.count(c.netId)) Send(kv.first, w, kChanEvents, true);
    }
}

void Server::HandleRaceProgress(Client &c, const RaceProgressMsg &m) {
    for (auto &kv : jobs_) {
        Job &j = kv.second;
        if ((j.kind != kJobRace && j.kind != kJobSurvival) || j.status != kJobRunning) continue;
        auto it = j.racers.find(c.netId);
        if (it == j.racers.end()) continue;
        Job::Racer &r = it->second;
        if (j.kind == kJobSurvival) {   // kills never go back; the waves only grow, the down flag comes and goes
            if (NowMs() < j.startAt || m.checkpoints < r.checkpoints) return;
            uint32_t waves = m.finishMs & 0xFFFF, had = r.finishMs & 0xFFFF;
            if (waves < had) waves = had;
            if (waves > kSurvivalWaves) waves = kSurvivalWaves;
            if (m.checkpoints != r.checkpoints) r.atMs = NowMs();
            r.checkpoints = m.checkpoints;
            r.finishMs = waves | (m.finishMs & kSurvivalDown);
            SendRaceStandings(j);
            return;
        }
        if (r.finishMs || NowMs() < j.startAt) return;              // finished already / before the start
        if (m.checkpoints < r.checkpoints) return;                   // never backwards
        if (m.checkpoints != r.checkpoints) { r.checkpoints = m.checkpoints; r.atMs = NowMs(); }
        if (m.finishMs) {
            r.finishMs = m.finishMs;
            LOGI("jobs", c.name.c_str(), "finished race %u \"%s\" in %.3f s", j.id, j.title.c_str(), m.finishMs / 1000.0);
        }
        SendRaceStandings(j);
        return;
    }
}

void Server::HandleJobUpdate(Client &c, const JobUpdateMsg &m) {
    Job *j = nullptr;
    for (auto &kv : jobs_) if (kv.second.host == c.netId) j = &kv.second;
    if (!j) return;
    bool statusChanged = j->status != m.status;
    if (j->status == kJobLobby && m.status == kJobRunning) {
        j->startedMs = NowMs();
        if (StartsTogether(j->kind)) {   // everyone starts together: the countdown ends at startAt
            j->startAt = NowMs() + kRaceStartDelayMs;
            j->startedMs = j->startAt;
            j->racers.clear();
            j->racers[j->host] = {};
            for (uint16_t id : j->members) j->racers[id] = {};
            LOGI("jobs", c.name.c_str(), "%s %u \"%s\" starts with %d player(s)", j->kind == kJobRace ? "race" : j->kind == kJobDeathmatch ? "deathmatch" : "survival", j->id, j->title.c_str(), (int)j->racers.size());
        }
    }
    j->status = m.status; j->step = m.step; j->objective = m.objective; j->text = m.text;
    if (m.status == kJobPassed) {
        LOGI("jobs", c.name.c_str(), "job %u \"%s\" passed with %d member(s)", j->id, j->title.c_str(), (int)j->members.size());
        PayJob(*j);
    } else if (m.status == kJobFailed) LOGI("jobs", c.name.c_str(), "job %u \"%s\" failed", j->id, j->title.c_str());
    SendJobState(*j);
    if (m.status >= kJobPassed) { CloseJob(j->id, m.status == kJobPassed ? "passed" : m.status == kJobFailed ? "failed" : "closed by host"); return; }
    if (statusChanged) { SendJobList(nullptr); RefreshSessions(); }
}
