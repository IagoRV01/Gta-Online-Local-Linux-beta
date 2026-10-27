#include "game/remote.h"
#include <windows.h>
#include <cmath>
#include <map>
#include <string>
#include "common/character.h"
#include "common/interp.h"
#include "common/json.h"
#include "common/log.h"
#include "game/godeye.h"
#include "game/camera_client.h"
#include "engine/gta.h"
#include "engine/shv.h"
#include "game/appearance.h"
#include "game/combat.h"
#include "game/economy.h"
#include "game/jobs.h"
#include "game/garage.h"
#include "game/world_sync.h"
#include "game/net_client.h"

namespace remote {

using namespace proto;

int VehicleOfDriver(uint16_t driverNetId);   // defined below, outside the anonymous namespace

namespace {

constexpr double kDelay = 0.10;          // render this far in the past (2 snapshots at 20 Hz)
constexpr float kSnapDistance = 6.f;     // farther than this from the target: teleport
constexpr int kTaskEveryMs = 150;
constexpr int kPuppetHealth = 1000;      // puppets never die from local hits: damage is measured and reported

struct Player {
    uint16_t netId = 0;
    std::string name;
    bool hasLook = false;
    character::Character look;
    bool lookDirty = false;
    interp::Buffer buf;
    int ped = 0, blip = 0;
    int tag = -1;                     // GTA Online gamer tag (CREATE_FAKE_MP_GAMER_TAG), -1 = drawn fallback
    bool tagShown = false;
    int tagWanted = -1;
    int lastTaskMs = 0;
    uint8_t lastMove = 255;
    bool wasStale = false;
    // Phase 5: the vehicle this player drives (owned by them: only their client moves it)
    int veh = 0;
    uint32_t vehModel = 0;
    VehicleBlock lastVeh;            // newest discrete vehicle info (seat, driver, colours, flags)
    uint8_t appliedVFlags = 0xFF;
    unsigned long long vehIdleSince = 0;   // when the driver stopped using it (0 = in use)
    unsigned long long leavingSince = 0;   // exit animation started
    int lastHornMs = 0;
    // Phase 6
    uint32_t weapon = 0;              // weapon currently given to the puppet
    Vec3 aim;                         // newest aim point
    bool shownDead = false;
    uint8_t wanted = 0, shownWanted = 0;   // Phase 7: stars on the name tag, flashing red blip
    uint16_t rank = 0;                // v11
    uint8_t infoFlags = 0;
    uint32_t session = 0;
    unsigned long long highlightUntil = 0;
    bool shownHighlight = false;
};

std::map<uint16_t, Player> g_players;
int g_specCam = 0;
uint16_t g_specId = 0;
int g_lastMetric = 0;
interp::Clock g_clock;
uint16_t g_self = 0;
uint16_t Self() { return g_self ? g_self : net::SelfNetId(); }   // SetSelf is optional: the connection knows our netId
int g_myRank = 0, g_myFlags = 0;
uint32_t g_mySession = 0;
bool g_allSessions = false;          // a camera client sees the players of every session
uint16_t g_infoSentAs = 0;           // the netId our info was last sent under (0 = send again)
uint32_t g_infoSentLogin = 0;        // ...and the login: after a quick server restart the netId can be the same
int g_infoSentRank = -1, g_infoSentFlags = -1;

double LocalSeconds() { return (double)GetTickCount64() / 1000.0; }

void DeleteVehicle(Player &p) {
    if (gta::Exists(p.veh)) { gta::MakeMission(p.veh); N(N_DELETE_VEHICLE, &p.veh); }
    p.veh = 0;
    p.vehModel = 0;
    p.appliedVFlags = 0xFF;
}

void DeletePuppet(Player &p) {
    if (p.tag >= 0) { N(N_REMOVE_MP_GAMER_TAG, p.tag); p.tag = -1; p.tagShown = false; p.tagWanted = -1; }
    if (p.blip) { N(N_REMOVE_BLIP, &p.blip); p.blip = 0; }
    if (gta::Exists(p.ped)) { gta::MakeMission(p.ped); N(N_DELETE_PED, &p.ped); }
    p.ped = 0;
    DeleteVehicle(p);
}

float YawFromQuat(const interp::Quat &q) {   // GTA heading in degrees
    float yaw = std::atan2(2.f * (q.w * q.z + q.x * q.y), 1.f - 2.f * (q.y * q.y + q.z * q.z)) * 57.29578f;
    return yaw < 0 ? yaw + 360.f : yaw;
}

void OnMessage(MsgType type, wire::Reader &r) {
    switch (type) {
        case MsgType::PlayerJoined: {
            PlayerJoinedMsg m;
            if (!Get(r, m) || m.netId == g_self) return;
            Player &p = g_players[m.netId];
            p.netId = m.netId;
            p.name = m.name;
            LOGI("remote", m.name.c_str(), "joined (netId %u)", m.netId);
            gta::Notify("~b~" + m.name + "~s~ joined Local Online.");
            break;
        }
        case MsgType::PlayerLeft: {
            PlayerLeftMsg m;
            if (!Get(r, m)) return;
            auto it = g_players.find(m.netId);
            if (it == g_players.end()) return;
            LOGI("remote", it->second.name.c_str(), "left (netId %u)", m.netId);
            gta::Notify("~b~" + it->second.name + "~s~ left.");
            DeletePuppet(it->second);
            g_players.erase(it);
            break;
        }
        case MsgType::PlayerInfoRelay: {
            PlayerInfoRelayMsg m;
            if (!Get(r, m)) return;
            if (m.netId == Self()) {
                if (m.session != g_mySession) LOGI("remote", "-", "our session: %u -> %u", g_mySession, m.session);
                g_mySession = m.session;
                return;
            }
            Player &p = g_players[m.netId];
            p.netId = m.netId;
            p.rank = m.rank; p.infoFlags = m.flags; p.session = m.session;
            break;
        }
        case MsgType::PlayerAppearance: {
            PlayerAppearanceMsg m;
            if (!Get(r, m) || m.netId == g_self) return;
            json::Value v;
            std::string err;
            character::Character c;
            if (!json::Parse(m.json, v, err) || !character::FromJson(v, c, err)) { LOGW("remote", "-", "bad appearance for netId %u: %s", m.netId, err.c_str()); return; }
            Player &p = g_players[m.netId];
            p.netId = m.netId;
            bool genderChanged = p.hasLook && p.look.female != c.female;
            p.look = c;
            p.hasLook = true;
            p.lookDirty = true;
            if (genderChanged) DeletePuppet(p);   // respawned with the right model next frame
            break;
        }
        case MsgType::Snapshot: {
            uint32_t tick, serverMs;
            std::vector<RemoteState> states;
            if (!GetSnapshot(r, tick, serverMs, states)) return;
            double st = serverMs / 1000.0;
            g_clock.OnSnapshot(st, LocalSeconds());
            for (const auto &s : states) {
                if (s.netId == g_self) continue;
                Player &p = g_players[s.netId];
                p.netId = s.netId;
                interp::Sample smp;
                smp.t = st;
                smp.pos = {s.pos.x, s.pos.y, s.pos.z};
                smp.vel = {s.vel.x, s.vel.y, s.vel.z};
                smp.heading = s.heading;
                smp.move = (uint8_t)s.move;
                smp.flags = s.flags;
                p.aim = s.aim;
                p.wanted = s.wanted;
                if (s.weapon != p.weapon && gta::Exists(p.ped)) {
                    uint32_t unarmed = gta::Joaat("WEAPON_UNARMED");
                    if (s.weapon && s.weapon != unarmed) {
                        N(N_GIVE_WEAPON_TO_PED, p.ped, s.weapon, 9999, 0, 1);
                        N(N_SET_PED_INFINITE_AMMO_CLIP, p.ped, 1);
                    }
                    N(N_SET_CURRENT_PED_WEAPON, p.ped, s.weapon ? s.weapon : unarmed, 1);
                    p.weapon = s.weapon;
                }
                if (s.flags & kFlagInVehicle) {   // in a vehicle the vehicle's transform is what moves
                    smp.pos = {s.veh.pos.x, s.veh.pos.y, s.veh.pos.z};
                    smp.vel = {s.veh.vel.x, s.veh.vel.y, s.veh.vel.z};
                    smp.rot = {s.veh.rot.x, s.veh.rot.y, s.veh.rot.z, s.veh.rot.w};
                    p.lastVeh = s.veh;
                }
                p.buf.Push(smp);
            }
            break;
        }
        case MsgType::ShotRelay: {
            ShotRelayMsg m;
            if (!Get(r, m)) return;
            auto it = g_players.find(m.shooter);
            int owner = it != g_players.end() && gta::Exists(it->second.ped) ? it->second.ped : 0;
            Vec3 o = m.origin;
            if (owner) {   // start the tracer at the puppet's own gun when it is close to the reported origin
                int wobj = N<int>(N_GET_CURRENT_PED_WEAPON_ENTITY_INDEX, owner, 0);
                if (gta::Exists(wobj)) {
                    gta::Vec3 g = gta::Pos(wobj);
                    float dx = g.x - o.x, dy = g.y - o.y, dz = g.z - o.z;
                    if (dx * dx + dy * dy + dz * dz < 9.f) o = {g.x, g.y, g.z};
                }
            }
            if (!NB(N<int>(N_HAS_WEAPON_ASSET_LOADED, m.weapon))) N(N_REQUEST_WEAPON_ASSET, m.weapon, 31, 0);
            // damage 0: the visual and sound only; real damage goes through Damage/DamageApply
            N(N_SHOOT_SINGLE_BULLET_BETWEEN_COORDS_IGNORE_ENTITY, o.x, o.y, o.z, m.target.x, m.target.y, m.target.z, 0, 1, m.weapon, owner, 1, 0, -1.f, owner, 0);
            break;
        }
        case MsgType::DamageApply: {
            DamageApplyMsg m;
            if (Get(r, m)) combat::OnDamageApply(m);
            break;
        }
        case MsgType::VehicleList: {
            VehicleListMsg m;
            if (Get(r, m)) garage::OnList(m);
            break;
        }
        case MsgType::VehicleResult: {
            VehicleResultMsg m;
            if (Get(r, m)) garage::OnResult(m);
            break;
        }
        case MsgType::MoneyResult: {
            MoneyResultMsg m;
            if (Get(r, m)) economy::OnResult(m);
            break;
        }
        case MsgType::JobList: {
            JobListMsg m;
            if (Get(r, m)) jobs::OnList(m);
            break;
        }
        case MsgType::JobState: {
            JobStateMsg m;
            if (Get(r, m)) jobs::OnState(m);
            break;
        }
        case MsgType::RaceStandings: {
            RaceStandingsMsg m;
            if (Get(r, m)) jobs::OnStandings(m);
            break;
        }
        case MsgType::PlayerMessageRelay: {   // v14: for Gori Online (the apartment buzzer...)
            PlayerMessageRelayMsg m;
            if (Get(r, m)) jobs::OnPlayerMessage(m);
            break;
        }
        case MsgType::Ledger: {
            LedgerMsg m;
            if (Get(r, m)) economy::OnLedger(m);
            break;
        }
        case MsgType::WorldState: {
            WorldStateMsg m;
            if (Get(r, m)) world_sync::OnWorldState(m);
            break;
        }
        case MsgType::CameraSet: {
            CameraSetMsg m;
            if (Get(r, m)) camera_client::OnCameraSet(m);
            break;
        }
        case MsgType::GodEyeAction: {
            GodEyeActionMsg m;
            if (Get(r, m)) godeye::OnAction(m);
            break;
        }
        case MsgType::KillFeed: {
            KillFeedMsg m;
            if (Get(r, m)) combat::OnKillFeed(m);
            break;
        }
        default: break;
    }
}

void Spawn(Player &p, const interp::Sample &s) {
    character::Character look = p.hasLook ? p.look : character::MakeDefault(false);
    uint32_t model = character::ModelHash(look);
    if (!gta::LoadModel(model)) return;
    p.ped = N<int>(N_CREATE_PED, look.female ? 5 : 4, model, s.pos.x, s.pos.y, s.pos.z - 1.f, s.heading, 0, 1);
    gta::ReleaseModel(model);
    if (!p.ped) return;
    gta::MakeMission(p.ped);
    N(N_SET_BLOCKING_OF_NON_TEMPORARY_EVENTS, p.ped, 1);
    N(N_SET_PED_FLEE_ATTRIBUTES, p.ped, 0, 0);
    N(N_SET_PED_CAN_BE_TARGETTED, p.ped, 1);
    N(N_SET_PED_CAN_RAGDOLL, p.ped, 0);
    // Phase 6: hits on the puppet are measured (health drop) and reported, never applied locally
    N(N_SET_PED_MAX_HEALTH, p.ped, kPuppetHealth);
    N(N_SET_ENTITY_HEALTH, p.ped, kPuppetHealth, 0, 0);
    N(N_SET_PED_DIES_WHEN_INJURED, p.ped, 0);
    N(N_SET_PED_SUFFERS_CRITICAL_HITS, p.ped, 1);
    p.weapon = 0;
    N(N_SET_PED_KEEP_TASK, p.ped, 1);
    appearance::ApplyLook(p.ped, look);
    p.lookDirty = false;
    p.blip = N<int>(N_ADD_BLIP_FOR_ENTITY, p.ped);
    N(N_SET_BLIP_SPRITE, p.blip, 1);
    N(N_SET_BLIP_COLOUR, p.blip, 0);              // other players are white with a heading arrow, like Online
    N(N_SHOW_HEADING_INDICATOR_ON_BLIP, p.blip, 1);
    N(N_SET_BLIP_CATEGORY, p.blip, 7);            // "Other Players" in the map legend
    N(N_SET_BLIP_SCALE, p.blip, 0.85f);
    N(N_BEGIN_TEXT_COMMAND_SET_BLIP_NAME, "STRING");
    N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, (p.name.empty() ? std::string("Player") : p.name).c_str());
    N(N_END_TEXT_COMMAND_SET_BLIP_NAME, p.blip);
    p.tag = N<int>(N_CREATE_FAKE_MP_GAMER_TAG, p.ped, (p.name.empty() ? std::string("Player") : p.name).c_str(), 0, 0, "", 0);
    if (p.tag >= 0) {
        N(N_SET_MP_GAMER_TAG_VISIBILITY, p.tag, 0, 1, 0);    // MP_TAG_GAMER_NAME
        N(N_SET_MP_GAMER_TAG_COLOUR, p.tag, 0, 0);           // HUD_COLOUR_PURE_WHITE
        N(N_SET_MP_GAMER_TAG_ALPHA, p.tag, 0, 255);
        p.tagShown = true;
    }
    LOGI("remote", p.name.c_str(), "puppet spawned (ped %d) at %.1f %.1f %.1f", p.ped, s.pos.x, s.pos.y, s.pos.z);
}

float MoveSpeed(uint8_t move) {
    switch ((Move)move) {
        case Move::Walk: return 1.0f;
        case Move::Run: return 2.0f;
        case Move::Sprint: return 3.0f;
        default: return 0.f;
    }
}

void Drive(Player &p, const interp::Result &r) {
    const interp::Sample &s = r.s;
    gta::Vec3 cur = gta::Pos(p.ped);
    float dx = s.pos.x - cur.x, dy = s.pos.y - cur.y, dz = s.pos.z - cur.z;
    float err = std::sqrt(dx * dx + dy * dy + dz * dz);
    int now = gta::GameTimeMs();
    if (err > kSnapDistance) {
        N(N_SET_ENTITY_COORDS_NO_OFFSET, p.ped, s.pos.x, s.pos.y, s.pos.z, 0, 0, 0);   // same reference point as GET_ENTITY_COORDS
        gta::SetHeading(p.ped, s.heading);
        p.lastTaskMs = 0;
        return;
    }
    float speed = MoveSpeed(s.move);
    if (s.flags & (kFlagAiming | kFlagShooting)) {   // aim where the owner aims (walking or standing)
        if (now - p.lastTaskMs >= 250) {
            if (speed > 0.f && !r.stale)
                N(N_TASK_GO_TO_COORD_WHILE_AIMING_AT_COORD, p.ped, s.pos.x + s.vel.x * 0.5f, s.pos.y + s.vel.y * 0.5f, s.pos.z, p.aim.x, p.aim.y, p.aim.z,
                  speed > 1.f ? 1.f : speed, 0, 0.5f, 0.f, 0, 0, 1, gta::Joaat("FIRING_PATTERN_FULL_AUTO"));
            else N(N_TASK_AIM_GUN_AT_COORD, p.ped, p.aim.x, p.aim.y, p.aim.z, -1, 1, 0);
            p.lastTaskMs = now;
            p.lastMove = 254;   // force a fresh movement task when aiming stops
        }
        return;
    }
    if (speed <= 0.f || r.stale) {
        if (p.lastMove != 0 || now - p.lastTaskMs > 1000) {
            if (err > 0.6f) N(N_TASK_GO_STRAIGHT_TO_COORD, p.ped, s.pos.x, s.pos.y, s.pos.z, 1.0f, 2000, s.heading, 0.2f);
            else N(N_TASK_ACHIEVE_HEADING, p.ped, s.heading, 1000);
            p.lastTaskMs = now;
            p.lastMove = 0;
        }
        return;
    }
    // Catch-up: when the puppet falls behind (its animation speed is not exactly the sender's
    // speed), go one gait faster and speed the animation up a little; ease back when close.
    float along = dx * s.vel.x + dy * s.vel.y;   // > 0: target is ahead of us along the motion
    float boost = 1.f;
    if (err > 1.0f && along > 0) { speed = speed < 3.f ? speed + 1.f : 3.f; boost = err > 2.5f ? 1.25f : 1.12f; }
    else if (err > 1.0f && along < 0) boost = 0.85f;   // ahead of the target: slow down a little
    N(N_SET_PED_MOVE_RATE_OVERRIDE, p.ped, boost);
    if (now - p.lastTaskMs >= kTaskEveryMs || p.lastMove != s.move) {
        // aim slightly ahead along the velocity so the ped keeps moving between task updates
        float lead = 0.5f;
        float tx = s.pos.x + s.vel.x * lead, ty = s.pos.y + s.vel.y * lead, tz = s.pos.z + s.vel.z * lead;
        N(N_TASK_GO_STRAIGHT_TO_COORD, p.ped, tx, ty, tz, speed, 2000, s.heading, 0.f);
        p.lastTaskMs = now;
        p.lastMove = s.move;
    }
}

// ---------------------------------------------------------------- vehicles (Phase 5)

void SpawnVehicle(Player &p, const interp::Sample &s) {
    const VehicleBlock &vb = p.lastVeh;
    if (!gta::LoadModel(vb.model, 3000)) { LOGW("remote", p.name.c_str(), "vehicle model 0x%08X not available", vb.model); return; }
    p.veh = N<int>(N_CREATE_VEHICLE, vb.model, s.pos.x, s.pos.y, s.pos.z, YawFromQuat(s.rot), 0, 1, 0);
    gta::ReleaseModel(vb.model);
    if (!p.veh) return;
    p.vehModel = vb.model;
    gta::MakeMission(p.veh);
    N(N_SET_ENTITY_QUATERNION, p.veh, s.rot.x, s.rot.y, s.rot.z, s.rot.w);
    N(N_SET_VEHICLE_COLOURS, p.veh, (int)vb.color1, (int)vb.color2);
    N(N_SET_VEHICLE_DOORS_LOCKED, p.veh, 2);            // only its owner drives it
    N(N_SET_VEHICLE_HAS_BEEN_OWNED_BY_PLAYER, p.veh, 0);
    N(N_SET_VEHICLE_NUMBER_PLATE_TEXT, p.veh, p.name.substr(0, 8).c_str());
    gta::SetInvincible(p.veh, true);                     // vehicle damage sync comes later
    LOGI("remote", p.name.c_str(), "vehicle 0x%08X spawned (veh %d)", vb.model, p.veh);
}

void DriveVehicle(Player &p, const interp::Result &r) {
    const interp::Sample &s = r.s;
    const VehicleBlock &vb = p.lastVeh;
    gta::Vec3 cur = gta::Pos(p.veh);
    float ex = s.pos.x - cur.x, ey = s.pos.y - cur.y, ez = s.pos.z - cur.z;
    float err = std::sqrt(ex * ex + ey * ey + ez * ez);
    if (err > kSnapDistance) {
        N(N_SET_ENTITY_COORDS_NO_OFFSET, p.veh, s.pos.x, s.pos.y, s.pos.z, 0, 0, 0);
        ex = ey = ez = 0;
    }
    // velocity from the owner plus a proportional pull towards where it should be (~0.3 s)
    const float k = 3.f;
    N(N_SET_ENTITY_VELOCITY, p.veh, s.vel.x + ex * k, s.vel.y + ey * k, s.vel.z + ez * k);
    N(N_SET_ENTITY_QUATERNION, p.veh, s.rot.x, s.rot.y, s.rot.z, s.rot.w);
    float speed = std::sqrt(s.vel.x * s.vel.x + s.vel.y * s.vel.y);
    float steer = speed > 1.f ? vb.angVel.z * 3.f / speed : 0.f;   // cosmetic front-wheel angle
    N(N_SET_VEHICLE_STEER_BIAS, p.veh, steer > 1.f ? 1.f : steer < -1.f ? -1.f : steer);
    if (vb.vflags != p.appliedVFlags) {
        N(N_SET_VEHICLE_ENGINE_ON, p.veh, (vb.vflags & kVehEngine) ? 1 : 0, 1, 1);
        N(N_SET_VEHICLE_LIGHTS, p.veh, (vb.vflags & kVehLights) ? 2 : 1);
        N(N_SET_VEHICLE_SIREN, p.veh, (vb.vflags & kVehSiren) ? 1 : 0);
        p.appliedVFlags = vb.vflags;
    }
    int now = gta::GameTimeMs();
    if ((vb.vflags & kVehHorn) && now - p.lastHornMs > 120) {
        N(N_START_VEHICLE_HORN, p.veh, 150, gta::Joaat("HELDDOWN"), 0);
        p.lastHornMs = now;
    }
}

void VehicleFrame(Player &p, const interp::Result &r) {
    const VehicleBlock &vb = p.lastVeh;
    int veh = 0;
    if (vb.seat == -1) {
        if (p.veh && (!gta::Exists(p.veh) || p.vehModel != vb.model)) DeleteVehicle(p);
        if (!p.veh) SpawnVehicle(p, r.s);
        if (!p.veh) return;
        veh = p.veh;
        DriveVehicle(p, r);
    } else {
        veh = VehicleOfDriver(vb.driverNetId);
    }
    p.vehIdleSince = 0;
    p.leavingSince = 0;
    if (veh && !NB(N<int>(N_IS_PED_IN_VEHICLE, p.ped, veh, 0))) {
        N(N_SET_PED_INTO_VEHICLE, p.ped, veh, (int)vb.seat);
        LOGI("remote", p.name.c_str(), "in vehicle %d seat %d", veh, (int)vb.seat);
    }
}

// Returns true while the puppet is still getting out (on-foot driving must wait).
bool LeaveVehicleIfNeeded(Player &p) {
    if (!NB(N<int>(N_IS_PED_IN_ANY_VEHICLE, p.ped, 0))) return false;
    unsigned long long now = GetTickCount64();
    if (!p.leavingSince) {
        p.leavingSince = now;
        N(N_TASK_LEAVE_VEHICLE, p.ped, N<int>(N_GET_VEHICLE_PED_IS_IN, p.ped, 0), 0);
        if (p.veh) p.vehIdleSince = now;
        LOGI("remote", p.name.c_str(), "leaving the vehicle");
        return true;
    }
    if (now - p.leavingSince > 3000) {   // animation blocked: warp out
        gta::Vec3 at = gta::Pos(p.ped);
        N(N_SET_ENTITY_COORDS_NO_OFFSET, p.ped, at.x + 1.5f, at.y, at.z + 0.5f, 0, 0, 0);
        p.leavingSince = 0;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- combat (Phase 6)

// Returns true while the player is dead (puppet lies dead, nothing else to do).
bool PuppetCombat(Player &p, const interp::Result &r) {
    bool dead = (r.s.flags & kFlagDead) != 0;
    if (dead) {
        if (!p.shownDead) {
            p.shownDead = true;
            N(N_SET_ENTITY_HEALTH, p.ped, 0, 0, 0);
            LOGI("remote", p.name.c_str(), "died");
        }
        return true;
    }
    if (p.shownDead || gta::Dead(p.ped)) {   // respawned (or died locally from something huge): bring it back
        p.shownDead = false;
        N(N_RESURRECT_PED, p.ped);
        N(N_SET_ENTITY_HEALTH, p.ped, kPuppetHealth, 0, 0);
        N(N_CLEAR_PED_TASKS_IMMEDIATELY, p.ped);
        N(N_SET_ENTITY_COORDS_NO_OFFSET, p.ped, r.s.pos.x, r.s.pos.y, r.s.pos.z, 0, 0, 0);
        p.lastTaskMs = 0;
        p.weapon = 0;
        LOGI("remote", p.name.c_str(), "respawned");
        return false;
    }
    // hits we caused: measure, report to the server, undo locally
    int hp = gta::Health(p.ped);
    if (hp < kPuppetHealth) {
        int me = gta::PlayerPed();
        int myVeh = gta::InVehicle(me) ? N<int>(N_GET_VEHICLE_PED_IS_IN, me, 0) : 0;
        bool mine = NB(N<int>(N_HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY, p.ped, me, 1)) || (myVeh && NB(N<int>(N_HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY, p.ped, myVeh, 1)));
        if (mine) {
            int measured = kPuppetHealth - hp;
            int bone = 0;
            N(N_GET_PED_LAST_DAMAGE_BONE, p.ped, &bone);
            DamageMsg d;
            d.target = p.netId;
            d.weapon = combat::AttackWeapon(me);
            if (myVeh && NB(N<int>(N_HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY, p.ped, myVeh, 1))) d.weapon = gta::Joaat("WEAPON_RUN_OVER_BY_CAR");
            float base = d.weapon == gta::Joaat("WEAPON_RUN_OVER_BY_CAR") ? 0.f : N<float>(N_GET_WEAPON_DAMAGE, d.weapon, 0);
            d.bone = (uint16_t)bone;
            d.amount = PvpDamage(base, d.bone, measured);
            int dmg = d.amount;
            wire::Writer w;
            Put(w, d);
            net::SendEvent(w);
            LOGI("remote", p.name.c_str(), "we hit it: %d pvp damage (base %.0f, bone %d, local drop %d)", dmg, base, bone, measured);
        }
        N(N_SET_ENTITY_HEALTH, p.ped, kPuppetHealth, 0, 0);
        N(N_CLEAR_ENTITY_LAST_DAMAGE_ENTITY, p.ped);
    }
    return false;
}

void WantedBlip(Player &p) {
    bool hl = p.highlightUntil > GetTickCount64();
    if (p.blip && hl != p.shownHighlight) {   // Highlight Player: a flashing, bigger, blue blip
        p.shownHighlight = hl;
        N(N_SET_BLIP_SCALE, p.blip, hl ? 1.3f : 1.0f);
        N(N_SET_BLIP_FLASHES, p.blip, hl ? 1 : 0);
        N(N_SET_BLIP_COLOUR, p.blip, hl ? 3 : p.wanted ? 1 : 0);
        if (!hl) p.shownWanted = 255;
    }
    if (hl) return;
    if (!p.blip || p.wanted == p.shownWanted) return;
    N(N_SET_BLIP_COLOUR, p.blip, p.wanted ? 1 : 0);   // red while wanted, white otherwise
    N(N_SET_BLIP_FLASHES, p.blip, p.wanted ? 1 : 0);
    p.shownWanted = p.wanted;
}

void NameTag(Player &p) {
    gta::Vec3 me = gta::Pos(gta::PlayerPed()), at = gta::Pos(p.ped);
    float d = std::sqrt((me.x - at.x) * (me.x - at.x) + (me.y - at.y) * (me.y - at.y) + (me.z - at.z) * (me.z - at.z));
    if (p.tag >= 0 && NB(N<int>(N_IS_MP_GAMER_TAG_ACTIVE, p.tag))) {   // the game's own Online tag
        bool show = d < 100.f;
        if (show != p.tagShown) { N(N_SET_MP_GAMER_TAG_VISIBILITY, p.tag, 0, show ? 1 : 0, 0); p.tagShown = show; }
        if (p.wanted != p.tagWanted) {
            N(N_SET_MP_GAMER_TAG_WANTED_LEVEL, p.tag, (int)p.wanted);
            N(N_SET_MP_GAMER_TAG_VISIBILITY, p.tag, 7, p.wanted ? 1 : 0, 0);   // MP_TAG_WANTED_STARS
            p.tagWanted = p.wanted;
        }
        return;
    }
    if (!N<int>(N_IS_ENTITY_ON_SCREEN, p.ped)) return;
    if (d > 60.f) return;
    float sx = 0, sy = 0;
    if (!NB(N<int>(N_GET_SCREEN_COORD_FROM_WORLD_COORD, at.x, at.y, at.z + 0.95f, &sx, &sy))) return;
    float scale = 0.42f - d * 0.003f;
    std::string label = p.name.empty() ? "Player" : p.name;
    if (p.wanted) label += "  " + std::string(p.wanted, '*');
    gta::Text(label, sx, sy, scale < 0.25f ? 0.25f : scale, p.wanted ? gta::kYellow : gta::kWhite, 0, 4);
}

}  // namespace

int VehicleOfDriver(uint16_t driverNetId) {
    if (!driverNetId) return 0;
    if (driverNetId == net::SelfNetId()) return gta::InVehicle(gta::PlayerPed()) ? N<int>(N_GET_VEHICLE_PED_IS_IN, gta::PlayerPed(), 0) : 0;
    auto it = g_players.find(driverNetId);
    return it != g_players.end() && gta::Exists(it->second.veh) ? it->second.veh : 0;
}

int PedOfNetId(uint16_t netId) {
    auto it = g_players.find(netId);
    return it != g_players.end() && gta::Exists(it->second.ped) ? it->second.ped : 0;
}
int VehOfNetId(uint16_t netId) {
    auto it = g_players.find(netId);
    return it != g_players.end() && gta::Exists(it->second.veh) ? it->second.veh : 0;
}
void SeeAllSessions(bool on) { g_allSessions = on; }

int PedOf(const std::string &prefix) {
    for (auto &kv : g_players)
        if (gta::Exists(kv.second.ped) && kv.second.name.compare(0, prefix.size(), prefix) == 0) return kv.second.ped;
    return 0;
}

std::string NameOf(uint16_t id) {
    auto it = g_players.find(id);
    if (it != g_players.end() && !it->second.name.empty()) return it->second.name;
    return "player " + std::to_string(id);
}

std::vector<Row> Rows() {
    std::vector<Row> out;
    for (auto &kv : g_players) {
        const Player &p = kv.second;
        if (p.session != g_mySession || kv.first == Self()) continue;
        out.push_back({kv.first, p.name.empty() ? "player " + std::to_string(kv.first) : p.name, p.wanted, p.rank, (p.infoFlags & kInfoPassive) != 0,
                       gta::Exists(p.ped) ? p.ped : 0});
    }
    return out;
}

void SetMyInfo(int rank, int flags) { g_myRank = rank; g_myFlags = flags; }
void Highlight(uint16_t netId, int ms) {
    auto it = g_players.find(netId);
    if (it != g_players.end()) it->second.highlightUntil = GetTickCount64() + (unsigned long long)(ms < 0 ? 0 : ms);
}
int MyRank() { return g_myRank; }
int MyFlags() { return g_myFlags; }
uint32_t MySession() { return g_mySession; }
std::string PlayersText() {
    std::string out;
    for (auto &kv : g_players) {
        const Player &p = kv.second;
        out += std::to_string(kv.first) + "|" + (p.name.empty() ? "player " + std::to_string(kv.first) : p.name) + "|" + std::to_string(p.rank) + "|" +
               std::to_string(p.infoFlags) + "|" + std::to_string(p.session) + (p.session == g_mySession ? "|1" : "|0") + "\n";
    }
    return out;
}

uint16_t NetIdOfPed(int ped) {
    for (auto &kv : g_players)
        if (kv.second.ped == ped) return kv.first;
    return 0;
}

void Init() { net::OnGameMessage(OnMessage); }

bool Spectate(const std::string &prefix) {
    for (auto &kv : g_players)
        if (gta::Exists(kv.second.ped) && kv.second.name.compare(0, prefix.size(), prefix) == 0) {
            g_specId = kv.first;
            LOGI("remote", kv.second.name.c_str(), "spectating");
            return true;
        }
    return false;
}

void StopSpectate() { g_specId = 0; gta::CamDestroy(g_specCam); }

static void SpectatorCamera() {
    auto it = g_players.find(g_specId);
    if (it == g_players.end() || !gta::Exists(it->second.ped)) { StopSpectate(); return; }
    gta::Vec3 at = gta::Pos(it->second.ped);
    float h = gta::Heading(it->second.ped) * 3.14159265f / 180.f;
    gta::Vec3 eye{at.x + std::sin(h) * 4.5f, at.y - std::cos(h) * 4.5f, at.z + 1.6f};   // behind and above
    if (!g_specCam) g_specCam = gta::CamCreate(eye, at, 55.f);
    else gta::CamMove(g_specCam, eye, at, 55.f);
}
void SetSelf(uint16_t id) { g_self = id; }
int Count() { return (int)g_players.size(); }

void Frame() {
    uint16_t self = Self();
    if (self && (g_infoSentAs != self || g_infoSentLogin != net::Logins() || g_infoSentRank != g_myRank || g_infoSentFlags != g_myFlags)) {   // our rank / flags to the server
        wire::Writer w;
        Put(w, PlayerInfoMsg{(uint16_t)(g_myRank < 0 ? 0 : g_myRank > kMaxRank ? kMaxRank : g_myRank), (uint8_t)(g_myFlags & 3)});
        net::SendEvent(w);
        g_infoSentAs = self; g_infoSentLogin = net::Logins(); g_infoSentRank = g_myRank; g_infoSentFlags = g_myFlags;
    }
    if (!g_clock.Synced()) return;
    double renderT = g_clock.ServerNow(LocalSeconds()) - kDelay;
    for (auto &kv : g_players) {
        Player &p = kv.second;
        if (p.session != g_mySession && !g_allSessions) {   // another session (a job, a solo activity): not here
            if (p.ped || p.veh) { LOGI("remote", p.name.c_str(), "in another session: hidden"); DeletePuppet(p); }
            p.buf.Clear();
            continue;
        }
        if (p.buf.Empty()) continue;
        interp::Result r = p.buf.At(renderT);
        if (!r.valid) continue;
        if (!gta::Exists(p.ped)) { Spawn(p, r.s); continue; }
        if (p.lookDirty) { appearance::ApplyLook(p.ped, p.look); p.lookDirty = false; }
        if (r.stale != p.wasStale) { LOGD("remote", p.name.c_str(), r.stale ? "no data, holding" : "data again"); p.wasStale = r.stale; }
        if (PuppetCombat(p, r)) continue;   // dead: no movement
        if (r.s.flags & kFlagInVehicle) VehicleFrame(p, r);
        else if (!LeaveVehicleIfNeeded(p)) Drive(p, r);
        if (p.veh && p.vehIdleSince && GetTickCount64() - p.vehIdleSince > 120000) {   // parked and unused for 2 min
            LOGI("remote", p.name.c_str(), "parked vehicle removed");
            DeleteVehicle(p);
        }
        WantedBlip(p);
        NameTag(p);
        int now = gta::GameTimeMs();
        if (now - g_lastMetric > 2000) {   // sync quality: how far the puppet is from where it should be
            g_lastMetric = now;
            gta::Vec3 cur = gta::Pos((r.s.flags & kFlagInVehicle) && p.veh ? p.veh : p.ped);
            float ex = cur.x - r.s.pos.x, ey = cur.y - r.s.pos.y;
            LOGI("remote", p.name.c_str(), "sync: error %.2f m, move %u, %s, buffer %.0f ms ahead", std::sqrt(ex * ex + ey * ey), r.s.move,
                 r.stale ? "STALE" : r.extrapolated ? "extrapolated" : "interpolated", (p.buf.NewestTime() - renderT) * 1000.0);
        }
    }
    if (g_specId) SpectatorCamera();
}

void Clear() {
    StopSpectate();
    for (auto &kv : g_players) DeletePuppet(kv.second);
    g_players.clear();
    g_mySession = 0; g_infoSentAs = 0;
}

double ServerNowMs() { return g_clock.Synced() ? g_clock.ServerNow(LocalSeconds()) * 1000.0 : -1.0; }

}  // namespace remote
