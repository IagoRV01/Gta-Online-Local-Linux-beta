#include "game/population.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>
#include <vector>
#include "common/interp.h"
#include "common/log.h"
#include "engine/gta.h"
#include "engine/shv.h"
#include "game/net_client.h"
#include "game/remote.h"

namespace population {

using namespace proto;

namespace {

constexpr int kSendEveryMs = 125;           // the authority's stream, 8 Hz
constexpr size_t kPacketBudget = 1100;      // bytes of entities per PopState (one UDP datagram)
constexpr float kPedRange = 180.f, kVehRange = 300.f, kMissionRange = 400.f;
constexpr size_t kMaxPeds = 90, kMaxVehicles = 70;
constexpr double kDelay = 0.15;             // viewers draw this far in the past
constexpr unsigned long long kPedTimeoutMs = 1500, kVehTimeoutMs = 2500;
constexpr int kMaxSpawnsPerFrame = 6;

uint8_t g_mode = kPopOwn, g_flags = 0, g_viewers = 0;
uint16_t g_authority = 0;
unsigned long long Now() { return GetTickCount64(); }
float Dist2(const gta::Vec3 &a, const gta::Vec3 &b) { float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z; return x * x + y * y + z * z; }

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
// "lo.pe" / "lo.nd" go out as player messages, batched (at most ~7 a second each way)
std::string g_outPe; uint16_t g_outPeTo = 0; unsigned long long g_outPeAt = 0;
void SendMsg(uint16_t to, const char *kind, const std::string &text) {
    if (!net::Connected() || text.empty()) return;
    wire::Writer w;
    Put(w, PlayerMessageMsg{to, kind, text.substr(0, kMaxMsgText)});
    net::SendEvent(w);
}
void QueueEvent(const std::string &item) {
    if (g_outPe.size() + item.size() + 1 > kMaxMsgText) { SendMsg(g_outPeTo, kMsgPopEvent, g_outPe); g_outPe.clear(); }
    g_outPeTo = g_authority;
    if (!g_outPe.empty()) g_outPe += ';';
    g_outPe += item;
}
void FlushEvents() {
    if (g_outPe.empty() || Now() - g_outPeAt < 140) return;
    SendMsg(g_outPeTo, kMsgPopEvent, g_outPe);
    g_outPe.clear();
    g_outPeAt = Now();
}

// ================================================================ authority: stream our NPCs and traffic
// GTA hands out script handles for ambient entities on demand and frees them again when no script holds them, so the
// same pedestrian or car gets a new handle now and then (seen 2026-11-18: ~20 of ~100 a frame). A new handle that shows
// up where an entity of the same model just lost its own keeps that entity's stream id.
struct Src { uint16_t id = 0; uint32_t model = 0; int descLeft = 3; gta::Vec3 pos, vel; unsigned long long seen = 0; };
std::unordered_map<int, Src> g_src;          // local handle -> stream id
std::map<uint16_t, int> g_srcHandle;         // stream id -> local handle (events from viewers)
std::vector<Src> g_lost;                     // entries whose handle went away in the last second
uint16_t g_nextId = 1;
unsigned g_srcNew = 0, g_srcHit = 0, g_srcMatched = 0;
uint32_t g_seq = 0, g_frame = 0;
unsigned long long g_lastSend = 0, g_lastStat = 0;
int g_sentPeds = 0, g_sentVehs = 0;
std::map<uint16_t, unsigned long long> g_npcDmgAt;   // victim -> last lo.nd
std::map<uint16_t, int> g_npcDmg;

// before a frame: handles the pools no longer list (the entity is gone, or listed under a new handle) give their entry
// to g_lost; the old handle can still "exist" for a while after the new one shows up
void LoseGoneHandles(const std::unordered_map<int, bool> &listed) {
    unsigned long long now = Now();
    for (auto it = g_src.begin(); it != g_src.end();) {
        if (!listed.count(it->first) || !gta::Exists(it->first) || gta::ModelOf(it->first) != it->second.model) {
            g_srcHandle.erase(it->second.id);
            g_lost.push_back(it->second);
            it = g_src.erase(it);
        } else ++it;
    }
    g_lost.erase(std::remove_if(g_lost.begin(), g_lost.end(), [&](const Src &s) { return now - s.seen > 1500; }), g_lost.end());
}

uint16_t IdOf(int h) {
    unsigned long long now = Now();
    uint32_t model = gta::ModelOf(h);
    gta::Vec3 pos = gta::Pos(h);
    auto it = g_src.find(h);
    if (it != g_src.end() && it->second.model == model) {
        ++g_srcHit;
        if (it->second.seen != now) {
            NVec3 v = N<NVec3>(N_GET_ENTITY_VELOCITY, h);
            it->second.pos = pos; it->second.vel = {v.x, v.y, v.z}; it->second.seen = now;
        }
        return it->second.id;
    }
    Src s;
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < (int)g_lost.size(); ++i) {   // the same entity under a new handle?
        const Src &l = g_lost[i];
        if (l.model != model) continue;
        float dt = (float)(now - l.seen) / 1000.f;
        float ex = l.pos.x + l.vel.x * dt - pos.x, ey = l.pos.y + l.vel.y * dt - pos.y, ez = l.pos.z + l.vel.z * dt - pos.z;
        float d = ex * ex + ey * ey + ez * ez;
        if (d < bd) { bd = d; best = i; }
    }
    if (best >= 0 && bd < 3.f * 3.f) {
        s = g_lost[best];
        g_lost.erase(g_lost.begin() + best);
        ++g_srcMatched;
    } else {
        s.model = model;
        do { s.id = g_nextId++; if (!g_nextId) g_nextId = 1; } while (g_srcHandle.count(s.id));
        ++g_srcNew;
    }
    NVec3 v = N<NVec3>(N_GET_ENTITY_VELOCITY, h);
    s.pos = pos; s.vel = {v.x, v.y, v.z}; s.seen = now;
    g_src[h] = s;
    g_srcHandle[s.id] = h;
    return s.id;
}

Move PedMove(int ped) {
    if (NB(N<int>(N_IS_PED_SPRINTING, ped))) return Move::Sprint;
    if (NB(N<int>(N_IS_PED_RUNNING, ped))) return Move::Run;
    if (NB(N<int>(N_IS_PED_WALKING, ped))) return Move::Walk;
    return Move::Still;
}

void FillDesc(int h, PopEntity &e) {
    Src &s = g_src[h];
    bool want = s.descLeft > 0 || (g_frame + e.id) % 16 == 0;   // the first frames, then every 2 s
    if (!want) return;
    if (s.descLeft > 0) --s.descLeft;
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

void Stream() {
    unsigned long long now = Now();
    if (now - g_lastSend < (unsigned long long)kSendEveryMs) return;
    g_lastSend = now;
    ++g_frame;
    int nv = AllVehicles(), np = AllPeds();
    {
        std::unordered_map<int, bool> listed;
        for (int i = 0; i < nv; ++i) listed[g_vehs[i]] = true;
        for (int i = 0; i < np; ++i) listed[g_peds[i]] = true;
        LoseGoneHandles(listed);
    }
    int me = gta::PlayerPed();
    gta::Vec3 at = gta::Pos(me);
    int myVeh = gta::InVehicle(me) ? N<int>(N_GET_VEHICLE_PED_IS_IN, me, 0) : 0;
    int myLast = N<int>(N_GET_PLAYERS_LAST_VEHICLE);   // the viewers already draw it as our car (remote.cpp)
    bool mission = (g_flags & kPopRoleMission) != 0;
    auto wanted = [&](int e) {   // ambient, or (a mission's host) a mission entity that is not one of LocalOnline's own
        if (remote::IsPuppetEntity(e) || !NB(N<int>(N_IS_ENTITY_VISIBLE, e))) return false;   // hidden: GodEye's drivers, our own tricks
        return !Mission(e) || mission;
    };
    struct Cand { int h; float d; };
    std::vector<Cand> vehs, peds;
    for (int i = 0; i < nv; ++i) {
        int v = g_vehs[i];
        if (!gta::Exists(v) || v == myVeh || v == myLast || !wanted(v) || Train(v)) continue;
        if (!Mission(v) && PoliceVehicle(v)) continue;   // police stay each game's own
        int drv = N<int>(N_GET_PED_IN_VEHICLE_SEAT, v, -1, 0);
        if (gta::Exists(drv) && (NB(N<int>(N_IS_PED_A_PLAYER, drv)) || remote::IsPuppetEntity(drv))) continue;
        float d = Dist2(gta::Pos(v), at), r = Mission(v) ? kMissionRange : kVehRange;
        if (d < r * r) { vehs.push_back({v, d}); IdOf(v); }   // ids for all in range, streamed or not: new handles keep them
    }
    std::sort(vehs.begin(), vehs.end(), [](const Cand &a, const Cand &b) { return a.d < b.d; });
    if (vehs.size() > kMaxVehicles) vehs.resize(kMaxVehicles);
    std::unordered_map<int, bool> vehSet;
    for (auto &c : vehs) vehSet[c.h] = true;
    for (int i = 0; i < np; ++i) {
        int p = g_peds[i];
        if (!gta::Exists(p) || p == me || NB(N<int>(N_IS_PED_A_PLAYER, p)) || !wanted(p)) continue;
        if (!Mission(p) && PolicePed(p)) continue;
        if (gta::InVehicle(p)) {
            int v = N<int>(N_GET_VEHICLE_PED_IS_IN, p, 0);
            if (!vehSet.count(v)) continue;   // only with its vehicle
            peds.push_back({p, 0.f});
            continue;
        }
        float d = Dist2(gta::Pos(p), at), r = Mission(p) ? kMissionRange : kPedRange;
        if (d < r * r) { peds.push_back({p, d}); IdOf(p); }
    }
    std::sort(peds.begin(), peds.end(), [](const Cand &a, const Cand &b) { return a.d < b.d; });
    if (peds.size() > kMaxPeds) peds.resize(kMaxPeds);

    std::vector<PopEntity> out;
    out.reserve(vehs.size() + peds.size());
    std::unordered_map<int, bool> keep;
    for (auto &c : vehs) {
        int v = c.h;
        PopEntity e;
        e.kind = kPopVehicle;
        e.id = IdOf(v);
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
        FillDesc(v, e);
        out.push_back(e);
        keep[v] = true;
    }
    for (auto &c : peds) {
        int h = c.h;
        PopEntity e;
        e.kind = kPopPed;
        e.id = IdOf(h);
        e.model = gta::ModelOf(h);
        gta::Vec3 p = gta::Pos(h);
        e.pos = {p.x, p.y, p.z};
        NVec3 vel = N<NVec3>(N_GET_ENTITY_VELOCITY, h);
        e.vel = {vel.x, vel.y, vel.z};
        e.heading = gta::Heading(h);
        e.move = (uint8_t)PedMove(h);
        e.health = (uint8_t)std::clamp(gta::Health(h), 0, 255);
        if (gta::Dead(h)) e.flags |= kPopDead;
        else {
            if (NB(N<int>(N_IS_PED_RAGDOLL, h))) e.flags |= kPopRagdoll;
            if (NB(N<int>(N_IS_PED_USING_ANY_SCENARIO, h))) e.flags |= kPopScenario;
        }
        if (gta::InVehicle(h)) {
            int v = N<int>(N_GET_VEHICLE_PED_IS_IN, h, 0);
            e.flags |= kPopInVehicle;
            e.vehId = IdOf(v);
            e.seat = -1;
            int seats = N<int>(N_GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS, v);
            for (int s = -1; s < seats && s < 15; ++s)
                if (N<int>(N_GET_PED_IN_VEHICLE_SEAT, v, s, 0) == h) { e.seat = (int8_t)s; break; }
        }
        uint32_t w = N<uint32_t>(N_GET_SELECTED_PED_WEAPON, h);
        if (w && w != gta::Joaat("WEAPON_UNARMED")) { e.flags |= kPopArmed; e.weapon = w; }
        if (Mission(h)) e.flags |= kPopMissionEnt;
        int rel = N<int>(N_GET_RELATIONSHIP_BETWEEN_PEDS, h, me);   // 4 dislike, 5 hate (255: none)
        if ((rel == 4 || rel == 5) && !gta::Dead(h)) e.flags |= kPopHostile;
        if ((e.flags & kPopArmed) && NB(N<int>(N_IS_PED_SHOOTING, h))) {
            int t = N<int>(N_GET_PED_TARGET_FROM_COMBAT_PED, h, 0);
            NVec3 hit{};
            if (gta::Exists(t)) { gta::Vec3 tp = gta::Pos(t); e.aim = {tp.x, tp.y, tp.z + 0.3f}; e.flags |= kPopShooting; }
            else if (NB(N<int>(N_GET_PED_LAST_WEAPON_IMPACT_COORD, h, &hit))) { e.aim = {hit.x, hit.y, hit.z}; e.flags |= kPopShooting; }
        }
        FillDesc(h, e);
        out.push_back(e);
        keep[h] = true;
    }
    g_sentPeds = (int)peds.size(); g_sentVehs = (int)vehs.size();
    // vehicles first: a viewer seats the peds in them
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
    if (now - g_lastStat > 30000) {
        g_lastStat = now;
        LOGI("pop", "-", "authority for %u game(s)%s: streaming %d peds, %d vehicles (%u new ids in 30 s, %d tracked)", g_viewers, mission ? " (mission)" : "", g_sentPeds, g_sentVehs,
             g_srcNew, (int)g_src.size());
        LOGI("pop", "-", "ids: %u found again, %u kept across a new handle", g_srcHit, g_srcMatched);
        g_srcNew = g_srcHit = g_srcMatched = 0;
    }
}

int HandleOf(uint16_t id) {
    auto it = g_srcHandle.find(id);
    return it != g_srcHandle.end() && gta::Exists(it->second) ? it->second : 0;
}

void OnViewerEvent(uint16_t from, const std::string &text) {   // "d|id|amount|weapon;k|id|weapon;v|id|body|engine|expl;c|id"
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find(';', i);
        std::string it = text.substr(i, j == std::string::npos ? std::string::npos : j - i);
        i = j == std::string::npos ? text.size() : j + 1;
        char k = 0; unsigned id = 0; int a = 0, b = 0, c = 0; unsigned w = 0;
        if (it.size() < 3 || it[1] != '|') continue;
        k = it[0];
        const char *rest = it.c_str() + 2;
        if (k == 'd' && sscanf(rest, "%u|%d|%u", &id, &a, &w) >= 2) {
            int h = HandleOf((uint16_t)id);
            if (!h || gta::Dead(h)) continue;
            a = std::clamp(a, 0, 1000);
            N(N_APPLY_DAMAGE_TO_PED, h, a, 0, 0, w);
            LOGD("pop", remote::NameOf(from).c_str(), "hit ped %u for %d", id, a);
        } else if (k == 'k' && sscanf(rest, "%u|%u", &id, &w) >= 1) {
            int h = HandleOf((uint16_t)id);
            if (!h || gta::Dead(h)) continue;
            N(N_SET_ENTITY_HEALTH, h, 0, 0, w);
            LOGI("pop", remote::NameOf(from).c_str(), "killed ped %u", id);
        } else if (k == 'v' && sscanf(rest, "%u|%d|%d|%d", &id, &a, &b, &c) >= 3) {
            int h = HandleOf((uint16_t)id);
            if (!h || gta::Dead(h)) continue;
            a = std::clamp(a, 0, 2000); b = std::clamp(b, 0, 2000);
            float body = N<float>(N_GET_VEHICLE_BODY_HEALTH, h) - (float)a, eng = N<float>(N_GET_VEHICLE_ENGINE_HEALTH, h) - (float)b;
            N(N_SET_VEHICLE_BODY_HEALTH, h, body < 0.f ? 0.f : body);
            N(N_SET_VEHICLE_ENGINE_HEALTH, h, eng < -3900.f ? -3900.f : eng);
            if (c && (a >= 800 || eng < -1000.f)) N(N_EXPLODE_VEHICLE, h, 1, 0);
            LOGI("pop", remote::NameOf(from).c_str(), "damaged vehicle %u: body -%d engine -%d%s", id, a, b, c ? " (explosion)" : "");
        } else if (k == 'c' && sscanf(rest, "%u", &id) == 1) {   // the viewer takes this car: its people out, the car gone
            int h = HandleOf((uint16_t)id);
            if (!h || Mission(h)) continue;
            int seats = N<int>(N_GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS, h);
            for (int s = -1; s < seats && s < 15; ++s) {
                int p = N<int>(N_GET_PED_IN_VEHICLE_SEAT, h, s, 0);
                if (gta::Exists(p) && !NB(N<int>(N_IS_PED_A_PLAYER, p))) N(N_CLEAR_PED_TASKS_IMMEDIATELY, p);
            }
            g_srcHandle.erase((uint16_t)id);
            g_src.erase(h);
            Delete(h, false);
            LOGI("pop", remote::NameOf(from).c_str(), "took vehicle %u: removed here, it is theirs now", id);
        }
    }
}

// ================================================================ viewer: puppets of the authority's world
struct Pup {
    PopEntity last;
    bool haveDesc = false;
    PopEntity desc;
    interp::Buffer buf;
    int h = 0, blip = 0;
    unsigned long long seenAt = 0, holdUntil = 0;
    int lastTaskMs = 0;
    uint8_t lastMove = 255;
    bool scenario = false, deadShown = false, inVeh = false;
    int hpSet = 0;
    float body = 1000.f, engine = 1000.f;
    uint32_t weapon = 0;
    int lastShotMs = 0;
    uint8_t vflags = 0xFF;
    int lastHornMs = 0;
    bool requested = false;
};
std::map<uint16_t, Pup> g_pups;
unsigned g_newIds = 0, g_relays = 0, g_adopted = 0;
std::unordered_map<int, uint16_t> g_pupOf;   // handle -> id
interp::Clock g_clock;
uint16_t g_pupsOf = 0;                         // the authority these puppets belong to
unsigned long long g_lastClean = 0, g_lastViewStat = 0;
bool g_suppressed = false;
int g_removed = 0;

void DeletePup(Pup &p) {
    if (p.blip) { N(N_REMOVE_BLIP, &p.blip); p.blip = 0; }
    if (p.h) { g_pupOf.erase(p.h); Delete(p.h, p.last.kind == kPopPed); }
}
void ClearPups() {
    for (auto &kv : g_pups) DeletePup(kv.second);
    g_pups.clear();
    g_pupOf.clear();
}

void Suppress(bool on) {
    if (on == g_suppressed) return;
    g_suppressed = on;
    N(N_SET_PED_POPULATION_BUDGET, on ? 0 : 3);
    N(N_SET_VEHICLE_POPULATION_BUDGET, on ? 0 : 3);
    N(N_SET_RANDOM_BOATS, on ? 0 : 1);
    N(N_SET_GARBAGE_TRUCKS, on ? 0 : 1);
    LOGI("pop", "-", on ? "our own NPCs and traffic are off" : "our own NPCs and traffic are back");
}

// our own ambient population: gone while we draw the authority's (police, the player's cars and trains stay)
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
        if (!gta::Exists(p) || p == me || Mission(p) || NB(N<int>(N_IS_PED_A_PLAYER, p)) || PolicePed(p) || g_pupOf.count(p)) continue;
        if (gta::InVehicle(p)) { int v = N<int>(N_GET_VEHICLE_PED_IS_IN, p, 0); if (v == myVeh || PoliceVehicle(v) || Train(v)) continue; }
        Delete(p, true);
        ++g_removed;
    }
    int nv = AllVehicles();
    for (int i = 0; i < nv; ++i) {
        int v = g_vehs[i];
        if (!gta::Exists(v) || Mission(v) || v == myVeh || v == entering || v == last || g_pupOf.count(v) || Train(v) || PoliceVehicle(v)) continue;
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

Pup *PupById(uint16_t id) { auto it = g_pups.find(id); return it == g_pups.end() ? nullptr : &it->second; }

bool Spawn(uint16_t id, Pup &p, const interp::Sample &s) {
    uint32_t m = p.last.model;
    if (!NB(N<int>(N_IS_MODEL_IN_CDIMAGE, m)) || !NB(N<int>(N_IS_MODEL_VALID, m))) return false;
    N(N_REQUEST_MODEL, m);
    if (!NB(N<int>(N_HAS_MODEL_LOADED, m))) { p.requested = true; return false; }
    if (p.last.kind == kPopVehicle) {
        p.h = N<int>(N_CREATE_VEHICLE, m, s.pos.x, s.pos.y, s.pos.z, p.last.heading, 0, 1, 0);
        if (!p.h) return false;
        gta::MakeMission(p.h);
        N(N_SET_ENTITY_QUATERNION, p.h, s.rot.x, s.rot.y, s.rot.z, s.rot.w);
        N(N_SET_VEHICLE_DOORS_LOCKED, p.h, (p.last.flags & kPopMissionEnt) ? 2 : 1);
        N(N_SET_ENTITY_PROOFS, p.h, 0, 0, 0, 1, 0, 0, 0, 0);   // crashes: the authority's game does them
        N(N_SET_DISABLE_VEHICLE_PETROL_TANK_FIRES, p.h, 1);
        N(N_SET_VEHICLE_EXPLODES_ON_HIGH_EXPLOSION_DAMAGE, p.h, 0);
        p.body = N<float>(N_GET_VEHICLE_BODY_HEALTH, p.h);
        p.engine = N<float>(N_GET_VEHICLE_ENGINE_HEALTH, p.h);
    } else {
        Pup *veh = (p.last.flags & kPopInVehicle) ? PupById(p.last.vehId) : nullptr;
        if ((p.last.flags & kPopInVehicle) && (!veh || !veh->h)) return false;   // its car first
        p.h = N<int>(N_CREATE_PED, 4, m, s.pos.x, s.pos.y, s.pos.z - 1.f, s.heading, 0, 1);
        if (!p.h) return false;
        gta::MakeMission(p.h);
        N(N_SET_BLOCKING_OF_NON_TEMPORARY_EVENTS, p.h, 1);
        N(N_SET_PED_FLEE_ATTRIBUTES, p.h, 0, 0);
        N(N_SET_PED_KEEP_TASK, p.h, 1);
        N(N_SET_PED_CAN_BE_TARGETTED, p.h, 1);
        N(N_SET_PED_DROPS_WEAPONS_WHEN_DEAD, p.h, 0);
        int hp = p.last.health > 100 ? p.last.health : 200;
        N(N_SET_ENTITY_MAX_HEALTH, p.h, hp > 200 ? hp : 200);
        N(N_SET_ENTITY_HEALTH, p.h, hp, 0, 0);
        p.hpSet = hp;
        if (veh && veh->h) { N(N_SET_PED_INTO_VEHICLE, p.h, veh->h, (int)p.last.seat); p.inVeh = true; }
        if (p.last.flags & kPopHostile) {
            p.blip = N<int>(N_ADD_BLIP_FOR_ENTITY, p.h);
            N(N_SET_BLIP_COLOUR, p.blip, 1);
            N(N_SET_BLIP_SCALE, p.blip, 0.7f);
        }
    }
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
    if (p.haveDesc) ApplyDesc(p);
    g_pupOf[p.h] = id;
    return true;
}

// A new id where a puppet of the same model stopped being updated a moment ago is most likely the same entity under a
// new id (the authority lost track of it): the puppet moves over instead of a second one popping in.
bool Adopt(uint16_t id, Pup &p, const interp::Sample &s) {
    unsigned long long now = Now();
    for (auto &kv : g_pups) {
        Pup &o = kv.second;
        if (kv.first == id || !o.h || o.last.model != p.last.model || o.last.kind != p.last.kind || now - o.seenAt < 300) continue;
        gta::Vec3 at = gta::Pos(o.h);
        float dx = at.x - s.pos.x, dy = at.y - s.pos.y, dz = at.z - s.pos.z;
        if (dx * dx + dy * dy + dz * dz > 4.f * 4.f) continue;
        p.h = o.h; p.blip = o.blip; p.hpSet = o.hpSet; p.body = o.body; p.engine = o.engine; p.weapon = o.weapon; p.vflags = o.vflags; p.inVeh = o.inVeh;
        o.h = 0; o.blip = 0;
        g_pupOf[p.h] = id;
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

void ReportPed(uint16_t id, Pup &p) {
    int hp = gta::Health(p.h);
    if (hp >= p.hpSet) return;
    bool mine = NB(N<int>(N_HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY, p.h, g_me, 1)) || (g_myVeh && NB(N<int>(N_HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY, p.h, g_myVeh, 1)));
    if (mine) {
        uint32_t w = N<uint32_t>(N_GET_SELECTED_PED_WEAPON, g_me);
        char b[64];
        if (hp <= 0 || gta::Dead(p.h)) snprintf(b, sizeof b, "k|%u|%u", id, w);
        else snprintf(b, sizeof b, "d|%u|%d|%u", id, p.hpSet - hp, w);
        QueueEvent(b);
    }
    N(N_CLEAR_ENTITY_LAST_DAMAGE_ENTITY, p.h);
    p.hpSet = hp;
}

void ReportVehicle(uint16_t id, Pup &p) {
    float body = N<float>(N_GET_VEHICLE_BODY_HEALTH, p.h), eng = N<float>(N_GET_VEHICLE_ENGINE_HEALTH, p.h);
    bool dead = gta::Dead(p.h);
    if (body >= p.body - 1.f && eng >= p.engine - 1.f && !(dead && !(p.last.flags & kPopDead))) return;
    bool mine = NB(N<int>(N_HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY, p.h, g_me, 1)) || (g_myVeh && NB(N<int>(N_HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY, p.h, g_myVeh, 1)));
    if (mine) {
        char b[64];
        snprintf(b, sizeof b, "v|%u|%d|%d|%d", id, (int)std::max(0.f, p.body - body), (int)std::max(0.f, p.engine - eng), dead ? 1 : 0);
        QueueEvent(b);
    }
    N(N_CLEAR_ENTITY_LAST_DAMAGE_ENTITY, p.h);
    p.body = body; p.engine = eng;
}

void DriveVehicle(Pup &p, const interp::Result &r) {
    const interp::Sample &s = r.s;
    const PopEntity &e = p.last;
    if ((e.flags & kPopDead) && !gta::Dead(p.h)) {
        gta::Vec3 at = gta::Pos(p.h);
        N(N_ADD_EXPLOSION, at.x, at.y, at.z, 7, 1.f, 1, 0, 1.f, 1);   // the blast hurt nobody here: the authority's did
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
    // damage the authority's game did (crashes, other players): never better than ours
    float body = e.body * 4.f, eng = e.engine * 4.f;
    if (body < p.body - 20.f) { N(N_SET_VEHICLE_BODY_HEALTH, p.h, body); p.body = body; }
    if (eng < p.engine - 20.f) { N(N_SET_VEHICLE_ENGINE_HEALTH, p.h, eng); p.engine = eng; }
}

void DrivePed(Pup &p, const interp::Result &r) {
    const interp::Sample &s = r.s;
    const PopEntity &e = p.last;
    int now = gta::GameTimeMs();
    if (e.flags & kPopDead) {
        if (!gta::Dead(p.h)) { N(N_SET_ENTITY_HEALTH, p.h, 0, 0, 0); p.hpSet = 0; }
        return;
    }
    if (gta::Dead(p.h)) return;   // we killed it: the authority's comes next
    if (e.health + 5 < p.hpSet && e.health > 100) { N(N_SET_ENTITY_HEALTH, p.h, (int)e.health, 0, 0); p.hpSet = e.health; }
    if (e.weapon != p.weapon) {
        uint32_t unarmed = gta::Joaat("WEAPON_UNARMED");
        if (e.weapon) { N(N_GIVE_WEAPON_TO_PED, p.h, e.weapon, 999, 0, 1); N(N_SET_PED_INFINITE_AMMO_CLIP, p.h, 1); }
        N(N_SET_CURRENT_PED_WEAPON, p.h, e.weapon ? e.weapon : unarmed, 1);
        p.weapon = e.weapon;
    }
    Pup *veh = (e.flags & kPopInVehicle) ? PupById(e.vehId) : nullptr;
    if (veh && veh->h) {   // seated: the car moves it
        if (!NB(N<int>(N_IS_PED_IN_VEHICLE, p.h, veh->h, 0)) && Now() >= p.holdUntil) N(N_SET_PED_INTO_VEHICLE, p.h, veh->h, (int)e.seat);
        p.inVeh = true;
        return;
    }
    if (p.inVeh) {   // got out at the authority
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
        return;
    }
    if (e.flags & kPopShooting) {   // aims where the authority's NPC shoots; tracers without damage (the authority's game deals it)
        if (now - p.lastTaskMs >= 400) { N(N_TASK_AIM_GUN_AT_COORD, p.h, e.aim.x, e.aim.y, e.aim.z, 1000, 1, 0); p.lastTaskMs = now; p.lastMove = 254; }
        if (now - p.lastShotMs >= 180 && e.weapon) {
            p.lastShotMs = now;
            int wobj = N<int>(N_GET_CURRENT_PED_WEAPON_ENTITY_INDEX, p.h, 0);
            gta::Vec3 o = gta::Exists(wobj) ? gta::Pos(wobj) : gta::Vec3{cur.x, cur.y, cur.z + 0.5f};
            if (!NB(N<int>(N_HAS_WEAPON_ASSET_LOADED, e.weapon))) N(N_REQUEST_WEAPON_ASSET, e.weapon, 31, 0);
            N(N_SHOOT_SINGLE_BULLET_BETWEEN_COORDS_IGNORE_ENTITY, o.x, o.y, o.z, e.aim.x, e.aim.y, e.aim.z, 0, 1, e.weapon, p.h, 1, 0, -1.f, p.h, 0);
        }
        return;
    }
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

// the player gets into one of the authority's cars: it becomes ours (the authority removes its own)
void TakeCars() {
    int v = N<int>(N_GET_VEHICLE_PED_IS_TRYING_TO_ENTER, g_me);
    if (!gta::Exists(v)) v = g_myVeh;
    if (!v) return;
    auto it = g_pupOf.find(v);
    if (it == g_pupOf.end()) return;
    uint16_t id = it->second;
    Pup *p = PupById(id);
    if (!p || p->last.kind != kPopVehicle || (p->last.flags & kPopMissionEnt)) return;
    char b[24];
    snprintf(b, sizeof b, "c|%u", id);
    QueueEvent(b);
    g_outPeAt = 0;   // at once
    FlushEvents();
    for (auto &kv : g_pups)   // its people: hands off while the game pulls them out
        if (kv.second.last.kind == kPopPed && (kv.second.last.flags & kPopInVehicle) && kv.second.last.vehId == id) kv.second.holdUntil = Now() + 6000;
    g_pupOf.erase(v);
    N(N_SET_ENTITY_PROOFS, v, 0, 0, 0, 0, 0, 0, 0, 0);
    N(N_SET_VEHICLE_EXPLODES_ON_HIGH_EXPLOSION_DAMAGE, v, 1);
    N(N_SET_DISABLE_VEHICLE_PETROL_TANK_FIRES, v, 0);
    N(N_SET_ENTITY_AS_NO_LONGER_NEEDED, &v);
    p->h = 0;
    g_pups.erase(id);
    LOGI("pop", "-", "we took the authority's vehicle %u", id);
}

void View() {
    Suppress(true);
    N(N_SET_PED_DENSITY_MULTIPLIER_THIS_FRAME, 0.f);
    N(N_SET_SCENARIO_PED_DENSITY_MULTIPLIER_THIS_FRAME, 0.f, 0.f);
    N(N_SET_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME, 0.f);
    N(N_SET_RANDOM_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME, 0.f);
    N(N_SET_PARKED_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME, 0.f);
    g_me = gta::PlayerPed();
    g_myVeh = gta::InVehicle(g_me) ? N<int>(N_GET_VEHICLE_PED_IS_IN, g_me, 0) : 0;
    CleanOwn();
    TakeCars();
    if (!g_clock.Synced()) return;
    unsigned long long now = Now();
    double renderT = g_clock.ServerNow((double)now / 1000.0) - kDelay;
    int spawns = 0;
    // vehicles before peds (seats)
    for (int pass = 0; pass < 2; ++pass)
        for (auto it = g_pups.begin(); it != g_pups.end();) {
            uint16_t id = it->first;
            Pup &p = it->second;
            bool veh = p.last.kind == kPopVehicle;
            if (veh != (pass == 0)) { ++it; continue; }
            if (now - p.seenAt > (veh ? kVehTimeoutMs : kPedTimeoutMs)) { DeletePup(p); it = g_pups.erase(it); continue; }
            if (p.h && !gta::Exists(p.h)) { g_pupOf.erase(p.h); p.h = 0; }   // the game removed it
            interp::Result r = p.buf.At(renderT, 0.4);
            if (!r.valid) { ++it; continue; }
            if (!p.h && Adopt(id, p, r.s)) { ++it; continue; }
            if (!p.h) {
                if (p.haveDesc && spawns < kMaxSpawnsPerFrame && Spawn(id, p, r.s)) ++spawns;
                ++it;
                continue;
            }
            if (veh) { ReportVehicle(id, p); DriveVehicle(p, r); }
            else { ReportPed(id, p); DrivePed(p, r); }
            ++it;
        }
    FlushEvents();
    if (now - g_lastViewStat > 30000) {
        g_lastViewStat = now;
        int peds = 0, vehs = 0, fresh = 0, inVeh = 0, dead = 0, mis = 0, hos = 0;
        for (auto &kv : g_pups) {
            if (kv.second.h) (kv.second.last.kind == kPopPed ? peds : vehs)++;
            if (now - kv.second.seenAt < 500) ++fresh;
            if (kv.second.last.flags & kPopInVehicle) ++inVeh;
            if (kv.second.last.flags & kPopDead) ++dead;
            if ((kv.second.last.flags & kPopMissionEnt) && kv.second.last.kind == kPopPed && now - kv.second.seenAt < 500) {
                ++mis;
                if (kv.second.last.flags & kPopHostile) ++hos;
            }
        }
        LOGI("pop", "-", "viewer: %d mission peds (%d hostile)", mis, hos);
        LOGI("pop", "-", "viewer: %d fresh (<0.5 s), %d in vehicles, %d dead; %u new ids (%u took over a puppet) and %u relay packets in the last 30 s", fresh, inVeh, dead, g_newIds, g_adopted, g_relays);
        g_newIds = g_relays = g_adopted = 0;
        LOGI("pop", "-", "viewer of %s: %d peds, %d vehicles drawn (%d known), %d of our own removed", remote::NameOf(g_authority).c_str(), peds, vehs, (int)g_pups.size(), g_removed);
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
}

}  // namespace

void OnRole(const PopRoleMsg &m) {
    bool changed = m.mode != g_mode || m.authority != g_authority;
    if (changed) LOGI("pop", "-", "%s%s%s", m.mode == kPopOwn ? "own NPCs and traffic" : m.mode == kPopAuthority ? "we run the NPCs and traffic for " : "we see the NPCs and traffic of ",
                      m.mode == kPopAuthority ? (std::to_string(m.viewers) + " game(s)").c_str() : m.mode == kPopViewer ? remote::NameOf(m.authority).c_str() : "",
                      (m.flags & kPopRoleMission) ? " (mission)" : "");
    if (changed && g_mode == kPopViewer) { ClearPups(); }
    if (m.mode != kPopViewer) Suppress(false);
    if (m.mode != kPopAuthority) { g_src.clear(); g_srcHandle.clear(); g_lost.clear(); }
    g_mode = m.mode; g_authority = m.authority; g_flags = m.flags; g_viewers = m.viewers;
    if (changed) g_lastStat = g_lastViewStat = 0;
}

void OnRelay(const PopRelayMsg &m) {
    if (g_mode != kPopViewer || m.authority != g_authority) return;
    if (g_pupsOf != m.authority) { ClearPups(); g_pupsOf = m.authority; }
    double st = m.serverMs / 1000.0;
    g_clock.OnSnapshot(st, (double)Now() / 1000.0);
    unsigned long long now = Now();
    ++g_relays;
    for (const PopEntity &e : m.e) {
        if (!g_pups.count(e.id)) ++g_newIds;
        Pup &p = g_pups[e.id];
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
    if (kind == kMsgPopEvent) { if (g_mode == kPopAuthority) OnViewerEvent(from, text); return; }
    if (kind == kMsgNpcDamage) {   // the authority's NPCs hurt us
        if (g_mode != kPopViewer || from != g_authority) return;
        int amount = 0; unsigned w = 0;
        if (sscanf(text.c_str(), "%d|%u", &amount, &w) < 1) return;
        int me = gta::PlayerPed();
        if (gta::Dead(me)) return;
        amount = std::clamp(amount, 0, 150);
        N(N_APPLY_DAMAGE_TO_PED, me, amount, 0, 0, w);
        LOGD("pop", "-", "the authority's NPCs hit us for %d", amount);
    }
}

void Frame() {
    if (!net::Connected()) { if (g_mode != kPopOwn) Clear(); return; }
    Groups();
    if (g_mode == kPopAuthority) {
        Stream();
        unsigned long long now = Now();   // NPC damage to the viewers' puppets, at most ~7 a second each
        for (auto &kv : g_npcDmg) {
            if (kv.second <= 0 || now - g_npcDmgAt[kv.first] < 140) continue;
            SendMsg(kv.first, kMsgNpcDamage, std::to_string(kv.second) + "|0");
            g_npcDmgAt[kv.first] = now;
            kv.second = 0;
        }
    } else if (g_mode == kPopViewer) View();
}

void Clear() {
    ClearPups();
    Suppress(false);
    g_src.clear(); g_srcHandle.clear(); g_lost.clear();
    g_mode = kPopOwn; g_authority = 0; g_flags = 0; g_viewers = 0; g_pupsOf = 0;
    g_outPe.clear();
}

int Mode() { return g_mode; }
uint16_t Authority() { return g_authority; }
bool IsPuppet(int e) { return g_pupOf.count(e) != 0; }

bool StreamsFor(int remotePed) {
    if (g_mode != kPopAuthority || !gta::Exists(remotePed)) return false;
    return Dist2(gta::Pos(remotePed), gta::Pos(gta::PlayerPed())) < 400.f * 400.f;
}

void ReportNpcDamage(uint16_t victim, int amount, uint32_t weapon) {
    if (amount <= 0) return;
    g_npcDmg[victim] += amount;
}

std::string StatusLine() {
    char b[160];
    if (g_mode == kPopAuthority) snprintf(b, sizeof b, "authority for %u game(s): %d peds, %d vehicles", g_viewers, g_sentPeds, g_sentVehs);
    else if (g_mode == kPopViewer) {
        int n = 0;
        for (auto &kv : g_pups) n += kv.second.h != 0;
        snprintf(b, sizeof b, "viewer of %s: %d puppets", remote::NameOf(g_authority).c_str(), n);
    } else snprintf(b, sizeof b, "own world");
    return b;
}

}  // namespace population
