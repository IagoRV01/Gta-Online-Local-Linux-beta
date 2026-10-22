#include "game/camera_client.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include "common/log.h"
#include "engine/gta.h"
#include "engine/shv.h"
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
    if (g_cam.mode == kCamCctv || g_cam.mode == kCamFree) {
        pos = {g_cam.pos.x, g_cam.pos.y, g_cam.pos.z};
        look = LookFromRot(pos, {g_cam.rot.x, g_cam.rot.y, g_cam.rot.z});
        return true;
    }
    int ped = remote::PedOfNetId(g_cam.target), veh = remote::VehOfNetId(g_cam.target);
    Vec3 t = g_lastTarget;
    float h = g_lastHeading;
    bool inVeh = false;
    if (veh && ped && gta::InVehicle(ped)) { t = gta::Pos(veh); h = gta::Heading(veh); inVeh = true; }
    else if (ped) { t = gta::Pos(ped); h = gta::Heading(ped); }
    else if (g_lastTarget.x == 0 && g_lastTarget.y == 0) return false;   // not seen yet
    g_lastTarget = t;
    g_lastHeading = h;
    Vec3 f = Fwd(h), r = Right(h), up{0, 0, 1};
    float secs = (now - g_modeSince) / 1000.f;
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
    return true;
}

void Overlay(int now) {
    if (!g_haveCam) return;
    int hh = N<int>(N_GET_CLOCK_HOURS), mm = N<int>(N_GET_CLOCK_MINUTES);
    char clock[16];
    std::snprintf(clock, sizeof clock, "%02d:%02d", hh, mm);
    gta::Rect(0.13f, 0.93f, 0.24f, 0.05f, gta::Color{0, 0, 0, 140});
    if ((now / 600) % 2) gta::Text("~r~REC", 0.012f, 0.912f, 0.32f, gta::kWhite, 1, 4);
    gta::Text(g_cam.label + "   " + clock, 0.045f, 0.912f, 0.38f, gta::kWhite, 1, 4);
}

}  // namespace

bool Active() { return g_active; }

void OnCameraSet(const CameraSetMsg &m) {
    bool newShot = !g_haveCam || m.mode != g_cam.mode || m.target != g_cam.target || m.cameraId != g_cam.cameraId;
    g_cam = m;
    g_haveCam = true;
    if (newShot) { g_snap = true; g_modeSince = gta::GameTimeMs(); g_lastTarget = {}; }
    LOGI("camera", "-", "now showing %s (%s)", m.label.c_str(), kCameraModeNames[m.mode <= kCamFree ? m.mode : 0]);
}

void Run(const Settings &s) {
    g_active = true;
    LOGI("camera", s.name.c_str(), "camera client mode: %s:%d", s.host.c_str(), s.port);
    remote::Init();
    remote::SeeAllSessions(true);
    world_sync::Begin();
    N(N_DISPLAY_RADAR, 0);
    unsigned long long nextLogin = 0, nextMove = 0;
    int lastFrozenPed = 0;
    for (;;) {
        int now = gta::GameTimeMs();
        unsigned long long tick = GetTickCount64();
        if (!net::Connected() && tick >= nextLogin) {
            nextLogin = tick + 5000;
            net::LoginResult r = net::Login(s.host, s.port, s.name, ReadToken(s.tokenFile), 3500, kRoleCamera);
            if (r.ok) {
                if (!r.newToken.empty()) { std::ofstream(s.tokenFile) << r.newToken; LOGI("camera", s.name.c_str(), "camera account created; token stored"); }
                remote::SetSelf(r.netId);
            } else LOGW("camera", s.name.c_str(), "offline: %s", r.error.c_str());
        }
        int me = gta::PlayerPed();
        if (me != lastFrozenPed) { HideSelf(); lastFrozenPed = me; }   // the game can rebuild the player ped
        if (net::Connected()) { remote::Frame(); world_sync::Frame(); }
        N(N_HIDE_HUD_AND_RADAR_THIS_FRAME);
        gta::DisableAllControls();

        Vec3 pos, look;
        float fov = 55.f;
        if (g_haveCam && Desired(pos, look, fov, now)) {
            if (g_snap || Len(Vec3{pos.x - g_pos.x, pos.y - g_pos.y, pos.z - g_pos.z}) > 200.f) { g_pos = pos; g_look = look; g_snap = false; }
            else {
                float k = g_cam.mode == kCamCctv || g_cam.mode == kCamFree ? 1.f : 0.12f;   // follow modes glide
                g_pos = Lerp(g_pos, pos, k);
                g_look = Lerp(g_look, look, k * 1.5f > 1.f ? 1.f : k * 1.5f);
            }
            if (!g_scriptCam) g_scriptCam = gta::CamCreate(g_pos, g_look, fov);
            else gta::CamMove(g_scriptCam, g_pos, g_look, fov);
            // stream the world round the camera, not round the hidden player: move the frozen ped along with it
            N(N_SET_FOCUS_POS_AND_VEL, g_pos.x, g_pos.y, g_pos.z, 0.f, 0.f, 0.f);
            if (tick >= nextMove) {
                nextMove = tick + 1000;
                if (Len(Vec3{gta::Pos(me).x - g_pos.x, gta::Pos(me).y - g_pos.y, 0}) > 60.f)
                    N(N_SET_ENTITY_COORDS_NO_OFFSET, me, g_pos.x, g_pos.y, g_pos.z + 5.f, 0, 0, 0);
            }
        }
        Overlay(now);
        gta::Wait(0);
    }
}

}  // namespace camera_client
