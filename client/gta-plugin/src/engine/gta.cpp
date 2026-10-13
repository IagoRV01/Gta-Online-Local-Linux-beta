#include "engine/gta.h"
#include "engine/shv.h"

namespace gta {

static Vec3 V(const NVec3 &n) { return {n.x, n.y, n.z}; }

static void (*g_waitHook)() = nullptr;
static bool g_inHook = false;
void SetWaitHook(void (*hook)()) { g_waitHook = hook; }
void Wait(unsigned ms) {
    if (g_waitHook && !g_inHook) { g_inHook = true; g_waitHook(); g_inHook = false; }
    shv::scriptWait(ms);
}
uint32_t Joaat(const char *s) {
    uint32_t h = 0;
    for (; *s; ++s) { char c = *s; if (c >= 'A' && c <= 'Z') c += 32; h += (uint8_t)c; h += h << 10; h ^= h >> 6; }
    h += h << 3; h ^= h >> 11; h += h << 15;
    return h;
}
int GameTimeMs() { return N<int>(N_GET_GAME_TIMER); }

int PlayerId() { return N<int>(N_PLAYER_ID); }
int PlayerPed() { return N<int>(N_PLAYER_PED_ID); }
bool PlayerPlaying() { return NB(N<int>(N_IS_PLAYER_PLAYING, PlayerId())); }

bool Exists(int e) { return e != 0 && NB(N<int>(N_DOES_ENTITY_EXIST, e)); }
bool Dead(int e) { return NB(N<int>(N_IS_ENTITY_DEAD, e, 0)); }
Vec3 Pos(int e) { return V(N<NVec3>(N_GET_ENTITY_COORDS, e, 1)); }
float Heading(int e) { return N<float>(N_GET_ENTITY_HEADING, e); }
void SetHeading(int e, float deg) { N(N_SET_ENTITY_HEADING, e, deg); }
Vec3 Velocity(int e) { return V(N<NVec3>(N_GET_ENTITY_VELOCITY, e)); }
int Health(int e) { return N<int>(N_GET_ENTITY_HEALTH, e); }
void Freeze(int e, bool on) { N(N_FREEZE_ENTITY_POSITION, e, on ? 1 : 0); }
void SetVisible(int e, bool on) { N(N_SET_ENTITY_VISIBLE, e, on ? 1 : 0, 0); }
void SetCollision(int e, bool on) { N(N_SET_ENTITY_COLLISION, e, on ? 1 : 0, on ? 1 : 0); }
void SetInvincible(int e, bool on) { N(N_SET_ENTITY_INVINCIBLE, e, on ? 1 : 0); }
void MakeMission(int e) { N(N_SET_ENTITY_AS_MISSION_ENTITY, e, 1, 1); }
bool InVehicle(int ped) { return NB(N<int>(N_IS_PED_IN_ANY_VEHICLE, ped, 0)); }
uint32_t ModelOf(int e) { return N<uint32_t>(N_GET_ENTITY_MODEL, e); }

bool LoadModel(uint32_t m, int timeoutMs) {
    if (!NB(N<int>(N_IS_MODEL_IN_CDIMAGE, m)) || !NB(N<int>(N_IS_MODEL_VALID, m))) return false;
    for (int t = 0; t < timeoutMs; t += 50) {
        N(N_REQUEST_MODEL, m);
        if (NB(N<int>(N_HAS_MODEL_LOADED, m))) return true;
        Wait(50);
    }
    return NB(N<int>(N_HAS_MODEL_LOADED, m));
}
void ReleaseModel(uint32_t m) { N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m); }

float GroundZ(float x, float y, float zTop, float fallback) {
    float out = 0.f;
    if (NB(N<int>(N_GET_GROUND_Z_FOR_3D_COORD, x, y, zTop, &out, 0, 0))) return out;
    return fallback;
}

void Teleport(const Vec3 &p, float heading) {
    int me = PlayerPed();
    N(N_SET_ENTITY_COORDS_NO_OFFSET, me, p.x, p.y, p.z, 0, 0, 0);
    N(N_SET_FOCUS_POS_AND_VEL, p.x, p.y, p.z, 0.f, 0.f, 0.f);
    Freeze(me, true);
    float gz = p.z;
    bool found = false;
    for (int i = 0; i < 160 && !found; ++i) {   // up to 8 s for far areas to stream
        N(N_REQUEST_COLLISION_AT_COORD, p.x, p.y, p.z);
        Wait(50);
        float out = 0.f;
        // probe from just above the saved height first (interiors/under bridges), then from high up
        if (NB(N<int>(N_GET_GROUND_Z_FOR_3D_COORD, p.x, p.y, p.z + 2.f, &out, 0, 0)) && out > p.z - 5.f) { gz = out; found = true; }
        else if (i > 40 && NB(N<int>(N_GET_GROUND_Z_FOR_3D_COORD, p.x, p.y, p.z + 100.f, &out, 0, 0))) { gz = out; found = true; }
    }
    N(N_CLEAR_FOCUS);
    N(N_SET_ENTITY_COORDS_NO_OFFSET, me, p.x, p.y, (found ? gz : p.z) + 0.05f, 0, 0, 0);
    N(N_SET_ENTITY_HEADING, me, heading);
    Freeze(me, false);
}

