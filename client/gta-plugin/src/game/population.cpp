#include "game/population.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>
#include "common/interp.h"
#include "common/log.h"
#include "engine/gta.h"
#include "engine/shv.h"
#include "game/net_client.h"
#include "game/remote.h"

// v16 (2026-11-18): every NPC and vehicle has one owner game, as in GTA Online (rage's network objects: an owner that
// runs it, clones on the other machines, proximity migration). See population.h and docs/multiplayer.md.
namespace population {

using namespace proto;

namespace {

constexpr int kSendEveryMs = 125;           // an owner's stream, 8 Hz
constexpr size_t kPacketBudget = 1100;      // bytes of entities per PopState (one UDP datagram)
constexpr float kPedRange = 180.f, kVehRange = 300.f, kMissionRange = 400.f;
constexpr size_t kMaxPeds = 90, kMaxVehicles = 70;
constexpr double kDelay = 0.15;             // puppets are drawn this far in the past
constexpr unsigned long long kPedTimeoutMs = 1500, kVehTimeoutMs = 2500;
constexpr int kMaxSpawnsPerFrame = 6;
// hand-over (proximity migration): an entity goes to the game whose player is clearly nearer, or that still has it in
// range when we no longer do; a new entity of ours that GTA put in a peer's part of the world goes (that game fills it)
constexpr float kHandRatio = 0.6f, kHandMinDist = 40.f, kCullRatio = 0.85f;
constexpr unsigned long long kOfferRetryMs = 3000, kMigratedShowMs = 2500, kGivenMs = 20000;
constexpr float kTakeOverRange = 250.f;     // a peer leaves our group: its NPCs this near us become ours

// ---------------------------------------------------------------- role
uint8_t g_mode = kPopOwn, g_flags = 0, g_viewers = 0;
uint16_t g_host = 0;
std::vector<uint16_t> g_peers;
bool IsPeer(uint16_t n) { return std::find(g_peers.begin(), g_peers.end(), n) != g_peers.end(); }
bool MissionHost() { return (g_flags & kPopRoleMission) && g_host && g_host == net::SelfNetId(); }

unsigned long long Now() { return GetTickCount64(); }
float Dist2(const gta::Vec3 &a, const gta::Vec3 &b) { float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z; return x * x + y * y + z * z; }
float Dist2D2(const gta::Vec3 &a, float x, float y) { float dx = a.x - x, dy = a.y - y; return dx * dx + dy * dy; }

int g_peds[1024], g_vehs[1024];
int AllPeds() { return shv::worldGetAllPeds ? shv::worldGetAllPeds(g_peds, 1024) : 0; }
int AllVehicles() { return shv::worldGetAllVehicles ? shv::worldGetAllVehicles(g_vehs, 1024) : 0; }
bool Mission(int e) { return NB(N<int>(N_IS_ENTITY_A_MISSION_ENTITY, e)); }
bool PolicePed(int ped) { int t = N<int>(N_GET_PED_TYPE, ped); return t == 6 || t == 27 || t == 29; }   // cop, swat, army
bool PoliceVehicle(int veh) {
    if (N<int>(N_GET_VEHICLE_CLASS, veh) != 18) return false;   // emergency
    int d = N<int>(N_GET_PED_IN_VEHICLE_SEAT, veh, -1, 0);
    return !gta::Exists(d) || PolicePed(d);
}
bool Train(int veh) { return NB(N<int>(N_IS_THIS_MODEL_A_TRAIN, gta::ModelOf(veh))); }
void Delete(int &e, bool ped) {
    if (!gta::Exists(e)) { e = 0; return; }
    gta::MakeMission(e);
    if (ped) N(N_DELETE_PED, &e); else N(N_DELETE_VEHICLE, &e);
    e = 0;
}
// The entity behind a script handle. GTA hands ambient entities a new script handle now and then (v15, 2026-11-18: ~20
// of ~100 a frame); the entity's own address stays the same for its whole life, so that is what our ids follow.
uint64_t Key(int h) {
    if (shv::getScriptHandleBaseAddress) if (BYTE *p = shv::getScriptHandleBaseAddress(h)) return (uint64_t)(uintptr_t)p;
    return (1ull << 63) | (uint32_t)h;
}
uint32_t PupKey(uint16_t owner, uint16_t id) { return ((uint32_t)owner << 16) | id; }
uint16_t OwnerOf(uint32_t key) { return (uint16_t)(key >> 16); }
uint16_t IdOfKey(uint32_t key) { return (uint16_t)(key & 0xFFFF); }

// where the peers' players are (their puppets here)
struct PeerAt { uint16_t id; gta::Vec3 pos; };
std::vector<PeerAt> g_peerAt;
void RefreshPeers() {
    g_peerAt.clear();
    for (uint16_t n : g_peers) {
        int ped = remote::PedOfNetId(n);
        if (ped && gta::Exists(ped)) g_peerAt.push_back({n, gta::Pos(ped)});
    }
}
const PeerAt *NearestPeer(const gta::Vec3 &p, float &d2) {
    const PeerAt *best = nullptr;
    d2 = 1e18f;
    for (auto &pa : g_peerAt) { float d = Dist2(pa.pos, p); if (d < d2) { d2 = d; best = &pa; } }
    return best;
}

// ---------------------------------------------------------------- player messages out, batched per receiver
std::map<uint16_t, std::string> g_out;
std::map<uint16_t, unsigned long long> g_outAt;
std::map<uint16_t, std::string> g_outPm;
void SendMsg(uint16_t to, const char *kind, const std::string &text) {
    if (!net::Connected() || text.empty() || !to) return;
    wire::Writer w;
    Put(w, PlayerMessageMsg{to, kind, text.substr(0, kMaxMsgText)});
    net::SendEvent(w);
}
void QueueTo(std::map<uint16_t, std::string> &box, const char *kind, uint16_t to, const std::string &item) {
    std::string &s = box[to];
    if (s.size() + item.size() + 1 > kMaxMsgText) { SendMsg(to, kind, s); s.clear(); }
    if (!s.empty()) s += ';';
    s += item;
}
void QueueEvent(uint16_t owner, const std::string &item) { QueueTo(g_out, kMsgPopEvent, owner, item); }
void FlushEvents(bool force = false) {
    unsigned long long now = Now();
    for (auto &kv : g_out) {
        if (kv.second.empty() || (!force && now - g_outAt[kv.first] < 140)) continue;
        SendMsg(kv.first, kMsgPopEvent, kv.second);
        kv.second.clear();
        g_outAt[kv.first] = now;
    }
    for (auto &kv : g_outPm) if (!kv.second.empty()) { SendMsg(kv.first, kMsgPopMigrate, kv.second); kv.second.clear(); }
}

// ================================================================ owner: stream our NPCs and traffic
struct Src {
    uint16_t id = 0;
    uint32_t model = 0;
    int h = 0;
    bool ped = true;
    int descLeft = 3;
    gta::Vec3 pos, vel;                 // vel from its last two frames (to find it again after GTA re-creates it)
    uint32_t frame = 0;                 // the last stream frame that listed it
    bool streamed = false;              // the peers have it (since we joined them)
    uint16_t fromOwner = 0, fromId = 0; // handed over to us: shown to the peers for a moment
    unsigned long long fromUntil = 0, offeredAt = 0;
};
std::unordered_map<uint64_t, Src> g_src;      // entity -> its stream id
struct Gone { Src s; unsigned long long at = 0; };
std::unordered_map<uint64_t, Gone> g_gone;    // entries not listed a moment (a filter flickered): the same id if they come back
std::map<uint16_t, uint64_t> g_srcById;       // stream id -> entity (events from the peers)
uint16_t g_nextId = 1;
unsigned g_srcBack = 0, g_culledPeds = 0;
unsigned g_srcNew = 0, g_culled = 0, g_offered = 0, g_handedOut = 0, g_handedIn = 0;
uint32_t g_seq = 0, g_frame = 0;
unsigned long long g_lastSend = 0, g_lastStat = 0;
int g_sentPeds = 0, g_sentVehs = 0;
float g_share = 1.f;                          // our part of the world around our player (the peers have the rest)
std::map<uint16_t, unsigned long long> g_npcDmgAt;   // victim -> last lo.nd
std::map<uint16_t, int> g_npcDmg;

// cars we gave to a remote player who got into them (their game drives them now): remote.cpp takes the entity over
struct Given { int h = 0; uint16_t to = 0; uint32_t model = 0; bool mission = false; unsigned long long until = 0; };
std::vector<Given> g_given;     // given, not yet drawn by remote.cpp
std::vector<Given> g_claimed;   // a mission car remote.cpp draws as that player's car (the mission's script keeps its handle)
std::vector<Given> g_orphans;   // a mission car that player left: kept here until it shows up in their stream
std::set<int> g_reserved;       // their handles: never streamed as ours
bool Reserved(int h) { return g_reserved.count(h) != 0; }

uint16_t NewId() {
    uint16_t id;
    do { id = g_nextId++; if (!g_nextId) g_nextId = 1; } while (!id || g_srcById.count(id));
    return id;
}
Src *SrcById(uint16_t id) {
    auto it = g_srcById.find(id);
    if (it == g_srcById.end()) return nullptr;
    auto s = g_src.find(it->second);
    return s == g_src.end() ? nullptr : &s->second;
}
void DropSrc(uint16_t id) {
    auto it = g_srcById.find(id);
    if (it == g_srcById.end()) return;
    g_src.erase(it->second);
    g_srcById.erase(it);
}
// the entry of an entity (new id the first time); isNew tells a fresh one
Src &Track(int h, bool ped, bool *isNew = nullptr) {
    uint64_t k = Key(h);
    uint32_t model = gta::ModelOf(h);
    gta::Vec3 pos = gta::Pos(h);
    auto it = g_src.find(k);
    if (it != g_src.end() && (it->second.model != model || (it->second.frame + 8 > g_frame && Dist2(it->second.pos, pos) > 40.f * 40.f))) {
        g_srcById.erase(it->second.id);   // the same address, another entity (the pool reused the slot)
        g_src.erase(it);
        it = g_src.end();
    }
    if (it == g_src.end()) {
        auto g = g_gone.find(k);
        if (g != g_gone.end() && g->second.s.model == model && !g_srcById.count(g->second.s.id) && Dist2(g->second.s.pos, pos) < 60.f * 60.f) {
            it = g_src.emplace(k, g->second.s).first;   // back after a moment away: same id, not new
            g_srcById[it->second.id] = k;
            g_gone.erase(g);
            ++g_srcBack;
            if (isNew) *isNew = false;
        } else {
            // GTA re-creates far entities now and then: a new object (address and handle) of the same model, where the old one
            // would be now (seen 2026-11-18: 0.2-2.3 m from where one vanished 125 ms before). It keeps the old one's id.
            auto best = g_gone.end();
            float bd = 6.f * 6.f;
            unsigned long long now = Now();
            for (auto gi = g_gone.begin(); gi != g_gone.end(); ++gi) {
                const Src &o = gi->second.s;
                if (o.model != model || now - gi->second.at > 2500 || g_srcById.count(o.id)) continue;
                float dt = (float)(now - gi->second.at) / 1000.f + 0.125f;
                float ex = o.pos.x + o.vel.x * dt - pos.x, ey = o.pos.y + o.vel.y * dt - pos.y, ez = o.pos.z + o.vel.z * dt - pos.z;
                float d = ex * ex + ey * ey + ez * ez;
                if (d < bd) { bd = d; best = gi; }
            }
            if (best != g_gone.end()) {
                it = g_src.emplace(k, best->second.s).first;
                g_srcById[it->second.id] = k;
                g_gone.erase(best);
                if (g != g_gone.end() && g != best) g_gone.erase(g);
                ++g_srcBack;
                if (isNew) *isNew = false;
                it->second.h = h; it->second.frame = g_frame; it->second.pos = pos;
                return it->second;
            }
            if (g != g_gone.end()) g_gone.erase(g);
            Src s;
            s.id = NewId(); s.model = model; s.ped = ped;
            it = g_src.emplace(k, s).first;
            g_srcById[s.id] = k;
            ++g_srcNew;
            if (isNew) *isNew = true;
        }
    } else if (isNew) *isNew = false;
    Src &sr = it->second;
    if (sr.frame && sr.frame != g_frame && g_frame - sr.frame < 8) {
        float dt = 0.125f * (float)(g_frame - sr.frame);
        sr.vel = {(pos.x - sr.pos.x) / dt, (pos.y - sr.pos.y) / dt, (pos.z - sr.pos.z) / dt};
    }
    sr.h = h;
    sr.pos = pos;
    sr.frame = g_frame;
    return sr;
}
uint16_t IdOf(int h) { auto it = g_src.find(Key(h)); return it == g_src.end() ? 0 : it->second.id; }

Move PedMove(int ped) {
    if (NB(N<int>(N_IS_PED_SPRINTING, ped))) return Move::Sprint;
    if (NB(N<int>(N_IS_PED_RUNNING, ped))) return Move::Run;
    if (NB(N<int>(N_IS_PED_WALKING, ped))) return Move::Walk;
    return Move::Still;
}

void ReadDesc(int h, PopEntity &e) {
    e.flags |= kPopHasDesc;
    if (e.kind == kPopPed) {
        for (int c = 0; c < (int)kPopComps; ++c) {
            e.comp[c][0] = (uint8_t)N<int>(N_GET_PED_DRAWABLE_VARIATION, h, c);
            e.comp[c][1] = (uint8_t)N<int>(N_GET_PED_TEXTURE_VARIATION, h, c);
        }
        for (int p = 0; p < (int)kPopProps; ++p) {
            int d = N<int>(N_GET_PED_PROP_INDEX, h, p, 0);
            e.prop[p][0] = d < 0 ? 255 : (uint8_t)d;
            e.prop[p][1] = d < 0 ? 0 : (uint8_t)N<int>(N_GET_PED_PROP_TEXTURE_INDEX, h, p);
        }
    } else {
        int c1 = 0, c2 = 0, pe = 0, wh = 0;
        N(N_GET_VEHICLE_COLOURS, h, &c1, &c2);
        N(N_GET_VEHICLE_EXTRA_COLOURS, h, &pe, &wh);
        e.color1 = (uint8_t)c1; e.color2 = (uint8_t)c2; e.pearl = (uint8_t)pe; e.wheel = (uint8_t)wh;
        int lv = N<int>(N_GET_VEHICLE_LIVERY, h);
        e.livery = lv < 0 || lv > 254 ? 255 : (uint8_t)lv;
        const char *pl = N<const char *>(N_GET_VEHICLE_NUMBER_PLATE_TEXT, h);
        e.plate = pl ? std::string(pl).substr(0, 8) : "";
    }
}
void FillDesc(Src &s, PopEntity &e) {
    bool want = s.descLeft > 0 || (g_frame + e.id) % 16 == 0;   // the first frames, then every 2 s
    if (!want) return;
    if (s.descLeft > 0) --s.descLeft;
    ReadDesc(s.h, e);
}

int SeatOf(int veh, int ped) {
    int seats = N<int>(N_GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS, veh);
    for (int s = -1; s < seats && s < 15; ++s)
        if (N<int>(N_GET_PED_IN_VEHICLE_SEAT, veh, s, 0) == ped) return s;
    return -1;
}

void FillVehicle(int v, PopEntity &e) {
    e.kind = kPopVehicle;
    e.model = gta::ModelOf(v);
    gta::Vec3 p = gta::Pos(v);
    e.pos = {p.x, p.y, p.z};
    NVec3 vel = N<NVec3>(N_GET_ENTITY_VELOCITY, v);
    e.vel = {vel.x, vel.y, vel.z};
    N(N_GET_ENTITY_QUATERNION, v, &e.rot.x, &e.rot.y, &e.rot.z, &e.rot.w);
    NVec3 av = N<NVec3>(N_GET_ENTITY_ROTATION_VELOCITY, v);
    e.angZ = av.z;
    uint8_t vf = 0;
    if (NB(N<int>(N_GET_IS_VEHICLE_ENGINE_RUNNING, v))) vf |= kVehEngine;
    int lo = 0, hi = 0;
    N(N_GET_VEHICLE_LIGHTS_STATE, v, &lo, &hi);
    if (lo) vf |= kVehLights;
    if (hi) vf |= kVehHighBeams;
    if (NB(N<int>(N_IS_VEHICLE_SIREN_ON, v))) vf |= kVehSiren;
    if (NB(N<int>(N_IS_HORN_ACTIVE, v))) vf |= kVehHorn;
    e.vflags = vf;
    float body = N<float>(N_GET_VEHICLE_BODY_HEALTH, v), eng = N<float>(N_GET_VEHICLE_ENGINE_HEALTH, v);
    e.body = (uint8_t)std::clamp(body / 4.f, 0.f, 250.f);
    e.engine = (uint8_t)std::clamp(eng / 4.f, 0.f, 250.f);
    if (gta::Dead(v)) e.flags |= kPopDead;
    if (Mission(v)) e.flags |= kPopMissionEnt;
}

// what the ped is doing, for the puppets' animations
void PedAction(int h, PopEntity &e, const std::unordered_map<int, bool> &vehSet) {
    uint8_t act = kActNone, arg = 0;
    if (NB(N<int>(N_IS_PED_IN_COVER, h, 0)) || NB(N<int>(N_IS_PED_GOING_INTO_COVER, h))) {
        act = kActCover;
        if (NB(N<int>(N_IS_PED_IN_COVER_FACING_LEFT, h))) arg |= kActCoverLeft;
        if (NB(N<int>(N_IS_PED_AIMING_FROM_COVER, h))) arg |= kActCoverAiming;
    } else if (NB(N<int>(N_IS_PED_CLIMBING, h)) || NB(N<int>(N_IS_PED_VAULTING, h))) act = kActClimb;
    else if (NB(N<int>(N_IS_PED_JUMPING, h))) act = kActJump;
    else if (NB(N<int>(N_IS_PED_RELOADING, h))) act = kActReload;
    else if (NB(N<int>(N_GET_IS_TASK_ACTIVE, h, 0))) act = kActHandsUp;                                          // CTaskHandsUp
    else if (NB(N<int>(N_GET_IS_TASK_ACTIVE, h, 126)) || NB(N<int>(N_GET_IS_TASK_ACTIVE, h, 146))) act = kActCower;   // CTaskCower, CTaskDuckAndCover
    else if (NB(N<int>(N_GET_PED_STEALTH_MOVEMENT, h)) || NB(N<int>(N_IS_PED_DUCKING, h))) act = kActCrouch;
    if (act) { e.flags |= kPopAction; e.act = act; e.actArg = arg; }
    if (NB(N<int>(N_IS_PED_GETTING_INTO_A_VEHICLE, h))) {
        int v = N<int>(N_GET_VEHICLE_PED_IS_ENTERING, h);
        if (gta::Exists(v) && vehSet.count(v)) {
            e.flags |= kPopEntering;
            e.vehId = IdOf(v);
            int seat = N<int>(N_GET_SEAT_PED_IS_TRYING_TO_ENTER, h);
            e.seat = (int8_t)std::clamp(seat, -1, 15);
            if (!e.vehId) e.flags &= ~kPopEntering;
        }
    }
}

void FillPed(int h, PopEntity &e, int me, const std::unordered_map<int, bool> &vehSet) {
    e.kind = kPopPed;
    e.model = gta::ModelOf(h);
    gta::Vec3 p = gta::Pos(h);
    e.pos = {p.x, p.y, p.z};
    NVec3 vel = N<NVec3>(N_GET_ENTITY_VELOCITY, h);
    e.vel = {vel.x, vel.y, vel.z};
    e.heading = gta::Heading(h);
    e.move = (uint8_t)PedMove(h);
    e.health = (uint8_t)std::clamp(gta::Health(h), 0, 255);
    bool dead = gta::Dead(h);
    if (dead) e.flags |= kPopDead;
    else {
        if (NB(N<int>(N_IS_PED_RAGDOLL, h))) e.flags |= kPopRagdoll;
        if (NB(N<int>(N_IS_PED_USING_ANY_SCENARIO, h))) e.flags |= kPopScenario;
    }
    if (gta::InVehicle(h)) {
        int v = N<int>(N_GET_VEHICLE_PED_IS_IN, h, 0);
        e.flags |= kPopInVehicle;
        e.vehId = IdOf(v);
        e.seat = (int8_t)SeatOf(v, h);
    } else if (!dead) PedAction(h, e, vehSet);
    uint32_t w = N<uint32_t>(N_GET_SELECTED_PED_WEAPON, h);
    if (w && w != gta::Joaat("WEAPON_UNARMED")) { e.flags |= kPopArmed; e.weapon = w; }
    bool mission = Mission(h);
    if (mission) e.flags |= kPopMissionEnt;
    if (PolicePed(h)) e.flags |= kPopPolice;
    int rel = N<int>(N_GET_RELATIONSHIP_BETWEEN_PEDS, h, me);   // 4 dislike, 5 hate (255: none)
    if (mission && (rel == 4 || rel == 5) && !dead) e.flags |= kPopHostile;
    if ((e.flags & kPopArmed) && NB(N<int>(N_IS_PED_SHOOTING, h))) {
        int t = N<int>(N_GET_PED_TARGET_FROM_COMBAT_PED, h, 0);
        NVec3 hit{};
        if (gta::Exists(t)) { gta::Vec3 tp = gta::Pos(t); e.aim = {tp.x, tp.y, tp.z + 0.3f}; e.flags |= kPopShooting; }
        else if (NB(N<int>(N_GET_PED_LAST_WEAPON_IMPACT_COORD, h, &hit))) { e.aim = {hit.x, hit.y, hit.z}; e.flags |= kPopShooting; }
    }
}

// the people in a vehicle that are ours to hand over / delete with it (no players, no puppets)
bool AmbientOccupants(int v, std::vector<int> &out) {
    out.clear();
    int seats = N<int>(N_GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS, v);
    for (int s = -1; s < seats && s < 15; ++s) {
        int p = N<int>(N_GET_PED_IN_VEHICLE_SEAT, v, s, 0);
        if (!gta::Exists(p)) continue;
        if (NB(N<int>(N_IS_PED_A_PLAYER, p)) || remote::IsPuppetEntity(p) || IsPuppet(p)) return false;
        out.push_back(p);
    }
    return true;
}

// GTA spawns nothing in the peers' parts of the world: one zero-density sphere per peer, from the line halfway between
// us outwards (its near edge on that line). What GTA put there before still goes through the cull in Stream.
struct Zone { uint16_t peer = 0; int id = 0; gta::Vec3 at; float r = 0; int part = 0; };
std::vector<Zone> g_zones;
void DropZone(Zone &z) { if (z.id) N(N_REMOVE_POP_MULTIPLIER_SPHERE, z.id, 0); z.id = 0; }
void ClearZones() { for (auto &z : g_zones) DropZone(z); g_zones.clear(); }
void UpdateZones(const gta::Vec3 &me) {
    std::vector<Zone> keep;
    for (auto &pa : g_peerAt) {
        float dx = pa.pos.x - me.x, dy = pa.pos.y - me.y, d = std::sqrt(dx * dx + dy * dy);
        float ux = d > 1.f ? dx / d : 0.f, uy = d > 1.f ? dy / d : 0.f;
        float r1 = std::max(d * 0.5f, 30.f);
        const float radii[3] = {r1, std::min(r1 * 2.5f, 600.f), std::min(r1 * 6.f, 600.f)};
        for (int part = 0; part < 3; ++part) {   // three spheres in a row from the halfway line outwards: a cone of the peer's part
            Zone want;
            want.peer = pa.id; want.part = part;
            if (d > 30.f) {
                float off = 0.5f * d + 6.f + radii[part];
                want.at = {me.x + ux * off, me.y + uy * off, pa.pos.z};
                want.r = radii[part];
            }
            auto it = std::find_if(g_zones.begin(), g_zones.end(), [&](const Zone &z) { return z.peer == pa.id && z.part == part; });
            if (it != g_zones.end()) {
                float mx = it->at.x - want.at.x, my = it->at.y - want.at.y;
                if (want.r > 0 && it->id && mx * mx + my * my < 12.f * 12.f && std::fabs(it->r - want.r) < 12.f) { keep.push_back(*it); it->id = 0; continue; }
                DropZone(*it);
            }
            if (want.r > 0) want.id = N<int>(N_ADD_POP_MULTIPLIER_SPHERE, want.at.x, want.at.y, want.at.z, want.r, 0.f, 0.f, 0, 1);
            if (want.id) keep.push_back(want);
        }
    }
    for (auto &z : g_zones) DropZone(z);   // peers that are gone
    g_zones = keep;
}

void DropOwnedEntity(int h, bool ped) {   // one of ours goes (a new one in a peer's part of the world)
    auto it = g_src.find(Key(h));
    if (it != g_src.end()) { g_srcById.erase(it->second.id); g_src.erase(it); }
    g_gone.erase(Key(h));
    int x = h;
    Delete(x, ped);
}
void HandOver();
void StreamTrains(std::vector<PopEntity> &out, const gta::Vec3 &at);   // v17 trains
unsigned g_trainsStreamed = 0, g_trainsUnknown = 0, g_copsSent = 0;

// ---------------------------------------------------------------- the stream
void Stream() {
    unsigned long long now = Now();
    if (now - g_lastSend < (unsigned long long)kSendEveryMs) return;
    g_lastSend = now;
    ++g_frame;
    RefreshPeers();
    int nv = AllVehicles(), np = AllPeds();
    int me = gta::PlayerPed();
    gta::Vec3 at = gta::Pos(me);
    int myVeh = gta::InVehicle(me) ? N<int>(N_GET_VEHICLE_PED_IS_IN, me, 0) : 0;
    int myLast = N<int>(N_GET_PLAYERS_LAST_VEHICLE);   // the others already draw it as our car (remote.cpp)
    bool host = MissionHost();
    auto wanted = [&](int e) {   // ours: ambient, police, or (a mission's host) a mission entity that is not LocalOnline's own
        if (remote::IsPuppetEntity(e) || IsPuppet(e) || Reserved(e) || !NB(N<int>(N_IS_ENTITY_VISIBLE, e))) return false;   // hidden: GodEye's drivers, our own tricks
        return !Mission(e) || host;
    };
    bool peers = !g_peerAt.empty();
    struct Cand { int h; float d; };
    std::vector<Cand> vehs, peds;
    std::vector<int> cull;
    for (int i = 0; i < nv; ++i) {
        int v = g_vehs[i];
        if (!gta::Exists(v) || v == myVeh || v == myLast || !wanted(v) || Train(v)) continue;
        int drv = N<int>(N_GET_PED_IN_VEHICLE_SEAT, v, -1, 0);
        if (gta::Exists(drv) && (NB(N<int>(N_IS_PED_A_PLAYER, drv)) || remote::IsPuppetEntity(drv) || IsPuppet(drv))) continue;
        bool fresh = false;
        Src &s = Track(v, false, &fresh);
        float d = Dist2(s.pos, at), r = Mission(v) ? kMissionRange : kVehRange;
        if (peers && !s.streamed && !Mission(v) && !PoliceVehicle(v)) {   // GTA put it in a peer's part of the world: theirs fills it
            float dp = 0; NearestPeer(s.pos, dp);
            if (dp < kCullRatio * kCullRatio * d && !NB(N<int>(N_IS_ENTITY_ON_SCREEN, v))) { cull.push_back(v); continue; }
        }
        if (d < r * r) vehs.push_back({v, d});
    }
    std::sort(vehs.begin(), vehs.end(), [](const Cand &a, const Cand &b) { return a.d < b.d; });
    if (vehs.size() > kMaxVehicles) vehs.resize(kMaxVehicles);
    std::unordered_map<int, bool> vehSet;
    for (auto &c : vehs) vehSet[c.h] = true;
    for (int i = 0; i < np; ++i) {
        int p = g_peds[i];
        if (!gta::Exists(p) || p == me || NB(N<int>(N_IS_PED_A_PLAYER, p)) || !wanted(p)) continue;
        if (gta::InVehicle(p)) {
            int v = N<int>(N_GET_VEHICLE_PED_IS_IN, p, 0);
            Track(p, true);                   // its id stays while its car is out of the nearest 70
            if (vehSet.count(v)) peds.push_back({p, 0.f});   // streamed only with its vehicle
            continue;
        }
        bool fresh = false;
        Src &s = Track(p, true, &fresh);
        float d = Dist2(s.pos, at), r = Mission(p) ? kMissionRange : kPedRange;
        if (peers && !s.streamed && !Mission(p) && !PolicePed(p)) {
            float dp = 0; NearestPeer(s.pos, dp);
            if (dp < kCullRatio * kCullRatio * d && !NB(N<int>(N_IS_ENTITY_ON_SCREEN, p))) { cull.push_back(p); continue; }
        }
        if (d < r * r) peds.push_back({p, d});
    }
    std::sort(peds.begin(), peds.end(), [](const Cand &a, const Cand &b) { return a.d < b.d; });
    if (peds.size() > kMaxPeds) peds.resize(kMaxPeds);
    for (int h : cull) {
        bool ped = NB(N<int>(N_IS_ENTITY_A_PED, h));
        if (!ped) { std::vector<int> occ; if (!AmbientOccupants(h, occ)) continue; for (int o : occ) DropOwnedEntity(o, true); }
        DropOwnedEntity(h, ped);
        ++g_culled;
        if (ped) ++g_culledPeds;
    }
    for (auto it = g_src.begin(); it != g_src.end();) {   // gone (or no longer ours): kept 10 s in case it comes back
        if (it->second.frame != g_frame) { g_srcById.erase(it->second.id); g_gone[it->first] = {it->second, now}; it = g_src.erase(it); }
        else ++it;
    }
    for (auto it = g_gone.begin(); it != g_gone.end();) it = now - it->second.at > 10000 ? g_gone.erase(it) : std::next(it);

    std::vector<PopEntity> out;
    out.reserve(vehs.size() + peds.size());
    auto finish = [&](Src &s, PopEntity &e) {
        e.id = s.id;
        if (s.fromOwner && now < s.fromUntil) { e.flags |= kPopMigrated; e.fromOwner = s.fromOwner; e.fromId = s.fromId; }
        FillDesc(s, e);
        s.streamed = true;
        out.push_back(e);
    };
    for (auto &c : vehs) {
        auto it = g_src.find(Key(c.h));
        if (it == g_src.end()) continue;
        PopEntity e;
        FillVehicle(c.h, e);
        if (PoliceVehicle(c.h)) e.flags |= kPopPolice;
        finish(it->second, e);
    }
    for (auto &c : peds) {
        auto it = g_src.find(Key(c.h));
        if (it == g_src.end()) continue;
        PopEntity e;
        FillPed(c.h, e, me, vehSet);
        if ((e.flags & kPopInVehicle) && !e.vehId) continue;
        finish(it->second, e);
    }
    StreamTrains(out, at);
    g_sentPeds = (int)peds.size(); g_sentVehs = (int)vehs.size();
    // vehicles first: the others seat the peds in them
    ++g_seq;
    PopStateMsg m;
    m.seq = g_seq;
    size_t bytes = 0;
    auto flush = [&]() {
        if (m.e.empty()) return;
        wire::Writer w;
        Put(w, m);
        net::SendUnreliable(w);
        m.e.clear();
        bytes = 0;
    };
    for (auto &e : out) {
        wire::Writer one;
        PutPopEntity(one, e);
        if (bytes + one.Size() > kPacketBudget || m.e.size() >= kMaxPopPerPacket) flush();
        m.e.push_back(e);
        bytes += one.Size();
    }
    flush();
    // our share of the world around us: the part nearer us than any peer (GTA fills only that, at its usual density)
    if (g_frame % 4 == 0) {
        int mine = 0, all = 0;
        for (float r : {30.f, 70.f, 110.f, 150.f})
            for (int a = 0; a < 16; ++a) {
                float x = at.x + r * std::cos(a * 0.3927f), y = at.y + r * std::sin(a * 0.3927f);
                float dm = Dist2D2(at, x, y);
                bool ours = true;
                for (auto &pa : g_peerAt) if (Dist2D2(pa.pos, x, y) < dm) { ours = false; break; }
                mine += ours; ++all;
            }
        g_share = peers ? std::max(0.2f, (float)mine / (float)all) : 1.f;
        if (g_frame % 8 == 0) UpdateZones(at);
        HandOver();
    }
    if (now - g_lastStat > 30000) {
        g_lastStat = now;
        LOGI("pop", "-", "owner for %u game(s)%s: streaming %d peds, %d vehicles; %u new ids in 30 s (%u found again), %d tracked, our share %.0f%%", g_viewers,
             host ? " (mission host)" : "", g_sentPeds, g_sentVehs, g_srcNew, g_srcBack, (int)g_src.size(), g_share * 100.f);
        LOGI("pop", "-", "hand-over: %u offered, %u handed to peers, %u taken from peers; %u new ones in a peer's part removed (%u peds) (%d no-spawn zone(s))", g_offered, g_handedOut,
             g_handedIn, g_culled, g_culledPeds, (int)g_zones.size());
        if (g_trainsStreamed || g_trainsUnknown) LOGI("pop", "-", "trains: %u engine states streamed, %u of an unknown config", g_trainsStreamed, g_trainsUnknown);
        if (g_copsSent) LOGI("pop", "-", "wanted players: %u police sent after them", g_copsSent);
        g_trainsStreamed = g_trainsUnknown = g_copsSent = 0;
        g_srcNew = g_srcBack = g_culledPeds = g_culled = g_offered = g_handedOut = g_handedIn = 0;
    }
}

// ================================================================ puppets of the peers' entities
struct Pup {
    PopEntity last;
    bool haveDesc = false;
    PopEntity desc;
    interp::Buffer buf;
    int h = 0, blip = 0;
    unsigned long long seenAt = 0, holdUntil = 0;
    int lastTaskMs = 0;
    uint8_t lastMove = 255;
    bool scenario = false, inVeh = false, stealth = false, entering = false;
    uint8_t act = 0;
    int actMs = 0, enterMs = 0;
    int hpSet = 0;
    float body = 1000.f, engine = 1000.f;
    uint32_t weapon = 0;
    int lastShotMs = 0;
    uint8_t vflags = 0xFF;
    int lastHornMs = 0;
};
std::map<uint32_t, Pup> g_pups;               // PupKey(owner, id)
std::unordered_map<int, uint32_t> g_pupOf;    // handle -> key
unsigned g_newIds = 0, g_relays = 0, g_adopted = 0, g_rekeyed = 0;
interp::Clock g_clock;
unsigned long long g_lastClean = 0, g_lastViewStat = 0;
bool g_suppressed = false;
int g_removed = 0;
unsigned g_missionKills = 0;

void DeletePup(Pup &p) {
    if (p.blip) { N(N_REMOVE_BLIP, &p.blip); p.blip = 0; }
    if (p.h) { g_pupOf.erase(p.h); Delete(p.h, p.last.kind == kPopPed); }
}
void ClearPups() {
    for (auto &kv : g_pups) DeletePup(kv.second);
    g_pups.clear();
    g_pupOf.clear();
}
Pup *PupAt(uint32_t key) { auto it = g_pups.find(key); return it == g_pups.end() ? nullptr : &it->second; }

void Suppress(bool on) {
    if (on == g_suppressed) return;
    g_suppressed = on;
    N(N_SET_PED_POPULATION_BUDGET, on ? 0 : 3);
    N(N_SET_VEHICLE_POPULATION_BUDGET, on ? 0 : 3);
    N(N_SET_RANDOM_BOATS, on ? 0 : 1);
    N(N_SET_GARBAGE_TRUCKS, on ? 0 : 1);
    LOGI("pop", "-", on ? "our own NPCs and traffic are off" : "our own NPCs and traffic are back");
}

// a camera: its own ambient population goes while it draws the players' (its free car, trains stay)
void CleanOwn() {
    unsigned long long now = Now();
    if (now - g_lastClean < 400) return;
    g_lastClean = now;
    int me = gta::PlayerPed();
    int myVeh = gta::InVehicle(me) ? N<int>(N_GET_VEHICLE_PED_IS_IN, me, 0) : 0;
    int entering = N<int>(N_GET_VEHICLE_PED_IS_TRYING_TO_ENTER, me);
    int last = N<int>(N_GET_PLAYERS_LAST_VEHICLE);
    int np = AllPeds();
    for (int i = 0; i < np; ++i) {
        int p = g_peds[i];
        if (!gta::Exists(p) || p == me || Mission(p) || NB(N<int>(N_IS_PED_A_PLAYER, p)) || g_pupOf.count(p)) continue;
        if (gta::InVehicle(p)) { int v = N<int>(N_GET_VEHICLE_PED_IS_IN, p, 0); if (v == myVeh || Train(v)) continue; }
        Delete(p, true);
        ++g_removed;
    }
    int nv = AllVehicles();
    for (int i = 0; i < nv; ++i) {
        int v = g_vehs[i];
        if (!gta::Exists(v) || Mission(v) || v == myVeh || v == entering || v == last || g_pupOf.count(v) || Train(v)) continue;
        int drv = N<int>(N_GET_PED_IN_VEHICLE_SEAT, v, -1, 0);
        if (gta::Exists(drv) && (NB(N<int>(N_IS_PED_A_PLAYER, drv)) || Mission(drv))) continue;
        Delete(v, false);
        ++g_removed;
    }
}

void ApplyDesc(Pup &p) {
    const PopEntity &d = p.desc;
    if (d.kind == kPopPed) {
        for (int c = 0; c < (int)kPopComps; ++c) N(N_SET_PED_COMPONENT_VARIATION, p.h, c, (int)d.comp[c][0], (int)d.comp[c][1], 0);
        for (int i = 0; i < (int)kPopProps; ++i) {
            if (d.prop[i][0] == 255) N(N_CLEAR_PED_PROP, p.h, i, 0);
            else N(N_SET_PED_PROP_INDEX, p.h, i, (int)d.prop[i][0], (int)d.prop[i][1], 1, 0);
        }
    } else {
        N(N_SET_VEHICLE_COLOURS, p.h, (int)d.color1, (int)d.color2);
        N(N_SET_VEHICLE_EXTRA_COLOURS, p.h, (int)d.pearl, (int)d.wheel);
        if (d.livery != 255) N(N_SET_VEHICLE_LIVERY, p.h, (int)d.livery);
        if (!d.plate.empty()) N(N_SET_VEHICLE_NUMBER_PLATE_TEXT, p.h, d.plate.c_str());
    }
}

// how a puppet is held: it never thinks for itself, crashes and fires are its owner's game's
void PuppetVehicleSetup(int v) {
    gta::MakeMission(v);
    N(N_SET_VEHICLE_DOORS_LOCKED, v, 1);
    N(N_SET_ENTITY_PROOFS, v, 0, 0, 0, 1, 0, 0, 0, 0);   // crashes: the owner's game does them
    N(N_SET_DISABLE_VEHICLE_PETROL_TANK_FIRES, v, 1);
    N(N_SET_VEHICLE_EXPLODES_ON_HIGH_EXPLOSION_DAMAGE, v, 0);
}
void PuppetPedSetup(int p) {
    gta::MakeMission(p);
    N(N_SET_BLOCKING_OF_NON_TEMPORARY_EVENTS, p, 1);
    N(N_SET_PED_FLEE_ATTRIBUTES, p, 0, 0);
    N(N_SET_PED_KEEP_TASK, p, 1);
    N(N_SET_PED_CAN_BE_TARGETTED, p, 1);
    N(N_SET_PED_DROPS_WEAPONS_WHEN_DEAD, p, 0);
}
void HostileBlip(Pup &p) {
    if (!(p.last.flags & kPopHostile) || p.blip || !p.h) return;
    p.blip = N<int>(N_ADD_BLIP_FOR_ENTITY, p.h);
    N(N_SET_BLIP_COLOUR, p.blip, 1);
    N(N_SET_BLIP_SCALE, p.blip, 0.7f);
}

bool Spawn(uint32_t key, Pup &p, const interp::Sample &s) {
    uint32_t m = p.last.model;
    if (!NB(N<int>(N_IS_MODEL_IN_CDIMAGE, m)) || !NB(N<int>(N_IS_MODEL_VALID, m))) return false;
    N(N_REQUEST_MODEL, m);
    if (!NB(N<int>(N_HAS_MODEL_LOADED, m))) return false;
    if (p.last.kind == kPopVehicle) {
        p.h = N<int>(N_CREATE_VEHICLE, m, s.pos.x, s.pos.y, s.pos.z, p.last.heading, 0, 1, 0);
        if (!p.h) return false;
        PuppetVehicleSetup(p.h);
        N(N_SET_ENTITY_QUATERNION, p.h, s.rot.x, s.rot.y, s.rot.z, s.rot.w);
        p.body = N<float>(N_GET_VEHICLE_BODY_HEALTH, p.h);
        p.engine = N<float>(N_GET_VEHICLE_ENGINE_HEALTH, p.h);
    } else {
        Pup *veh = (p.last.flags & kPopInVehicle) ? PupAt(PupKey(OwnerOf(key), p.last.vehId)) : nullptr;
        if ((p.last.flags & kPopInVehicle) && (!veh || !veh->h)) return false;   // its car first
        p.h = N<int>(N_CREATE_PED, (p.last.flags & kPopPolice) ? 6 : 4, m, s.pos.x, s.pos.y, s.pos.z - 1.f, s.heading, 0, 1);
        if (!p.h) return false;
        PuppetPedSetup(p.h);
        int hp = p.last.health > 100 ? p.last.health : 200;
        N(N_SET_ENTITY_MAX_HEALTH, p.h, hp > 200 ? hp : 200);
        N(N_SET_ENTITY_HEALTH, p.h, hp, 0, 0);
        p.hpSet = hp;
        if (veh && veh->h) { N(N_SET_PED_INTO_VEHICLE, p.h, veh->h, (int)p.last.seat); p.inVeh = true; }
        HostileBlip(p);
    }
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
    if (p.haveDesc) ApplyDesc(p);
    g_pupOf[p.h] = key;
    return true;
}

// A new id where a puppet of the same owner and model stopped being updated a moment ago is most likely the same entity
// under a new id: the puppet moves over instead of a second one popping in. A mission car a remote player left (an
// orphan) is taken back the same way.
bool Adopt(uint32_t key, Pup &p, const interp::Sample &s) {
    unsigned long long now = Now();
    for (auto it = g_orphans.begin(); it != g_orphans.end(); ++it) {
        if (it->to != OwnerOf(key) || it->model != p.last.model || !gta::Exists(it->h)) continue;
        if (Dist2D2(gta::Pos(it->h), s.pos.x, s.pos.y) > 8.f * 8.f) continue;
        p.h = it->h;
        PuppetVehicleSetup(p.h);
        p.body = N<float>(N_GET_VEHICLE_BODY_HEALTH, p.h); p.engine = N<float>(N_GET_VEHICLE_ENGINE_HEALTH, p.h);
        g_reserved.erase(it->h);
        g_orphans.erase(it);
        g_pupOf[p.h] = key;
        if (p.haveDesc) ApplyDesc(p);
        LOGI("pop", remote::NameOf(OwnerOf(key)).c_str(), "the mission car they left is drawn from their stream again");
        return true;
    }
    for (auto &kv : g_pups) {
        Pup &o = kv.second;
        if (kv.first == key || OwnerOf(kv.first) != OwnerOf(key) || !o.h || o.last.model != p.last.model || o.last.kind != p.last.kind || now - o.seenAt < 100) continue;
        gta::Vec3 at = gta::Pos(o.h);
        float dx = at.x - s.pos.x, dy = at.y - s.pos.y, dz = at.z - s.pos.z;
        if (dx * dx + dy * dy + dz * dz > 6.f * 6.f) continue;
        if (g_adopted < 4) LOGI("pop", remote::NameOf(OwnerOf(key)).c_str(), "adopt: id %u -> %u, model %08X, %.1f m, quiet %llu ms, kind %d", IdOfKey(kv.first), IdOfKey(key),
                                p.last.model, std::sqrt(dx * dx + dy * dy + dz * dz), now - o.seenAt, (int)p.last.kind);
        p.h = o.h; p.blip = o.blip; p.hpSet = o.hpSet; p.body = o.body; p.engine = o.engine; p.weapon = o.weapon; p.vflags = o.vflags; p.inVeh = o.inVeh;
        p.stealth = o.stealth; p.act = o.act;
        o.h = 0; o.blip = 0;
        g_pupOf[p.h] = key;
        if (p.haveDesc) ApplyDesc(p);
        g_pups.erase(kv.first);
        ++g_adopted;
        return true;
    }
    return false;
}

float MoveSpeed(uint8_t move) {
    switch ((Move)move) { case Move::Walk: return 1.f; case Move::Run: return 2.f; case Move::Sprint: return 3.f; default: return 0.f; }
}

int g_me = 0, g_myVeh = 0;

void ReportPed(uint32_t key, Pup &p) {
    int hp = gta::Health(p.h);
    if (hp >= p.hpSet) return;
    bool mine = NB(N<int>(N_HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY, p.h, g_me, 1)) || (g_myVeh && NB(N<int>(N_HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY, p.h, g_myVeh, 1)));
    if (mine && g_mode == kPopShared) {
        uint32_t w = N<uint32_t>(N_GET_SELECTED_PED_WEAPON, g_me);
        char b[64];
        bool kill = hp <= 0 || gta::Dead(p.h);
        if (kill) snprintf(b, sizeof b, "k|%u|%u", IdOfKey(key), w);
        else snprintf(b, sizeof b, "d|%u|%d|%u", IdOfKey(key), p.hpSet - hp, w);
        QueueEvent(OwnerOf(key), b);
        if (kill && (p.last.flags & kPopMissionEnt) && !(p.last.flags & kPopDead)) ++g_missionKills;
    }
    N(N_CLEAR_ENTITY_LAST_DAMAGE_ENTITY, p.h);
    p.hpSet = hp;
}

void ReportVehicle(uint32_t key, Pup &p) {
    float body = N<float>(N_GET_VEHICLE_BODY_HEALTH, p.h), eng = N<float>(N_GET_VEHICLE_ENGINE_HEALTH, p.h);
    bool dead = gta::Dead(p.h);
    if (body >= p.body - 1.f && eng >= p.engine - 1.f && !(dead && !(p.last.flags & kPopDead))) return;
    bool mine = NB(N<int>(N_HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY, p.h, g_me, 1)) || (g_myVeh && NB(N<int>(N_HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY, p.h, g_myVeh, 1)));
    if (mine && g_mode == kPopShared) {
        char b[64];
        snprintf(b, sizeof b, "v|%u|%d|%d|%d", IdOfKey(key), (int)std::max(0.f, p.body - body), (int)std::max(0.f, p.engine - eng), dead ? 1 : 0);
        QueueEvent(OwnerOf(key), b);
    }
    N(N_CLEAR_ENTITY_LAST_DAMAGE_ENTITY, p.h);
    p.body = body; p.engine = eng;
}

void DriveVehicle(Pup &p, const interp::Result &r) {
    const interp::Sample &s = r.s;
    const PopEntity &e = p.last;
    if ((e.flags & kPopDead) && !gta::Dead(p.h)) {
        gta::Vec3 at = gta::Pos(p.h);
        N(N_ADD_EXPLOSION, at.x, at.y, at.z, 7, 1.f, 1, 0, 1.f, 1);   // the blast hurt nobody here: the owner's did
        N(N_EXPLODE_VEHICLE, p.h, 0, 1);
        return;
    }
    if (gta::Dead(p.h)) return;
    gta::Vec3 cur = gta::Pos(p.h);
    float ex = s.pos.x - cur.x, ey = s.pos.y - cur.y, ez = s.pos.z - cur.z;
    float err = std::sqrt(ex * ex + ey * ey + ez * ez);
    float speed = std::sqrt(s.vel.x * s.vel.x + s.vel.y * s.vel.y + s.vel.z * s.vel.z);
    if (speed < 0.15f) {   // parked / stopped: put it there and let it rest
        if (err > 0.4f) {
            N(N_SET_ENTITY_COORDS_NO_OFFSET, p.h, s.pos.x, s.pos.y, s.pos.z, 0, 0, 0);
            N(N_SET_ENTITY_QUATERNION, p.h, s.rot.x, s.rot.y, s.rot.z, s.rot.w);
            N(N_SET_ENTITY_VELOCITY, p.h, 0.f, 0.f, 0.f);
        }
    } else {
        if (err > 6.f) { N(N_SET_ENTITY_COORDS_NO_OFFSET, p.h, s.pos.x, s.pos.y, s.pos.z, 0, 0, 0); ex = ey = ez = 0; }
        const float k = 3.f;
        N(N_SET_ENTITY_VELOCITY, p.h, s.vel.x + ex * k, s.vel.y + ey * k, s.vel.z + ez * k);
        N(N_SET_ENTITY_QUATERNION, p.h, s.rot.x, s.rot.y, s.rot.z, s.rot.w);
        float st = speed > 1.f ? e.angZ * 3.f / speed : 0.f;
        N(N_SET_VEHICLE_STEER_BIAS, p.h, std::clamp(st, -1.f, 1.f));
    }
    if (e.vflags != p.vflags) {
        N(N_SET_VEHICLE_ENGINE_ON, p.h, (e.vflags & kVehEngine) ? 1 : 0, 1, 1);
        N(N_SET_VEHICLE_LIGHTS, p.h, (e.vflags & kVehLights) ? 2 : 1);
        N(N_SET_VEHICLE_SIREN, p.h, (e.vflags & kVehSiren) ? 1 : 0);
        p.vflags = e.vflags;
    }
    int now = gta::GameTimeMs();
    if ((e.vflags & kVehHorn) && now - p.lastHornMs > 150) { N(N_START_VEHICLE_HORN, p.h, 200, gta::Joaat("HELDDOWN"), 0); p.lastHornMs = now; }
    // damage the owner's game did (crashes, other players): never better than ours
    float body = e.body * 4.f, eng = e.engine * 4.f;
    if (body < p.body - 20.f) { N(N_SET_VEHICLE_BODY_HEALTH, p.h, body); p.body = body; }
    if (eng < p.engine - 20.f) { N(N_SET_VEHICLE_ENGINE_HEALTH, p.h, eng); p.engine = eng; }
}

// the owner's ped stopped doing something lasting (cover, hands up, cowering): back to plain moving
void EndAct(Pup &p) {
    if (p.act == kActCover || p.act == kActHandsUp || p.act == kActCower) { N(N_CLEAR_PED_TASKS, p.h); p.lastTaskMs = 0; p.lastMove = 255; }
    p.act = kActNone;
}

void DrivePed(uint32_t key, Pup &p, const interp::Result &r) {
    const interp::Sample &s = r.s;
    const PopEntity &e = p.last;
    int now = gta::GameTimeMs();
    if (e.flags & kPopDead) {
        if (!gta::Dead(p.h)) { N(N_SET_ENTITY_HEALTH, p.h, 0, 0, 0); p.hpSet = 0; }
        if (p.blip) { N(N_REMOVE_BLIP, &p.blip); p.blip = 0; }
        return;
    }
    if (gta::Dead(p.h)) return;   // we killed it: the owner's comes next
    if (e.health + 5 < p.hpSet && e.health > 100) { N(N_SET_ENTITY_HEALTH, p.h, (int)e.health, 0, 0); p.hpSet = e.health; }
    if (e.weapon != p.weapon) {
        uint32_t unarmed = gta::Joaat("WEAPON_UNARMED");
        if (e.weapon) { N(N_GIVE_WEAPON_TO_PED, p.h, e.weapon, 999, 0, 1); N(N_SET_PED_INFINITE_AMMO_CLIP, p.h, 1); }
        N(N_SET_CURRENT_PED_WEAPON, p.h, e.weapon ? e.weapon : unarmed, 1);
        p.weapon = e.weapon;
    }
    Pup *veh = (e.flags & (kPopInVehicle | kPopEntering)) ? PupAt(PupKey(OwnerOf(key), e.vehId)) : nullptr;
    if ((e.flags & kPopInVehicle) && veh && veh->h) {   // seated: the car moves it (after its own way in, if it walked to it)
        if (!NB(N<int>(N_IS_PED_IN_VEHICLE, p.h, veh->h, 0)) && Now() >= p.holdUntil) {
            bool walkingIn = p.entering && now - p.enterMs < 4000 && NB(N<int>(N_IS_PED_GETTING_INTO_A_VEHICLE, p.h));
            if (!walkingIn) N(N_SET_PED_INTO_VEHICLE, p.h, veh->h, (int)e.seat);
        }
        p.inVeh = true; p.entering = false;
        if (p.act) p.act = kActNone;
        return;
    }
    if (p.inVeh) {   // got out at the owner
        p.inVeh = false;
        if (gta::InVehicle(p.h)) N(N_TASK_LEAVE_VEHICLE, p.h, N<int>(N_GET_VEHICLE_PED_IS_IN, p.h, 0), 0);
        p.lastTaskMs = now;
        return;
    }
    if (gta::InVehicle(p.h)) {   // still climbing out (or jacked by us): leave it a moment
        if (now - p.lastTaskMs < 3000) return;
        N(N_CLEAR_PED_TASKS_IMMEDIATELY, p.h);
    }
    if (Now() < p.holdUntil) return;
    if (e.flags & kPopRagdoll) {
        if (!NB(N<int>(N_IS_PED_RAGDOLL, p.h))) N(N_SET_PED_TO_RAGDOLL, p.h, 800, 1200, 0, 0, 0, 0);
        return;
    }
    if (NB(N<int>(N_IS_PED_RAGDOLL, p.h))) return;   // knocked over here: let it get up
    gta::Vec3 cur = gta::Pos(p.h);
    float dx = s.pos.x - cur.x, dy = s.pos.y - cur.y, dz = s.pos.z - cur.z;
    float err = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (err > 6.f) {
        N(N_SET_ENTITY_COORDS_NO_OFFSET, p.h, s.pos.x, s.pos.y, s.pos.z, 0, 0, 0);
        gta::SetHeading(p.h, s.heading);
        p.lastTaskMs = 0; p.scenario = false;
        if (p.act == kActCover) p.act = kActNone;
        return;
    }
    // walking to a car to get in: its own way in
    if ((e.flags & kPopEntering) && veh && veh->h && err < 8.f) {
        if (!p.entering) {
            N(N_TASK_ENTER_VEHICLE, p.h, veh->h, 6000, (int)e.seat, s.move >= (uint8_t)Move::Run ? 2.f : 1.f, 1, (const char *)nullptr);
            p.entering = true; p.enterMs = now;
        }
        return;
    }
    p.entering = false;
    bool crouch = (e.flags & kPopAction) && e.act == kActCrouch;
    if (crouch != p.stealth) { N(N_SET_PED_STEALTH_MOVEMENT, p.h, crouch ? 1 : 0, "DEFAULT_ACTION"); p.stealth = crouch; }
    uint8_t act = (e.flags & kPopAction) ? e.act : (uint8_t)kActNone;
    if (act != p.act && p.act != kActNone && p.act != kActCrouch) EndAct(p);
    bool shooting = (e.flags & kPopShooting) && e.weapon;
    auto tracer = [&]() {   // shots without damage (the owner's game deals it)
        if (now - p.lastShotMs < 180) return;
        p.lastShotMs = now;
        int wobj = N<int>(N_GET_CURRENT_PED_WEAPON_ENTITY_INDEX, p.h, 0);
        gta::Vec3 o = gta::Exists(wobj) ? gta::Pos(wobj) : gta::Vec3{cur.x, cur.y, cur.z + 0.5f};
        if (!NB(N<int>(N_HAS_WEAPON_ASSET_LOADED, e.weapon))) N(N_REQUEST_WEAPON_ASSET, e.weapon, 31, 0);
        N(N_SHOOT_SINGLE_BULLET_BETWEEN_COORDS_IGNORE_ENTITY, o.x, o.y, o.z, e.aim.x, e.aim.y, e.aim.z, 0, 1, e.weapon, p.h, 1, 0, -1.f, p.h, 0);
    };
    switch (act) {
        case kActCover:   // in cover where the owner's ped is; it peeks out (stands and aims) when that one does
            if (e.actArg & kActCoverAiming) break;
            if (p.act != kActCover || (now - p.actMs > 4000 && !NB(N<int>(N_IS_PED_IN_COVER, p.h, 0)))) {
                if (err > 1.2f) N(N_SET_ENTITY_COORDS_NO_OFFSET, p.h, s.pos.x, s.pos.y, s.pos.z - 1.f, 0, 0, 0);
                N(N_TASK_PUT_PED_DIRECTLY_INTO_COVER, p.h, s.pos.x, s.pos.y, s.pos.z, -1, 0, 0.f, 1, (e.actArg & kActCoverLeft) ? 1 : 0, 0, 0);
                p.act = kActCover; p.actMs = now;
            }
            if (shooting) tracer();
            return;
        case kActHandsUp:
            if (p.act != kActHandsUp) { N(N_TASK_HANDS_UP, p.h, -1, 0, -1, 0); p.act = kActHandsUp; p.actMs = now; }
            return;
        case kActCower:
            if (p.act != kActCower) { N(N_TASK_COWER, p.h, -1); p.act = kActCower; p.actMs = now; }
            return;
        case kActClimb:
            if (p.act != kActClimb) { N(N_TASK_CLIMB, p.h, 0); p.act = kActClimb; p.actMs = now; p.holdUntil = Now() + 1200; }
            return;
        case kActJump:
            if (p.act != kActJump) { N(N_TASK_JUMP, p.h, 0, 0, 0); p.act = kActJump; p.actMs = now; p.holdUntil = Now() + 700; }
            return;
        case kActReload:
            if (p.act != kActReload && MoveSpeed(s.move) <= 0.f) { N(N_TASK_RELOAD_WEAPON, p.h, 1); p.act = kActReload; p.actMs = now; p.lastTaskMs = now; p.lastMove = 0; return; }
            break;
        default: break;
    }
    if (act == kActNone || act == kActCrouch) p.act = act;
    if (shooting) {   // aims where the owner's NPC shoots
        if (now - p.lastTaskMs >= 400) { N(N_TASK_AIM_GUN_AT_COORD, p.h, e.aim.x, e.aim.y, e.aim.z, 1000, 1, 0); p.lastTaskMs = now; p.lastMove = 254; }
        tracer();
        return;
    }
    if (p.act == kActReload && now - p.actMs < 1500) return;
    float speed = MoveSpeed(s.move);
    if (speed <= 0.f || r.stale) {
        if ((e.flags & kPopScenario) && err < 2.f) {
            if (!p.scenario) { N(N_TASK_USE_NEAREST_SCENARIO_TO_COORD_WARP, p.h, s.pos.x, s.pos.y, s.pos.z, 2.f, 0); p.scenario = true; p.lastTaskMs = now; }
            return;
        }
        if (p.scenario) { N(N_CLEAR_PED_TASKS, p.h); p.scenario = false; }
        if (p.lastMove != 0 || now - p.lastTaskMs > 1500) {
            if (err > 0.6f) N(N_TASK_GO_STRAIGHT_TO_COORD, p.h, s.pos.x, s.pos.y, s.pos.z, 1.0f, 2000, s.heading, 0.2f);
            else N(N_TASK_ACHIEVE_HEADING, p.h, s.heading, 1000);
            p.lastTaskMs = now;
            p.lastMove = 0;
        }
        return;
    }
    if (p.scenario) { N(N_CLEAR_PED_TASKS, p.h); p.scenario = false; }
    float along = dx * s.vel.x + dy * s.vel.y, boost = 1.f;
    if (err > 1.f && along > 0) { speed = speed < 3.f ? speed + 1.f : 3.f; boost = err > 2.5f ? 1.25f : 1.12f; }
    else if (err > 1.f && along < 0) boost = 0.85f;
    N(N_SET_PED_MOVE_RATE_OVERRIDE, p.h, boost);
    if (now - p.lastTaskMs >= 200 || p.lastMove != s.move) {
        N(N_TASK_GO_STRAIGHT_TO_COORD, p.h, s.pos.x + s.vel.x * 0.5f, s.pos.y + s.vel.y * 0.5f, s.pos.z + s.vel.z * 0.5f, speed, 2000, s.heading, 0.f);
        p.lastTaskMs = now;
        p.lastMove = s.move;
    }
}

// ---------------------------------------------------------------- hand-over (proximity migration)
// a puppet becomes a real NPC / vehicle of ours, thinking for itself from where it is
void ReleaseToAI(int h, const PopEntity &e, bool driver, int veh) {
    if (e.kind == kPopVehicle) {
        N(N_SET_ENTITY_PROOFS, h, 0, 0, 0, 0, 0, 0, 0, 0);
        N(N_SET_VEHICLE_EXPLODES_ON_HIGH_EXPLOSION_DAMAGE, h, 1);
        N(N_SET_DISABLE_VEHICLE_PETROL_TANK_FIRES, h, 0);
        N(N_SET_VEHICLE_STEER_BIAS, h, 0.f);
        N(N_SET_VEHICLE_DOORS_LOCKED, h, 1);
        return;
    }
    N(N_SET_BLOCKING_OF_NON_TEMPORARY_EVENTS, h, 0);
    N(N_SET_PED_MOVE_RATE_OVERRIDE, h, 1.f);
    N(N_SET_PED_STEALTH_MOVEMENT, h, 0, "DEFAULT_ACTION");
    if (driver && veh) {
        float spd = N<float>(N_GET_ENTITY_SPEED, veh);
        N(N_TASK_VEHICLE_DRIVE_WANDER, h, veh, std::max(spd, 12.f), 786603);
    } else if (!veh) {
        if (e.flags & kPopScenario) N(N_TASK_USE_NEAREST_SCENARIO_TO_COORD, h, e.pos.x, e.pos.y, e.pos.z, 3.f, -1);
        else N(N_TASK_WANDER_STANDARD, h, 10.f, 10);
    }
    N(N_SET_PED_KEEP_TASK, h, 1);
}
// GTA's population takes it back (it may clean it up once far from us, like any NPC of ours)
void NoLongerNeeded(int h) { int x = h; N(N_SET_ENTITY_AS_NO_LONGER_NEEDED, &x); }

// takes the puppet over; returns its new id (0: could not)
uint16_t TakeOver(uint32_t key, Pup &p, bool driver, int veh, uint16_t fromOwner) {
    if (!p.h || !gta::Exists(p.h)) return 0;
    int h = p.h;
    if (p.blip) { N(N_REMOVE_BLIP, &p.blip); p.blip = 0; }
    g_pupOf.erase(h);
    ReleaseToAI(h, p.last, driver, veh);
    Src s;
    s.id = NewId(); s.model = p.last.model; s.h = h; s.ped = p.last.kind == kPopPed; s.pos = gta::Pos(h); s.frame = g_frame;
    s.streamed = true; s.descLeft = 1;
    if (fromOwner) { s.fromOwner = fromOwner; s.fromId = IdOfKey(key); s.fromUntil = Now() + kMigratedShowMs; }
    uint64_t k = Key(h);
    g_src[k] = s;
    g_srcById[s.id] = k;
    p.h = 0;
    return s.id;
}

// "o|id" from its owner: we take it if we draw it (a vehicle with all its people)
std::string OnOffer(uint16_t owner, uint16_t id) {
    char b[24];
    snprintf(b, sizeof b, "n|%u", id);
    if (g_mode != kPopShared) return b;
    uint32_t key = PupKey(owner, id);
    Pup *p = PupAt(key);
    if (!p || !p->h || !gta::Exists(p->h) || gta::Dead(p->h) || (p->last.flags & (kPopDead | kPopMissionEnt | kPopPolice)) || Now() - p->seenAt > 600) return b;
    std::string reply;
    if (p->last.kind == kPopVehicle) {
        std::vector<std::pair<uint32_t, Pup *>> occ;
        for (auto &kv : g_pups) {
            Pup &o = kv.second;
            if (OwnerOf(kv.first) != owner || o.last.kind != kPopPed || !(o.last.flags & kPopInVehicle) || o.last.vehId != id) continue;
            if (!o.h || !NB(N<int>(N_IS_PED_IN_VEHICLE, o.h, p->h, 0)) || (o.last.flags & (kPopDead | kPopMissionEnt | kPopPolice))) return b;   // not all of them here yet
            occ.push_back({kv.first, &o});
        }
        int veh = p->h;
        uint16_t nid = TakeOver(key, *p, false, 0, owner);
        if (!nid) return b;
        std::vector<int> release{veh};
        reply = "a|" + std::to_string(id) + "|" + std::to_string(nid) + "|";
        bool first = true;
        for (auto &o : occ) {
            int ph = o.second->h;
            uint16_t pid = TakeOver(o.first, *o.second, o.second->last.seat == -1, veh, owner);
            if (!pid) continue;
            release.push_back(ph);
            if (!first) reply += ',';
            reply += std::to_string(IdOfKey(o.first)) + ":" + std::to_string(pid);
            first = false;
        }
        for (auto &o : occ) g_pups.erase(o.first);
        g_pups.erase(key);
        for (int h : release) NoLongerNeeded(h);   // last: GTA may hand them new handles from here on (our ids follow the entities)
    } else {
        if ((p->last.flags & (kPopInVehicle | kPopEntering)) || gta::InVehicle(p->h)) return b;
        int h = p->h;
        uint16_t nid = TakeOver(key, *p, false, 0, owner);
        if (!nid) return b;
        NoLongerNeeded(h);
        g_pups.erase(key);
        reply = "a|" + std::to_string(id) + "|" + std::to_string(nid) + "|";
    }
    ++g_handedIn;
    return reply;
}

// one of ours becomes a puppet of a peer's (it took it over): same entity, held from their stream from now on
void MakePup(int h, bool ped, uint32_t model, uint16_t owner, uint16_t newId) {
    uint32_t key = PupKey(owner, newId);
    Pup &p = g_pups[key];
    p = Pup{};
    p.h = h;
    PopEntity e;
    if (ped) {
        PuppetPedSetup(h);
        N(N_CLEAR_PED_TASKS, h);
        e.kind = kPopPed;
        e.health = (uint8_t)std::clamp(gta::Health(h), 0, 255);
        p.hpSet = gta::Health(h);
        p.inVeh = gta::InVehicle(h);
        uint32_t w = N<uint32_t>(N_GET_SELECTED_PED_WEAPON, h);
        p.weapon = w == gta::Joaat("WEAPON_UNARMED") ? 0 : w;
    } else {
        PuppetVehicleSetup(h);
        e.kind = kPopVehicle;
        p.body = N<float>(N_GET_VEHICLE_BODY_HEALTH, h); p.engine = N<float>(N_GET_VEHICLE_ENGINE_HEALTH, h);
        p.vflags = 0xFF;
    }
    e.id = newId; e.model = model;
    gta::Vec3 at = gta::Pos(h);
    e.pos = {at.x, at.y, at.z};
    if (ped && p.inVeh) e.flags |= kPopInVehicle;
    ReadDesc(h, e);
    p.last = e; p.desc = e; p.haveDesc = true;
    p.seenAt = Now();
    p.lastTaskMs = gta::GameTimeMs();
    g_pupOf[h] = key;
}
// the people of a car we handed over: held as puppets at once (their new ids come with the answer or the stream)
struct Pending { int h = 0; bool ped = true; uint32_t model = 0; unsigned long long until = 0; };
std::map<uint16_t, Pending> g_pending;   // our old id -> the entity
void BecomePuppet(uint16_t id, uint16_t owner, uint16_t newId) {
    auto pe = g_pending.find(id);
    if (pe != g_pending.end()) {
        Pending pd = pe->second;
        g_pending.erase(pe);
        g_reserved.erase(pd.h);
        if (gta::Exists(pd.h)) MakePup(pd.h, pd.ped, pd.model, owner, newId);
        return;
    }
    Src *s = SrcById(id);
    if (!s || !gta::Exists(s->h)) { DropSrc(id); return; }
    int h = s->h;
    bool ped = s->ped;
    uint32_t model = s->model;
    DropSrc(id);
    if (!ped) {
        std::vector<int> occ;
        AmbientOccupants(h, occ);
        for (int o : occ) {
            auto it = g_src.find(Key(o));
            if (it == g_src.end()) continue;
            PuppetPedSetup(o);
            N(N_CLEAR_PED_TASKS, o);
            g_pending[it->second.id] = {o, true, it->second.model, Now() + 3000};
            g_reserved.insert(o);
            g_srcById.erase(it->second.id);
            g_src.erase(it);
        }
    }
    MakePup(h, ped, model, owner, newId);
}

void OnAccept(uint16_t from, const char *rest) {   // "id|newId|old:new,old:new"
    unsigned id = 0, nid = 0;
    if (sscanf(rest, "%u|%u", &id, &nid) != 2 || !nid) return;
    if (SrcById((uint16_t)id) || g_pending.count((uint16_t)id)) { BecomePuppet((uint16_t)id, from, (uint16_t)nid); ++g_handedOut; }
    const char *bar = strchr(rest, '|');
    bar = bar ? strchr(bar + 1, '|') : nullptr;
    if (!bar) return;
    for (const char *q = bar + 1; *q;) {
        unsigned a = 0, b = 0;
        if (sscanf(q, "%u:%u", &a, &b) == 2 && (SrcById((uint16_t)a) || g_pending.count((uint16_t)a))) BecomePuppet((uint16_t)a, from, (uint16_t)b);
        const char *c = strchr(q, ',');
        if (!c) break;
        q = c + 1;
    }
}

// every 500 ms: what is clearly nearer a peer (or only still in its range) is offered to that peer's game
void HandOver() {
    if (g_mode != kPopShared || g_peerAt.empty()) return;
    unsigned long long now = Now();
    gta::Vec3 at = gta::Pos(gta::PlayerPed());
    int offers = 0;
    for (auto &kv : g_src) {
        Src &s = kv.second;
        if (offers >= 10) break;
        if (!s.streamed || now - s.offeredAt < kOfferRetryMs || !gta::Exists(s.h) || gta::Dead(s.h) || Mission(s.h)) continue;
        if (s.ped && (gta::InVehicle(s.h) || PolicePed(s.h) || NB(N<int>(N_IS_PED_GETTING_INTO_A_VEHICLE, s.h)))) continue;   // people in cars go with their car
        std::vector<int> occ;
        if (!s.ped && (PoliceVehicle(s.h) || !AmbientOccupants(s.h, occ))) continue;
        bool occOk = true;
        for (int o : occ) if (PolicePed(o) || Mission(o) || gta::Dead(o) || !IdOf(o)) occOk = false;
        if (!occOk) continue;
        float dm = Dist2(s.pos, at), dp = 0;
        const PeerAt *pa = NearestPeer(s.pos, dp);
        if (!pa) continue;
        float range = s.ped ? kPedRange : kVehRange;
        bool closer = dp < kHandRatio * kHandRatio * dm && dm > kHandMinDist * kHandMinDist;
        bool leaving = dm > 0.85f * 0.85f * range * range && dp < 0.81f * dm;
        if (!closer && !leaving) continue;
        s.offeredAt = now;
        QueueTo(g_outPm, kMsgPopMigrate, pa->id, "o|" + std::to_string(s.id));
        ++offers; ++g_offered;
    }
}

// a peer's puppets become ours (they left the group / the game): what is near us stays, the rest goes
void TakeOverAll(bool keepNear) {
    if (g_pups.empty()) return;
    gta::Vec3 at = gta::Pos(gta::PlayerPed());
    int kept = 0, gone = 0;
    std::set<int> cars;
    std::vector<int> release;
    // vehicles first: their people go with them
    for (int pass = 0; pass < 2; ++pass)
        for (auto it = g_pups.begin(); it != g_pups.end();) {
            Pup &p = it->second;
            bool veh = p.last.kind == kPopVehicle;
            if (veh != (pass == 0)) { ++it; continue; }
            bool ok = keepNear && p.h && gta::Exists(p.h) && !(p.last.flags & (kPopMissionEnt | kPopPolice | kPopDead)) &&
                      Dist2(gta::Pos(p.h), at) < kTakeOverRange * kTakeOverRange;
            int car = 0;
            if (ok && !veh && gta::InVehicle(p.h)) { car = N<int>(N_GET_VEHICLE_PED_IS_IN, p.h, 0); ok = cars.count(car) != 0; }
            int h = p.h;
            if (ok && TakeOver(it->first, p, car && p.last.seat == -1, car, 0)) { release.push_back(h); if (veh) cars.insert(h); ++kept; }
            else { DeletePup(p); ++gone; }
            it = g_pups.erase(it);
        }
    for (int h : release) NoLongerNeeded(h);
    if (kept || gone) LOGI("pop", "-", "the peers' NPCs and cars: %d near us are ours now, %d removed", kept, gone);
}

// the player gets into a peer's car: it becomes ours (its owner gives the entity to remote.cpp as our car there)
void TakeCars() {
    if (g_mode != kPopShared) return;
    int v = N<int>(N_GET_VEHICLE_PED_IS_TRYING_TO_ENTER, g_me);
    bool trying = gta::Exists(v);
    if (!trying) v = g_myVeh;
    if (!v) return;
    auto it = g_pupOf.find(v);
    if (it == g_pupOf.end()) return;
    uint32_t key = it->second;
    Pup *p = PupAt(key);
    if (!p || p->last.kind != kPopVehicle) return;
    int seat = trying ? N<int>(N_GET_SEAT_PED_IS_TRYING_TO_ENTER, g_me) : (N<int>(N_GET_PED_IN_VEHICLE_SEAT, v, -1, 0) == g_me ? -1 : 0);
    if (seat != -1) return;   // a passenger rides along: its owner keeps driving it
    if (trying && Dist2(gta::Pos(v), gta::Pos(g_me)) > 6.f * 6.f) return;   // at its door, not when we set off towards it
    char b[24];
    snprintf(b, sizeof b, "c|%u", IdOfKey(key));
    QueueEvent(OwnerOf(key), b);
    FlushEvents(true);
    for (auto &kv : g_pups)   // its people: hands off while the game pulls them out
        if (OwnerOf(kv.first) == OwnerOf(key) && kv.second.last.kind == kPopPed && (kv.second.last.flags & kPopInVehicle) && kv.second.last.vehId == IdOfKey(key))
            kv.second.holdUntil = Now() + 6000;
    g_pupOf.erase(v);
    ReleaseToAI(v, p->last, false, 0);
    NoLongerNeeded(v);
    p->h = 0;
    LOGI("pop", remote::NameOf(OwnerOf(key)).c_str(), "we took their %svehicle %u", (p->last.flags & kPopMissionEnt) ? "mission " : "", IdOfKey(key));
    g_pups.erase(key);
}

// ================================================================ trains (v17)
// One game of a group (PopRole.trains) runs GTA's random trains and streams each one as its engine: the trains.xml config
// (which carriages), where its engine is, and its speed. The other games switch their own trains off and run the very
// same config as a mission train on the same track, held to the stream by its speed and, when it drifts, its place.
const char *kTrainCfgText[kTrainConfigs] = {
    "freight:1 freightcar:2",   // 0 freight_config0
    "freight:1 freightcar:8",   // 1 freight_config1
    "freight:1 freightcont2:1 freightcont1:1 freightgrain:1 tankercar:1 freightcar:1",   // 2 freight_config2
    "freight:1 freightcar:1 tankercar:3 freightcar:2 tankercar:2",   // 3 freight_config3
    "freight:1 freightgrain:2 freightcar:1 freightgrain:1 freightcar:3 freightgrain:2",   // 4 freight_config4
    "freight:1 freightcar:4 freightgrain:1 freightcar:1",   // 5 freight_config5
    "freight:1 freightcont1:2 freightcar:1 tankercar:1",   // 6 freight_config6
    "freight:1 freightcar:3 freightcont2:3 freightcar:2",   // 7 freight_config7
    "freight:1 freightcont2:2 freightcar:1 freightcont2:2 freightcar:1 tankercar:1",   // 8 freight_config8
    "freight:1 freightcar:1 freightgrain:2 freightcar:2 freightgrain:1",   // 9 freight_config9
    "freight:1 freightcont1:3 freightcont2:2 freightcar:3",   // 10 freight_config10
    "freight:1 freightcont1:1 freightcar:1 freightcont1:5",   // 11 freight_config11
    "freight:1 freightgrain:3 freightcar:1 freightgrain:2",   // 12 freight_config12
    "freight:1 freightcar:2 tankercar:3 freightcar:3",   // 13 freight_config13
    "freight:1 freightcont1:1 freightcar:2 freightcont1:3 freightcar:1 freightcont1:1",   // 14 freight_config14
    "freight:1 freightgrain:3 freightcar:1 freightcont1:2 freightgrain:1 freightcar:1",   // 15 freight_config15
    "freight:1 freightcar:1 tankercar:3 freightcar:1 tankercar:2",   // 16 freight_config16
    "freight:1 freightcont2:40",   // 17 freight_config17
    "freight:1",   // 18 freight_config18
    "freight:1 tankercar:2",   // 19 freight_config19
    "freight:1 freightcont2:3",   // 20 freight_config20
    "metrotrain:1",   // 21 freight_config21
    "freight:1 freightcar:12",   // 22 freight_config22
    "freight:1 freightcar:1 freightcont1:1 freightcar:1 freightcont1:1 freightcar:1 freightcont1:1 freightcar:1 freightcont1:1 freightcar:1 freightcont1:1 freightcar:1 freightcont1:1 freightcar:1",   // 23 freight_config23
    "freight:1 freightcar2:4 freightgrain:1 freightcar2:1",   // 24 freight_config24
    "freight:1 freightgrain:1 freightcar2:1 tankercar:1 freightgrain:1 tankercar:1 freightcar2:1 freightgrain:1 freightcar2:1 tankercar:2 freightcar2:1 tankercar:1 freightcar2:1 freightgrain:1 tankercar:2",   // 25 freight_config25
    "freight2:1 freightcar2:5",   // 26 freight_config26
    "freight:1 freightcar3:6",   // 27 freight_config27
    "metrotrain:1 metrotrain:1",   // 28 metro_config0
};
std::vector<std::vector<uint32_t>> g_trainCfg;   // config -> engine + carriage models, in order
void TrainTable() {
    if (!g_trainCfg.empty()) return;
    g_trainCfg.resize(kTrainConfigs);
    for (int i = 0; i < (int)kTrainConfigs; ++i) {
        std::string t = kTrainCfgText[i];
        size_t at = 0;
        while (at < t.size()) {
            size_t sp = t.find(' ', at), colon = t.find(':', at);
            std::string name = t.substr(at, colon - at);
            int n = std::atoi(t.c_str() + colon + 1);
            for (int k = 0; k < n; ++k) g_trainCfg[i].push_back(gta::Joaat(name.c_str()));
            at = sp == std::string::npos ? t.size() : sp + 1;
        }
    }
}
uint16_t g_trainsBy = 0;   // PopRole.trains
bool TrainsOurs() { return !g_trainsBy || g_trainsBy == net::SelfNetId(); }
std::set<int> g_trainCars;   // our train puppets' engines and carriages
// the engines among the game's trains (a carriage is reachable from its engine with GET_TRAIN_CARRIAGE)
std::vector<int> TrainEngines(bool puppetsToo) {
    std::vector<int> trains;
    int nv = AllVehicles();
    for (int i = 0; i < nv; ++i) if (gta::Exists(g_vehs[i]) && Train(g_vehs[i]) && (puppetsToo || !g_trainCars.count(g_vehs[i]))) trains.push_back(g_vehs[i]);
    std::set<int> cars;
    for (int t : trains)
        for (int i = 1; i < 80; ++i) { int c = N<int>(N_GET_TRAIN_CARRIAGE, t, i); if (!c || !gta::Exists(c)) break; cars.insert(c); }
    std::vector<int> engines;
    for (int t : trains) if (!cars.count(t)) engines.push_back(t);
    return engines;
}
int TrainConfigOf(int engine) {
    TrainTable();
    std::vector<uint32_t> m{gta::ModelOf(engine)};
    for (int i = 1; i < 80; ++i) { int c = N<int>(N_GET_TRAIN_CARRIAGE, engine, i); if (!c || !gta::Exists(c)) break; m.push_back(gta::ModelOf(c)); }
    for (int i = 0; i < (int)kTrainConfigs; ++i) if (g_trainCfg[i] == m) return i;
    return -1;
}
// the runner: our trains go out with the rest of our stream
void StreamTrains(std::vector<PopEntity> &out, const gta::Vec3 &at) {
    if (g_trainsBy != net::SelfNetId()) return;
    for (int t : TrainEngines(false)) {
        gta::Vec3 p = gta::Pos(t);
        if (Dist2(p, at) > 800.f * 800.f) continue;
        int cfg = TrainConfigOf(t);
        if (cfg < 0) { ++g_trainsUnknown; continue; }   // a config we do not know (another game version)
        Src &s = Track(t, false);
        PopEntity e;
        e.kind = kPopTrain; e.id = s.id; e.model = gta::ModelOf(t); e.config = (uint8_t)cfg;
        e.pos = {p.x, p.y, p.z};
        NVec3 v = N<NVec3>(N_GET_ENTITY_VELOCITY, t), f = N<NVec3>(N_GET_ENTITY_FORWARD_VECTOR, t);
        e.vel = {v.x, v.y, v.z};
        e.speed = v.x * f.x + v.y * f.y + v.z * f.z;
        N(N_GET_ENTITY_QUATERNION, t, &e.rot.x, &e.rot.y, &e.rot.z, &e.rot.w);
        out.push_back(e);
        ++g_trainsStreamed;
    }
}
struct TrainPup { PopEntity last; interp::Buffer buf; int h = 0; unsigned long long seenAt = 0; int tries = 0; };
std::map<uint32_t, TrainPup> g_trainPups;
unsigned g_trainSpawned = 0, g_trainSnaps = 0, g_ownTrainsRemoved = 0;
unsigned long long g_lastTrainClean = 0;
bool g_randomTrainsOff = false;
void RandomTrains(bool on) {
    if (on == !g_randomTrainsOff) return;
    g_randomTrainsOff = !on;
    N(N_SET_RANDOM_TRAINS, on ? 1 : 0);
    LOGI("pop", "-", on ? "our own trains are back" : "our own trains are off: we run the group's");
}
void DeleteTrainPup(TrainPup &t) {
    if (!t.h) return;
    for (int i = 1; i < 80; ++i) { int c = N<int>(N_GET_TRAIN_CARRIAGE, t.h, i); if (!c) break; g_trainCars.erase(c); }
    g_trainCars.erase(t.h);
    if (gta::Exists(t.h)) { gta::MakeMission(t.h); N(N_DELETE_MISSION_TRAIN, &t.h); }
    t.h = 0;
}
void ClearTrainPups() { for (auto &kv : g_trainPups) DeleteTrainPup(kv.second); g_trainPups.clear(); }
float YawOf(const Quat &q) {
    float yaw = std::atan2(2.f * (q.w * q.z + q.x * q.y), 1.f - 2.f * (q.y * q.y + q.z * q.z)) * 57.29578f;
    return yaw < 0 ? yaw + 360.f : yaw;
}
// another game runs the trains: ours go, theirs run here
void TrainsFrame() {
    if (TrainsOurs()) { ClearTrainPups(); RandomTrains(true); return; }
    RandomTrains(false);
    unsigned long long now = Now();
    if (now - g_lastTrainClean > 1000) {   // what GTA still had goes
        g_lastTrainClean = now;
        for (int t : TrainEngines(false)) {
            int x = t;
            gta::MakeMission(x);
            N(N_DELETE_MISSION_TRAIN, &x);
            ++g_ownTrainsRemoved;
        }
    }
    if (!g_clock.Synced()) return;
    double renderT = g_clock.ServerNow((double)now / 1000.0) - kDelay;
    TrainTable();
    for (auto it = g_trainPups.begin(); it != g_trainPups.end();) {
        TrainPup &t = it->second;
        if (now - t.seenAt > 4000) { DeleteTrainPup(t); it = g_trainPups.erase(it); continue; }
        if (t.h && !gta::Exists(t.h)) { g_trainCars.erase(t.h); t.h = 0; }
        interp::Result r = t.buf.At(renderT, 1.0);
        if (!r.valid) { ++it; continue; }
        if (!t.h) {
            if (t.tries > 6) { ++it; continue; }   // never on a track here
            bool ready = true;
            for (uint32_t m : g_trainCfg[t.last.config]) { N(N_REQUEST_MODEL, m); if (!NB(N<int>(N_HAS_MODEL_LOADED, m))) ready = false; }
            if (!ready) { ++it; continue; }
            float want = YawOf(t.last.rot);
            for (int dir = 1; dir >= 0 && !t.h; --dir) {   // the direction whose engine faces the owner's
                int h = N<int>(N_CREATE_MISSION_TRAIN, (int)t.last.config, r.s.pos.x, r.s.pos.y, r.s.pos.z, dir, 0, 0);
                if (!h) continue;
                float d = std::fabs(std::fmod(gta::Heading(h) - want + 540.f, 360.f) - 180.f);
                if (d > 90.f && dir == 1) { int x = h; N(N_DELETE_MISSION_TRAIN, &x); continue; }
                t.h = h;
            }
            ++t.tries;
            if (!t.h) { ++it; continue; }
            g_trainCars.insert(t.h);
            for (int i = 1; i < 80; ++i) { int c = N<int>(N_GET_TRAIN_CARRIAGE, t.h, i); if (!c) break; g_trainCars.insert(c); }
            ++g_trainSpawned;
            gta::Vec3 got = gta::Pos(t.h);
            LOGI("pop", remote::NameOf(OwnerOf(it->first)).c_str(), "train config %u drawn at %.0f %.0f %.0f (theirs at %.0f %.0f %.0f), heading %.0f / %.0f", t.last.config, got.x, got.y,
                 got.z, r.s.pos.x, r.s.pos.y, r.s.pos.z, gta::Heading(t.h), want);
        }
        // held to the stream: its speed, plus a pull along its track; a jump when it is far off
        gta::Vec3 cur = gta::Pos(t.h);
        NVec3 f = N<NVec3>(N_GET_ENTITY_FORWARD_VECTOR, t.h);
        float ex = r.s.pos.x - cur.x, ey = r.s.pos.y - cur.y, ez = r.s.pos.z - cur.z;
        float err = std::sqrt(ex * ex + ey * ey + ez * ez);
        if (err > 15.f) { N(N_SET_MISSION_TRAIN_COORDS, t.h, r.s.pos.x, r.s.pos.y, r.s.pos.z); ++g_trainSnaps; }
        float along = ex * f.x + ey * f.y + ez * f.z;
        float speed = t.last.speed + std::clamp(along * 0.6f, -5.f, 5.f);
        N(N_SET_TRAIN_SPEED, t.h, speed);
        N(N_SET_TRAIN_CRUISE_SPEED, t.h, speed);
        ++it;
    }
}
void OnTrain(uint16_t owner, const PopEntity &e, double st) {
    if (TrainsOurs() || owner != g_trainsBy) return;
    TrainPup &t = g_trainPups[PupKey(owner, e.id)];
    if (t.h && t.last.config != e.config) DeleteTrainPup(t);
    t.last = e;
    t.seenAt = Now();
    interp::Sample s;
    s.t = st;
    s.pos = {e.pos.x, e.pos.y, e.pos.z};
    s.vel = {e.vel.x, e.vel.y, e.vel.z};
    t.buf.Push(s);
}

// ================================================================ wanted players (v17)
// GTA Online's police chase every wanted player, whoever's game they belong to: a remote player with a wanted level is in
// the LO_WANTED relationship group (remote.cpp), which the police hate, and our idle police near one go for them; what
// they do to its puppet goes to that player as NPC damage ("lo.nd").
unsigned long long g_lastCops = 0;
void CopsOnWanted() {
    unsigned long long now = Now();
    if (now - g_lastCops < 1000) return;
    g_lastCops = now;
    std::vector<gta::Vec3> wanted;
    for (int ped : remote::WantedPuppets()) if (gta::Exists(ped)) wanted.push_back(gta::Pos(ped));
    if (wanted.empty()) return;
    int np = AllPeds();
    for (int i = 0; i < np; ++i) {
        int p = g_peds[i];
        if (!gta::Exists(p) || gta::Dead(p) || !PolicePed(p) || IsPuppet(p) || remote::IsPuppetEntity(p) || Mission(p)) continue;
        if (NB(N<int>(N_IS_PED_IN_COMBAT, p, 0))) continue;
        gta::Vec3 at = gta::Pos(p);
        for (auto &w : wanted)
            if (Dist2(at, w) < 90.f * 90.f) { N(N_TASK_COMBAT_HATED_TARGETS_AROUND_PED, p, 90.f, 0); ++g_copsSent; break; }
    }
}

void DrawPuppets() {
    if (!g_clock.Synced()) return;
    unsigned long long now = Now();
    double renderT = g_clock.ServerNow((double)now / 1000.0) - kDelay;
    int spawns = 0;
    // vehicles before peds (seats)
    for (int pass = 0; pass < 2; ++pass)
        for (auto it = g_pups.begin(); it != g_pups.end();) {
            uint32_t key = it->first;
            Pup &p = it->second;
            bool veh = p.last.kind == kPopVehicle;
            if (veh != (pass == 0)) { ++it; continue; }
            if (now - p.seenAt > (veh ? kVehTimeoutMs : kPedTimeoutMs)) { DeletePup(p); it = g_pups.erase(it); continue; }
            if (p.h && !gta::Exists(p.h)) { g_pupOf.erase(p.h); p.h = 0; }   // the game removed it
            interp::Result r = p.buf.At(renderT, 0.4);
            if (!r.valid) { ++it; continue; }
            if (!p.h && Adopt(key, p, r.s)) { ++it; continue; }
            if (!p.h) {
                if (p.haveDesc && spawns < kMaxSpawnsPerFrame && Spawn(key, p, r.s)) ++spawns;
                ++it;
                continue;
            }
            if (veh) { ReportVehicle(key, p); DriveVehicle(p, r); }
            else { ReportPed(key, p); HostileBlip(p); DrivePed(key, p, r); }
            ++it;
        }
}

void ViewStats() {
    unsigned long long now = Now();
    if (now - g_lastViewStat < 30000) return;
    g_lastViewStat = now;
    std::map<uint16_t, int> byOwner;
    int peds = 0, vehs = 0, fresh = 0, mis = 0, hos = 0, cover = 0;
    for (auto &kv : g_pups) {
        if (kv.second.h) { (kv.second.last.kind == kPopPed ? peds : vehs)++; ++byOwner[OwnerOf(kv.first)]; }
        if (now - kv.second.seenAt < 500) ++fresh;
        if (kv.second.act == kActCover) ++cover;
        if ((kv.second.last.flags & kPopMissionEnt) && kv.second.last.kind == kPopPed && now - kv.second.seenAt < 500) { ++mis; if (kv.second.last.flags & kPopHostile) ++hos; }
    }
    std::string owners;
    for (auto &kv : byOwner) owners += (owners.empty() ? "" : ", ") + remote::NameOf(kv.first) + " " + std::to_string(kv.second);
    LOGI("pop", "-", "puppets: %d peds, %d vehicles drawn (%d known, %d fresh) from %s; %d mission peds (%d hostile), %d in cover", peds, vehs, (int)g_pups.size(), fresh,
         owners.empty() ? "nobody" : owners.c_str(), mis, hos, cover);
    LOGI("pop", "-", "puppets: %u new ids (%u took over a puppet, %u re-keyed after a hand-over), %u relay packets in 30 s%s", g_newIds, g_adopted, g_rekeyed, g_relays,
         g_mode == kPopViewer ? (", " + std::to_string(g_removed) + " of our own removed").c_str() : "");
    if (!g_trainPups.empty() || g_ownTrainsRemoved) {
        int drawn = 0; for (auto &kv : g_trainPups) drawn += kv.second.h != 0;
        LOGI("pop", "-", "trains: %d of %d drawn (%u spawned, %u put back on their place), %u of our own removed", drawn, (int)g_trainPups.size(), g_trainSpawned, g_trainSnaps, g_ownTrainsRemoved);
        g_trainSpawned = g_trainSnaps = g_ownTrainsRemoved = 0;
    }
    g_newIds = g_relays = g_adopted = g_rekeyed = 0;
}

void View() {   // a camera: only the players' world
    Suppress(true);
    N(N_SET_PED_DENSITY_MULTIPLIER_THIS_FRAME, 0.f);
    N(N_SET_SCENARIO_PED_DENSITY_MULTIPLIER_THIS_FRAME, 0.f, 0.f);
    N(N_SET_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME, 0.f);
    N(N_SET_RANDOM_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME, 0.f);
    N(N_SET_PARKED_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME, 0.f);
    g_me = gta::PlayerPed();
    g_myVeh = gta::InVehicle(g_me) ? N<int>(N_GET_VEHICLE_PED_IS_IN, g_me, 0) : 0;
    CleanOwn();
    DrawPuppets();
    TrainsFrame();
    ViewStats();
}

void Shared() {   // a player of a group: our part of the world is ours, the peers' parts are their puppets
    g_me = gta::PlayerPed();
    g_myVeh = gta::InVehicle(g_me) ? N<int>(N_GET_VEHICLE_PED_IS_IN, g_me, 0) : 0;
    if (g_share < 0.999f) {
        N(N_SET_PED_DENSITY_MULTIPLIER_THIS_FRAME, g_share);
        N(N_SET_SCENARIO_PED_DENSITY_MULTIPLIER_THIS_FRAME, g_share, g_share);
        N(N_SET_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME, g_share);
        N(N_SET_RANDOM_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME, g_share);
        N(N_SET_PARKED_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME, g_share);
    }
    Stream();
    TakeCars();
    DrawPuppets();
    TrainsFrame();
    CopsOnWanted();
    FlushEvents();
    ViewStats();
    unsigned long long now = Now();   // NPC damage to the peers' puppets, at most ~7 a second each
    for (auto &kv : g_npcDmg) {
        if (kv.second <= 0 || now - g_npcDmgAt[kv.first] < 140) continue;
        SendMsg(kv.first, kMsgNpcDamage, std::to_string(kv.second) + "|0");
        g_npcDmgAt[kv.first] = now;
        kv.second = 0;
    }
    // cars given to a player that never came for them: back to us (an ambient one simply goes)
    for (auto it = g_given.begin(); it != g_given.end();) {
        if (now < it->until && gta::Exists(it->h)) { ++it; continue; }
        g_reserved.erase(it->h);
        if (gta::Exists(it->h) && !it->mission) { int h = it->h; Delete(h, false); }
        it = g_given.erase(it);
    }
    for (auto it = g_pending.begin(); it != g_pending.end();) {   // a car's people whose new owner never said: gone
        if (now < it->second.until) { ++it; continue; }
        g_reserved.erase(it->second.h);
        int h = it->second.h;
        Delete(h, it->second.ped);
        it = g_pending.erase(it);
    }
    for (auto *box : {&g_claimed, &g_orphans})
        for (auto it = box->begin(); it != box->end();) {
            if (gta::Exists(it->h)) { ++it; continue; }
            g_reserved.erase(it->h);
            it = box->erase(it);
        }
}

// remote players' puppets on our game are fair game for a mission's enemies (Gori's GORI_HOSTILE hates them like the player)
unsigned long long g_lastGroups = 0;
void Groups() {
    unsigned long long now = Now();
    if (now - g_lastGroups < 2000) return;
    g_lastGroups = now;
    uint32_t lo = 0;
    N(N_ADD_RELATIONSHIP_GROUP, "LO_REMOTE", &lo);
    uint32_t hostile = gta::Joaat("GORI_HOSTILE"), player = gta::Joaat("PLAYER");
    lo = gta::Joaat("LO_REMOTE");
    N(N_SET_RELATIONSHIP_BETWEEN_GROUPS, 5, hostile, lo);
    N(N_SET_RELATIONSHIP_BETWEEN_GROUPS, 5, lo, hostile);
    N(N_SET_RELATIONSHIP_BETWEEN_GROUPS, 1, lo, player);
    N(N_SET_RELATIONSHIP_BETWEEN_GROUPS, 1, player, lo);
    // v17: a wanted remote player (remote.cpp) is fair game for our police, like the player is for its own game's
    uint32_t wanted = 0;
    N(N_ADD_RELATIONSHIP_GROUP, "LO_WANTED", &wanted);
    wanted = gta::Joaat("LO_WANTED");
    for (const char *g : {"COP", "ARMY", "SECURITY_GUARD"}) {
        N(N_SET_RELATIONSHIP_BETWEEN_GROUPS, 5, gta::Joaat(g), wanted);
        N(N_SET_RELATIONSHIP_BETWEEN_GROUPS, 5, wanted, gta::Joaat(g));
    }
    N(N_SET_RELATIONSHIP_BETWEEN_GROUPS, 5, hostile, wanted);
    N(N_SET_RELATIONSHIP_BETWEEN_GROUPS, 5, wanted, hostile);
    N(N_SET_RELATIONSHIP_BETWEEN_GROUPS, 1, wanted, player);
    N(N_SET_RELATIONSHIP_BETWEEN_GROUPS, 1, player, wanted);
}

void OnEvent(uint16_t from, const std::string &text) {   // "d|id|amount|weapon;k|id|weapon;v|id|body|engine|expl;c|id"
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find(';', i);
        std::string it = text.substr(i, j == std::string::npos ? std::string::npos : j - i);
        i = j == std::string::npos ? text.size() : j + 1;
        char k = 0; unsigned id = 0; int a = 0, b = 0, c = 0; unsigned w = 0;
        if (it.size() < 3 || it[1] != '|') continue;
        k = it[0];
        const char *rest = it.c_str() + 2;
        if (sscanf(rest, "%u", &id) != 1) continue;
        Src *s = SrcById((uint16_t)id);
        int h = s && gta::Exists(s->h) ? s->h : 0;
        if (!h) continue;
        if (k == 'd' && sscanf(rest, "%u|%d|%u", &id, &a, &w) >= 2) {
            if (gta::Dead(h)) continue;
            a = std::clamp(a, 0, 1000);
            N(N_APPLY_DAMAGE_TO_PED, h, a, 0, 0, w);
            LOGD("pop", remote::NameOf(from).c_str(), "hit ped %u for %d", id, a);
        } else if (k == 'k' && sscanf(rest, "%u|%u", &id, &w) >= 1) {
            if (gta::Dead(h)) continue;
            N(N_SET_ENTITY_HEALTH, h, 0, 0, w);
            LOGI("pop", remote::NameOf(from).c_str(), "killed ped %u", id);
        } else if (k == 'v' && sscanf(rest, "%u|%d|%d|%d", &id, &a, &b, &c) >= 3) {
            if (gta::Dead(h)) continue;
            a = std::clamp(a, 0, 2000); b = std::clamp(b, 0, 2000);
            float body = N<float>(N_GET_VEHICLE_BODY_HEALTH, h) - (float)a, eng = N<float>(N_GET_VEHICLE_ENGINE_HEALTH, h) - (float)b;
            N(N_SET_VEHICLE_BODY_HEALTH, h, body < 0.f ? 0.f : body);
            N(N_SET_VEHICLE_ENGINE_HEALTH, h, eng < -3900.f ? -3900.f : eng);
            if (c && (a >= 800 || eng < -1000.f)) N(N_EXPLODE_VEHICLE, h, 1, 0);
            LOGI("pop", remote::NameOf(from).c_str(), "damaged vehicle %u: body -%d engine -%d%s", id, a, b, c ? " (explosion)" : "");
        } else if (k == 'c') {   // they take this car: its people out, the car is theirs (remote.cpp draws it as their car)
            if (s->ped) continue;
            bool mission = Mission(h);
            int thief = remote::PedOfNetId(from);
            int seats = N<int>(N_GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS, h);
            for (int seat = -1; seat < seats && seat < 15; ++seat) {
                int p = N<int>(N_GET_PED_IN_VEHICLE_SEAT, h, seat, 0);
                if (!gta::Exists(p) || NB(N<int>(N_IS_PED_A_PLAYER, p)) || remote::IsPuppetEntity(p)) continue;
                N(N_CLEAR_PED_TASKS_IMMEDIATELY, p);
                if (!Mission(p) && thief) N(N_TASK_SMART_FLEE_PED, p, thief, 100.f, -1, 0, 0);
            }
            PuppetVehicleSetup(h);
            g_given.push_back({h, from, gta::ModelOf(h), mission, Now() + kGivenMs});
            g_reserved.insert(h);
            DropSrc((uint16_t)id);
            LOGI("pop", remote::NameOf(from).c_str(), "took our %svehicle %u: theirs now", mission ? "mission " : "", id);
        }
    }
}

void OnMigrate(uint16_t from, const std::string &text) {   // "o|id;a|id|newId|old:new,...;n|id"
    size_t i = 0;
    std::string reply;
    while (i < text.size()) {
        size_t j = text.find(';', i);
        std::string it = text.substr(i, j == std::string::npos ? std::string::npos : j - i);
        i = j == std::string::npos ? text.size() : j + 1;
        if (it.size() < 3 || it[1] != '|') continue;
        unsigned id = 0;
        if (it[0] == 'o' && sscanf(it.c_str() + 2, "%u", &id) == 1) QueueTo(g_outPm, kMsgPopMigrate, from, OnOffer(from, (uint16_t)id));
        else if (it[0] == 'a') OnAccept(from, it.c_str() + 2);
    }
    FlushEvents(true);
}

}  // namespace

