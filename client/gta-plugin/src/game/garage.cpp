#include "game/garage.h"
#include <windows.h>
#include <cmath>
#include <istream>
#include <map>
#include <string>
#include <vector>
#include "common/json.h"
#include "common/log.h"
#include "common/vehicle.h"
#include "engine/gta.h"
#include "engine/shv.h"
#include "game/devcmd.h"
#include "game/net_client.h"
#include "ui/menu.h"

namespace garage {

using namespace proto;
using vehicle::Personal;
using vehicle::State;

namespace {

std::vector<Personal> g_list;          // last list from the server
bool g_listFresh = false;
std::map<std::string, int> g_spawned;  // uuid -> vehicle handle (called out this session)
std::map<std::string, int> g_blips;
struct Pending { std::string action, uuid; int veh = 0; };
std::map<uint32_t, Pending> g_pending;
uint32_t g_nextId = 1;
unsigned long long g_lastPosUpdate = 0;

std::string Trim(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    while (!s.empty() && s[0] == ' ') s.erase(0, 1);
    return s;
}

Personal Capture(int veh) {
    Personal p;
    p.model = gta::ModelOf(veh);
    const char *plate = N<const char *>(N_GET_VEHICLE_NUMBER_PLATE_TEXT, veh);
    p.plate = Trim(plate ? plate : "");
    for (auto &ch : p.plate) ch = (char)toupper((unsigned char)ch);
    if (!vehicle::ValidPlate(p.plate)) p.plate = "LO" + std::to_string(GetTickCount64() % 100000);
    N(N_GET_VEHICLE_COLOURS, veh, &p.color1, &p.color2);
    N(N_GET_VEHICLE_EXTRA_COLOURS, veh, &p.pearl, &p.wheelColor);
    p.wheelType = N<int>(N_GET_VEHICLE_WHEEL_TYPE, veh);
    p.windowTint = N<int>(N_GET_VEHICLE_WINDOW_TINT, veh);
    p.plateIndex = N<int>(N_GET_VEHICLE_NUMBER_PLATE_TEXT_INDEX, veh);
    p.livery = N<int>(N_GET_VEHICLE_LIVERY, veh);
    N(N_SET_VEHICLE_MOD_KIT, veh, 0);
    for (int i = 0; i < vehicle::kNumMods; ++i) p.mods[i] = N<int>(N_GET_VEHICLE_MOD, veh, i);
    p.turbo = NB(N<int>(N_IS_TOGGLE_MOD_ON, veh, 18));
    p.xenon = NB(N<int>(N_IS_TOGGLE_MOD_ON, veh, 22));
    p.engineHealth = N<float>(N_GET_VEHICLE_ENGINE_HEALTH, veh);
    p.bodyHealth = N<float>(N_GET_VEHICLE_BODY_HEALTH, veh);
    gta::Vec3 at = gta::Pos(veh);
    p.x = at.x; p.y = at.y; p.z = at.z; p.heading = gta::Heading(veh);
    std::string err;
    vehicle::Validate(p, err);
    return p;
}

void Apply(int veh, const Personal &p) {
    N(N_SET_VEHICLE_MOD_KIT, veh, 0);
    N(N_SET_VEHICLE_WHEEL_TYPE, veh, p.wheelType);
    for (int i = 0; i < vehicle::kNumMods; ++i)
        if (i < 17 || i > 22) { if (p.mods[i] >= 0) N(N_SET_VEHICLE_MOD, veh, i, p.mods[i], 0); }
    N(N_TOGGLE_VEHICLE_MOD, veh, 18, p.turbo ? 1 : 0);
    N(N_TOGGLE_VEHICLE_MOD, veh, 22, p.xenon ? 1 : 0);
    N(N_SET_VEHICLE_COLOURS, veh, p.color1, p.color2);
    N(N_SET_VEHICLE_EXTRA_COLOURS, veh, p.pearl, p.wheelColor);
    if (p.windowTint >= 0) N(N_SET_VEHICLE_WINDOW_TINT, veh, p.windowTint);
    N(N_SET_VEHICLE_NUMBER_PLATE_TEXT, veh, p.plate.c_str());
    N(N_SET_VEHICLE_NUMBER_PLATE_TEXT_INDEX, veh, p.plateIndex);
    if (p.livery >= 0) N(N_SET_VEHICLE_LIVERY, veh, p.livery);
    N(N_SET_VEHICLE_ENGINE_HEALTH, veh, p.engineHealth < 300.f ? 300.f : p.engineHealth);
    N(N_SET_VEHICLE_BODY_HEALTH, veh, p.bodyHealth < 300.f ? 300.f : p.bodyHealth);
}

void AddBlip(const std::string &uuid, int veh, const std::string &plate) {
    int b = N<int>(N_ADD_BLIP_FOR_ENTITY, veh);
    N(N_SET_BLIP_SPRITE, b, 225);   // personal vehicle
    N(N_SET_BLIP_COLOUR, b, 3);
    N(N_BEGIN_TEXT_COMMAND_SET_BLIP_NAME, "STRING");
    N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, ("Personal vehicle " + plate).c_str());
    N(N_END_TEXT_COMMAND_SET_BLIP_NAME, b);
    g_blips[uuid] = b;
}

void Forget(const std::string &uuid) {
    auto b = g_blips.find(uuid);
    if (b != g_blips.end()) { N(N_REMOVE_BLIP, &b->second); g_blips.erase(b); }
    g_spawned.erase(uuid);
}

void SendUpdate(const std::string &action, const std::string &uuid, State to, int veh) {
    VehicleUpdateMsg m;
    m.requestId = g_nextId++;
    m.uuid = uuid;
    m.state = (uint8_t)to;
    if (gta::Exists(veh)) {
        gta::Vec3 at = gta::Pos(veh);
        m.pos = {at.x, at.y, at.z};
        m.heading = gta::Heading(veh);
        m.engine = N<float>(N_GET_VEHICLE_ENGINE_HEALTH, veh);
        m.body = N<float>(N_GET_VEHICLE_BODY_HEALTH, veh);
    } else {
        gta::Vec3 me = gta::Pos(gta::PlayerPed());
        m.pos = {me.x, me.y, me.z};
    }
    g_pending[m.requestId] = {action, uuid, veh};
    wire::Writer w;
    Put(w, m);
    net::SendEvent(w);
}

void RequestList() {
    g_listFresh = false;
    wire::Writer w;
    PutVehicleListRequest(w);
    net::SendEvent(w);
}

int SpawnNear(const Personal &p) {
    if (!gta::LoadModel(p.model, 5000)) { gta::Notify("~r~Vehicle model not available."); return 0; }
    int me = gta::PlayerPed();
    gta::Vec3 at = gta::Pos(me);
    NVec3 node{};
    float heading = gta::Heading(me);
    gta::Vec3 spot{at.x, at.y, at.z};
    if (NB(N<int>(N_GET_CLOSEST_VEHICLE_NODE_WITH_HEADING, at.x, at.y, at.z, &node, &heading, 1, 3.f, 0.f))) {
        float dx = node.x - at.x, dy = node.y - at.y;
        if (dx * dx + dy * dy < 60.f * 60.f) spot = {node.x, node.y, node.z};
    }
    if (spot.x == at.x && spot.y == at.y) {
        float h = gta::Heading(me) * 3.14159265f / 180.f;
        spot = {at.x - std::sin(h) * 5.f, at.y + std::cos(h) * 5.f, at.z};
    }
    int veh = N<int>(N_CREATE_VEHICLE, p.model, spot.x, spot.y, spot.z, heading, 0, 1, 0);
    gta::ReleaseModel(p.model);
    if (!veh) return 0;
    gta::MakeMission(veh);
    N(N_SET_VEHICLE_ON_GROUND_PROPERLY, veh, 5.f);
    N(N_SET_VEHICLE_HAS_BEEN_OWNED_BY_PLAYER, veh, 1);
    Apply(veh, p);
    return veh;
}

const Personal *Find(const std::string &uuid) {
    for (const auto &p : g_list)
        if (p.uuid == uuid) return &p;
    return nullptr;
}

void Register(int veh) {
    for (const auto &kv : g_spawned)
        if (kv.second == veh) { gta::Notify("This vehicle is already yours."); return; }
    Personal p = Capture(veh);
    VehicleRegisterMsg m{g_nextId++, json::Write(vehicle::ToJson(p), 0)};
    g_pending[m.requestId] = {"register", "", veh};
    wire::Writer w;
    Put(w, m);
    net::SendEvent(w);
    LOGI("garage", "-", "registering 0x%08X plate %s", p.model, p.plate.c_str());
}

}  // namespace

