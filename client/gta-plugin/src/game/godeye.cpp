#include "game/godeye.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "common/log.h"
#include "engine/gta.h"
#include "engine/shv.h"
#include "game/net_client.h"

namespace godeye {

using namespace proto;
using gta::Vec3;

namespace {

std::deque<GodEyeActionMsg> g_queue;
int g_flashUntil = 0;             // the wanted stars flash for a police alert

// ---- police units (alert, roadblock): ours until they are released to the game
struct Unit {
    int kind = 0;                 // kGePoliceAlert | kGeRoadblock
    std::vector<int> vehs, peds, props;
    int blip = 0;
    int startMs = 0, untilMs = 0, retaskMs = 0;
    bool arrived = false;
    Vec3 at, across;              // roadblock: the node and the road's sideways axis (spikes)
};
std::vector<Unit> g_units;

// ---- traffic
uint8_t g_traffic = kTrafficNormal;
int g_trafficUntil = 0;           // game ms, 0 = until changed
int g_nextLights = 0, g_nextDrivers = 0;
std::map<int, std::pair<int, int>> g_lights;   // light object -> (state shown, next change in random mode)
std::set<int> g_restyled;                      // drivers whose driving style we changed
std::vector<Vec3> g_lightPos;                  // the lights round the player, for the red-light brakes
int g_brakes = 0, g_nextReport = 0;            // red-light stops since the last log line

constexpr int kStyleNormal = 786603;           // GTA's default ambient driving style
constexpr int kStopAtLights = 128;
constexpr float kTrafficRadius = 160.f;

int (*g_allObjects)(int *, int) = nullptr;
int (*g_allVehicles)(int *, int) = nullptr;

void BindWorld() {
    static bool done = false;
    if (done) return;
    done = true;
    HMODULE h = GetModuleHandleA("ScriptHookV.dll");
    if (!h) return;
    g_allObjects = reinterpret_cast<int (*)(int *, int)>(GetProcAddress(h, "?worldGetAllObjects@@YAHPEAHH@Z"));
    g_allVehicles = reinterpret_cast<int (*)(int *, int)>(GetProcAddress(h, "?worldGetAllVehicles@@YAHPEAHH@Z"));
    if (!g_allObjects || !g_allVehicles) LOGW("godeye", "-", "ScriptHookV has no world pools: traffic hacks off");
}

float Dist2(const Vec3 &a, const Vec3 &b) { float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z; return dx * dx + dy * dy + dz * dz; }
Vec3 Forward(float headingDeg) { float h = headingDeg * 3.14159265f / 180.f; return {-std::sin(h), std::cos(h), 0}; }

bool IsTrafficLight(uint32_t model) {
    static std::set<uint32_t> lights;
    if (lights.empty())
        for (const char *n : {"prop_traffic_01a", "prop_traffic_01b", "prop_traffic_01d", "prop_traffic_02a", "prop_traffic_02b",
                              "prop_traffic_03a", "prop_traffic_03b", "prop_traffic_lightset_01"})
            lights.insert(gta::Joaat(n));
    return lights.count(model) != 0;
}

// a road node `minD`..`maxD` metres from p (the n-th closest nodes, walking outwards)
bool RoadNodeAround(const Vec3 &p, float minD, float maxD, Vec3 &out, float &heading) {
    for (int n = 10; n < 160; n += 6) {
        NVec3 v{};
        float h = 0;
        int lanes = 0;
        if (!NB(N<int>(N_GET_NTH_CLOSEST_VEHICLE_NODE_WITH_HEADING, p.x, p.y, p.z, n, &v, &h, &lanes, 1, 3.f, 0.f))) continue;
        Vec3 c{v.x, v.y, v.z};
        float d = std::sqrt(Dist2(c, p));
        if (d >= minD && d <= maxD) { out = c; heading = h; return true; }
    }
    return false;
}

int SpawnCar(uint32_t model, const Vec3 &p, float heading) {
    int v = N<int>(N_CREATE_VEHICLE, model, p.x, p.y, p.z + 0.5f, heading, 0, 1, 0);
    if (!v) return 0;
    N(N_SET_VEHICLE_ON_GROUND_PROPERLY, v, 5.f);
    gta::MakeMission(v);
    return v;
}

int SpawnCop(uint32_t model, int veh, int seat, const Vec3 &p, float heading) {
    int ped = veh ? N<int>(N_CREATE_PED_INSIDE_VEHICLE, veh, 6, model, seat, 0, 1) : N<int>(N_CREATE_PED, 6, model, p.x, p.y, p.z, heading, 0, 1);
    if (!ped) return 0;
    gta::MakeMission(ped);
    N(N_SET_PED_AS_COP, ped, 1);
    N(N_GIVE_WEAPON_TO_PED, ped, gta::Joaat(seat <= 0 ? "weapon_pistol" : "weapon_carbinerifle"), 250, 0, 1);
    N(N_SET_PED_ACCURACY, ped, 35);
    N(N_SET_PED_KEEP_TASK, ped, 1);
    return ped;
}

void Release(Unit &u, bool remove) {
    if (u.blip) { N(N_REMOVE_BLIP, &u.blip); u.blip = 0; }
    auto drop = [remove](int &e) {
        if (!gta::Exists(e)) return;
        if (remove) N(N_DELETE_ENTITY, &e);
        else N(N_SET_ENTITY_AS_NO_LONGER_NEEDED, &e);   // the game despawns it when nobody looks
    };
    for (int &e : u.peds) drop(e);
    for (int &e : u.vehs) drop(e);
    for (int &e : u.props) if (gta::Exists(e)) N(N_DELETE_ENTITY, &e);   // spike strips never linger
}

// ---------------------------------------------------------------- actions

void Wanted(int level) {
    int player = gta::PlayerId();
    if (level == 0) {
        N(N_CLEAR_PLAYER_WANTED_LEVEL, player);
        gta::Notify("~b~LSPD~s~  Your record was cleared.");
    } else {
        N(N_SET_PLAYER_WANTED_LEVEL, player, level, 0);
        N(N_SET_PLAYER_WANTED_LEVEL_NOW, player, 0);
        gta::Notify("~r~LSPD~s~  You have been reported. Wanted level ~r~" + std::to_string(level) + "~s~.");
    }
    LOGI("godeye", "-", "wanted level %d", level);
}

void PoliceAlert(int me, const std::string &text) {
    Vec3 p = gta::Pos(me), node;
    float h = 0;
    gta::Notify("~b~POLICE ALERT~s~  " + text);
    N(N_FLASH_WANTED_DISPLAY, 1);
    g_flashUntil = gta::GameTimeMs() + 6000;
    if (!RoadNodeAround(p, 120.f, 300.f, node, h)) { LOGW("godeye", "-", "police alert: no road near the player"); return; }
    uint32_t car = gta::Joaat("police3"), cop = gta::Joaat("s_m_y_cop_01");
    if (!gta::LoadModel(car) || !gta::LoadModel(cop)) return;
    Unit u;
    u.kind = kGePoliceAlert;
    int v = SpawnCar(car, node, h);
    if (!v) return;
    u.vehs.push_back(v);
    u.peds.push_back(SpawnCop(cop, v, -1, node, h));
    u.peds.push_back(SpawnCop(cop, v, 0, node, h));
    N(N_SET_VEHICLE_SIREN, v, 1);
    N(N_SET_VEHICLE_ENGINE_ON, v, 1, 1, 0);
    u.blip = N<int>(N_ADD_BLIP_FOR_ENTITY, v);
    N(N_SET_BLIP_COLOUR, u.blip, 3);
    N(N_SET_BLIP_AS_SHORT_RANGE, u.blip, 1);
    u.startMs = gta::GameTimeMs();
    u.untilMs = u.startMs + 150000;
    g_units.push_back(u);
    gta::ReleaseModel(car); gta::ReleaseModel(cop);
    LOGI("godeye", "-", "police alert: unit sent from %.0f,%.0f (%.0f m)", node.x, node.y, std::sqrt(Dist2(node, p)));
}

void Roadblock(int me) {
    Vec3 p = gta::Pos(me);
    int veh = gta::InVehicle(me) ? N<int>(N_GET_VEHICLE_PED_IS_IN, me, 0) : 0;
    Vec3 dir;
    Vec3 vel = gta::Velocity(veh ? veh : me);
    float sp = std::sqrt(vel.x * vel.x + vel.y * vel.y);
    if (sp > 3.f) dir = {vel.x / sp, vel.y / sp, 0};
    else dir = Forward(gta::Heading(veh ? veh : me));
    Vec3 ahead{p.x + dir.x * 140.f, p.y + dir.y * 140.f, p.z}, node;
    NVec3 n{};
    float h = 0;
    if (!NB(N<int>(N_GET_CLOSEST_VEHICLE_NODE_WITH_HEADING, ahead.x, ahead.y, ahead.z, &n, &h, 1, 3.f, 0.f))) { LOGW("godeye", "-", "roadblock: no road ahead"); return; }
    node = {n.x, n.y, n.z};
    uint32_t car = gta::Joaat("police"), cop = gta::Joaat("s_m_y_cop_01"), stinger = gta::Joaat("p_ld_stinger_s");
    if (!gta::LoadModel(car) || !gta::LoadModel(cop)) return;
    Unit u;
    u.kind = kGeRoadblock;
    Vec3 f = Forward(h), across{f.y, -f.x, 0};
    // from the node towards the far side, away from the player: the cops stand behind their cars
    float sx = node.x - p.x, sy = node.y - p.y, sl = std::sqrt(sx * sx + sy * sy);
    Vec3 away = sl > 1.f ? Vec3{sx / sl, sy / sl, 0} : f;
    for (int side : {-1, 1}) {
        Vec3 c{node.x + across.x * 3.2f * side, node.y + across.y * 3.2f * side, node.z};
        int v = SpawnCar(car, c, h + 90.f + (side > 0 ? 180.f : 0.f));
        if (!v) continue;
        u.vehs.push_back(v);
        N(N_SET_VEHICLE_SIREN, v, 1);
        for (int k = 0; k < 2; ++k) {
            Vec3 cp{c.x + away.x * 3.f + across.x * (k ? 1.2f : -1.2f), c.y + away.y * 3.f + across.y * (k ? 1.2f : -1.2f), c.z + 0.5f};
            int ped = SpawnCop(cop, 0, k, cp, h + 180.f);
            if (!ped) continue;
            u.peds.push_back(ped);
            N(N_TASK_GUARD_CURRENT_POSITION, ped, 4.f, 4.f, 1);
        }
    }
    if (gta::LoadModel(stinger, 3000)) {   // a spike strip across the road in front of the cars
        Vec3 sp2{node.x - away.x * 9.f, node.y - away.y * 9.f, node.z};
        int s = N<int>(N_CREATE_OBJECT, stinger, sp2.x, sp2.y, sp2.z, 0, 1, 0);
        if (s) {
            gta::SetHeading(s, h);
            N<int>(N_PLACE_OBJECT_ON_GROUND_PROPERLY, s);
            gta::Freeze(s, true);
            u.props.push_back(s);
            u.at = sp2;
            u.across = across;
        }
        gta::ReleaseModel(stinger);
    }
    u.startMs = gta::GameTimeMs();
    u.untilMs = u.startMs + 180000;
    g_units.push_back(u);
    gta::ReleaseModel(car); gta::ReleaseModel(cop);
    gta::Notify("~b~LSPD~s~  Units are setting up a roadblock nearby.");
    LOGI("godeye", "-", "roadblock at %.0f,%.0f (%.0f m ahead)", node.x, node.y, std::sqrt(Dist2(node, p)));
}

void UnitsTick(int me) {
    int now = gta::GameTimeMs();
    Vec3 p = gta::Pos(me);
    for (size_t i = 0; i < g_units.size();) {
        Unit &u = g_units[i];
        float far2 = Dist2(p, u.vehs.empty() ? u.at : gta::Pos(u.vehs[0]));
        if (now > u.untilMs || far2 > 650.f * 650.f) {
            Release(u, far2 > 250.f * 250.f);
            LOGI("godeye", "-", "%s released", u.kind == kGeRoadblock ? "roadblock" : "police unit");
            g_units.erase(g_units.begin() + i);
            continue;
        }
        if (u.kind == kGePoliceAlert && !u.vehs.empty() && gta::Exists(u.vehs[0]) && !u.peds.empty() && gta::Exists(u.peds[0])) {
            int v = u.vehs[0], driver = u.peds[0];
            if (!u.arrived && far2 < 40.f * 40.f) {   // on the scene: out of the car, look around
                u.arrived = true;
                for (int ped : u.peds)
                    if (gta::Exists(ped)) N(N_TASK_WANDER_STANDARD, ped, 10.f, 10);
                N(N_SET_VEHICLE_SIREN, v, 0);
            } else if (!u.arrived && now >= u.retaskMs) {   // drive to where the player is now
                u.retaskMs = now + 5000;
                N(N_TASK_VEHICLE_DRIVE_TO_COORD_LONGRANGE, driver, v, p.x, p.y, p.z, 22.f, 1074528293, 25.f);
            }
        }
        if (u.kind == kGeRoadblock && !u.props.empty() && gta::InVehicle(me)) {   // the spike strip bursts tyres
            int veh = N<int>(N_GET_VEHICLE_PED_IS_IN, me, 0);
            Vec3 c = gta::Pos(veh);
            float dx = c.x - u.at.x, dy = c.y - u.at.y;
            float along = dx * u.across.x + dy * u.across.y, through = std::fabs(dx * u.across.y - dy * u.across.x);
            if (std::fabs(along) < 4.f && through < 1.6f)
                for (int w = 0; w < 8; ++w) N(N_SET_VEHICLE_TYRE_BURST, veh, w, 0, 1000.f);
        }
        ++i;
    }
}

// ---------------------------------------------------------------- traffic

void SetTraffic(uint8_t mode, uint32_t durationMs) {
    g_traffic = mode;
    g_trafficUntil = mode != kTrafficNormal && durationMs ? gta::GameTimeMs() + (int)durationMs : 0;
    g_nextLights = g_nextDrivers = 0;
    static const char *msg[] = {"Traffic lights are back to normal.", "Every traffic light is ~r~RED~s~.",
                                "Every traffic light is ~g~GREEN~s~.", "The traffic lights have gone ~y~haywire~s~."};
    gta::Notify(std::string("~p~CITY TRAFFIC~s~  ") + msg[mode]);
    LOGI("godeye", "-", "traffic %s%s", TrafficModeName(), g_trafficUntil ? " (timed)" : "");
}

void RestoreTraffic() {
    for (auto &kv : g_lights) {
        int e = kv.first;
        if (gta::Exists(e)) N(N_SET_ENTITY_TRAFFICLIGHT_OVERRIDE, e, 3);   // 3 = the game's own cycle
    }
    g_lights.clear();
    for (int d : g_restyled)
        if (gta::Exists(d) && !gta::Dead(d)) N(N_SET_DRIVE_TASK_DRIVING_STYLE, d, kStyleNormal);
    g_restyled.clear();
    g_lightPos.clear();
}

void TrafficTick(int me) {
    int now = gta::GameTimeMs();
    if (g_trafficUntil && now > g_trafficUntil) SetTraffic(kTrafficNormal, 0);
    if (g_traffic == kTrafficNormal) { if (!g_lights.empty() || !g_restyled.empty()) RestoreTraffic(); return; }
    if (!g_allObjects || !g_allVehicles) return;
    Vec3 p = gta::Pos(me);
    if (now >= g_nextLights) {   // the light props round the player (new ones stream in as we move)
        g_nextLights = now + 500;
        static int objs[4096];
        int n = g_allObjects(objs, 4096);
        g_lightPos.clear();
        for (int i = 0; i < n; ++i) {
            int o = objs[i];
            if (!IsTrafficLight(gta::ModelOf(o))) continue;
            Vec3 lp = gta::Pos(o);
            if (Dist2(lp, p) > kTrafficRadius * kTrafficRadius) continue;
            g_lightPos.push_back(lp);
            auto &st = g_lights[o];
            int want = g_traffic == kTrafficAllRed ? 1 : g_traffic == kTrafficAllGreen ? 0 : st.first;
            if (g_traffic == kTrafficRandom && now >= st.second) { want = std::rand() % 3; st.second = now + 2000 + std::rand() % 5000; }
            if (want != st.first || st.second == 0) {
                N(N_SET_ENTITY_TRAFFICLIGHT_OVERRIDE, o, want);
                st.first = want;
                if (st.second == 0) st.second = now + 2000 + std::rand() % 5000;
            }
        }
    }
    if (now >= g_nextReport) {   // evidence in the log: what the hack is doing right now
        g_nextReport = now + 5000;
        LOGI("godeye", "-", "traffic %s: %d lights overridden near the player, %d drivers ignoring lights, %d red-light stops in 5 s",
             TrafficModeName(), (int)g_lightPos.size(), (int)g_restyled.size(), g_brakes);
        g_brakes = 0;
    }
    if (now < g_nextDrivers) return;
    g_nextDrivers = now + 250;
    static int vehs[1024];
    int n = g_allVehicles(vehs, 1024);
    int mine = gta::InVehicle(me) ? N<int>(N_GET_VEHICLE_PED_IS_IN, me, 0) : 0;
    std::set<int> styledNow;   // rebuilt every pass: drivers that left the area are forgotten (the set stays small)
    for (int i = 0; i < n; ++i) {
        int v = vehs[i];
        if (v == mine) continue;
        Vec3 vp = gta::Pos(v);
        if (Dist2(vp, p) > kTrafficRadius * kTrafficRadius) continue;
        int d = N<int>(N_GET_PED_IN_VEHICLE_SEAT, v, -1, 0);
        if (!d || NB(N<int>(N_IS_PED_A_PLAYER, d)) || NB(N<int>(N_IS_ENTITY_A_MISSION_ENTITY, d))) continue;
        bool ignoreLights = g_traffic == kTrafficAllGreen || (g_traffic == kTrafficRandom && (d & 1));
        if (ignoreLights) {
            if (!g_restyled.count(d)) N(N_SET_DRIVE_TASK_DRIVING_STYLE, d, kStyleNormal & ~kStopAtLights);
            styledNow.insert(d);
            continue;
        }
        if (g_restyled.erase(d)) N(N_SET_DRIVE_TASK_DRIVING_STYLE, d, kStyleNormal);
        if (g_traffic != kTrafficAllRed || N<float>(N_GET_ENTITY_SPEED, v) < 0.5f) continue;
        // red everywhere: brake before a light ahead (a pole 6-28 m in front, roughly in the driving direction)
        Vec3 f = Forward(gta::Heading(v));
        for (const Vec3 &lp : g_lightPos) {
            float dx = lp.x - vp.x, dy = lp.y - vp.y, dl = std::sqrt(dx * dx + dy * dy);
            if (dl < 6.f || dl > 28.f || (dx * f.x + dy * f.y) / dl < 0.7f) continue;
            N(N_TASK_VEHICLE_TEMP_ACTION, d, v, 27, 1200);   // brake until stopped or the time runs out
            ++g_brakes;
            break;
        }
    }
    g_restyled.swap(styledNow);
}

// ---------------------------------------------------------------- GodEye's hacks on a player (2026-11-16, paid in Hack
// Credits on the web): a drone with a taser, an RC Bandito packed with C4, hacked self-driving taxis. All of it on this
// player's own game, like the police units: theirs to escape, shoot down or outrun.
struct Gadget {
    int kind = 0;                 // kGeDrone | kGeRcBomb | kGeTaxis
    int obj = 0;                  // the drone (an object we fly)
    std::vector<int> vehs, peds;  // the RC car / the taxis and their hidden drivers
    std::vector<int> blips;
    int startMs = 0, untilMs = 0, nextMs = 0, retaskMs = 0, zaps = 0;
    Vec3 at;                      // the drone's place
    float yaw = 0;                // the drone's heading
    // driven from GodEye (2026-11-17): the stick of whoever bought it, while it keeps coming
    bool aiStopped = false, told = false, actionHeld = false, boosted = false;
    int tempAction = -1, tempUntil = 0;   // the driven car's current TASK_VEHICLE_TEMP_ACTION
    std::vector<char> borrowed;   // per car: a Knoway of Gori Online's own fleet (hacked, never deleted: given back)
    std::vector<int> stuck;       // per car: checks in a row that found it standing still far from the player
    std::vector<int> mode;        // per car: 0 not tasked yet, 1 ramming the player, 2 driving to where the player was
    std::vector<Vec3> gotoAt;     // per car in mode 2: where it drives to
    int boomAt = 0;               // free driving: the RC car went off, gone at this time
};
std::vector<Gadget> g_gadgets;
GodEyeSteerMsg g_steer[kGeTaxis + 1];   // the newest stick per hack kind
int g_steerUntil[kGeTaxis + 1] = {};    // game ms: driven until then (the stick comes ~10 a second)
int g_nextGadgetReport = 0;
bool g_reported = false;                // the last report had gadgets: one empty report when they are gone
constexpr int kDroneZaps = 5;

void GadgetRelease(Gadget &g, bool remove) {
    for (int &b : g.blips) if (b) N(N_REMOVE_BLIP, &b);
    g.blips.clear();
    if (gta::Exists(g.obj)) N(N_DELETE_ENTITY, &g.obj);
    for (size_t k = 0; k < g.vehs.size(); ++k) {
        int &v = g.vehs[k];
        int &d = g.peds[k];
        if (k < g.borrowed.size() && g.borrowed[k]) {   // a Knoway of Gori Online's fleet: back to wandering, as Gori had it
            if (gta::Exists(v)) {
                N(N_SET_ENTITY_PROOFS, v, 0, 0, 0, 0, 0, 0, 0, 0);
                N(N_SET_VEHICLE_CHEAT_POWER_INCREASE, v, 1.f);
                N(N_SET_VEHICLE_DOORS_LOCKED, v, 1);
                if (gta::Exists(d)) N(N_TASK_VEHICLE_DRIVE_WANDER, d, v, 14.f, 786603);
            }
            continue;
        }
        if (gta::Exists(d)) N(N_DELETE_ENTITY, &d);   // our hidden drivers never linger
        if (!gta::Exists(v)) continue;
        if (remove) N(N_DELETE_ENTITY, &v); else N(N_SET_ENTITY_AS_NO_LONGER_NEEDED, &v);
    }
}

int Blip(int e, int colour) {
    int b = N<int>(N_ADD_BLIP_FOR_ENTITY, e);
    if (b) { N(N_SET_BLIP_COLOUR, b, colour); N(N_SET_BLIP_AS_SHORT_RANGE, b, 1); }
    return b;
}

// a hidden driver: the "self-driving" taxi and the RC car have nobody you can see at the wheel
int HiddenDriver(int veh) {
    uint32_t m = gta::Joaat("a_m_y_business_01");
    if (!gta::LoadModel(m)) return 0;
    int ped = N<int>(N_CREATE_PED_INSIDE_VEHICLE, veh, 26, m, -1, 0, 1);
    gta::ReleaseModel(m);
    if (!ped) return 0;
    gta::MakeMission(ped);
    gta::SetVisible(ped, false);
    gta::SetInvincible(ped, true);
    N(N_SET_BLOCKING_OF_NON_TEMPORARY_EVENTS, ped, 1);
    N(N_SET_DRIVER_ABILITY, ped, 1.f);
    N(N_SET_PED_KEEP_TASK, ped, 1);
    return ped;
}

uint32_t Lifetime(uint32_t durationMs, int fallbackMs) { return durationMs ? durationMs : (uint32_t)fallbackMs; }

void Drone(int me, uint32_t durationMs) {
    uint32_t m = 0;
    for (const char *n : {"xs_prop_arena_drone_01", "xs_prop_arena_drone_02", "ba_prop_battle_drone_quad"})
        if (NB(N<int>(N_IS_MODEL_VALID, gta::Joaat(n)))) { m = gta::Joaat(n); break; }
    if (!m || !gta::LoadModel(m)) { LOGW("godeye", "-", "drone: no drone model in this game"); return; }
    Vec3 p = gta::Pos(me);
    float a = (float)(gta::GameTimeMs() % 6283) / 1000.f;
    Gadget g;
    g.kind = kGeDrone;
    g.at = {p.x + std::cos(a) * 60.f, p.y + std::sin(a) * 60.f, p.z + 25.f};
    g.obj = N<int>(N_CREATE_OBJECT, m, g.at.x, g.at.y, g.at.z, 0, 1, 0);
    gta::ReleaseModel(m);
    if (!g.obj) return;
    gta::MakeMission(g.obj);
    gta::Freeze(g.obj, true);
    g.blips.push_back(Blip(g.obj, 1));
    g.startMs = gta::GameTimeMs();
    g.untilMs = g.startMs + (int)Lifetime(durationMs, 45000);
    g.nextMs = g.startMs + 6000;
    g.yaw = std::atan2(-(p.x - g.at.x), p.y - g.at.y) * 57.2958f;
    g_gadgets.push_back(g);
    gta::Notify("~p~GodEye~s~  A ~r~taser drone~s~ has been hacked onto you. Shoot it down!");
    LOGI("godeye", "-", "taser drone launched 60 m out");
}

void RcBomb(int me, uint32_t durationMs) {
    Vec3 p = gta::Pos(me), node;
    float h = 0;
    if (!RoadNodeAround(p, 70.f, 140.f, node, h)) { LOGW("godeye", "-", "rc bomb: no road near the player"); return; }
    uint32_t m = gta::Joaat("rcbandito");
    if (!gta::LoadModel(m)) { LOGW("godeye", "-", "rc bomb: no rcbandito in this game"); return; }
    Gadget g;
    g.kind = kGeRcBomb;
    int v = SpawnCar(m, node, h);
    gta::ReleaseModel(m);
    if (!v) return;
    g.vehs.push_back(v);
    g.peds.push_back(HiddenDriver(v));
    g.borrowed.push_back(0);
    N(N_SET_VEHICLE_ENGINE_ON, v, 1, 1, 0);
    g.blips.push_back(Blip(v, 1));
    g.startMs = gta::GameTimeMs();
    g.untilMs = g.startMs + (int)Lifetime(durationMs, 75000);
    g_gadgets.push_back(g);
    gta::Notify("~p~GodEye~s~  An ~r~RC Bandito packed with C4~s~ is coming for you. Destroy it or run!");
    LOGI("godeye", "-", "rc bomb sent from %.0f,%.0f (%.0f m)", node.x, node.y, std::sqrt(Dist2(node, p)));
}

// GodEye hacks the city's self-driving cars (2026-11-17): Gori Online's Knoway fleet (the knowayest add-on, a hidden
// computer driver; Knoway.inc) round the player is taken over first; the rest of the pair comes as new Knoways from a
// road out of sight (taxis in a game without the add-on). The taken ones go back to wandering afterwards.
uint32_t KnowayModel() { static uint32_t h = gta::Joaat("knowayest"); return h; }
bool HaveKnoway() { return NB(N<int>(N_IS_MODEL_IN_CDIMAGE, KnowayModel())) && NB(N<int>(N_IS_MODEL_A_VEHICLE, KnowayModel())); }

void Taxis(int me, uint32_t durationMs) {
    Vec3 p = gta::Pos(me);
    Gadget g;
    g.kind = kGeTaxis;
    int taken = 0;
    if (g_allVehicles && HaveKnoway()) {   // the Knoways already driving round the player
        static int vehs[1024];
        int n = g_allVehicles(vehs, 1024);
        std::vector<std::pair<float, int>> around;   // ("near" is a macro in windows.h)
        for (int i = 0; i < n; ++i) {
            int v = vehs[i];
            if (N<uint32_t>(N_GET_ENTITY_MODEL, v) != KnowayModel() || gta::Dead(v)) continue;
            int d = N<int>(N_GET_PED_IN_VEHICLE_SEAT, v, -1, 0);
            if (!d || NB(N<int>(N_IS_PED_A_PLAYER, d)) || N<int>(N_GET_PED_IN_VEHICLE_SEAT, v, 1, 0) == me || N<int>(N_GET_PED_IN_VEHICLE_SEAT, v, 2, 0) == me) continue;
            float d2 = Dist2(gta::Pos(v), p);
            if (d2 < 300.f * 300.f) around.push_back({d2, v});
        }
        std::sort(around.begin(), around.end());
        for (size_t k = 0; k < around.size() && g.vehs.size() < 2; ++k) {
            int v = around[k].second;
            g.vehs.push_back(v);
            g.peds.push_back(N<int>(N_GET_PED_IN_VEHICLE_SEAT, v, -1, 0));
            g.borrowed.push_back(1);
            N(N_SET_VEHICLE_DOORS_LOCKED, v, 2);
            g.blips.push_back(Blip(v, 5));
            ++taken;
        }
    }
    uint32_t m = HaveKnoway() ? KnowayModel() : gta::Joaat("taxi");
    if (g.vehs.size() < 2 && gta::LoadModel(m)) {
        for (int k = (int)g.vehs.size(); k < 2; ++k) {
            Vec3 node;
            float h = 0;
            if (!RoadNodeAround(p, 80.f + 50.f * k, 180.f + 40.f * k, node, h)) continue;
            int v = SpawnCar(m, node, h);
            if (!v) continue;
            g.vehs.push_back(v);
            g.peds.push_back(HiddenDriver(v));
            g.borrowed.push_back(0);
            N(N_SET_VEHICLE_ENGINE_ON, v, 1, 1, 0);
            N(N_SET_VEHICLE_DOORS_LOCKED, v, 2);
            g.blips.push_back(Blip(v, 5));
        }
        gta::ReleaseModel(m);
    }
    if (g.vehs.empty()) { LOGW("godeye", "-", "self-driving cars: none near the player and no road"); return; }
    g.startMs = gta::GameTimeMs();
    g.untilMs = g.startMs + (int)Lifetime(durationMs, 45000);
    g_gadgets.push_back(g);
    gta::Notify(m == KnowayModel() ? "~p~GodEye~s~  The ~b~Knoway~s~ self-driving cars have been hacked. They are coming for you!"
                                   : "~p~GodEye~s~  The city's ~y~self-driving taxis~s~ have been hacked. They are coming for you!");
    LOGI("godeye", "-", "%d self-driving cars hacked (%d of the Knoway fleet taken over, model %s)", (int)g.vehs.size(), taken,
         m == KnowayModel() ? "knowayest" : "taxi");
}

float FrameS() { float dt = N<float>(N_GET_FRAME_TIME); return dt > 0.f && dt < 0.1f ? dt : 0.016f; }

// the taser: at the player when it is close enough
void Zap(Gadget &g, int me, const Vec3 &p, int now, int cooldownMs) {
    g.nextMs = now + cooldownMs;
    ++g.zaps;
    N(N_SHOOT_SINGLE_BULLET_BETWEEN_COORDS, g.at.x, g.at.y, g.at.z - 0.3f, p.x, p.y, p.z + 0.3f, 0, 1, gta::Joaat("weapon_stungun"), 0, 1, 0, -1.f);
    if (gta::InVehicle(me)) {   // an EMP into the car: the engine dies for a few seconds
        int veh = N<int>(N_GET_VEHICLE_PED_IS_IN, me, 0);
        N(N_SET_VEHICLE_ENGINE_ON, veh, 0, 1, 1);
        g.retaskMs = now + 4000;
        gta::Notify("~r~ZAP!~s~  The drone's EMP killed your engine.");
    } else {
        N(N_SET_PED_TO_RAGDOLL, me, 3500, 4000, 0, 0, 0, 0);
        gta::Notify("~r~ZAP!~s~  Tased by GodEye's drone.");
    }
    LOGI("godeye", "-", "drone zap %d", g.zaps);
}

// a GodEye user flies the drone: x turns, y forward/back, z up/down; it stays above the ground and near the player
void DriveDrone(Gadget &g, const GodEyeSteerMsg &s, const Vec3 &p) {
    float dt = FrameS();
    g.yaw -= s.x / 100.f * 110.f * dt;
    Vec3 f = Forward(g.yaw);
    float v = s.y / 100.f * 17.f;
    g.at.x += f.x * v * dt;
    g.at.y += f.y * v * dt;
    g.at.z += s.z / 100.f * 9.f * dt;
    float dx = g.at.x - p.x, dy = g.at.y - p.y, d = std::sqrt(dx * dx + dy * dy);
    if (d > 250.f) { g.at.x = p.x + dx / d * 250.f; g.at.y = p.y + dy / d * 250.f; }   // the player's world ends there
    float ground = gta::GroundZ(g.at.x, g.at.y, g.at.z + 1.f, g.at.z - 50.f);
    if (g.at.z < ground + 1.2f) g.at.z = ground + 1.2f;
    if (g.at.z > p.z + 120.f) g.at.z = p.z + 120.f;
}

// a GodEye user drives a car (the RC Bandito, the lead taxi): arcade steering on the car GTA keeps simulating
// The stick as GTA's own driving: the car's (hidden) driver gets the matching temporary action, so the engine, the gears,
// the wheels and the suspension do the work exactly as for any car in the game (2026-11-17: the first version pushed
// the car with SET_VEHICLE_FORWARD_SPEED and turned it with SET_ENTITY_HEADING, and it slid like a toy).
int TempActionFor(const GodEyeSteerMsg &s) {
    bool left = s.x < -30, right = s.x > 30;
    if (s.y > 30) return left ? 7 : right ? 8 : 23;     // turn left / right and accelerate; accelerate hard
    if (s.y < -30) return left ? 13 : right ? 14 : 28;  // turn and reverse; brake and reverse
    return left ? 10 : right ? 11 : 1;                  // steer while rolling; brake
}

void DriveCar(Gadget &g, size_t k, const GodEyeSteerMsg &s, bool boost, int now) {
    int veh = g.vehs[k], drv = k < g.peds.size() ? g.peds[k] : 0;
    if (!gta::Exists(drv)) return;
    if (!g.aiStopped) {
        // its driver's crashes do not wreck it (an RC Bandito lost 600 health on one wall at speed in the 2026-11-17
        // test); bullets, fire and explosions still do, so the player can still destroy it
        N(N_SET_ENTITY_PROOFS, veh, 0, 0, 0, 1, 0, 0, 0, 0);
        g.aiStopped = true;
        g.tempAction = -1;
    }
    int a = TempActionFor(s);
    if (a != g.tempAction || now >= g.tempUntil) {
        N(N_TASK_VEHICLE_TEMP_ACTION, drv, veh, a, 700);
        g.tempAction = a;
        g.tempUntil = now + 450;
    }
    if (boost != g.boosted) { N(N_SET_VEHICLE_CHEAT_POWER_INCREASE, veh, boost ? 2.2f : 1.f); g.boosted = boost; }
}

// ~10 a second: where the hacks are, for GodEye's map and the camera clients
void ReportGadgets(const Vec3 &p, int now) {
    if (now < g_nextGadgetReport) return;
    g_nextGadgetReport = now + 100;
    GadgetStateMsg m;
    for (const Gadget &g : g_gadgets) {
        bool driven = now < g_steerUntil[g.kind];
        uint16_t left = (uint16_t)(g.untilMs > now ? (g.untilMs - now + 999) / 1000 : 0);
        if (g.kind == kGeDrone && gta::Exists(g.obj) && m.g.size() < kMaxGadgets) {
            uint8_t fl = (driven ? kGadgetDriven : 0) | (Dist2(g.at, p) < 20.f * 20.f ? kGadgetInRange : 0);
            m.g.push_back(GadgetPose{(uint8_t)g.kind, fl, {g.at.x, g.at.y, g.at.z}, g.yaw, left});
        }
        for (size_t k = 0; k < g.vehs.size() && m.g.size() < kMaxGadgets; ++k) {
            if (!gta::Exists(g.vehs[k])) continue;
            Vec3 c = gta::Pos(g.vehs[k]);
            uint8_t fl = (driven && k == 0 ? kGadgetDriven : 0) | (N<uint32_t>(N_GET_ENTITY_MODEL, g.vehs[k]) == KnowayModel() ? kGadgetKnoway : 0);
            m.g.push_back(GadgetPose{(uint8_t)g.kind, fl, {c.x, c.y, c.z}, gta::Heading(g.vehs[k]), left});
        }
    }
    if (m.g.empty() && !g_reported) return;
    g_reported = !m.g.empty();
    wire::Writer w;
    Put(w, m);
    net::SendUnreliable(w);
}

// every frame: the drone flies and zaps, the RC car homes in and blows up, the taxis ram
void GadgetsTick(int me) {
    int now = gta::GameTimeMs();
    Vec3 p = gta::Pos(me);
    bool dead = gta::Dead(me);
    for (size_t i = 0; i < g_gadgets.size();) {
        Gadget &g = g_gadgets[i];
        bool done = now > g.untilMs || dead;
        if (g.kind == kGeDrone && !done) {
            if (!gta::Exists(g.obj)) done = true;
            else if (NB(N<int>(N_HAS_ENTITY_BEEN_DAMAGED_BY_ANY_PED, g.obj))) {   // shot down
                N(N_ADD_EXPLOSION, g.at.x, g.at.y, g.at.z, 0, 0.f, 1, 0, 0.3f, 1);
                gta::Notify("~g~Drone down.~s~");
                LOGI("godeye", "-", "taser drone shot down");
                done = true;
            } else {
                if (now < g_steerUntil[kGeDrone]) {   // a GodEye user flies it; the action button zaps
                    const GodEyeSteerMsg &s = g_steer[kGeDrone];
                    if (!g.told) { g.told = true; gta::Notify("~p~GodEye~s~  Someone on GodEye is ~r~flying the drone~s~ now."); LOGI("godeye", "-", "drone driven from GodEye"); }
                    DriveDrone(g, s, p);
                    N(N_SET_ENTITY_COORDS_NO_OFFSET, g.obj, g.at.x, g.at.y, g.at.z, 0, 0, 0);
                    N(N_SET_ENTITY_ROTATION, g.obj, 0.f, 0.f, g.yaw, 2, 1);
                    bool press = (s.buttons & kSteerAction) && !g.actionHeld;
                    g.actionHeld = (s.buttons & kSteerAction) != 0;
                    if (press && now >= g.nextMs && Dist2(g.at, p) < 20.f * 20.f && g.zaps < kDroneZaps) Zap(g, me, p, now, 2500);
                } else {
                    // circle 7 m from the player, 4 m up, closing in at up to 14 m/s
                    float a = (float)(now % 9000) / 9000.f * 6.2832f;
                    Vec3 want{p.x + std::cos(a) * 7.f, p.y + std::sin(a) * 7.f, p.z + 4.f};
                    float dx = want.x - g.at.x, dy = want.y - g.at.y, dz = want.z - g.at.z, d = std::sqrt(dx * dx + dy * dy + dz * dz);
                    float step = 14.f / 60.f;
                    if (d > step) { g.at.x += dx / d * step; g.at.y += dy / d * step; g.at.z += dz / d * step; } else g.at = want;
                    N(N_SET_ENTITY_COORDS_NO_OFFSET, g.obj, g.at.x, g.at.y, g.at.z, 0, 0, 0);
                    g.yaw = std::atan2(-(p.x - g.at.x), p.y - g.at.y) * 57.2958f;
                    N(N_SET_ENTITY_ROTATION, g.obj, 0.f, 0.f, g.yaw, 2, 1);
                    if (now >= g.nextMs && Dist2(g.at, p) < 18.f * 18.f && g.zaps < kDroneZaps) Zap(g, me, p, now, 5000);
                }
                if (g.retaskMs && now >= g.retaskMs && gta::InVehicle(me)) {   // the engine back on
                    g.retaskMs = 0;
                    N(N_SET_VEHICLE_ENGINE_ON, N<int>(N_GET_VEHICLE_PED_IS_IN, me, 0), 1, 0, 0);
                }
                if (g.zaps >= kDroneZaps && now >= g.nextMs) done = true;   // its battery is flat
            }
        }
        if (g.kind == kGeRcBomb && !done) {
            int v = g.vehs.empty() ? 0 : g.vehs[0];
            if (!gta::Exists(v)) done = true;
            else {
                Vec3 c = gta::Pos(v);
                bool driven = now < g_steerUntil[kGeRcBomb];
                // destroyed: a driven one takes the knocks of its driver's own crashes (one wall at speed went under 700
                // within 2 s in the 2026-11-17 test) and only goes when it is really wrecked
                bool wrecked = NB(N<int>(N_IS_ENTITY_DEAD, v)) || gta::Health(v) < (driven || g.told ? 250 : 700);   // told: it was driven at some point
                const GodEyeSteerMsg &s = g_steer[kGeRcBomb];
                bool press = driven && (s.buttons & kSteerAction) && !g.actionHeld;
                g.actionHeld = driven && (s.buttons & kSteerAction);
                if (wrecked) LOGI("godeye", "-", "rc wrecked: dead %d health %d", (int)NB(N<int>(N_IS_ENTITY_DEAD, v)), gta::Health(v));
                if (wrecked || press || Dist2(c, p) < 4.5f * 4.5f) {   // boom: on the player, by remote, or where it was stopped
                    N(N_ADD_EXPLOSION, c.x, c.y, c.z + 0.3f, 2, 1.f, 1, 0, 1.f, 0);
                    LOGI("godeye", "-", "rc bomb %s", wrecked ? "destroyed before it arrived" : press ? "set off from GodEye" : "went off at the player");
                    if (wrecked) gta::Notify("~g~The RC Bandito was destroyed.~s~");
                    done = true;
                } else if (driven) {   // a GodEye user drives it; the action button sets it off
                    if (!g.told) { g.told = true; gta::Notify("~p~GodEye~s~  Someone on GodEye is ~r~driving the RC Bandito~s~ now."); LOGI("godeye", "-", "rc bomb driven from GodEye"); }
                    DriveCar(g, 0, s, false, now);
                    if (now >= g.nextMs) {   // trace (2026-11-17): how a driven RC car holds up
                        g.nextMs = now + 2000;
                        LOGI("godeye", "-", "rc driven: health %d body %.0f engine %.0f speed %.1f action %d wheels %d", gta::Health(v),
                             N<float>(N_GET_VEHICLE_BODY_HEALTH, v), N<float>(N_GET_VEHICLE_ENGINE_HEALTH, v), N<float>(N_GET_ENTITY_SPEED, v), g.tempAction,
                             (int)NB(N<int>(N_IS_VEHICLE_ON_ALL_WHEELS, v)));
                    }
                } else if (g.aiStopped) {   // the user let go: the AI takes it back at once
                    g.aiStopped = false;
                    g.retaskMs = 0;
                } else if (now >= g.retaskMs && !g.peds.empty() && gta::Exists(g.peds[0])) {
                    g.retaskMs = now + 2500;
                    N(N_TASK_VEHICLE_MISSION_PED_TARGET, g.peds[0], v, me, 2, 30.f, 786469, 1.f, 0.f, 1);
                }
            }
        }
        bool taxiDriven = g.kind == kGeTaxis && !done && now < g_steerUntil[kGeTaxis] && !g.vehs.empty() && gta::Exists(g.vehs[0]);
        if (taxiDriven) {   // a GodEye user drives the lead taxi (action: horn, boost: a burst of speed); the other one rams on its own
            const GodEyeSteerMsg &s = g_steer[kGeTaxis];
            if (!g.told) { g.told = true; gta::Notify("~p~GodEye~s~  Someone on GodEye is ~y~driving a hacked self-driving car~s~ at you."); LOGI("godeye", "-", "self-driving car driven from GodEye"); }
            DriveCar(g, 0, s, (s.buttons & kSteerBoost) != 0, now);
            bool press = (s.buttons & kSteerAction) && !g.actionHeld;
            g.actionHeld = (s.buttons & kSteerAction) != 0;
            if (press) N(N_START_VEHICLE_HORN, g.vehs[0], 1200, gta::Joaat("HELDDOWN"), 0);
        } else if (g.kind == kGeTaxis && g.aiStopped) {   // let go: its computer rams again, at normal power
            g.aiStopped = false; g.retaskMs = 0;
            if (g.boosted && gta::Exists(g.vehs[0])) { N(N_SET_VEHICLE_CHEAT_POWER_INCREASE, g.vehs[0], 1.f); g.boosted = false; }
        }
        if (g.kind == kGeTaxis && !done && now >= g.retaskMs) {
            g.retaskMs = now + 3000;
            g.stuck.resize(g.vehs.size(), 0); g.mode.resize(g.vehs.size(), 0); g.gotoAt.resize(g.vehs.size());
            if (taxiDriven) g.mode[0] = 0;   // the stick has it: the AI tasks it afresh when it's let go
            for (size_t k = taxiDriven ? 1 : 0; k < g.vehs.size() && k < g.peds.size(); ++k) {
                int v = g.vehs[k];
                if (!gta::Exists(v) || gta::Dead(v)) continue;
                if (!gta::Exists(g.peds[k]) && !(k < g.borrowed.size() && g.borrowed[k])) {   // its hidden driver never came: seat a new one
                    g.peds[k] = HiddenDriver(v);
                    g.mode[k] = 0;
                    LOGI("godeye", "-", "self-driving car %d had no driver: new one %d", (int)k, g.peds[k]);
                }
                if (!gta::Exists(g.peds[k])) continue;
                // 2026-11-17 test: re-giving the ram task every 3 s restarted its route each time, so the cars kept
                // braking and some never got going. Now it is given once and changed only when the car is stuck: two
                // checks standing still far from the player swap ram <-> drive-to-the-player; a drive-to ends near its
                // point or when the player has moved on, and the ram takes over again.
                float speed = N<float>(N_GET_ENTITY_SPEED, v), dist = std::sqrt(Dist2(gta::Pos(v), p));
                g.stuck[k] = speed < 1.f && dist > 15.f ? g.stuck[k] + 1 : 0;
                int was = g.mode[k];
                if (g.mode[k] == 0) g.mode[k] = 1;
                else if (g.stuck[k] >= 2) { g.mode[k] = g.mode[k] == 1 ? 2 : 1; g.stuck[k] = 0; }
                else if (g.mode[k] == 2 && (Dist2(g.gotoAt[k], p) > 20.f * 20.f || Dist2(g.gotoAt[k], gta::Pos(v)) < 20.f * 20.f)) g.mode[k] = 1;
                bool retask = g.mode[k] != was;
                if (g.mode[k] == 2 && retask) {
                    g.gotoAt[k] = p;
                    N(N_TASK_VEHICLE_DRIVE_TO_COORD_LONGRANGE, g.peds[k], v, p.x, p.y, p.z, 26.f, 786469, 5.f);
                } else if (g.mode[k] == 1 && retask) N(N_TASK_VEHICLE_MISSION_PED_TARGET, g.peds[k], v, me, 2, 26.f, 786469, 1.f, 0.f, 1);
                N(N_START_VEHICLE_HORN, v, 1500, gta::Joaat("HELDDOWN"), 0);
                if (now - g.startMs < 40000 || g.mode[k] != was)
                    LOGI("godeye", "-", "self-driving car %d: speed %.1f, %.0f m from the player, %s%s", (int)k, speed, dist,
                         g.mode[k] == 2 ? "driving to the player" : "ramming", g.mode[k] != was ? " (new task)" : "");
            }
        }
        if (done) {
            float far2 = 0.f;
            for (int v : g.vehs) if (gta::Exists(v)) far2 = std::max(far2, Dist2(p, gta::Pos(v)));
            GadgetRelease(g, g.kind != kGeTaxis || far2 > 200.f * 200.f);
            g_gadgets.erase(g_gadgets.begin() + i);
            continue;
        }
        ++i;
    }
    ReportGadgets(p, now);
}

// ---- free driving (2026-11-17): with nobody online, GodEye's RC Bandito / Knoway lives in the camera client's own GTA.
// It comes out on the road nearest to what the camera shows; a GodEye user drives it like the hacks (GTA's own driving),
// with no stick it brakes and waits, and the camera follows it. One at a time: a new one replaces the old.
void FreeStart(int kind, uint32_t durationMs, const Vec3 &around) {
    for (auto &g : g_gadgets) GadgetRelease(g, true);
    g_gadgets.clear();
    Vec3 node;
    float h = 0;
    if (!RoadNodeAround(around, 0.f, 120.f, node, h)) { LOGW("godeye", "-", "free drive: no road near %.0f,%.0f", around.x, around.y); return; }
    uint32_t m = kind == kGeRcBomb ? gta::Joaat("rcbandito") : HaveKnoway() ? KnowayModel() : gta::Joaat("taxi");
    if (!gta::LoadModel(m)) { LOGW("godeye", "-", "free drive: model %08x not loaded", m); return; }
    int v = SpawnCar(m, node, h);
    gta::ReleaseModel(m);
    if (!v) return;
    Gadget g;
    g.kind = kind;
    g.vehs.push_back(v);
    g.peds.push_back(HiddenDriver(v));
    g.borrowed.push_back(0);
    N(N_SET_VEHICLE_ENGINE_ON, v, 1, 1, 0);
    N(N_SET_VEHICLE_DOORS_LOCKED, v, 2);
    g.startMs = gta::GameTimeMs();
    g.untilMs = g.startMs + (int)Lifetime(durationMs, 120000);
    g_gadgets.push_back(g);
    LOGI("godeye", "-", "free drive: %s at %.0f,%.0f,%.0f for %d s", kind == kGeRcBomb ? "RC Bandito" : m == KnowayModel() ? "Knoway" : "taxi",
         node.x, node.y, node.z, (g.untilMs - g.startMs) / 1000);
}

void FreeTick() {
    int now = gta::GameTimeMs();
    Vec3 at{};
    for (size_t i = 0; i < g_gadgets.size();) {
        Gadget &g = g_gadgets[i];
        int v = g.vehs.empty() ? 0 : g.vehs[0];
        bool done = now > g.untilMs || !gta::Exists(v) || (g.boomAt && now >= g.boomAt);
        if (!done && !g.boomAt) {
            at = gta::Pos(v);
            bool driven = now < g_steerUntil[g.kind];
            GodEyeSteerMsg s = driven ? g_steer[g.kind] : GodEyeSteerMsg{(uint8_t)g.kind, 0, 0, 0, 0};   // no stick: it brakes and waits
            if (driven && !g.told) { g.told = true; LOGI("godeye", "-", "free drive: driven from GodEye"); }
            DriveCar(g, 0, s, g.kind == kGeTaxis && (s.buttons & kSteerBoost), now);
            bool press = (s.buttons & kSteerAction) && !g.actionHeld;
            g.actionHeld = (s.buttons & kSteerAction) != 0;
            if (press && g.kind == kGeTaxis) N(N_START_VEHICLE_HORN, v, 1200, gta::Joaat("HELDDOWN"), 0);
            if (press && g.kind == kGeRcBomb) {   // the C4: a bang where it is, and it is gone
                N(N_ADD_EXPLOSION, at.x, at.y, at.z + 0.3f, 2, 1.f, 1, 0, 1.f, 0);
                g.boomAt = now + 2500;
                LOGI("godeye", "-", "free drive: the RC Bandito went off at %.0f,%.0f", at.x, at.y);
            }
        }
        if (done) {
            LOGI("godeye", "-", "free drive: over");
            GadgetRelease(g, true);
            g_gadgets.erase(g_gadgets.begin() + i);
            continue;
        }
        ++i;
    }
    ReportGadgets(at, now);
}

}  // namespace

void CameraFrame(const Vec3 &around) {
    while (!g_queue.empty()) {
        GodEyeActionMsg m = g_queue.front();
        g_queue.pop_front();
        if (m.kind == kGeRcBomb || m.kind == kGeTaxis) FreeStart(m.kind, m.durationMs, around);
    }
    FreeTick();
}

bool FreeCar(Vec3 &pos, float &heading, int &kind) {
    for (const Gadget &g : g_gadgets) {
        if (g.vehs.empty() || !gta::Exists(g.vehs[0])) continue;
        pos = gta::Pos(g.vehs[0]);
        heading = gta::Heading(g.vehs[0]);
        kind = g.kind;
        return true;
    }
    return false;
}

const char *TrafficModeName() {
    static const char *n[] = {"normal", "all red", "all green", "random"};
    return n[g_traffic <= kTrafficRandom ? g_traffic : 0];
}

void OnSteer(const GodEyeSteerMsg &m) {   // ~10 a second while a GodEye user drives (unreliable)
    g_steer[m.kind] = m;
    g_steerUntil[m.kind] = gta::GameTimeMs() + 700;
}

void OnAction(const GodEyeActionMsg &m) {
    if (g_queue.size() < 16) g_queue.push_back(m);
}

void Frame(int me) {
    BindWorld();
    while (!g_queue.empty()) {
        GodEyeActionMsg m = g_queue.front();
        g_queue.pop_front();
        switch (m.kind) {
            case kGeWanted: Wanted(m.value); break;
            case kGePoliceAlert: PoliceAlert(me, m.text); break;
            case kGeRoadblock: Roadblock(me); break;
            case kGeTraffic: if (m.value != g_traffic || m.durationMs) SetTraffic(m.value, m.durationMs); break;
            case kGeNotice: gta::Notify("~p~GodEye~s~  " + m.text); break;
            case kGeDrone: Drone(me, m.durationMs); break;
            case kGeRcBomb: RcBomb(me, m.durationMs); break;
            case kGeTaxis: Taxis(me, m.durationMs); break;
            default: break;
        }
    }
    if (g_flashUntil && gta::GameTimeMs() > g_flashUntil) { g_flashUntil = 0; N(N_FLASH_WANTED_DISPLAY, 0); }
    UnitsTick(me);
    GadgetsTick(me);
    TrafficTick(me);
}

void End() {
    g_queue.clear();
    if (g_flashUntil) { g_flashUntil = 0; N(N_FLASH_WANTED_DISPLAY, 0); }
    for (auto &u : g_units) Release(u, true);
    if (!g_units.empty()) LOGI("godeye", "-", "%d police units released", (int)g_units.size());
    g_units.clear();
    for (auto &g : g_gadgets) GadgetRelease(g, true);
    g_gadgets.clear();
    if (g_reported) { wire::Writer w; Put(w, GadgetStateMsg{}); net::SendUnreliable(w); g_reported = false; }
    if (g_traffic != kTrafficNormal || !g_lights.empty() || !g_restyled.empty()) RestoreTraffic();
    g_traffic = kTrafficNormal;
    g_trafficUntil = 0;
}

}  // namespace godeye