void OnRole(const PopRoleMsg &m) {
    std::vector<uint16_t> peers = m.peers;
    bool changed = m.mode != g_mode || peers != g_peers || m.host != g_host || m.flags != g_flags;
    if (changed) {
        std::string names;
        for (uint16_t n : peers) names += (names.empty() ? "" : ", ") + remote::NameOf(n);
        LOGI("pop", "-", "%s%s%s%s", m.mode == kPopOwn ? "own NPCs and traffic" : m.mode == kPopShared ? "we share the NPCs and traffic with " : "we draw the NPCs and traffic of ",
             m.mode == kPopShared && peers.empty() ? "a camera" : names.c_str(), (m.flags & kPopRoleMission) ? " (mission entities: " : "",
             (m.flags & kPopRoleMission) ? (remote::NameOf(m.host) + ")").c_str() : "");
    }
    if (g_mode == kPopViewer && m.mode != kPopViewer) { ClearPups(); Suppress(false); }
    if (m.mode == kPopViewer && g_mode != kPopViewer) { TakeOverAll(false); g_src.clear(); g_srcById.clear(); }
    if (m.mode != kPopViewer && g_mode != kPopViewer) {
        // puppets of games that are no longer our peers become ours where they are near us (mission ones go)
        std::map<uint32_t, Pup> keep, drop;
        for (auto &kv : g_pups) {
            bool still = std::find(peers.begin(), peers.end(), OwnerOf(kv.first)) != peers.end() && m.mode == kPopShared;
            bool missionGone = (kv.second.last.flags & kPopMissionEnt) && !(m.flags & kPopRoleMission);
            if (still && !missionGone) keep.emplace(kv.first, std::move(kv.second)); else drop.emplace(kv.first, std::move(kv.second));
        }
        g_pups = std::move(keep);
        std::swap(g_pups, drop);
        TakeOverAll(true);
        std::swap(g_pups, drop);
    }
    bool newPeer = false;
    for (uint16_t n : peers) if (!IsPeer(n)) newPeer = true;
    if (newPeer) for (auto &kv : g_src) kv.second.streamed = false;   // a new peer: what is in their part goes, the rest they get
    if (m.trains != g_trainsBy) { ClearTrainPups(); LOGI("pop", "-", "trains: %s", !m.trains ? "each game its own" : m.trains == net::SelfNetId() ? "we run them for the group" : ("we draw " + remote::NameOf(m.trains) + "'s").c_str()); }
    g_mode = m.mode; g_peers = peers; g_host = m.host; g_flags = m.flags; g_viewers = m.viewers; g_trainsBy = m.mode == kPopOwn ? 0 : m.trains;
    if (g_mode != kPopShared) { g_share = 1.f; g_src.clear(); g_srcById.clear(); g_gone.clear(); ClearZones(); }
    if (changed) g_lastStat = g_lastViewStat = 0;
}