void OnList(const VehicleListMsg &m) {
    g_list.clear();
    for (const auto &s : m.vehicles) {
        json::Value v;
        std::string err;
        Personal p;
        if (json::Parse(s, v, err) && vehicle::FromJson(v, p, err)) g_list.push_back(p);
    }
    g_listFresh = true;
    LOGI("garage", "-", "%d personal vehicle(s)", (int)g_list.size());
}

void OnResult(const VehicleResultMsg &m) {
    auto it = g_pending.find(m.requestId);
    if (it == g_pending.end()) return;
    Pending p = it->second;
    g_pending.erase(it);
    if (!m.ok) {
        gta::Notify("~r~Vehicle:~s~ " + m.reason);
        LOGW("garage", "-", "%s refused: %s", p.action.c_str(), m.reason.c_str());
        return;
    }
    if (p.action == "register" && gta::Exists(p.veh)) {
        g_spawned[m.uuid] = p.veh;
        gta::MakeMission(p.veh);
        AddBlip(m.uuid, p.veh, N<const char *>(N_GET_VEHICLE_NUMBER_PLATE_TEXT, p.veh));
        gta::Notify("~g~Vehicle registered~s~ as yours. It will be in your garage.");
    } else if (p.action == "callout") {
        const Personal *pv = Find(m.uuid);
        if (pv) {
            int veh = SpawnNear(*pv);
            if (veh) { g_spawned[m.uuid] = veh; AddBlip(m.uuid, veh, pv->plate); gta::Notify("Your ~b~" + pv->plate + "~s~ has been delivered nearby."); }
        }
    } else if (p.action == "store") {
        if (gta::Exists(p.veh)) { int v = p.veh; gta::MakeMission(v); N(N_DELETE_VEHICLE, &v); }
        Forget(m.uuid);
        gta::Notify("Vehicle stored in your garage.");
    } else if (p.action == "destroyed") {
        Forget(m.uuid);
        gta::Notify("~r~Your personal vehicle was destroyed.~s~ Claim it from the insurance (F10 > My vehicles).");
    } else if (p.action == "claim") {
        gta::Notify("~g~Insurance paid out:~s~ the vehicle is back in your garage.");
    }
    LOGI("garage", "-", "%s ok (%s)", p.action.c_str(), m.uuid.c_str());
    if (p.action != "update") RequestList();
}