void FadeOut(int ms) {
    N(N_DO_SCREEN_FADE_OUT, ms);
    for (int t = 0; t < ms + 1000 && !NB(N<int>(N_IS_SCREEN_FADED_OUT)); t += 20) Wait(20);
}
void FadeIn(int ms) { N(N_DO_SCREEN_FADE_IN, ms); }

// ---------------------------------------------------------------- HUD
void Notify(const std::string &text) {
    N(N_BEGIN_TEXT_COMMAND_THEFEED_POST, "STRING");
    N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, text.c_str());
    N<int>(N_END_TEXT_COMMAND_THEFEED_POST_TICKER, 0, 1);
}
void Help(const std::string &text) {
    N(N_BEGIN_TEXT_COMMAND_DISPLAY_HELP, "STRING");
    N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, text.c_str());
    N(N_END_TEXT_COMMAND_DISPLAY_HELP, 0, 0, 0, -1);
}
void Rect(float x, float y, float w, float h, Color c) { N(N_DRAW_RECT, x, y, w, h, (int)c.r, (int)c.g, (int)c.b, (int)c.a, 0); }
void Text(const std::string &t, float x, float y, float scale, Color c, int align, int font, float wrapEnd) {
    N(N_SET_TEXT_FONT, font);
    N(N_SET_TEXT_SCALE, scale, scale);
    N(N_SET_TEXT_COLOUR, (int)c.r, (int)c.g, (int)c.b, (int)c.a);
    N(N_SET_TEXT_JUSTIFICATION, align);
    if (align == 2) N(N_SET_TEXT_WRAP, 0.0f, wrapEnd);
    else if (align == 1 && wrapEnd < 1.f) N(N_SET_TEXT_WRAP, x, wrapEnd);
    N(N_BEGIN_TEXT_COMMAND_DISPLAY_TEXT, "STRING");
    N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, t.c_str());
    N(N_END_TEXT_COMMAND_DISPLAY_TEXT, x, y, 0);
}
float TextWidth(const std::string &t, float scale, int font) {
    N(N_BEGIN_TEXT_COMMAND_GET_SCREEN_WIDTH_OF_DISPLAY_TEXT, "STRING");
    N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, t.c_str());
    N(N_SET_TEXT_FONT, font);
    N(N_SET_TEXT_SCALE, scale, scale);
    return N<float>(N_END_TEXT_COMMAND_GET_SCREEN_WIDTH_OF_DISPLAY_TEXT, 1);
}
void Big(const std::string &title, const std::string &sub, int ms) {
    int until = GameTimeMs() + ms;
    while (GameTimeMs() < until) {
        Rect(0.5f, 0.45f, 1.f, 0.16f, {0, 0, 0, 150});
        Text(title, 0.5f, 0.395f, 1.2f, kWhite, 0, 7);
        if (!sub.empty()) Text(sub, 0.5f, 0.475f, 0.45f, kGrey, 0, 0);
        Wait(0);
    }
}

// ---------------------------------------------------------------- controls
void DisableAllControls() { N(N_DISABLE_ALL_CONTROL_ACTIONS, 0); }
void DisableControl(int a) { N(N_DISABLE_CONTROL_ACTION, 0, a, 1); }
bool JustPressed(int a) { return NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 0, a)) || NB(N<int>(N_IS_CONTROL_JUST_PRESSED, 0, a)); }
bool Pressed(int a) { return NB(N<int>(N_IS_DISABLED_CONTROL_PRESSED, 0, a)) || NB(N<int>(N_IS_CONTROL_PRESSED, 0, a)); }

bool Keyboard(const char *title, const std::string &initial, int maxLen, std::string &out) {
    (void)title;   // FMMC_KEY_TIP8 is the stock "Enter message" prompt; custom titles need a GXT entry
    N(N_DISPLAY_ONSCREEN_KEYBOARD, 0, "FMMC_KEY_TIP8", "", initial.c_str(), "", "", "", maxLen);
    int st = 0;
    while ((st = N<int>(N_UPDATE_ONSCREEN_KEYBOARD)) == 0) Wait(0);
    if (st != 1) return false;
    const char *r = N<const char *>(N_GET_ONSCREEN_KEYBOARD_RESULT);
    out = r ? r : "";
    return true;
}

// ---------------------------------------------------------------- camera
static void PointAt(int cam, const Vec3 &pos, const Vec3 &at, float fov) {
    N(N_SET_CAM_COORD, cam, pos.x, pos.y, pos.z);
    N(N_POINT_CAM_AT_COORD, cam, at.x, at.y, at.z);
    N(N_SET_CAM_FOV, cam, fov);
}
int CamCreate(const Vec3 &pos, const Vec3 &at, float fov) {
    int cam = N<int>(N_CREATE_CAM, "DEFAULT_SCRIPTED_CAMERA", 1);
    PointAt(cam, pos, at, fov);
    N(N_SET_CAM_ACTIVE, cam, 1);
    N(N_RENDER_SCRIPT_CAMS, 1, 0, 0, 1, 0, 0);
    return cam;
}
void CamMove(int cam, const Vec3 &pos, const Vec3 &at, float fov) { PointAt(cam, pos, at, fov); }
void CamDestroy(int &cam) {
    if (!cam) return;
    N(N_RENDER_SCRIPT_CAMS, 0, 0, 0, 1, 0, 0);
    N(N_SET_CAM_ACTIVE, cam, 0);
    N(N_DESTROY_CAM, cam, 0);
    cam = 0;
}

}  // namespace gta