void OnRelay(const PopRelayMsg &m) {
    if (g_mode == kPopOwn || !IsPeer(m.authority)) return;
    double st = m.serverMs / 1000.0;
    g_clock.OnSnapshot(st, (double)Now() / 1000.0);
    unsigned long long now = Now();
    uint16_t self = net::SelfNetId();
    ++g_relays;
    for (const PopEntity &e : m.e) {
        if (e.kind == kPopTrain) { OnTrain(m.authority, e, st); continue; }
        uint32_t key = PupKey(m.authority, e.id);
        if (e.flags & kPopMigrated) {
            if (e.fromOwner == self && (SrcById(e.fromId) || g_pending.count(e.fromId))) { BecomePuppet(e.fromId, m.authority, e.id); ++g_handedOut; }   // ours a moment ago (before their answer)
            else if (e.fromOwner != self && !g_pups.count(key)) {   // went from one peer to another: our puppet follows it
                auto old = g_pups.find(PupKey(e.fromOwner, e.fromId));
                if (old != g_pups.end()) {
                    Pup moved = std::move(old->second);
                    g_pups.erase(old);
                    if (moved.h) g_pupOf[moved.h] = key;
                    g_pups.emplace(key, std::move(moved));
                    ++g_rekeyed;
                }
            }
        }
        if (!g_pups.count(key)) ++g_newIds;
        Pup &p = g_pups[key];
        if (p.seenAt && (p.last.model != e.model || p.last.kind != e.kind)) { DeletePup(p); p = Pup{}; }
        if (e.flags & kPopHasDesc) {
            bool newLook = p.haveDesc && p.h && (memcmp(p.desc.comp, e.comp, sizeof e.comp) != 0 || p.desc.color1 != e.color1 || p.desc.color2 != e.color2);
            p.desc = e; p.haveDesc = true;
            if (newLook) ApplyDesc(p);
        }
        p.last = e;
        p.seenAt = now;
        interp::Sample s;
        s.t = st;
        s.pos = {e.pos.x, e.pos.y, e.pos.z};
        s.vel = {e.vel.x, e.vel.y, e.vel.z};
        s.heading = e.heading;
        s.rot = {e.rot.x, e.rot.y, e.rot.z, e.rot.w};
        s.move = e.move;
        s.flags = e.flags;
        if (e.kind == kPopVehicle) {   // heading from the quaternion, for the spawn
            float yaw = std::atan2(2.f * (e.rot.w * e.rot.z + e.rot.x * e.rot.y), 1.f - 2.f * (e.rot.y * e.rot.y + e.rot.z * e.rot.z)) * 57.29578f;
            p.last.heading = yaw < 0 ? yaw + 360.f : yaw;
        }
        p.buf.Push(s);
    }
}