void Frame() {
    unsigned long long now = GetTickCount64();
    int me = gta::PlayerPed();
    int cur = gta::InVehicle(me) ? N<int>(N_GET_VEHICLE_PED_IS_IN, me, 0) : 0;
    for (auto it = g_spawned.begin(); it != g_spawned.end();) {
        std::string uuid = it->first;
        int veh = it->second;
        ++it;
        if (!gta::Exists(veh)) { g_spawned.erase(uuid); SendUpdate("store", uuid, State::Stored, 0); continue; }   // despawned: back to the garage
        if (gta::Dead(veh) || !NB(N<int>(N_IS_VEHICLE_DRIVEABLE, veh, 0))) { SendUpdate("destroyed", uuid, State::Destroyed, veh); g_spawned.erase(uuid); continue; }
        if (veh == cur && now - g_lastPosUpdate > 30000) { g_lastPosUpdate = now; SendUpdate("update", uuid, State::Active, veh); }
    }
}

void StoreAll() {
    for (auto kv : std::map<std::string, int>(g_spawned)) {
        SendUpdate("store", kv.first, State::Stored, kv.second);   // position/health captured here
        int v = kv.second;
        if (gta::Exists(v)) { gta::MakeMission(v); N(N_DELETE_VEHICLE, &v); }
        Forget(kv.first);
    }
}

