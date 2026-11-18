#include "game/camera_client.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include "common/log.h"
#include "engine/gta.h"
#include "engine/shv.h"
#include "game/godeye.h"
#include "game/net_client.h"
#include "game/remote.h"
#include "game/world_sync.h"

namespace camera_client {

using namespace proto;
using gta::Vec3;

namespace {

bool g_active = false;
CameraSetMsg g_cam;                 // what GodEye wants shown
bool g_haveCam = false;
int g_scriptCam = 0;
Vec3 g_pos{200, -900, 400}, g_look{200, -900, 0};   // the camera as drawn (smoothed)
bool g_snap = true;                 // jump straight to the next pose (after a switch)
int g_modeSince = 0;
Vec3 g_lastTarget;
float g_lastHeading = 0;
int g_interior = 0;                 // the interior the target is in (2026-11-17), 0 outside
int g_nextRoom = 0;
int g_freeKind = 0;                 // 2026-11-17: the free-driving car in this camera's GTA (godeye::FreeCar), 0 none
// 2026-11-18: a new free car gets an automatic chase shot until GodEye picks a shot (any CameraSet after it came out);
// GodEye can also aim any camera's modes at a free car, its own (target = our netId) or another camera's (a ghost)
int g_carSince = 0, g_camSetMs = 0;
bool g_autoChase = false;
uint16_t g_self = 0;

// ---- GodEye's hacks (2026-11-17): the hacked player's game reports its drone / RC car / taxis ~10 a second; the camera
// draws a copy of each (frozen, no collision) gliding between the reports, so the video can show them and follow them
struct Ghost {
    uint8_t kind = 0, flags = 0;
    uint32_t model = 0;
    Vec3 vel;                       // from the last two reports: the car copies roll with it
    int ent = 0;
    bool isObject = false;
    Vec3 from, to;
    float hFrom = 0, hTo = 0;
    int atMs = 0, seenMs = 0, retryAt = 0;
};
std::map<std::pair<uint16_t, int>, Ghost> g_ghosts;   // (owner netId, index in its report)

Vec3 Add(const Vec3 &a, const Vec3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Mul(const Vec3 &a, float k) { return {a.x * k, a.y * k, a.z * k}; }
Vec3 Lerp(const Vec3 &a, const Vec3 &b, float k) { return {a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k}; }
Vec3 Fwd(float headingDeg) { float h = headingDeg * 3.14159265f / 180.f; return {-std::sin(h), std::cos(h), 0}; }
Vec3 Right(float headingDeg) { Vec3 f = Fwd(headingDeg); return {f.y, -f.x, 0}; }
float Len(const Vec3 &v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

// a fixed camera's look point from its pitch/yaw (GTA: yaw counter-clockwise from north)
Vec3 LookFromRot(const Vec3 &pos, const Vec3 &rot) {
    float p = rot.x * 3.14159265f / 180.f, y = rot.z * 3.14159265f / 180.f;
    return Add(pos, Vec3{-std::sin(y) * std::cos(p) * 20.f, std::cos(y) * std::cos(p) * 20.f, std::sin(p) * 20.f});
}

float AngleLerp(float a, float b, float k) {
    float d = std::fmod(b - a + 540.f, 360.f) - 180.f;
    return a + d * k;
}

Vec3 GhostPos(const Ghost &g, int now, float *heading) {
    float k = std::min(1.f, (now - g.atMs) / 110.f);
    if (heading) *heading = AngleLerp(g.hFrom, g.hTo, k);
    return Lerp(g.from, g.to, k);
}

void GhostDelete(Ghost &g) {
    if (g.ent && gta::Exists(g.ent)) N(N_DELETE_ENTITY, &g.ent);
    g.ent = 0;
}

int GhostSpawn(const Ghost &g, bool &isObject) {
    uint32_t m = 0;
    isObject = g.kind == kGeDrone;
    if (isObject) {
        for (const char *n : {"xs_prop_arena_drone_01", "xs_prop_arena_drone_02", "ba_prop_battle_drone_quad"})
            if (NB(N<int>(N_IS_MODEL_VALID, gta::Joaat(n)))) { m = gta::Joaat(n); break; }
    } else {
        m = gta::Joaat(g.kind == kGeRcBomb ? "rcbandito" : "taxi");
        if (g.kind == kGeTaxis && (g.flags & kGadgetKnoway) && NB(N<int>(N_IS_MODEL_IN_CDIMAGE, gta::Joaat("knowayest")))) m = gta::Joaat("knowayest");
    }
    if (!m || !gta::LoadModel(m, 3000)) { LOGW("camera", "-", "hack copy (kind %d): model %08x not loaded", g.kind, m); return 0; }
    int e = isObject ? N<int>(N_CREATE_OBJECT, m, g.to.x, g.to.y, g.to.z, 0, 1, 0)
                     : N<int>(N_CREATE_VEHICLE, m, g.to.x, g.to.y, g.to.z, g.hTo, 0, 1, 0);
    gta::ReleaseModel(m);
    if (!e) { LOGW("camera", "-", "hack copy (kind %d): not created", g.kind); return 0; }
    gta::MakeMission(e);
    gta::SetInvincible(e, true);
    if (isObject) { gta::Freeze(e, true); gta::SetCollision(e, false); }   // the drone flies where it is put; cars drive (GhostsTick)
    if (!isObject) { N(N_SET_VEHICLE_ENGINE_ON, e, 1, 1, 0); N(N_SET_VEHICLE_LIGHTS, e, 2); }
    LOGI("camera", "-", "hack copy (kind %d) entity %d at %.0f,%.0f,%.0f", g.kind, e, g.to.x, g.to.y, g.to.z);
    return e;
}

void GhostsTick(int now) {
    for (auto it = g_ghosts.begin(); it != g_ghosts.end();) {
        Ghost &g = it->second;
        if (now - g.seenMs > 2000) { GhostDelete(g); it = g_ghosts.erase(it); continue; }   // the player left, or the hack ended
        if ((!g.ent || !gta::Exists(g.ent)) && now >= g.retryAt) {
            g.ent = GhostSpawn(g, g.isObject);
            if (!g.ent) g.retryAt = now + 5000;   // not every frame: a model load waits
        }
        if (g.ent) {
            float h = 0;
            Vec3 p = GhostPos(g, now, &h);
            N(N_SET_ENTITY_COORDS_NO_OFFSET, g.ent, p.x, p.y, p.z, 0, 0, 0);
            if (g.isObject) N(N_SET_ENTITY_ROTATION, g.ent, 0.f, 0.f, h, 2, 1);
            else {   // as the players' cars (remote.cpp DriveVehicle): their speed plus a pull towards the report; wheels turn
                Vec3 c = gta::Pos(g.ent), e{p.x - c.x, p.y - c.y, p.z - c.z};
                if (Len(e) > 8.f) { N(N_SET_ENTITY_COORDS_NO_OFFSET, g.ent, p.x, p.y, p.z, 0, 0, 0); e = {}; }
                N(N_SET_ENTITY_VELOCITY, g.ent, g.vel.x + e.x * 3.f, g.vel.y + e.y * 3.f, g.vel.z + e.z * 3.f);
                N(N_SET_ENTITY_HEADING, g.ent, h);
            }
        }
        ++it;
    }
}

// the hack the gadget camera follows on that player: the one a GodEye user drives, else the first
const Ghost *GhostOf(uint16_t owner) {
    const Ghost *first = nullptr;
    for (auto &kv : g_ghosts) {
        if (kv.first.first != owner) continue;
        if (kv.second.flags & kGadgetDriven) return &kv.second;
        if (!first) first = &kv.second;
    }
    return first;
}

// a camera inside walls: pulled in front of the first thing between the target's head and the camera
Vec3 Unblocked(const Vec3 &from, const Vec3 &to, int ignore) {
    int h = N<int>(N_START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE, from.x, from.y, from.z, to.x, to.y, to.z, 1 | 16, ignore, 7);
    int hit = 0, ent = 0;
    NVec3 end{}, normal{};
    if (N<int>(N_GET_SHAPE_TEST_RESULT, h, &hit, &end, &normal, &ent) != 2 || !hit) return to;
    Vec3 e{end.x, end.y, end.z}, d{to.x - from.x, to.y - from.y, to.z - from.z};
    float l = Len(d);
    if (l < 0.01f) return to;
    Vec3 back = Mul(d, 0.35f / l);
    return Vec3{e.x - back.x, e.y - back.y, e.z - back.z};
}

std::string ReadToken(const std::string &file) {
    std::ifstream f(file);
    std::string t;
    f >> t;
    return t;
}

// the hidden local player: no body, no collision, nobody's target, no controls
void HideSelf() {
    int me = gta::PlayerPed(), pl = gta::PlayerId();
    gta::SetVisible(me, false);
    gta::SetCollision(me, false);
    gta::SetInvincible(me, true);
    gta::Freeze(me, true);
    N(N_SET_EVERYONE_IGNORE_PLAYER, pl, 1);
    N(N_SET_POLICE_IGNORE_PLAYER, pl, 1);
    N(N_SET_MAX_WANTED_LEVEL, 0);
    N(N_CLEAR_PLAYER_WANTED_LEVEL, pl);
}

// where the camera should be this frame, for the current mode
bool Desired(Vec3 &pos, Vec3 &look, float &fov, int now) {
    fov = g_cam.fov;
    Vec3 fc;
    float fh = 0;
    int fk = 0;
    bool free = godeye::FreeCar(fc, fh, fk);
    if (free && !g_freeKind) g_carSince = now;
    g_freeKind = free ? fk : 0;
    bool autoChase = free && g_carSince >= g_camSetMs;
    if (autoChase != g_autoChase) g_snap = true;   // a cut to the car and back to the shot GodEye had
    g_autoChase = autoChase;
    if (autoChase) {   // free driving (2026-11-17): a chase shot behind the car until GodEye picks a shot
        Vec3 f = Fwd(fh), up{0, 0, 1};
        float back = fk == kGeRcBomb ? 3.6f : 7.5f, high = fk == kGeRcBomb ? 1.3f : 2.6f;
        pos = Add(Add(fc, Mul(f, -back)), Mul(up, high));
        look = Add(Add(fc, Mul(f, 6.f)), Mul(up, 0.4f));
        fov = 62.f;
        g_interior = 0;
        return true;
    }
    if (g_cam.mode == kCamCctv || g_cam.mode == kCamFree) {
        pos = {g_cam.pos.x, g_cam.pos.y, g_cam.pos.z};
        look = LookFromRot(pos, {g_cam.rot.x, g_cam.rot.y, g_cam.rot.z});
        return true;
    }
    if (g_cam.mode == kCamGadget) {   // behind the hack (2026-11-17); the player while there is none
        if (const Ghost *gh = GhostOf(g_cam.target)) {
            float gh_h = 0;
            Vec3 at = GhostPos(*gh, now, &gh_h), f = Fwd(gh_h), up{0, 0, 1};
            // the Arena War drone is small (about 30 px from 4.5 m in the 2026-11-17 test): close behind it
            float back = gh->kind == kGeDrone ? 2.3f : gh->kind == kGeRcBomb ? 3.2f : 7.5f, high = gh->kind == kGeDrone ? 0.7f : gh->kind == kGeRcBomb ? 1.2f : 2.6f;
            pos = Add(Add(at, Mul(f, -back)), Mul(up, high));
            look = Add(Add(at, Mul(f, 6.f)), Mul(up, gh->kind == kGeDrone ? -0.8f : 0.4f));
            g_lastTarget = at;
            g_lastHeading = gh_h;
            g_interior = 0;
            return true;
        }
    }
    int ped = remote::PedOfNetId(g_cam.target), veh = remote::VehOfNetId(g_cam.target);
    Vec3 t = g_lastTarget;
    float h = g_lastHeading;
    bool inVeh = false, small = false;
    const Ghost *gh = nullptr;
    if (veh && ped && gta::InVehicle(ped)) { t = gta::Pos(veh); h = gta::Heading(veh); inVeh = true; }
    else if (ped) { t = gta::Pos(ped); h = gta::Heading(ped); }
    else if (g_cam.target == g_self && free) { t = fc; h = fh; inVeh = true; small = fk == kGeRcBomb; }   // our own free car
    else if ((gh = GhostOf(g_cam.target)) != nullptr) { t = GhostPos(*gh, now, &h); inVeh = true; small = gh->kind == kGeRcBomb; }   // another camera's
    else if (g_lastTarget.x == 0 && g_lastTarget.y == 0) return false;   // not seen yet
    g_lastTarget = t;
    g_lastHeading = h;
    Vec3 f = Fwd(h), r = Right(h), up{0, 0, 1};
    float secs = (now - g_modeSince) / 1000.f;
    // inside (an apartment, a shop, a garage: 2026-11-17): every mode becomes a close shot in the room, kept off the
    // walls; the helicopter and the drone have no sky to fly in there
    int interior = inVeh ? 0 : N<int>(N_GET_INTERIOR_AT_COORDS, t.x, t.y, t.z);
    if (interior != g_interior) { g_interior = interior; g_snap = true; g_nextRoom = 0; }
    if (interior) {
        Vec3 head = Add(t, Mul(up, 0.7f));
        Vec3 want = g_cam.mode == kCamThird ? Add(Add(Add(t, Mul(f, -2.2f)), Mul(r, 0.6f)), Mul(up, 0.9f))
                                            : Add(Add(t, Mul(f, -3.2f)), Mul(up, 1.1f));
        pos = Unblocked(head, want, ped);
        look = g_cam.mode == kCamThird ? Add(Add(t, Mul(f, 5.f)), Mul(up, 0.6f)) : Add(t, Mul(up, 0.5f));
        fov = 70.f;
        return true;
    }
    switch (g_cam.mode) {
        case kCamFollow:
            pos = Add(Add(t, Mul(f, inVeh ? -9.f : -6.5f)), Mul(up, inVeh ? 3.2f : 2.4f));
            look = Add(t, Mul(up, 1.f));
            break;
        case kCamThird:
            pos = Add(Add(Add(t, Mul(f, -2.6f)), Mul(r, 0.7f)), Mul(up, 1.5f));
            look = Add(Add(t, Mul(f, 6.f)), Mul(up, 1.2f));
            break;
        case kCamChase:
        case kCamGadget:   // on a free car (2026-11-18): the hack camera is a chase shot
            pos = Add(Add(t, Mul(f, inVeh ? -11.f : -7.f)), Mul(up, inVeh ? 2.6f : 2.f));
            look = Add(Add(t, Mul(f, inVeh ? 8.f : 3.f)), Mul(up, 1.f));
            break;
        case kCamHeli: {
            float a = secs * 0.08f;
            pos = Add(t, Vec3{std::cos(a) * 70.f, std::sin(a) * 70.f, 55.f});
            look = t;
            break;
        }
        case kCamDrone: {
            float a = secs * 0.25f;
            pos = Add(t, Vec3{std::cos(a) * 18.f, std::sin(a) * 18.f, 9.f});
            look = Add(t, Mul(up, 1.f));
            break;
        }
        case kCamCinematic: {   // a new shot every 8 s: low front, side dolly, high orbit, low behind
            int shot = (int)(secs / 8.f) % 4;
            float k = std::fmod(secs, 8.f) / 8.f;
            if (shot == 0) { pos = Add(Add(t, Mul(f, 7.f)), Mul(up, 0.6f)); look = Add(t, Mul(up, 1.1f)); }
            else if (shot == 1) { pos = Add(Add(Add(t, Mul(r, 9.f)), Mul(f, -4.f + 8.f * k)), Mul(up, 1.8f)); look = Add(t, Mul(up, 1.f)); }
            else if (shot == 2) { float a = secs * 0.12f; pos = Add(t, Vec3{std::cos(a) * 30.f, std::sin(a) * 30.f, 25.f}); look = t; }
            else { pos = Add(Add(t, Mul(f, -5.f)), Mul(up, 0.4f)); look = Add(Add(t, Mul(f, 10.f)), Mul(up, 1.6f)); }
            if (k < 0.02f) g_snap = true;   // a cut, not a pan, between shots
            break;
        }
        default: return false;
    }
    if (small && g_cam.mode != kCamHeli) pos = Add(t, Mul(Vec3{pos.x - t.x, pos.y - t.y, pos.z - t.z}, 0.45f));   // an RC car is tiny
    return true;
}

void Overlay(int now) {
    if (!g_haveCam) return;
    int hh = N<int>(N_GET_CLOCK_HOURS), mm = N<int>(N_GET_CLOCK_MINUTES);
    char clock[16];
    std::snprintf(clock, sizeof clock, "%02d:%02d", hh, mm);
    gta::Rect(0.13f, 0.93f, 0.24f, 0.05f, gta::Color{0, 0, 0, 140});
    if ((now / 600) % 2) gta::Text("~r~REC", 0.012f, 0.912f, 0.32f, gta::kWhite, 1, 4);
    std::string label = !g_autoChase ? g_cam.label : g_freeKind == kGeRcBomb ? "GODEYE · RC BANDITO" : "GODEYE · KNOWAY";
    gta::Text(label + "   " + clock, 0.045f, 0.912f, 0.38f, gta::kWhite, 1, 4);
}

}  // namespace

bool Active() { return g_active; }

void OnCameraSet(const CameraSetMsg &m) {
    bool newShot = !g_haveCam || m.mode != g_cam.mode || m.target != g_cam.target || m.cameraId != g_cam.cameraId;
    g_cam = m;
    g_haveCam = true;
    if (newShot) { g_snap = true; g_modeSince = gta::GameTimeMs(); g_lastTarget = {}; }
    g_camSetMs = gta::GameTimeMs();   // GodEye picked a shot: a free car's automatic chase ends
    LOGI("camera", "-", "now showing %s (%s)", m.label.c_str(), kCameraModeNames[m.mode <= kCamGadget ? m.mode : 0]);
}

void OnGadgets(const GadgetRelayMsg &m) {
    if (!g_active) return;
    int now = gta::GameTimeMs();
    for (size_t i = 0; i < m.g.size(); ++i) {
        const GadgetPose &p = m.g[i];
        Ghost &g = g_ghosts[{m.owner, (int)i}];
        if (g.kind && (g.kind != p.kind || (g.flags & kGadgetKnoway) != (p.flags & kGadgetKnoway))) { GhostDelete(g); g = Ghost(); }
        Vec3 to{p.pos.x, p.pos.y, p.pos.z};
        if (g.kind && now - g.atMs > 30 && now - g.atMs < 1000) {
            float dt = (now - g.atMs) / 1000.f;
            g.vel = {(to.x - g.to.x) / dt, (to.y - g.to.y) / dt, (to.z - g.to.z) / dt};
        }
        if (!g.kind) { g.from = to; g.hFrom = p.heading; }
        else { float h = 0; g.from = GhostPos(g, now, &h); g.hFrom = h; }
        g.kind = p.kind; g.flags = p.flags; g.to = to; g.hTo = p.heading; g.atMs = now; g.seenMs = now;
    }
    for (auto it = g_ghosts.begin(); it != g_ghosts.end();) {   // hacks that ended
        if (it->first.first == m.owner && it->first.second >= (int)m.g.size()) { GhostDelete(it->second); it = g_ghosts.erase(it); }
        else ++it;
    }
}

void Run(const Settings &s) {
    g_active = true;
    LOGI("camera", s.name.c_str(), "camera client mode: %s:%d", s.host.c_str(), s.port);
    remote::Init();
    remote::SeeAllSessions(true);
    world_sync::Begin();
    N(N_DISPLAY_RADAR, 0);
    unsigned long long nextLogin = 0, nextMove = 0;
    int lastFrozenPed = 0, viewRoom = 0;
    for (;;) {
        int now = gta::GameTimeMs();
        unsigned long long tick = GetTickCount64();
        if (!net::Connected() && tick >= nextLogin) {
            nextLogin = tick + 5000;
            net::LoginResult r = net::Login(s.host, s.port, s.name, ReadToken(s.tokenFile), 3500, kRoleCamera);
            if (r.ok) {
                if (!r.newToken.empty()) { std::ofstream(s.tokenFile) << r.newToken; LOGI("camera", s.name.c_str(), "camera account created; token stored"); }
                remote::SetSelf(r.netId);
                g_self = r.netId;
            } else LOGW("camera", s.name.c_str(), "offline: %s", r.error.c_str());
        }
        int me = gta::PlayerPed();
        if (me != lastFrozenPed) { HideSelf(); lastFrozenPed = me; }   // the game can rebuild the player ped
        if (net::Connected()) {
            remote::Frame();
            world_sync::Frame();
            // free driving (2026-11-17): the RC Bandito / Knoway a GodEye user drives here, out on the road nearest to
            // what the camera shows (the player it follows, else the point it looks at)
            godeye::CameraFrame(g_lastTarget.x != 0 || g_lastTarget.y != 0 ? g_lastTarget : g_look);
        }
        N(N_HIDE_HUD_AND_RADAR_THIS_FRAME);
        gta::DisableAllControls();
        GhostsTick(now);

        Vec3 pos, look;
        float fov = 55.f;
        if (g_haveCam && Desired(pos, look, fov, now)) {
            if (g_snap || Len(Vec3{pos.x - g_pos.x, pos.y - g_pos.y, pos.z - g_pos.z}) > 200.f) { g_pos = pos; g_look = look; g_snap = false; }
            else {
                float k = g_autoChase ? 0.3f : g_cam.mode == kCamCctv || g_cam.mode == kCamFree ? 1.f : g_cam.mode == kCamGadget ? 0.25f : 0.12f;   // follow modes glide
                g_pos = Lerp(g_pos, pos, k);
                g_look = Lerp(g_look, look, k * 1.5f > 1.f ? 1.f : k * 1.5f);
            }
            if (!g_scriptCam) g_scriptCam = gta::CamCreate(g_pos, g_look, fov);
            else gta::CamMove(g_scriptCam, g_pos, g_look, fov);
            // stream the world round the camera, not round the hidden player: move the frozen ped along with it
            N(N_SET_FOCUS_POS_AND_VEL, g_pos.x, g_pos.y, g_pos.z, 0.f, 0.f, 0.f);
            if (g_interior) {   // GTA draws an interior for a viewport that is in one of its rooms
                if (!NB(N<int>(N_IS_INTERIOR_READY, g_interior))) N(N_PIN_INTERIOR_IN_MEMORY, g_interior);
                if (tick >= nextMove) {
                    nextMove = tick + 500;
                    Vec3 mp = gta::Pos(me);
                    if (N<int>(N_GET_INTERIOR_FROM_ENTITY, me) != g_interior || Len(Vec3{mp.x - g_lastTarget.x, mp.y - g_lastTarget.y, mp.z - g_lastTarget.z}) > 4.f)
                        N(N_SET_ENTITY_COORDS, me, g_lastTarget.x, g_lastTarget.y, g_lastTarget.z, 0, 0, 0, 0);   // the hidden ped at the target
                }
                int target = remote::PedOfNetId(g_cam.target), room = 0;
                if (target && N<int>(N_GET_INTERIOR_FROM_ENTITY, target) == g_interior) room = N<int>(N_GET_ROOM_KEY_FROM_ENTITY, target);
                else if (N<int>(N_GET_INTERIOR_FROM_ENTITY, me) == g_interior) room = N<int>(N_GET_ROOM_KEY_FROM_ENTITY, me);
                if (room && (now >= g_nextRoom || room != viewRoom)) {
                    N(N_FORCE_ROOM_FOR_GAME_VIEWPORT, g_interior, room);
                    if (room != viewRoom) LOGI("camera", "-", "inside: interior %d, room %08x", g_interior, (unsigned)room);
                    viewRoom = room;
                    g_nextRoom = now + 1000;
                }
            } else {
                if (viewRoom) { N(N_CLEAR_ROOM_FOR_GAME_VIEWPORT); viewRoom = 0; }
                if (tick >= nextMove) {
                    nextMove = tick + 1000;
                    Vec3 mp = gta::Pos(me);
                    bool wasInside = N<int>(N_GET_INTERIOR_FROM_ENTITY, me) != 0;
                    if (wasInside || Len(Vec3{mp.x - g_pos.x, mp.y - g_pos.y, 0}) > 60.f)
                        N(N_SET_ENTITY_COORDS_NO_OFFSET, me, g_pos.x, g_pos.y, g_pos.z + 5.f, 0, 0, 0);
                }
            }
        }
        Overlay(now);
        gta::Wait(0);
    }
}

}  // namespace camera_client