void OnPlayerMessage(uint16_t from, const std::string &kind, const std::string &text) {
    if (kind == kMsgPopEvent) { if (g_mode == kPopShared && IsPeer(from)) OnEvent(from, text); return; }
    if (kind == kMsgPopMigrate) { if (IsPeer(from)) OnMigrate(from, text); return; }
    if (kind == kMsgNpcDamage) {   // a peer's NPCs hurt us
        if (g_mode != kPopShared || !IsPeer(from)) return;
        int amount = 0; unsigned w = 0;
        if (sscanf(text.c_str(), "%d|%u", &amount, &w) < 1) return;
        int me = gta::PlayerPed();
        if (gta::Dead(me)) return;
        amount = std::clamp(amount, 0, 150);
        N(N_APPLY_DAMAGE_TO_PED, me, amount, 0, 0, w);
        LOGD("pop", remote::NameOf(from).c_str(), "their NPCs hit us for %d", amount);
    }
}

void Frame() {
    if (!net::Connected()) { if (g_mode != kPopOwn) Clear(); return; }
    Groups();
    if (g_mode == kPopShared) Shared();
    else if (g_mode == kPopViewer) View();
    else if (g_randomTrainsOff || !g_trainPups.empty()) TrainsFrame();   // back to our own trains
}

