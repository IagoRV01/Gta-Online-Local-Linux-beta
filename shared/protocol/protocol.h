// GTA Local Online wire protocol, version 1 — GTA client <-> Windows game server.
// Transport: ENet (UDP with optional reliability), see docs/protocol.md for the rationale.
//
// Every ENet packet carries exactly one message:
//   u8  magic  = 0x4C ('L')
//   u8  version = kProtocolVersion
//   u16 type   (MsgType)
//   ... payload (little endian, layout per message below)
// Channels:
//   0 kChanControl   reliable, ordered   handshake, auth, profile, join/leave, commands
//   1 kChanState     unreliable, sequenced   player state 20 Hz up, snapshots 20 Hz down
//   2 kChanEvents    reliable, ordered   gameplay events (shots, damage, deaths...) — Phase 6+
// The server is the authority: clients send *requests* and *their own* state; everything that
// matters (profile, money, ownership, world) is decided and stored by the server.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "protocol/wire.h"

namespace proto {

constexpr uint8_t kMagic = 0x4C;
constexpr uint8_t kProtocolVersion = 11;  // v2 vehicle; v3 aim+combat; v4 wanted+world; v5 economy; v6 vehicles; v7 bank; v8 ledger; v9 co-op jobs; v10 races; v11 player info + sessions
constexpr uint8_t kChanControl = 0, kChanState = 1, kChanEvents = 2, kNumChannels = 3;
constexpr size_t kMaxPacket = 64 * 1024;       // anything bigger is dropped before parsing
constexpr size_t kMaxProfileJson = 48 * 1024;
constexpr size_t kMaxName = 24, kMaxToken = 128, kMaxReason = 200;

enum class MsgType : uint16_t {
    // control (C = client -> server, S = server -> client)
    Hello = 1,           // C  {u32 gameBuild, str clientVersion}
    Welcome = 2,         // S  {u16 tickRate, u32 serverTimeMs, str serverName}
    Reject = 3,          // S  {u8 code, str reason}   followed by a disconnect
    Auth = 4,            // C  {str playerName, str token}
    AuthResult = 5,      // S  {u8 ok, u16 netId, str playerUuid, str newToken, str reason}  newToken only on first registration
    ProfileRequest = 6,  // C  {}
    Profile = 7,         // S  {u8 exists, u32 revision, str json}   exists=0 -> go to the character creator
    ProfileSave = 8,     // C  {u32 baseRevision, str json}
    ProfileSaved = 9,    // S  {u8 ok, u32 revision, str reason}
    PlayerJoined = 10,   // S  {u16 netId, str name, str appearanceJson}
    PlayerLeft = 11,     // S  {u16 netId, u8 reason}
    AppearanceUpdate = 12,   // C  {str json}   own look (character JSON without state); server validates + relays
    PlayerAppearance = 13,   // S  {u16 netId, str json}
    PlayerInfo = 14,         // C  {u16 rank, u8 flags}                      own rank and InfoFlags (Gori sets them)
    PlayerInfoRelay = 15,    // S  {u16 netId, u16 rank, u8 flags, u32 session}  to everyone, the player itself included
    // combat (channel 2, reliable) — Phase 6
    Shot = 30,           // C  {u32 weapon, vec3 origin, vec3 target}          visual only
    ShotRelay = 31,      // S  {u16 shooter, u32 weapon, vec3 origin, vec3 target}
    Damage = 32,         // C  {u16 target, u16 amount, u32 weapon, u16 bone}   "I hit this player"
    DamageApply = 33,    // S  {u16 attacker, u16 amount, u32 weapon, u16 bone}  to the victim only
    Death = 34,          // C  {u16 killer, u32 weapon}                         0 = not killed by a player
    KillFeed = 35,       // S  {u16 victim, u16 killer, u32 weapon}
    // world (channel 0) — Phase 7
    WorldState = 40,     // S  {u8 h, u8 m, u8 s, u8 flags, str weather, str nextWeather, f32 transition}
    // economy (channel 2) — Phase 8: the server owns every balance
    MoneyChange = 50,    // C  {u32 requestId, u8 account, i64 delta, str reason}   account: 0 wallet, 1 bank, 2 deposit (wallet->bank; negative = withdraw)
    MoneyResult = 51,    // S  {u32 requestId, u8 ok, i64 wallet, i64 bank, str reason}
    LedgerRequest = 52,  // C  {u32 requestId}                                   the Maze Bank transaction log
    Ledger = 53,         // S  {u32 requestId, u8 count, count x {i64 delta, u8 account, str reason, str at}}  newest first
    // personal vehicles (channel 0) — Phase 8
    VehicleListRequest = 60,   // C  {}
    VehicleList = 61,          // S  {u8 count, count x str json}         (vehicle::ToJson)
    VehicleRegister = 62,      // C  {u32 requestId, str json}            the car we are in becomes ours (active)
    VehicleUpdate = 63,        // C  {u32 requestId, str uuid, u8 state, vec3 pos, f32 heading, f32 engine, f32 body}
    VehicleResult = 64,        // S  {u32 requestId, u8 ok, str uuid, str reason}
    // co-op jobs (channel 2) — a host runs the mission, the members follow its objective and are paid by the server
    JobOpen = 70,        // C  {str title, vec3 start, u32 payout, u8 kind}                          host opens (replaces) its job
    JobList = 71,        // S  {u8 count, count x JobInfo (+ u8 kind)}                                to everyone, on any change
    JobJoin = 72,        // C  {u32 jobId}                                                            0 = leave
    JobUpdate = 73,      // C  {u8 status, u16 step, vec3 objective, str text}                        host only
    JobState = 74,       // S  {u32 jobId, u16 host, u8 status, u16 step, vec3 objective, str text, u32 payout, u8 count, count x u16, u8 kind, u32 startAt}  to host + members
    RaceProgress = 75,   // C  {u16 checkpoints, u32 finishMs}                                        a racer's progress in its race job
    RaceStandings = 76,  // S  {u32 jobId, u8 count, count x {u16 netId, u16 checkpoints, u32 finishMs}} in race order, to host + members
    // state (channel 1)
    PlayerState = 20,    // C  PlayerStateMsg
    Snapshot = 21,       // S  {u32 tick, u32 serverTimeMs, u8 count, count x RemoteState}
};

enum class RejectCode : uint8_t { BadVersion = 1, ServerFull = 2, BadAuth = 3, AlreadyConnected = 4, Malformed = 5, Banned = 6 };

// Movement flags in PlayerState / RemoteState.
enum StateFlags : uint16_t {
    kFlagInVehicle = 1 << 0,   // Phase 5
    kFlagAiming = 1 << 1,
    kFlagShooting = 1 << 2,
    kFlagDead = 1 << 3,
    kFlagRagdoll = 1 << 4,
    kFlagJumping = 1 << 5,
    kFlagCrouch = 1 << 6,
};
// Locomotion as GTA reports it (IS_PED_WALKING / RUNNING / SPRINTING / STILL) -> remote anim choice.
enum class Move : uint8_t { Still = 0, Walk = 1, Run = 2, Sprint = 3, Air = 4 };

struct Vec3 { float x = 0, y = 0, z = 0; };
struct Quat { float x = 0, y = 0, z = 0, w = 1; };

// Vehicle flags (VehicleBlock::vflags).
enum VehicleFlags : uint8_t { kVehEngine = 1 << 0, kVehLights = 1 << 1, kVehHighBeams = 1 << 2, kVehSiren = 1 << 3, kVehHorn = 1 << 4 };

// Present in PlayerState/RemoteState only when flags has kFlagInVehicle.
// The driver owns the vehicle: its pos/rot/vel are authoritative from the driver's client only;
// passengers send the block too (seat + driverNetId) so others can seat them correctly.
struct VehicleBlock {
    uint32_t model = 0;
    int8_t seat = -1;             // -1 driver, 0.. passengers
    uint16_t driverNetId = 0;     // 0 = no player driving (NPC/empty)
    Vec3 pos, vel, angVel;
    Quat rot;
    uint8_t color1 = 0, color2 = 0;
    uint8_t vflags = 0;
};

struct PlayerStateMsg {             // client's own ped, ~20 Hz, channel 1
    uint32_t seq = 0;               // monotonically increasing per client; server drops older ones
    uint32_t clientTimeMs = 0;
    Vec3 pos, vel;
    float heading = 0;              // degrees 0..360
    Move move = Move::Still;
    uint16_t flags = 0;
    uint8_t health = 0, armour = 0; // health = GET_ENTITY_HEALTH - 100 (0..100), armour 0..100
    uint8_t wanted = 0;             // GTA wanted level 0..5 (police stay local to each client)
    uint32_t weapon = 0;            // current weapon hash
    VehicleBlock veh;               // valid when flags & kFlagInVehicle
    Vec3 aim;                       // valid when flags & (kFlagAiming | kFlagShooting): world point aimed at
};

struct RemoteState {                // one player inside a Snapshot
    uint16_t netId = 0;
    uint32_t seq = 0;               // the owner's seq this was built from
    Vec3 pos, vel;
    float heading = 0;
    Move move = Move::Still;
    uint16_t flags = 0;
    uint8_t health = 0, armour = 0;
    uint8_t wanted = 0;
    uint32_t weapon = 0;
    VehicleBlock veh;
    Vec3 aim;
};

struct Header { uint8_t magic = 0, version = 0; MsgType type{}; };

// ---- encoding helpers (header + payload) ---------------------------------------------------

inline void PutHeader(wire::Writer &w, MsgType t) { w.U8(kMagic); w.U8(kProtocolVersion); w.U16((uint16_t)t); }

// Reads and checks the header; false for wrong magic/version or a truncated header.
inline bool GetHeader(wire::Reader &r, Header &h) {
    h.magic = r.U8(); h.version = r.U8(); h.type = (MsgType)r.U16();
    return r.ok() && h.magic == kMagic && h.version == kProtocolVersion;
}

inline void PutVec(wire::Writer &w, const Vec3 &v) { w.F32(v.x); w.F32(v.y); w.F32(v.z); }
inline Vec3 GetVec(wire::Reader &r) { Vec3 v; v.x = r.F32(); v.y = r.F32(); v.z = r.F32(); return v; }

inline void PutVehicle(wire::Writer &w, const VehicleBlock &v) {
    w.U32(v.model); w.U8((uint8_t)v.seat); w.U16(v.driverNetId);
    PutVec(w, v.pos); w.F32(v.rot.x); w.F32(v.rot.y); w.F32(v.rot.z); w.F32(v.rot.w);
    PutVec(w, v.vel); PutVec(w, v.angVel);
    w.U8(v.color1); w.U8(v.color2); w.U8(v.vflags);
}
// Rejects nonsense: unknown seat, zero model, non-unit quaternion, absurd speeds.
inline bool GetVehicle(wire::Reader &r, VehicleBlock &v) {
    v.model = r.U32(); v.seat = (int8_t)r.U8(); v.driverNetId = r.U16();
    v.pos = GetVec(r); v.rot.x = r.F32(); v.rot.y = r.F32(); v.rot.z = r.F32(); v.rot.w = r.F32();
    v.vel = GetVec(r); v.angVel = GetVec(r);
    v.color1 = r.U8(); v.color2 = r.U8(); v.vflags = r.U8();
    if (!r.ok()) return false;
    float qn = v.rot.x * v.rot.x + v.rot.y * v.rot.y + v.rot.z * v.rot.z + v.rot.w * v.rot.w;
    float sp = v.vel.x * v.vel.x + v.vel.y * v.vel.y + v.vel.z * v.vel.z;
    return v.model != 0 && v.seat >= -1 && v.seat <= 15 && qn > 0.8f && qn < 1.2f && sp < 250.f * 250.f;
}

// Heading is quantised to u16 (0.0055 deg steps) — plenty, and halves the bytes.
inline uint16_t PackHeading(float deg) {
    float d = deg - 360.f * (float)(long)(deg / 360.f);
    if (d < 0) d += 360.f;
    return (uint16_t)(d / 360.f * 65536.f);
}
inline float UnpackHeading(uint16_t q) { return (float)q * 360.f / 65536.f; }

inline void PutPlayerState(wire::Writer &w, const PlayerStateMsg &s) {
    PutHeader(w, MsgType::PlayerState);
    w.U32(s.seq); w.U32(s.clientTimeMs);
    PutVec(w, s.pos); PutVec(w, s.vel);
    w.U16(PackHeading(s.heading)); w.U8((uint8_t)s.move); w.U16(s.flags);
    w.U8(s.health); w.U8(s.armour); w.U8(s.wanted); w.U32(s.weapon);
    if (s.flags & kFlagInVehicle) PutVehicle(w, s.veh);
    if (s.flags & (kFlagAiming | kFlagShooting)) PutVec(w, s.aim);
}
inline bool GetPlayerState(wire::Reader &r, PlayerStateMsg &s) {   // after GetHeader
    s.seq = r.U32(); s.clientTimeMs = r.U32();
    s.pos = GetVec(r); s.vel = GetVec(r);
    s.heading = UnpackHeading(r.U16()); s.move = (Move)r.U8(); s.flags = r.U16();
    s.health = r.U8(); s.armour = r.U8(); s.wanted = r.U8(); s.weapon = r.U32();
    if ((s.flags & kFlagInVehicle) && !GetVehicle(r, s.veh)) return false;
    if (s.flags & (kFlagAiming | kFlagShooting)) s.aim = GetVec(r);
    return r.Done() && (uint8_t)s.move <= (uint8_t)Move::Air && s.wanted <= 5;
}

inline void PutRemote(wire::Writer &w, const RemoteState &s) {
    w.U16(s.netId); w.U32(s.seq);
    PutVec(w, s.pos); PutVec(w, s.vel);
    w.U16(PackHeading(s.heading)); w.U8((uint8_t)s.move); w.U16(s.flags);
    w.U8(s.health); w.U8(s.armour); w.U8(s.wanted); w.U32(s.weapon);
    if (s.flags & kFlagInVehicle) PutVehicle(w, s.veh);
    if (s.flags & (kFlagAiming | kFlagShooting)) PutVec(w, s.aim);
}
inline bool GetRemote(wire::Reader &r, RemoteState &s) {
    s.netId = r.U16(); s.seq = r.U32();
    s.pos = GetVec(r); s.vel = GetVec(r);
    s.heading = UnpackHeading(r.U16()); s.move = (Move)r.U8(); s.flags = r.U16();
    s.health = r.U8(); s.armour = r.U8(); s.wanted = r.U8(); s.weapon = r.U32();
    if ((s.flags & kFlagInVehicle) && !GetVehicle(r, s.veh)) return false;
    if (s.flags & (kFlagAiming | kFlagShooting)) s.aim = GetVec(r);
    return r.ok() && (uint8_t)s.move <= (uint8_t)Move::Air && s.wanted <= 5;
}

constexpr size_t kMaxSnapshotPlayers = 32;

inline void PutSnapshot(wire::Writer &w, uint32_t tick, uint32_t serverTimeMs, const std::vector<RemoteState> &players) {
    PutHeader(w, MsgType::Snapshot);
    w.U32(tick); w.U32(serverTimeMs);
    size_t n = players.size() < kMaxSnapshotPlayers ? players.size() : kMaxSnapshotPlayers;
    w.U8((uint8_t)n);
    for (size_t i = 0; i < n; ++i) PutRemote(w, players[i]);
}
inline bool GetSnapshot(wire::Reader &r, uint32_t &tick, uint32_t &serverTimeMs, std::vector<RemoteState> &players) {
    tick = r.U32(); serverTimeMs = r.U32();
    uint8_t n = r.U8();
    if (!r.ok() || n > kMaxSnapshotPlayers) return false;
    players.resize(n);
    for (auto &p : players)
        if (!GetRemote(r, p)) return false;
    return r.Done();
}

// ---- control messages --------------------------------------------------------------------------

struct HelloMsg { uint32_t gameBuild = 0; std::string clientVersion; };
struct WelcomeMsg { uint16_t tickRate = 0; uint32_t serverTimeMs = 0; std::string serverName; };
struct RejectMsg { RejectCode code{}; std::string reason; };
struct AuthMsg { std::string name, token; };
struct AuthResultMsg { bool ok = false; uint16_t netId = 0; std::string playerUuid, newToken, reason; };
struct ProfileMsg { bool exists = false; uint32_t revision = 0; std::string json; };
struct ProfileSaveMsg { uint32_t baseRevision = 0; std::string json; };
struct ProfileSavedMsg { bool ok = false; uint32_t revision = 0; std::string reason; };

inline void Put(wire::Writer &w, const HelloMsg &m) { PutHeader(w, MsgType::Hello); w.U32(m.gameBuild); w.Str(m.clientVersion, 32); }
inline bool Get(wire::Reader &r, HelloMsg &m) { m.gameBuild = r.U32(); m.clientVersion = r.Str(32); return r.Done(); }
inline void Put(wire::Writer &w, const WelcomeMsg &m) { PutHeader(w, MsgType::Welcome); w.U16(m.tickRate); w.U32(m.serverTimeMs); w.Str(m.serverName, 64); }
inline bool Get(wire::Reader &r, WelcomeMsg &m) { m.tickRate = r.U16(); m.serverTimeMs = r.U32(); m.serverName = r.Str(64); return r.Done(); }
inline void Put(wire::Writer &w, const RejectMsg &m) { PutHeader(w, MsgType::Reject); w.U8((uint8_t)m.code); w.Str(m.reason, kMaxReason); }
inline bool Get(wire::Reader &r, RejectMsg &m) { m.code = (RejectCode)r.U8(); m.reason = r.Str(kMaxReason); return r.Done(); }
inline void Put(wire::Writer &w, const AuthMsg &m) { PutHeader(w, MsgType::Auth); w.Str(m.name, kMaxName); w.Str(m.token, kMaxToken); }
inline bool Get(wire::Reader &r, AuthMsg &m) { m.name = r.Str(kMaxName); m.token = r.Str(kMaxToken); return r.Done(); }
inline void Put(wire::Writer &w, const AuthResultMsg &m) {
    PutHeader(w, MsgType::AuthResult); w.U8(m.ok); w.U16(m.netId); w.Str(m.playerUuid, 36); w.Str(m.newToken, kMaxToken); w.Str(m.reason, kMaxReason);
}
inline bool Get(wire::Reader &r, AuthResultMsg &m) {
    m.ok = r.U8() != 0; m.netId = r.U16(); m.playerUuid = r.Str(36); m.newToken = r.Str(kMaxToken); m.reason = r.Str(kMaxReason); return r.Done();
}
inline void PutProfileRequest(wire::Writer &w) { PutHeader(w, MsgType::ProfileRequest); }
inline void Put(wire::Writer &w, const ProfileMsg &m) { PutHeader(w, MsgType::Profile); w.U8(m.exists); w.U32(m.revision); w.Str(m.json, kMaxProfileJson); }
inline bool Get(wire::Reader &r, ProfileMsg &m) { m.exists = r.U8() != 0; m.revision = r.U32(); m.json = r.Str(kMaxProfileJson); return r.Done(); }
inline void Put(wire::Writer &w, const ProfileSaveMsg &m) { PutHeader(w, MsgType::ProfileSave); w.U32(m.baseRevision); w.Str(m.json, kMaxProfileJson); }
inline bool Get(wire::Reader &r, ProfileSaveMsg &m) { m.baseRevision = r.U32(); m.json = r.Str(kMaxProfileJson); return r.Done(); }
inline void Put(wire::Writer &w, const ProfileSavedMsg &m) { PutHeader(w, MsgType::ProfileSaved); w.U8(m.ok); w.U32(m.revision); w.Str(m.reason, kMaxReason); }
inline bool Get(wire::Reader &r, ProfileSavedMsg &m) { m.ok = r.U8() != 0; m.revision = r.U32(); m.reason = r.Str(kMaxReason); return r.Done(); }

// Player info (v11). The session is the server's: the id of the running job the player is in, a private one
// (0x80000000 | netId) for a solo activity, 0 for freemode. Players only see, shoot and hurt their own session.
enum InfoFlags : uint8_t { kInfoPassive = 1 << 0, kInfoActivity = 1 << 1 };
constexpr uint16_t kMaxRank = 8000;
constexpr uint32_t kPrivateSession = 0x80000000u;
struct PlayerInfoMsg { uint16_t rank = 0; uint8_t flags = 0; };
struct PlayerInfoRelayMsg { uint16_t netId = 0, rank = 0; uint8_t flags = 0; uint32_t session = 0; };
inline void Put(wire::Writer &w, const PlayerInfoMsg &m) { PutHeader(w, MsgType::PlayerInfo); w.U16(m.rank); w.U8(m.flags); }
inline bool Get(wire::Reader &r, PlayerInfoMsg &m) { m.rank = r.U16(); m.flags = r.U8(); return r.Done() && m.rank <= kMaxRank && m.flags < 4; }
inline void Put(wire::Writer &w, const PlayerInfoRelayMsg &m) { PutHeader(w, MsgType::PlayerInfoRelay); w.U16(m.netId); w.U16(m.rank); w.U8(m.flags); w.U32(m.session); }
inline bool Get(wire::Reader &r, PlayerInfoRelayMsg &m) { m.netId = r.U16(); m.rank = r.U16(); m.flags = r.U8(); m.session = r.U32(); return r.Done(); }

struct AppearanceUpdateMsg { std::string json; };
struct PlayerAppearanceMsg { uint16_t netId = 0; std::string json; };
struct PlayerJoinedMsg { uint16_t netId = 0; std::string name; };
struct PlayerLeftMsg { uint16_t netId = 0; uint8_t reason = 0; };
inline void Put(wire::Writer &w, const AppearanceUpdateMsg &m) { PutHeader(w, MsgType::AppearanceUpdate); w.Str(m.json, kMaxProfileJson); }
inline bool Get(wire::Reader &r, AppearanceUpdateMsg &m) { m.json = r.Str(kMaxProfileJson); return r.Done(); }
inline void Put(wire::Writer &w, const PlayerAppearanceMsg &m) { PutHeader(w, MsgType::PlayerAppearance); w.U16(m.netId); w.Str(m.json, kMaxProfileJson); }
inline bool Get(wire::Reader &r, PlayerAppearanceMsg &m) { m.netId = r.U16(); m.json = r.Str(kMaxProfileJson); return r.Done(); }
// PlayerJoined keeps its v1 layout {netId, name, appearanceJson(always empty now)}.
inline void Put(wire::Writer &w, const PlayerJoinedMsg &m) { PutHeader(w, MsgType::PlayerJoined); w.U16(m.netId); w.Str(m.name, kMaxName); w.Str(""); }
inline bool Get(wire::Reader &r, PlayerJoinedMsg &m) { m.netId = r.U16(); m.name = r.Str(kMaxName); r.Str(); return r.Done(); }
inline void Put(wire::Writer &w, const PlayerLeftMsg &m) { PutHeader(w, MsgType::PlayerLeft); w.U16(m.netId); w.U8(m.reason); }
inline bool Get(wire::Reader &r, PlayerLeftMsg &m) { m.netId = r.U16(); m.reason = r.U8(); return r.Done(); }

// ---- combat ------------------------------------------------------------------------------------
constexpr uint16_t kMaxDamagePerHit = 300;   // more than any single GTA weapon hit on a 200 hp ped

struct ShotMsg { uint32_t weapon = 0; Vec3 origin, target; };
struct ShotRelayMsg { uint16_t shooter = 0; uint32_t weapon = 0; Vec3 origin, target; };
struct DamageMsg { uint16_t target = 0, amount = 0; uint32_t weapon = 0; uint16_t bone = 0; };        // bone = GTA bone id (31086 head)
struct DamageApplyMsg { uint16_t attacker = 0, amount = 0; uint32_t weapon = 0; uint16_t bone = 0; };
struct DeathMsg { uint16_t killer = 0; uint32_t weapon = 0; };
struct KillFeedMsg { uint16_t victim = 0, killer = 0; uint32_t weapon = 0; };

inline void Put(wire::Writer &w, const ShotMsg &m) { PutHeader(w, MsgType::Shot); w.U32(m.weapon); PutVec(w, m.origin); PutVec(w, m.target); }
inline bool Get(wire::Reader &r, ShotMsg &m) { m.weapon = r.U32(); m.origin = GetVec(r); m.target = GetVec(r); return r.Done(); }
inline void Put(wire::Writer &w, const ShotRelayMsg &m) { PutHeader(w, MsgType::ShotRelay); w.U16(m.shooter); w.U32(m.weapon); PutVec(w, m.origin); PutVec(w, m.target); }
inline bool Get(wire::Reader &r, ShotRelayMsg &m) { m.shooter = r.U16(); m.weapon = r.U32(); m.origin = GetVec(r); m.target = GetVec(r); return r.Done(); }
inline void Put(wire::Writer &w, const DamageMsg &m) { PutHeader(w, MsgType::Damage); w.U16(m.target); w.U16(m.amount); w.U32(m.weapon); w.U16(m.bone); }
inline bool Get(wire::Reader &r, DamageMsg &m) { m.target = r.U16(); m.amount = r.U16(); m.weapon = r.U32(); m.bone = r.U16(); return r.Done() && m.amount > 0 && m.amount <= kMaxDamagePerHit; }
inline void Put(wire::Writer &w, const DamageApplyMsg &m) { PutHeader(w, MsgType::DamageApply); w.U16(m.attacker); w.U16(m.amount); w.U32(m.weapon); w.U16(m.bone); }
inline bool Get(wire::Reader &r, DamageApplyMsg &m) { m.attacker = r.U16(); m.amount = r.U16(); m.weapon = r.U32(); m.bone = r.U16(); return r.Done() && m.amount <= kMaxDamagePerHit; }

// PvP damage of one hit: the weapon's base damage scaled by where it landed. The health a puppet
// loses locally is NOT used as the amount: story mode scales player-vs-ped damage up a lot
// (a pistol round measured 208 on a ped), which would make PvP one-shot.
inline uint16_t PvpDamage(float weaponBase, uint16_t bone, int measured) {
    float mult = 1.f;
    if (bone == 31086 || bone == 39317 || bone == 12844) mult = 2.5f;          // head, neck, head (FB_)
    else if (bone == 45509 || bone == 61163 || bone == 18905 || bone == 40269 || bone == 28252 || bone == 57005 ||
             bone == 58271 || bone == 63931 || bone == 14201 || bone == 51826 || bone == 36864 || bone == 52301) mult = 0.8f;   // arms/legs
    float d = weaponBase > 0.f ? weaponBase * mult : (float)(measured < 50 ? measured : 50);    // no base: vehicles, melee...
    if (d < 1.f) d = 1.f;
    if (d > kMaxDamagePerHit) d = kMaxDamagePerHit;
    return (uint16_t)(d + 0.5f);
}
inline void Put(wire::Writer &w, const DeathMsg &m) { PutHeader(w, MsgType::Death); w.U16(m.killer); w.U32(m.weapon); }
inline bool Get(wire::Reader &r, DeathMsg &m) { m.killer = r.U16(); m.weapon = r.U32(); return r.Done(); }
inline void Put(wire::Writer &w, const KillFeedMsg &m) { PutHeader(w, MsgType::KillFeed); w.U16(m.victim); w.U16(m.killer); w.U32(m.weapon); }
inline bool Get(wire::Reader &r, KillFeedMsg &m) { m.victim = r.U16(); m.killer = r.U16(); m.weapon = r.U32(); return r.Done(); }

// ---- world (Phase 7) -------------------------------------------------------------------------------
enum WorldFlags : uint8_t { kWorldClockFrozen = 1 << 0, kWorldBlackout = 1 << 1 };
struct WorldStateMsg {
    uint8_t hour = 12, minute = 0, second = 0, flags = 0;
    std::string weather = "CLEAR", nextWeather = "CLEAR";
    float transition = 0.f;   // 0 = weather, 1 = nextWeather
};
inline void Put(wire::Writer &w, const WorldStateMsg &m) {
    PutHeader(w, MsgType::WorldState); w.U8(m.hour); w.U8(m.minute); w.U8(m.second); w.U8(m.flags);
    w.Str(m.weather, 16); w.Str(m.nextWeather, 16); w.F32(m.transition);
}
inline bool Get(wire::Reader &r, WorldStateMsg &m) {
    m.hour = r.U8(); m.minute = r.U8(); m.second = r.U8(); m.flags = r.U8();
    m.weather = r.Str(16); m.nextWeather = r.Str(16); m.transition = r.F32();
    return r.Done() && m.hour < 24 && m.minute < 60 && m.second < 60 && m.transition >= 0.f && m.transition <= 1.f;
}

// ---- economy (Phase 8) -------------------------------------------------------------------------------
constexpr size_t kMaxMoneyReason = 40;
enum Account : uint8_t { kWallet = 0, kBank = 1, kDeposit = 2 };
struct MoneyChangeMsg { uint32_t requestId = 0; uint8_t account = kWallet; int64_t delta = 0; std::string reason; };
struct MoneyResultMsg { uint32_t requestId = 0; bool ok = false; int64_t wallet = 0, bank = 0; std::string reason; };
inline void Put(wire::Writer &w, const MoneyChangeMsg &m) { PutHeader(w, MsgType::MoneyChange); w.U32(m.requestId); w.U8(m.account); w.U64((uint64_t)m.delta); w.Str(m.reason, kMaxMoneyReason); }
inline bool Get(wire::Reader &r, MoneyChangeMsg &m) { m.requestId = r.U32(); m.account = r.U8(); m.delta = (int64_t)r.U64(); m.reason = r.Str(kMaxMoneyReason); return r.Done() && m.delta != 0 && m.account <= kDeposit; }
inline void Put(wire::Writer &w, const MoneyResultMsg &m) { PutHeader(w, MsgType::MoneyResult); w.U32(m.requestId); w.U8(m.ok); w.U64((uint64_t)m.wallet); w.U64((uint64_t)m.bank); w.Str(m.reason, kMaxReason); }
inline bool Get(wire::Reader &r, MoneyResultMsg &m) { m.requestId = r.U32(); m.ok = r.U8() != 0; m.wallet = (int64_t)r.U64(); m.bank = (int64_t)r.U64(); m.reason = r.Str(kMaxReason); return r.Done(); }

// The newest ledger rows of the character (the Maze Bank website's transaction log). account: kWallet
// or kBank (the ledger keeps one row per account touched; a deposit is -wallet / +bank).
constexpr size_t kMaxLedgerRows = 16, kMaxLedgerAt = 32;
struct LedgerRequestMsg { uint32_t requestId = 0; };
struct LedgerRow { int64_t delta = 0; uint8_t account = kWallet; std::string reason, at; };
struct LedgerMsg { uint32_t requestId = 0; std::vector<LedgerRow> rows; };
inline void Put(wire::Writer &w, const LedgerRequestMsg &m) { PutHeader(w, MsgType::LedgerRequest); w.U32(m.requestId); }
inline bool Get(wire::Reader &r, LedgerRequestMsg &m) { m.requestId = r.U32(); return r.Done(); }
inline void Put(wire::Writer &w, const LedgerMsg &m) {
    PutHeader(w, MsgType::Ledger);
    w.U32(m.requestId);
    size_t n = m.rows.size() < kMaxLedgerRows ? m.rows.size() : kMaxLedgerRows;
    w.U8((uint8_t)n);
    for (size_t i = 0; i < n; ++i) { w.U64((uint64_t)m.rows[i].delta); w.U8(m.rows[i].account); w.Str(m.rows[i].reason, kMaxReason); w.Str(m.rows[i].at, kMaxLedgerAt); }
}
inline bool Get(wire::Reader &r, LedgerMsg &m) {
    m.requestId = r.U32();
    uint8_t n = r.U8();
    if (n > kMaxLedgerRows) return false;
    m.rows.resize(n);
    for (auto &row : m.rows) { row.delta = (int64_t)r.U64(); row.account = r.U8(); row.reason = r.Str(kMaxReason); row.at = r.Str(kMaxLedgerAt); }
    return r.Done();
}

// ---- personal vehicles (Phase 8) ----------------------------------------------------------------------
constexpr size_t kMaxVehicleJson = 4096;
struct VehicleListMsg { std::vector<std::string> vehicles; };
struct VehicleRegisterMsg { uint32_t requestId = 0; std::string json; };
struct VehicleUpdateMsg { uint32_t requestId = 0; std::string uuid; uint8_t state = 0; Vec3 pos; float heading = 0, engine = 1000, body = 1000; };
struct VehicleResultMsg { uint32_t requestId = 0; bool ok = false; std::string uuid, reason; };
inline void PutVehicleListRequest(wire::Writer &w) { PutHeader(w, MsgType::VehicleListRequest); }
inline void Put(wire::Writer &w, const VehicleListMsg &m) {
    PutHeader(w, MsgType::VehicleList);
    size_t n = m.vehicles.size() < 32 ? m.vehicles.size() : 32;
    w.U8((uint8_t)n);
    for (size_t i = 0; i < n; ++i) w.Str(m.vehicles[i], kMaxVehicleJson);
}
inline bool Get(wire::Reader &r, VehicleListMsg &m) {
    uint8_t n = r.U8();
    if (n > 32) return false;
    m.vehicles.clear();
    for (uint8_t i = 0; i < n && r.ok(); ++i) m.vehicles.push_back(r.Str(kMaxVehicleJson));
    return r.Done();
}
inline void Put(wire::Writer &w, const VehicleRegisterMsg &m) { PutHeader(w, MsgType::VehicleRegister); w.U32(m.requestId); w.Str(m.json, kMaxVehicleJson); }
inline bool Get(wire::Reader &r, VehicleRegisterMsg &m) { m.requestId = r.U32(); m.json = r.Str(kMaxVehicleJson); return r.Done(); }
inline void Put(wire::Writer &w, const VehicleUpdateMsg &m) {
    PutHeader(w, MsgType::VehicleUpdate); w.U32(m.requestId); w.Str(m.uuid, 36); w.U8(m.state); PutVec(w, m.pos); w.F32(m.heading); w.F32(m.engine); w.F32(m.body);
}
inline bool Get(wire::Reader &r, VehicleUpdateMsg &m) {
    m.requestId = r.U32(); m.uuid = r.Str(36); m.state = r.U8(); m.pos = GetVec(r); m.heading = r.F32(); m.engine = r.F32(); m.body = r.F32();
    return r.Done() && m.state <= 3;
}
inline void Put(wire::Writer &w, const VehicleResultMsg &m) { PutHeader(w, MsgType::VehicleResult); w.U32(m.requestId); w.U8(m.ok); w.Str(m.uuid, 36); w.Str(m.reason, kMaxReason); }
inline bool Get(wire::Reader &r, VehicleResultMsg &m) { m.requestId = r.U32(); m.ok = r.U8() != 0; m.uuid = r.Str(36); m.reason = r.Str(kMaxReason); return r.Done(); }

// ---- co-op jobs (v9) ----------------------------------------------------------------------------------
// The host's own mission script runs everything; the server keeps who is in which job, relays the host's
// current objective to the members, and pays the members when the host reports the job passed.
constexpr size_t kMaxJobTitle = 48, kMaxJobText = 96, kMaxJobs = 16, kMaxJobMembers = 8;
enum JobStatus : uint8_t { kJobLobby = 0, kJobRunning = 1, kJobPassed = 2, kJobFailed = 3, kJobClosed = 4 };
// v10: a race job's racers (host included) start together at the server time startAt and are paid by position.
// Deathmatches (kind 2, same wire format) start the same way; the server counts kills/deaths from Death
// messages and sends them as RaceStandings (checkpoints = kills, finishMs = deaths).
enum JobKind : uint8_t { kJobMission = 0, kJobRace = 1, kJobDeathmatch = 2, kJobSurvival = 3 };
// races, deathmatches and survivals start for everyone at once, after a countdown
inline bool StartsTogether(uint8_t kind) { return kind == kJobRace || kind == kJobDeathmatch || kind == kJobSurvival; }
// survival: RaceProgress carries the kills (checkpoints) and, in finishMs, the waves the player has survived plus
// kSurvivalDown while they are down; each player is paid payout * waves / kSurvivalWaves
constexpr uint32_t kSurvivalWaves = 10, kSurvivalDown = 0x10000;
constexpr uint32_t kRaceStartDelayMs = 6000;   // lobby -> running: the countdown everyone sees
struct JobOpenMsg { std::string title; Vec3 start; uint32_t payout = 0; uint8_t kind = kJobMission; };
struct JobInfo { uint32_t id = 0; uint16_t host = 0; std::string title; Vec3 start; uint8_t members = 0, status = kJobLobby, kind = kJobMission; };
struct JobListMsg { std::vector<JobInfo> jobs; };
struct JobJoinMsg { uint32_t jobId = 0; };
struct JobUpdateMsg { uint8_t status = kJobRunning; uint16_t step = 0; Vec3 objective; std::string text; };
struct JobStateMsg { uint32_t jobId = 0; uint16_t host = 0; uint8_t status = kJobLobby; uint16_t step = 0; Vec3 objective; std::string text; uint32_t payout = 0; std::vector<uint16_t> members;
                     uint8_t kind = kJobMission; uint32_t startAt = 0; };
struct RaceProgressMsg { uint16_t checkpoints = 0; uint32_t finishMs = 0; };   // finishMs 0 = still racing
struct RaceEntry { uint16_t netId = 0, checkpoints = 0; uint32_t finishMs = 0; };
struct RaceStandingsMsg { uint32_t jobId = 0; std::vector<RaceEntry> entries; };
inline void Put(wire::Writer &w, const JobOpenMsg &m) { PutHeader(w, MsgType::JobOpen); w.Str(m.title, kMaxJobTitle); PutVec(w, m.start); w.U32(m.payout); w.U8(m.kind); }
inline bool Get(wire::Reader &r, JobOpenMsg &m) { m.title = r.Str(kMaxJobTitle); m.start = GetVec(r); m.payout = r.U32(); m.kind = r.U8(); return r.Done() && !m.title.empty() && m.kind <= kJobSurvival; }
inline void Put(wire::Writer &w, const JobListMsg &m) {
    PutHeader(w, MsgType::JobList);
    size_t n = m.jobs.size() < kMaxJobs ? m.jobs.size() : kMaxJobs;
    w.U8((uint8_t)n);
    for (size_t i = 0; i < n; ++i) { const JobInfo &j = m.jobs[i]; w.U32(j.id); w.U16(j.host); w.Str(j.title, kMaxJobTitle); PutVec(w, j.start); w.U8(j.members); w.U8(j.status); w.U8(j.kind); }
}
inline bool Get(wire::Reader &r, JobListMsg &m) {
    uint8_t n = r.U8();
    if (n > kMaxJobs) return false;
    m.jobs.resize(n);
    for (auto &j : m.jobs) { j.id = r.U32(); j.host = r.U16(); j.title = r.Str(kMaxJobTitle); j.start = GetVec(r); j.members = r.U8(); j.status = r.U8(); j.kind = r.U8(); }
    return r.Done();
}
inline void Put(wire::Writer &w, const JobJoinMsg &m) { PutHeader(w, MsgType::JobJoin); w.U32(m.jobId); }
inline bool Get(wire::Reader &r, JobJoinMsg &m) { m.jobId = r.U32(); return r.Done(); }
inline void Put(wire::Writer &w, const JobUpdateMsg &m) { PutHeader(w, MsgType::JobUpdate); w.U8(m.status); w.U16(m.step); PutVec(w, m.objective); w.Str(m.text, kMaxJobText); }
inline bool Get(wire::Reader &r, JobUpdateMsg &m) { m.status = r.U8(); m.step = r.U16(); m.objective = GetVec(r); m.text = r.Str(kMaxJobText); return r.Done() && m.status <= kJobClosed; }
inline void Put(wire::Writer &w, const JobStateMsg &m) {
    PutHeader(w, MsgType::JobState);
    w.U32(m.jobId); w.U16(m.host); w.U8(m.status); w.U16(m.step); PutVec(w, m.objective); w.Str(m.text, kMaxJobText); w.U32(m.payout);
    size_t n = m.members.size() < kMaxJobMembers ? m.members.size() : kMaxJobMembers;
    w.U8((uint8_t)n);
    for (size_t i = 0; i < n; ++i) w.U16(m.members[i]);
    w.U8(m.kind); w.U32(m.startAt);
}
inline bool Get(wire::Reader &r, JobStateMsg &m) {
    m.jobId = r.U32(); m.host = r.U16(); m.status = r.U8(); m.step = r.U16(); m.objective = GetVec(r); m.text = r.Str(kMaxJobText); m.payout = r.U32();
    uint8_t n = r.U8();
    if (n > kMaxJobMembers) return false;
    m.members.resize(n);
    for (auto &x : m.members) x = r.U16();
    m.kind = r.U8(); m.startAt = r.U32();
    return r.Done();
}
inline void Put(wire::Writer &w, const RaceProgressMsg &m) { PutHeader(w, MsgType::RaceProgress); w.U16(m.checkpoints); w.U32(m.finishMs); }
inline bool Get(wire::Reader &r, RaceProgressMsg &m) { m.checkpoints = r.U16(); m.finishMs = r.U32(); return r.Done(); }
inline void Put(wire::Writer &w, const RaceStandingsMsg &m) {
    PutHeader(w, MsgType::RaceStandings);
    w.U32(m.jobId);
    size_t n = m.entries.size() < kMaxJobMembers ? m.entries.size() : kMaxJobMembers;
    w.U8((uint8_t)n);
    for (size_t i = 0; i < n; ++i) { w.U16(m.entries[i].netId); w.U16(m.entries[i].checkpoints); w.U32(m.entries[i].finishMs); }
}
inline bool Get(wire::Reader &r, RaceStandingsMsg &m) {
    m.jobId = r.U32();
    uint8_t n = r.U8();
    if (n > kMaxJobMembers) return false;
    m.entries.resize(n);
    for (auto &e : m.entries) { e.netId = r.U16(); e.checkpoints = r.U16(); e.finishMs = r.U32(); }
    return r.Done();
}

// Sequence comparison with wrap-around (a newer than b).
inline bool SeqNewer(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }

}  // namespace proto
