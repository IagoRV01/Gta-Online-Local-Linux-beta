#include "game/godeye.h"
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

}  // namespace

const char *TrafficModeName() {
    static const char *n[] = {"normal", "all red", "all green", "random"};
    return n[g_traffic <= kTrafficRandom ? g_traffic : 0];
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
            default: break;
        }
    }
    if (g_flashUntil && gta::GameTimeMs() > g_flashUntil) { g_flashUntil = 0; N(N_FLASH_WANTED_DISPLAY, 0); }
    UnitsTick(me);
    TrafficTick(me);
}

void End() {
    g_queue.clear();
    if (g_flashUntil) { g_flashUntil = 0; N(N_FLASH_WANTED_DISPLAY, 0); }
    for (auto &u : g_units) Release(u, true);
    if (!g_units.empty()) LOGI("godeye", "-", "%d police units released", (int)g_units.size());
    g_units.clear();
    if (g_traffic != kTrafficNormal || !g_lights.empty() || !g_restyled.empty()) RestoreTraffic();
    g_traffic = kTrafficNormal;
    g_trafficUntil = 0;
}

}  // namespace godeye