void Clear() {
    ClearPups();
    ClearTrainPups();
    g_trainsBy = 0;
    RandomTrains(true);
    Suppress(false);
    g_src.clear(); g_srcById.clear();
    g_mode = kPopOwn; g_host = 0; g_flags = 0; g_viewers = 0; g_peers.clear(); g_share = 1.f;
    ClearZones();
    g_out.clear(); g_outPm.clear();
    for (auto &gv : g_given) g_reserved.erase(gv.h);
    for (auto &gv : g_orphans) g_reserved.erase(gv.h);
    for (auto &gv : g_claimed) g_reserved.erase(gv.h);
    g_given.clear(); g_orphans.clear(); g_claimed.clear();
    for (auto &kv : g_pending) g_reserved.erase(kv.second.h);
    g_pending.clear();
}

int Mode() { return g_mode; }
uint16_t Authority() { return g_host; }
bool IsPuppet(int e) { return g_pupOf.count(e) != 0; }

bool StreamsFor(int remotePed) {
    if (g_mode != kPopShared || !gta::Exists(remotePed)) return false;
    uint16_t n = remote::NetIdOfPed(remotePed);
    return n && IsPeer(n) && Dist2(gta::Pos(remotePed), gta::Pos(gta::PlayerPed())) < 400.f * 400.f;
}