void Menu() {
    RequestList();
    for (int i = 0; i < 100 && !g_listFresh; ++i) gta::Wait(20);
    ui::Menu m;
    m.title = "MY VEHICLES";
    bool close = false;
    auto build = [&] {
        m.items.clear();
        m.subtitle = std::to_string(g_list.size()) + " / " + std::to_string(vehicle::kMaxPerCharacter);
        int me = gta::PlayerPed();
        if (gta::InVehicle(me) && N<int>(N_GET_PED_IN_VEHICLE_SEAT, N<int>(N_GET_VEHICLE_PED_IS_IN, me, 0), -1, 0) == me)
            m.items.push_back({"Make this vehicle mine", nullptr, nullptr, [&] { Register(N<int>(N_GET_VEHICLE_PED_IS_IN, gta::PlayerPed(), 0)); close = true; },
                               "Registers the car you are driving, with its paint and mods."});
        for (const auto &p : g_list) {
            std::string uuid = p.uuid;
            bool out = g_spawned.count(uuid) != 0;
            std::string state = out ? "out" : vehicle::StateName(p.state);
            std::string label = p.plate;
            if (p.state == State::Destroyed) {
                m.items.push_back({label, [state] { return std::string("destroyed"); }, nullptr, [&, uuid] { g_pending[g_nextId] = {"claim", uuid, 0}; VehicleUpdateMsg u; u.requestId = g_nextId++; u.uuid = uuid; u.state = (uint8_t)State::Stored;
                    wire::Writer w; Put(w, u); net::SendEvent(w); close = true; }, "Insurance claim: $1,000, the car returns to the garage repaired."});
            } else if (out) {
                m.items.push_back({label, [] { return std::string("out - store it"); }, nullptr, [&, uuid] { SendUpdate("store", uuid, State::Stored, g_spawned[uuid]); close = true; }, "Sends it back to the garage."});
            } else if (p.state == State::Stored || p.state == State::Active) {
                m.items.push_back({label, [] { return std::string("in garage - call it"); }, nullptr, [&, uuid] { SendUpdate("callout", uuid, State::Active, 0); close = true; }, "Delivered to the nearest road."});
            } else {
                m.items.push_back({label, [state] { return state; }, nullptr, nullptr, ""});
            }
        }
        m.items.push_back({"Close", nullptr, nullptr, [&] { close = true; }, ""});
    };
    build();
    while (!close) {
        std::string line;
        if (devcmd::NextFor("ui back", line)) {
            if (line.rfind("ui ", 0) == 0) m.Activate(line.substr(3));
            else close = true;
        }
        if (!m.Frame()) close = true;
        gta::Wait(0);
    }
}

bool HandleCommand(const std::string &verb, std::istream &in) {
    if (verb == "garage.list") { RequestList(); return true; }
    if (verb == "garage.register") {
        int me = gta::PlayerPed();
        if (gta::InVehicle(me)) Register(N<int>(N_GET_VEHICLE_PED_IS_IN, me, 0));
        return true;
    }
    if (verb == "garage.callout" || verb == "garage.store" || verb == "garage.claim") {
        size_t idx = 0;
        in >> idx;
        if (idx >= g_list.size()) { LOGW("garage", "-", "%s: no vehicle #%d (have %d)", verb.c_str(), (int)idx, (int)g_list.size()); return true; }
        const std::string uuid = g_list[idx].uuid;
        if (verb == "garage.callout") SendUpdate("callout", uuid, State::Active, 0);
        else if (verb == "garage.store") SendUpdate("store", uuid, State::Stored, g_spawned.count(uuid) ? g_spawned[uuid] : 0);
        else { g_pending[g_nextId] = {"claim", uuid, 0}; VehicleUpdateMsg u; u.requestId = g_nextId++; u.uuid = uuid; u.state = (uint8_t)State::Stored; wire::Writer w; Put(w, u); net::SendEvent(w); }
        return true;
    }
    if (verb == "garage.dump") {
        for (size_t i = 0; i < g_list.size(); ++i)
            LOGI("garage", "-", "#%d %s 0x%08X %s %s", (int)i, g_list[i].plate.c_str(), g_list[i].model, vehicle::StateName(g_list[i].state), g_spawned.count(g_list[i].uuid) ? "(out)" : "");
        return true;
    }
    return false;
}

}  // namespace garage