void ReportNpcDamage(uint16_t victim, int amount, uint32_t weapon) {
    (void)weapon;
    if (amount <= 0) return;
    g_npcDmg[victim] += amount;
}

int ClaimVehicle(uint16_t netId, uint32_t model, float x, float y, float z) {
    gta::Vec3 at{x, y, z};
    for (auto it = g_given.begin(); it != g_given.end(); ++it) {
        if (it->to != netId || it->model != model || !gta::Exists(it->h) || Dist2(gta::Pos(it->h), at) > 12.f * 12.f) continue;
        int h = it->h;
        if (it->mission) g_claimed.push_back(*it);   // still reserved: never ours to stream while it is theirs
        else g_reserved.erase(h);
        g_given.erase(it);
        LOGI("pop", remote::NameOf(netId).c_str(), "the car they took from us is now their car here (no new one spawned)");
        return h;
    }
    return 0;
}

bool KeepVehicle(int veh, uint16_t netId) {
    for (auto it = g_claimed.begin(); it != g_claimed.end(); ++it)
        if (it->h == veh && it->to == netId && gta::Exists(veh)) {
            g_orphans.push_back(*it);
            g_claimed.erase(it);
            LOGI("pop", remote::NameOf(netId).c_str(), "left the mission car: kept here until their stream has it");
            return true;
        }
    return false;
}

int GoriMode() {
    if (g_mode != kPopShared || !(g_flags & kPopRoleMission) || !g_host) return 0;
    return g_host == net::SelfNetId() ? 1 : 2;
}
int Hostiles() {
    unsigned long long now = Now();
    int n = 0;
    for (auto &kv : g_pups)
        if ((kv.second.last.flags & kPopMissionEnt) && (kv.second.last.flags & kPopHostile) && !(kv.second.last.flags & kPopDead) && now - kv.second.seenAt < 1500) ++n;
    return n;
}
int MissionKills() { return (int)g_missionKills; }
bool Shown(int entity) {
    if (g_mode != kPopShared || !gta::Exists(entity)) return false;
    auto it = g_src.find(Key(entity));
    return it != g_src.end() && it->second.streamed;
}

std::string StatusLine() {
    char b[200];
    int n = 0;
    for (auto &kv : g_pups) n += kv.second.h != 0;
    if (g_mode == kPopShared) snprintf(b, sizeof b, "shared with %d game(s): we own %d peds, %d vehicles (share %.0f%%), %d puppets of theirs", (int)g_peers.size(), g_sentPeds, g_sentVehs,
                                       g_share * 100.f, n);
    else if (g_mode == kPopViewer) snprintf(b, sizeof b, "camera: %d puppets", n);
    else snprintf(b, sizeof b, "own world");
    return b;
}

void DevCommand(const std::string &args) {
    char verb[32] = {0};
    int n = 0;
    sscanf(args.c_str(), " %31s %d", verb, &n);
    std::string v = verb;
    int me = gta::PlayerPed();
    gta::Vec3 at = gta::Pos(me);
    if (v == "status" || v.empty()) {
        std::map<uint16_t, int> peds, vehs;
        for (auto &kv : g_pups) if (kv.second.h) (kv.second.last.kind == kPopPed ? peds : vehs)[OwnerOf(kv.first)]++;
        LOGI("pop", "-", "status: %s", StatusLine().c_str());
        for (auto &kv : vehs) LOGI("pop", "-", "status: from %s %d vehicles, %d peds", remote::NameOf(kv.first).c_str(), kv.second, peds[kv.first]);
        for (auto &kv : g_trainPups) LOGI("pop", "-", "status: train %u config %u %s, speed %.1f", IdOfKey(kv.first), kv.second.last.config, kv.second.h ? "drawn" : "not drawn", kv.second.last.speed);
        for (auto &z : g_zones) LOGI("pop", "-", "status: no-spawn zone for %s at %.0f %.0f r %.0f", remote::NameOf(z.peer).c_str(), z.at.x, z.at.y, z.r);
        return;
    }
    if (v == "take") {   // walk to the nearest peer's car and get in as its driver (jacking whoever drives it)
        int best = 0; float bd = 40.f * 40.f;
        for (auto &kv : g_pups) {
            if (!kv.second.h || kv.second.last.kind != kPopVehicle || gta::Dead(kv.second.h)) continue;
            float d = Dist2(gta::Pos(kv.second.h), at);
            if (d < bd) { bd = d; best = kv.second.h; }
        }
        if (!best) { LOGW("pop", "-", "take: no peer's car within 40 m"); return; }
        N(N_TASK_ENTER_VEHICLE, me, best, 15000, -1, 2.f, 1 | 8, (const char *)nullptr);
        LOGI("pop", "-", "take: going for %s's car (%.1f m)", remote::NameOf(OwnerOf(g_pupOf[best])).c_str(), std::sqrt(bd));
        return;
    }
    if (v == "shoot") {   // a real rifle bullet of ours into the n nearest peds of the peers (not players)
        std::vector<std::pair<float, int>> c;
        for (auto &kv : g_pups)
            if (kv.second.h && kv.second.last.kind == kPopPed && !gta::Dead(kv.second.h) && !gta::InVehicle(kv.second.h)) c.push_back({Dist2(gta::Pos(kv.second.h), at), kv.second.h});
        std::sort(c.begin(), c.end());
        int k = 0;
        for (auto &x : c) {
            if (k >= (n > 0 ? n : 1) || x.first > 80.f * 80.f) break;
            gta::Vec3 t = gta::Pos(x.second);
            N(N_SHOOT_SINGLE_BULLET_BETWEEN_COORDS_IGNORE_ENTITY, t.x, t.y, t.z + 1.0f, t.x, t.y, t.z + .2f, 40, 1, gta::Joaat("WEAPON_CARBINERIFLE"), me, 0, 1, 2000.f, me, 0);
            LOGI("pop", "-", "shoot: %s's ped %u at %.1f m", remote::NameOf(OwnerOf(g_pupOf[x.second])).c_str(), IdOfKey(g_pupOf[x.second]), std::sqrt(x.first));
            ++k;
        }
        return;
    }
    LOGW("pop", "-", "pop: status | take | shoot <n>");
}

}  // namespace population
