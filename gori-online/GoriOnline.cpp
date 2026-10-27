// Gori Online (native ScriptHookV .asi) — makes GTA V story mode feel like GTA Online.
//  1. Living world: busier streets, more police, Online-style events (armored trucks, crate drops).
//  2. Online map and interiors: apartments, stilt houses, mansions, casino penthouse, CEO offices,
//     clubhouses, MC labs, bunkers, hangars, facilities, nightclubs, arcades, auto shop, agency,
//     vehicle warehouse and the Kosatka. Buy them on Dynasty 8 / Maze Bank Foreclosures, walk in.
//  3. Businesses (supplies, production, safes, sell missions) and heists (preps + finale).
//  4. The whole Online vehicle catalogue on the in-game websites (Legendary, SSASA, Warstock...).
//  5. Isla Neón, the island from goricito.cc: a real map DLC (IslandBuilder) west of Los Santos;
//     this mod adds its traffic, people, parked vehicles and activities.
//  7. Online-style garages in every property: drive in to store a car, pick one and drive out.
//     Every modification (paint, wheels, neon, tint, livery, extras...) is saved.
//  6. Online interface: interaction menu on M (or hold Back on a controller), RP and rank bar.
// No .NET needed (works under Proton). Saves to GoriOnline.ini next to GTA5.exe, logs to GoriOnline.log.
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <initializer_list>
#include <map>
#include <set>
#include <deque>
#include <ctime>
#include <algorithm>
#include <functional>
#include <type_traits>
#include "nativehashes.h"
#include "vehicles_data.h"
#include "island_data.h"
#include "island_map.h"
#include "mhc_data.h"
#include "mhc_hide.h"
#include "dc_data.h"
#include "plaza_data.h"
#include "hawick_data.h"   // Hawick Gardens (IslandBuilder Hawick.cs)

// ---------------------------------------------------------------- ScriptHookV bindings
typedef void (*KeyboardHandler)(DWORD, WORD, BYTE, BOOL, BOOL, BOOL, BOOL);
static void (*shv_scriptRegister)(HMODULE, void (*)());
static void (*shv_scriptUnregister)(HMODULE);
static void (*shv_scriptWait)(DWORD);
static void (*shv_nativeInit)(uint64_t);
static void (*shv_nativePush64)(uint64_t);
static uint64_t *(*shv_nativeCall)();
static void (*shv_keyboardRegister)(KeyboardHandler);
static void (*shv_keyboardUnregister)(KeyboardHandler);
static int (*shv_worldGetAllPeds)(int *, int);
static int (*shv_worldGetAllVehicles)(int *, int);

static bool BindScriptHook() {
    HMODULE h = GetModuleHandleA("ScriptHookV.dll");
    if (!h) h = LoadLibraryA("ScriptHookV.dll");
    if (!h) return false;
#define BIND(var, name) var = reinterpret_cast<decltype(var)>(GetProcAddress(h, name)); if (!var) return false;
    BIND(shv_scriptRegister, "?scriptRegister@@YAXPEAUHINSTANCE__@@P6AXXZ@Z");
    BIND(shv_scriptUnregister, "?scriptUnregister@@YAXPEAUHINSTANCE__@@@Z");
    BIND(shv_scriptWait, "?scriptWait@@YAXK@Z");
    BIND(shv_nativeInit, "?nativeInit@@YAX_K@Z");
    BIND(shv_nativePush64, "?nativePush64@@YAX_K@Z");
    BIND(shv_nativeCall, "?nativeCall@@YAPEA_KXZ");
    BIND(shv_keyboardRegister, "?keyboardHandlerRegister@@YAXP6AXKGEHHHH@Z@Z");
    BIND(shv_keyboardUnregister, "?keyboardHandlerUnregister@@YAXP6AXKGEHHHH@Z@Z");
    BIND(shv_worldGetAllPeds, "?worldGetAllPeds@@YAHPEAHH@Z");
    shv_worldGetAllVehicles = reinterpret_cast<decltype(shv_worldGetAllVehicles)>(GetProcAddress(h, "?worldGetAllVehicles@@YAHPEAHH@Z"));
#undef BIND
    return true;
}

struct Vec3 { float x; uint32_t _a; float y; uint32_t _b; float z; uint32_t _c; };
static Vec3 V3(float x, float y, float z) { Vec3 v{}; v.x = x; v.y = y; v.z = z; return v; }

template <typename T> static inline void Push(T v) {
    static_assert(sizeof(T) <= 8, "native arg too big");
    uint64_t u = 0; std::memcpy(&u, &v, sizeof(T)); shv_nativePush64(u);
}
template <typename R = void, typename... A> static inline R N(uint64_t hash, A... args) {
    shv_nativeInit(hash);
    (Push(args), ...);
    uint64_t *r = shv_nativeCall();
    if constexpr (!std::is_void_v<R>) { R out; std::memcpy(&out, r, sizeof(R)); return out; }
}
static inline bool NB(int v) { return v != 0; }
static void Wait(DWORD ms) { shv_scriptWait(ms); }

static uint32_t Joaat(const char *s) {
    uint32_t h = 0;
    for (; *s; ++s) { char c = *s; if (c >= 'A' && c <= 'Z') c += 32; h += (uint8_t)c; h += h << 10; h ^= h >> 6; }
    h += h << 3; h ^= h >> 11; h += h << 15;
    return h;
}
static uint32_t ModelHash(const std::string &m) {
    if (m.rfind("0x", 0) == 0) return (uint32_t)std::strtoul(m.c_str() + 2, nullptr, 16);
    if (!m.empty() && m.find_first_not_of("0123456789") == std::string::npos) return (uint32_t)std::strtoul(m.c_str(), nullptr, 10);   // Rockstar's race files
    return Joaat(m.c_str());
}

static std::string Money(long long v) {
    std::string s = std::to_string(v < 0 ? -v : v), out;
    int n = 0;
    for (int i = (int)s.size() - 1; i >= 0; --i) { out.insert(out.begin(), s[i]); if (++n % 3 == 0 && i > 0) out.insert(out.begin(), ','); }
    return std::string(v < 0 ? "-$" : "$") + out;
}
static std::vector<std::string> Split(const std::string &s, char sep) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) { if (c == sep) { out.push_back(cur); cur.clear(); } else cur += c; }
    out.push_back(cur);
    return out;
}

static std::string g_dir, g_iniPath;
static void Log(const char *fmt, ...) {
    static FILE *f = nullptr;
    if (!f) f = std::fopen((g_dir + "GoriOnline.log").c_str(), "w");
    if (!f) return;
    va_list ap; va_start(ap, fmt); std::vfprintf(f, fmt, ap); va_end(ap);
    std::fputc('\n', f); std::fflush(f);
}

static unsigned g_rand = 12345;
static int Rand(int n) { g_rand = g_rand * 1103515245u + 12345u; return n <= 0 ? 0 : (int)((g_rand >> 8) % (unsigned)n); }

// ---------------------------------------------------------------- Game helpers
static int Me() { return N<int>(N_PLAYER_PED_ID); }
static int Player() { return N<int>(N_PLAYER_ID); }
static int Now() { return N<int>(N_GET_GAME_TIMER); }
static Vec3 Pos(int e) { return N<Vec3>(N_GET_ENTITY_COORDS, e, 1); }
static float Dist(const Vec3 &a, const Vec3 &b) { float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z; return std::sqrt(dx * dx + dy * dy + dz * dz); }
static float Dist2(const Vec3 &a, const Vec3 &b) { float dx = a.x - b.x, dy = a.y - b.y; return std::sqrt(dx * dx + dy * dy); }
static bool Exists(int e) { return e != 0 && NB(N<int>(N_DOES_ENTITY_EXIST, e)); }
static bool Dead(int e) { return NB(N<int>(N_IS_ENTITY_DEAD, e, 0)); }
static bool InAnyVehicle() { return NB(N<int>(N_IS_PED_IN_ANY_VEHICLE, Me(), 0)); }
static int MyVehicle() { return InAnyVehicle() ? N<int>(N_GET_VEHICLE_PED_IS_IN, Me(), 0) : 0; }
static bool JustPressed(int control) { return NB(N<int>(N_IS_CONTROL_JUST_PRESSED, 0, control)); }

static void Notify(const std::string &text) {
    N(N_BEGIN_TEXT_COMMAND_THEFEED_POST, "STRING");
    N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, text.c_str());
    N<int>(N_END_TEXT_COMMAND_THEFEED_POST_TICKER, 0, 1);
}
static void Subtitle(const std::string &text, int ms) {
    N(N_BEGIN_TEXT_COMMAND_PRINT, "STRING");
    N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, text.c_str());
    N(N_END_TEXT_COMMAND_PRINT, ms, 1);
}
static void Help(const char *text) {
    // a text component holds 99 characters: longer help goes in up to three, cut between the ~tokens~
    std::string s(text);
    std::vector<std::string> parts;
    for (size_t at = 0; at < s.size() && parts.size() < 3;) {
        size_t n = std::min<size_t>(90, s.size() - at);
        if (at + n < s.size()) {
            int tildes = 0;
            for (size_t i = at; i < at + n; ++i) tildes += s[i] == '~';
            if (tildes % 2) n = s.rfind('~', at + n - 1) - at;
        }
        parts.push_back(s.substr(at, n));
        at += n;
    }
    N(N_BEGIN_TEXT_COMMAND_DISPLAY_HELP, parts.size() == 3 ? "THREESTRINGS" : parts.size() == 2 ? "TWOSTRINGS" : "STRING");
    for (auto &p : parts) N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, p.c_str());
    N(N_END_TEXT_COMMAND_DISPLAY_HELP, 0, 0, 0, -1);
}
static void Help(const std::string &text) { Help(text.c_str()); }
static void Sound(const char *name, const char *set) { N(N_PLAY_SOUND_FRONTEND, -1, name, set, 0); }

static void Rect(float x, float y, float w, float h, int r, int g, int b, int a) { N(N_DRAW_RECT, x, y, w, h, r, g, b, a, 0); }
// align: 0 centre, 1 left, 2 right (wrapEnd used for right alignment)
static void Text(const std::string &t, float x, float y, float scale, int font, int r, int g, int b, int a, int align, float wrapEnd = 1.0f, bool outline = false) {
    N(N_SET_TEXT_FONT, font);
    N(N_SET_TEXT_SCALE, scale, scale);
    N(N_SET_TEXT_COLOUR, r, g, b, a);
    N(N_SET_TEXT_JUSTIFICATION, align == 0 ? 0 : align == 1 ? 1 : 2);
    if (align == 2) N(N_SET_TEXT_WRAP, 0.0f, wrapEnd);
    N(N_SET_TEXT_DROPSHADOW, 0, 0, 0, 0, 0);
    if (outline) N(N_SET_TEXT_OUTLINE);
    N(N_BEGIN_TEXT_COMMAND_DISPLAY_TEXT, "STRING");
    N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, t.c_str());
    N(N_END_TEXT_COMMAND_DISPLAY_TEXT, x, y, 0);
}
static void Text3D(const Vec3 &p, const std::string &t) {
    float sx = 0.f, sy = 0.f;
    if (NB(N<int>(N_GET_SCREEN_COORD_FROM_WORLD_COORD, p.x, p.y, p.z, &sx, &sy))) Text(t, sx, sy, 0.4f, 4, 255, 255, 255, 255, 0, 1.0f, true);
}
static void Marker(const Vec3 &p, int r, int g, int b) {
    N(N_DRAW_MARKER, 1, p.x, p.y, p.z - 1.0f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.4f, 1.4f, 0.8f, r, g, b, 150, 0, 0, 2, 0, (const char *)nullptr, (const char *)nullptr, 0);
}

static bool LoadModel(uint32_t m) {
    if (!NB(N<int>(N_IS_MODEL_IN_CDIMAGE, m)) || !NB(N<int>(N_IS_MODEL_VALID, m))) return false;
    N(N_REQUEST_MODEL, m);
    for (int i = 0; i < 100 && !NB(N<int>(N_HAS_MODEL_LOADED, m)); ++i) Wait(50);
    return NB(N<int>(N_HAS_MODEL_LOADED, m));
}
static float GroundAt(float x, float y, float zTop, float fallback) {
    float out = 0.f;
    if (NB(N<int>(N_GET_GROUND_Z_FOR_3D_COORD, x, y, zTop, &out, 0, 0))) return out;
    return fallback;
}

static void NameBlip(int b, const std::string &name) {
    N(N_BEGIN_TEXT_COMMAND_SET_BLIP_NAME, "STRING");
    N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, name.c_str());
    N(N_END_TEXT_COMMAND_SET_BLIP_NAME, b);
}
// the job session (JobSession.inc): blips made while in a job are the job's; the transition in and out
static bool InActivity();
static std::set<int> g_jobBlips;
static bool g_jobSky = false;   // Online's flight up/down is on: no fades meanwhile
static void JobSkyUp();
static void JobSkyDown();
static void JobEnter();
static bool Freemode();
static int g_jobStartedAt = 0;   // when the current job's session began
static void JobCelebrate(const std::string &job, int position, bool passed, int timeMs, int rpBefore, int rpGained, long long cash, const std::string &why = "");
static bool JobCelebrating();
static int AddBlip(int ent, int sprite, int colour, const char *name) {
    int b = N<int>(N_ADD_BLIP_FOR_ENTITY, ent);
    if (InActivity()) g_jobBlips.insert(b);
    N(N_SET_BLIP_SPRITE, b, sprite);
    N(N_SET_BLIP_COLOUR, b, colour);
    NameBlip(b, name);
    return b;
}
static int CoordBlip(const Vec3 &p, int sprite, int colour, const std::string &name, bool shortRange) {
    int b = N<int>(N_ADD_BLIP_FOR_COORD, p.x, p.y, p.z);
    if (InActivity()) g_jobBlips.insert(b);
    N(N_SET_BLIP_SPRITE, b, sprite);
    N(N_SET_BLIP_COLOUR, b, colour);
    N(N_SET_BLIP_AS_SHORT_RANGE, b, shortRange ? 1 : 0);
    NameBlip(b, name);
    return b;
}
static void RemoveBlip(int &b) { if (b && NB(N<int>(N_DOES_BLIP_EXIST, b))) N(N_REMOVE_BLIP, &b); b = 0; }
static void ForgetEntity(int &e) {
    if (Exists(e)) N(N_SET_ENTITY_AS_NO_LONGER_NEEDED, &e);
    e = 0;
}
static void DeleteEntity(int &e) {
    if (Exists(e)) { N(N_SET_ENTITY_AS_MISSION_ENTITY, e, 1, 1); N(N_DELETE_ENTITY, &e); }
    e = 0;
}

// Big Online-style message ("HEIST PASSED", "WASTED"...)
static int g_shard = 0, g_shardUntil = 0;
static void Shard(const std::string &title, const std::string &sub) {
    g_shard = N<int>(N_REQUEST_SCALEFORM_MOVIE, "MP_BIG_MESSAGE_FREEMODE");
    for (int i = 0; i < 40 && !NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, g_shard)); ++i) Wait(25);
    N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, g_shard, "SHOW_SHARD_WASTED_MP_MESSAGE");
    N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_TEXTURE_NAME_STRING, title.c_str());
    N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_TEXTURE_NAME_STRING, sub.c_str());
    N(N_END_SCALEFORM_MOVIE_METHOD);
    g_shardUntil = Now() + 6000;
}
static void DrawShard() { if (g_shard && Now() < g_shardUntil) N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, g_shard, 255, 255, 255, 255, 0); }

// ---------------------------------------------------------------- Interiors (from bob74_ipl's data)
// sets: "entity_set;entity_set:tint;..."
struct InteriorDef { const char *id; float x, y, z, h; const char *ipls; const char *sets; };
#define MANSION_SETS "set_style_cali;set_elev_cali;set_art_coastal;set_trophy_planter;set_shelving_planter;set_wallpaper_deco;set_style_cali_tint:0;set_ai_tablets_03;set_pet_cat;set_pet_dog"
#define WEED_SETS "weed_upgrade_equip;weed_security_upgrade;weed_production;weed_set_up;weed_drying;weed_chairs;" \
    "weed_growtha_stage3;weed_growthb_stage3;weed_growthc_stage3;weed_growthd_stage3;weed_growthe_stage3;weed_growthf_stage3;weed_growthg_stage3;weed_growthh_stage3;weed_growthi_stage3;" \
    "light_growtha_stage23_upgrade;light_growthb_stage23_upgrade;light_growthc_stage23_upgrade;light_growthd_stage23_upgrade;light_growthe_stage23_upgrade;" \
    "light_growthf_stage23_upgrade;light_growthg_stage23_upgrade;light_growthh_stage23_upgrade;light_growthi_stage23_upgrade"
static const InteriorDef INTERIORS[] = {
    {"hl_delperro", -1462.28f, -539.63f, 72.44f, 35.f, "mpbusiness_int_placement_interior_v_mp_apt_h_01_milo_", ""},
    {"hl_richards", -914.90f, -374.87f, 112.67f, 115.f, "mpbusiness_int_placement_interior_v_mp_apt_h_01_milo__1", ""},
    {"hl_tinsel", -609.57f, 51.28f, 96.60f, 0.f, "mpbusiness_int_placement_interior_v_mp_apt_h_01_milo__2", ""},
    {"hl_eclipse", -778.51f, 331.32f, 210.40f, 0.f, "mpbusiness_int_placement_interior_v_mp_apt_h_01_milo__3", ""},
    {"hl_integrity", -22.61f, -590.14f, 78.43f, 70.f, "mpbusiness_int_placement_interior_v_mp_apt_h_01_milo__4", ""},
    {"apt_iw30", -35.31f, -580.42f, 88.71f, 70.f, "", ""},
    {"apt_dp7", -1477.14f, -538.75f, 55.53f, 35.f, "", ""},
    {"house1", -169.29f, 486.49f, 137.44f, 190.f, "", ""},
    {"house2", 340.94f, 437.18f, 149.39f, 110.f, "", ""},
    {"house3", 373.02f, 416.11f, 145.70f, 15.f, "", ""},
    {"house4", -676.13f, 588.61f, 145.17f, 40.f, "", ""},
    {"house5", -763.11f, 615.91f, 144.14f, 290.f, "", ""},
    {"house6", -857.80f, 682.56f, 152.65f, 0.f, "", ""},
    {"house7", 120.50f, 549.95f, 184.10f, 180.f, "", ""},
    {"house8", -1288.0f, 440.75f, 97.69f, 0.f, "", ""},
    {"house_low", 261.46f, -998.82f, -99.01f, 0.f, "", ""},
    {"house_mid", 347.27f, -999.30f, -99.20f, 0.f, "", ""},
    {"exec1", -787.78f, 334.92f, 215.84f, 0.f, "apa_v_mp_h_01_a", ""},
    {"exec2", -773.23f, 322.83f, 194.89f, 0.f, "apa_v_mp_h_04_b", ""},
    {"exec3", -787.78f, 334.92f, 186.11f, 0.f, "apa_v_mp_h_07_c", ""},
    {"office_arc", -141.20f, -620.91f, 168.82f, 0.f, "ex_dt1_02_office_02b", "office_chairs"},
    {"office_mbt", -75.85f, -826.99f, 243.39f, 0.f, "ex_dt1_11_office_02b", "office_chairs"},
    {"office_lom", -1579.76f, -565.07f, 108.52f, 0.f, "ex_sm_13_office_02b", "office_chairs"},
    {"office_mbw", -1392.67f, -480.47f, 72.04f, 0.f, "ex_sm_15_office_02b", "office_chairs"},
    {"clubhouse1", 1107.04f, -3157.40f, -37.52f, 0.f, "bkr_biker_interior_placement_interior_0_biker_dlc_int_01_milo",
     "walls_02:3;furnishings_01:3;decorative_01;mural_01;gun_locker;mod_booth"},
    {"clubhouse2", 998.48f, -3164.71f, -38.91f, 0.f, "bkr_biker_interior_placement_interior_1_biker_dlc_int_02_milo",
     "walls_01:2;lower_walls_default:2;furnishings_02;decorative_02;mural_09;gun_locker;mod_booth"},
    {"meth", 1009.5f, -3196.6f, -38.99f, 0.f, "bkr_biker_interior_placement_interior_2_biker_dlc_int_ware01_milo",
     "meth_lab_upgrade;meth_lab_setup;meth_lab_security_high;meth_lab_production"},
    {"weed", 1051.49f, -3196.54f, -39.15f, 0.f, "bkr_biker_interior_placement_interior_3_biker_dlc_int_ware02_milo", WEED_SETS},
    {"coke", 1093.6f, -3196.6f, -39.0f, 0.f, "bkr_biker_interior_placement_interior_4_biker_dlc_int_ware03_milo",
     "set_up;equipment_upgrade;coke_press_upgrade;production_upgrade;table_equipment_upgrade;security_high;coke_cut_01;coke_cut_02;coke_cut_03"},
    {"cash", 1121.90f, -3195.34f, -40.40f, 0.f, "bkr_biker_interior_placement_interior_5_biker_dlc_int_ware04_milo",
     "counterfeit_upgrade_equip;counterfeit_security;dryera_on;dryerb_on;dryerc_on;dryerd_on;money_cutter;counterfeit_setup;counterfeit_cashpile100a;counterfeit_cashpile100b;counterfeit_cashpile100c;counterfeit_cashpile100d"},
    {"docs", 1165.0f, -3196.6f, -39.01f, 0.f, "bkr_biker_interior_placement_interior_6_biker_dlc_int_ware05_milo",
     "interior_upgrade;equipment_upgrade;security_high;production;set_up;clutter;chair01;chair02;chair03;chair04;chair05;chair06;chair07"},
    {"bunker", 899.55f, -3246.04f, -98.04f, 0.f, "gr_grdlc_interior_placement_interior_1_grdlc_int_02_milo_",
     "Bunker_Style_A;upgrade_bunker_set;security_upgrade;Office_Upgrade_set;gun_locker_upgrade;gun_range_lights"},
    {"hangar", -1266.80f, -3014.84f, -48.49f, 0.f, "sm_smugdlc_interior_placement_interior_0_smugdlc_int_01_milo_",
     "set_tint_shell:1;set_crane_tint:1;set_floor_2;set_floor_decal_1:1;set_lighting_hangar_a;set_lighting_wall_neutral;set_modarea:1;set_office_modern;set_bedroom_modern;set_bedroom_tint:1;set_bedroom_blinds_open"},
    {"facility", 345.0f, 4842.0f, -59.5f, 0.f, "xm_x17dlc_int_placement_interior_33_x17dlc_int_02_milo_",
     "set_int_02_shell:1;set_int_02_decal_01;set_int_02_lounge3:1;set_int_02_sleep3:1;set_int_02_security:1;set_int_02_cannon:1;Set_Int_02_outfit_khanjali;set_int_02_trophy1;set_int_02_trophy_iaa;set_int_02_trophy_sub:1;set_int_02_clutter1;set_int_02_clutter2"},
    {"nightclub", -1569.33f, -3017.28f, -74.41f, 0.f, "ba_int_placement_ba_interior_0_dlc_int_01_ba_milo_",
     "Int01_ba_clubname_01;Int01_ba_Style02;Int01_ba_style02_podium;Int01_ba_equipment_setup;Int01_ba_equipment_upgrade;Int01_ba_security_upgrade;Int01_ba_dj04;DJ_04_Lights_02;Int01_ba_bar_content;Int01_ba_booze_01;Int01_ba_booze_02;Int01_ba_booze_03;Int01_ba_trophy01"},
    {"arcade", 2732.0f, -380.0f, -49.0f, 0.f, "",
     "entity_set_constant_geometry;entity_set_arcade_set_ceiling_flat;entity_set_mural_option_05;entity_set_floor_option_01;entity_set_arcade_set_trophy_claw;entity_set_arcade_set_trophy_teller;entity_set_arcade_set_trophy_gunner;entity_set_arcade_set_trophy_brawler;entity_set_arcade_set_trophy_cabs;entity_set_plushie_01;entity_set_plushie_07;entity_set_plushie_08;entity_set_plushie_09;entity_set_big_screen;entity_set_screens"},
    {"autoshop", -1350.0f, 160.0f, -99.0f, 0.f, "",
     "entity_set_bedroom;entity_set_bombs;entity_set_car_lift_cutscene;entity_set_car_lift_default;entity_set_car_lift_purchase;entity_set_drive;entity_set_ecu;entity_set_IAA;entity_set_jammers;entity_set_laptop;entity_set_lightbox;entity_set_plate;entity_set_scope;entity_set_style_9;entity_set_thermal;entity_set_tints;entity_set_train;entity_set_virus"},
    {"agency", -1021.86f, -427.75f, 68.96f, 0.f, "sf_fixeroffice_bh1_05",
     "Entity_Set_Standard_Office;Entity_Set_Wpaper_8;Entity_Set_Moving;Entity_Set_Tint_AG;Entity_Set_Spare_Seats;Entity_Set_Player_Seats;Entity_Set_Player_Desk"},
    {"penthouse", 976.64f, 70.30f, 115.16f, 0.f, "vw_casino_penthouse",
     "Set_Pent_Tint_Shell:1;Set_Pent_Pattern_01:1;Set_Pent_Spa_Bar_Open;Set_Pent_Media_Bar_Open;Set_Pent_Dealer"},
    {"kosatka", 1561.0f, 405.0f, -48.5f, 0.f, "", "entity_set_weapons;entity_set_guide;entity_set_hatch_lights_on"},
    // special vehicles (am_mp_armory_aircraft / am_mp_hacker_truck / am_mp_armory_truck): where GTA Online puts you inside
    {"avenger", 527.08f, 4752.23f, -70.0f, 89.25f, "", "SHELL_TINT:1;CONTROL_1;CONTROL_2;CONTROL_3;WEAPONS_MOD;VEHICLE_MOD;touch_screen;thruster_dock"},
    {"terrorbyte", -1419.86f, -3012.39f, -80.0f, 91.44f, "", "Int_03_ba_Design_01;Int_03_ba_Tint:1;Int_03_ba_Light_Rig1;Int_03_ba_drone;Int_03_ba_weapons_mod;Int_03_ba_bikemod"},
    {"moc", 1103.57f, -2996.5f, -40.0f, 180.f, "", ""},
    {"mansion1", 543.85f, 712.75f, 201.0f, 0.f, "m25_2_ch2_04_mansion_interior_a;m25_2_ch2_04_mansion_interior_b;m25_2_ch2_04_mansion_interior_c", MANSION_SETS},
    {"mansion2", -1630.43f, 470.85f, 128.0f, 0.f, "m25_2_ch1_06e_mansion_interior_a;m25_2_ch1_06e_mansion_interior_b;m25_2_ch1_06e_mansion_interior_c", MANSION_SETS},
    {"mansion3", -2601.71f, 1874.83f, 166.0f, 0.f, "m25_2_ch1_09_mansion_interior_a;m25_2_ch1_09_mansion_interior_b;m25_2_ch1_09_mansion_interior_c", MANSION_SETS},
    {"vehware", 994.59f, -3002.59f, -39.65f, 0.f, "imp_impexp_interior_placement_interior_1_impexp_intwaremed_milo_", "branded_style_set;car_floor_hatch"},
    {"casino", 1089.97f, 206.0f, -49.0f, 0.f, "vw_casino_main", ""},
    {"whs_s", 1088.5f, -3099.4f, -39.0f, 270.f, "ex_exec_warehouse_placement_interior_1_int_warehouse_s_dlc_milo_", ""},   // CEO warehouses
    {"whs_m", 1048.4f, -3097.1f, -39.0f, 270.f, "ex_exec_warehouse_placement_interior_0_int_warehouse_m_dlc_milo_", ""},
    {"whs_l", 993.0f, -3097.8f, -39.0f, 270.f, "ex_exec_warehouse_placement_interior_2_int_warehouse_l_dlc_milo_", ""},
    {"datacenter", DC_SPAWN[0], DC_SPAWN[1], DC_SPAWN[2], DC_SPAWN[3], DC_IPL, "dc_equipment_basic;dc_staff_basic;dc_security_basic;dc_lights_blue"},   // Mile High Data Center (IslandBuilder Annex.cs)
    {"office_mhc", MHC_OFFICE[0], MHC_OFFICE[1], MHC_OFFICE[2], MHC_OFFICE[3], MHC_OFFICE_IPL, "office_chairs;office_booze"},   // Mile High Club: our own suite MLO (IslandBuilder MhcSuite.cs)
    {"islavilla", ISLAND_VILLAINSIDE[0], ISLAND_VILLAINSIDE[1], ISLAND_VILLAINSIDE[2] - 1.0f, 0.f, "", ""},   // the island mansion (IslandBuilder)
    {"freakshop", 570.0f, -415.0f, -68.6f, 0.f, "", ""},   // Los Santos Drug Wars: xm3_dlc_int_03_xm3 (Dax's Freakshop)
    {"acidlab", 485.0f, -2625.0f, -49.0f, 270.f, "", ""},
    {"xm3_lab", 528.25f, -2637.73f, -49.0f, 270.f, "", ""},    // xm3_dlc_int_02_xm3 (origin 520,-2625,-50): the dark lab (The Last Dose finale); the floor point from the fm_content scripts
    {"xm3_clinic", 495.0f, -2566.0f, -49.0f, 270.f, "", ""},   // xm3_dlc_int_04_xm3: the white clinic (The Last Dose); floor -50.04 found with intscan (the MLO origin 495,-2560 has none)
    // Ventures.inc: the newer businesses (floors found with intscan / Online's own interior positions)
    // entity sets from am_mp_salvage_yard / am_mp_bail_office / am_mp_sb_* (the upgraded versions)
    {"salvage", 1088.0f, -2275.0f, -49.0f, 0.f, "", "set_car_lift_01_down;set_car_lift_02_down;set_mechanic_upgrade;set_safe_upgrade;set_tint_b"},   // m23_2_dlc_int_salvage
    {"bailoffice", 563.6f, -2690.1f, -49.2f, 0.f, "", "set_staff_upgrade"},   // m24_1_dlc_int_bounty: the office floor (a storeroom below at -53.8)
    {"garment", 746.0f, -993.0f, -46.0f, 0.f, "", ""},             // m24_2_int_hacker_basement (the Hacker Den)
    {"carwash", 13.5f, -1397.1f, -74.0f, 0.f, "", "set_carwash_tints"},              // m25_1_int_tycoon_car_wash
    {"weedshop", -1161.9f, -1538.6f, -49.0f, 0.f, "", "!set_heli;!set_heli_tint;set_smoke;set_smoke_tint"},   // m25_1_int_tycoon_smoke_heli: Smoke on the Water's half
    {"helitours", -1161.9f, -1538.6f, -49.0f, 0.f, "", "!set_smoke;!set_smoke_tint;set_heli;set_heli_tint"},    // the same MLO dressed as Higgins Helitours
    // Arena War (mpchristmas2018): the Arena Workshop under the Maze Bank Arena (am_mp_arena_garage's spawn) and the arena
    {"arenaws", 209.421f, 5163.441f, -88.2f, 0.f, "xs_arena_interior_mod;xs_arena_interior_mod_2", "Set_Int_MOD_SHELL_DEF;Set_Int_MOD_BOOTH_COMBO;Set_Int_MOD_CONSTRUCTION_03;Set_Int_MOD_TROPHY_CAREER;Set_Int_MOD_TROPHY_WAR"},
    {"arena", 2800.0f, -3800.0f, 101.0f, 0.f, "xs_arena_interior", ""},   // xs_x18_int_01
    {"kortz", 2603.17f, 5903.76f, -50.0f, 0.f, "",
     "set_background_art;set_exhibit_walls;set_exhibit_robbable;set_exhibit_dressing;set_alarm_panel;set_cameras_02;set_keypads_exhibit;set_glass_pane;set_tvs"},
    {"psbank", 247.0f, 222.0f, 106.3f, 160.f, "", ""},
};
struct OBuilding;
static const InteriorDef *FindInteriorOnline(const char *id);
static const InteriorDef *FindInterior(const char *id) {
    for (const InteriorDef &d : INTERIORS) if (!std::strcmp(d.id, id)) return &d;
    return FindInteriorOnline(id);
}

// Exterior map changes that GTA Online loads (bunker doors, facility hatches, auto shops, mansions...)
static const char *WORLD_IPLS =
    "gr_case0_bunkerclosed;gr_case1_bunkerclosed;gr_case2_bunkerclosed;gr_case3_bunkerclosed;gr_case4_bunkerclosed;gr_case5_bunkerclosed;"
    "gr_case6_bunkerclosed;gr_case7_bunkerclosed;gr_case9_bunkerclosed;gr_case10_bunkerclosed;gr_case11_bunkerclosed;"
    "xm_hatch_closed;xm_siloentranceclosed_x17;xm_bunkerentrance_door;xm_hatches_terrain;xm_hatches_terrain_lod;"
    "tr_tuner_shop_burton;tr_tuner_shop_mesa;tr_tuner_shop_mission;tr_tuner_shop_rancho;tr_tuner_shop_strawberry;"
    "hei_carrier;hei_carrier_int1;hei_carrier_int2;hei_carrier_int3;hei_carrier_int4;hei_carrier_int5;hei_carrier_int6;hei_carrier_lodlights;"
    "hei_yacht_heist;hei_yacht_heist_bar;hei_yacht_heist_bar_lod;hei_yacht_heist_bedrm;hei_yacht_heist_bedrm_lod;hei_yacht_heist_bridge;"
    "hei_yacht_heist_bridge_lod;hei_yacht_heist_enginrm;hei_yacht_heist_enginrm_lod;hei_yacht_heist_lod;hei_yacht_heist_lounge;hei_yacht_heist_lounge_lod;hei_yacht_heist_slod;"
    "m23_1_legacy_fixes;h4_ch2_mansion_final;"
    "apa_ch2_04_mansion_shared;apa_ch2_04_mansion_private;apa_ch2_04_mansion_furniture;apa_ch2_04_mansion_firepit;apa_ch2_04_mansion_railings_p;"
    "m25_2_east_mansion_gym;m25_2_east_dog_house;m25_2_mansion_props;hei_ch1_roads_mansion;apa_ch2_04_mansion_shared_distantlights;apa_ch2_04_mansion_shared_lodlights;"
    "hei_ch1_06e_mansion_shared;hei_ch1_06f_mansion_shared;hei_ch1_06e_mansion_private;hei_ch1_06e_mansion_furniture;hei_ch1_06e_mansion_firepit;"
    "hei_ch1_06e_mansion_railings_p;m25_2_mansion_gym;m25_2_dog_house;hei_ch1_06e_mansion_shared_distantlights;hei_ch1_06e_mansion_shared_lodlights;"
    "hei_ch1_09_mansion_shared;hei_ch1_09_mansion_private;hei_ch1_09_mansion_furniture;hei_ch1_09_mansion_firepit;hei_ch1_09_mansion_railings_p;"
    "m25_2_tongva_mansion_gym;m25_2_tongva_dog_house;hei_ch1_09_mansion_shared_distantlights;hei_ch1_09_mansion_shared_lodlights;"
    "m26_1_mp2026_01_additions_exterior;m26_1_mp2026_01_additions_exterior_cctv;m26_1_mp2026_01_additions_kortz_lowerbarriers;"
    // the newer businesses' exteriors (Ventures.inc): salvage yard signs, bail office fronts, the garment factory door, money fronts
    "m23_2_cs1_05_reds;m23_2_cs4_11_reds;m23_2_id2_04_reds;m23_2_sc1_03_reds;m23_2_sp1_03_reds;"
    "m24_1_bailoffice_davis;m24_1_bailoffice_delperro;m24_1_bailoffice_missionrow;m24_1_bailoffice_paletobay;m24_1_bailoffice_vinewood;"
    "m24_2_garment_factory_door;m25_1_carwash;m25_1_helitours;m25_1_smokeonthewater;xs_arena_banners_ipl";

// ---------------------------------------------------------------- Properties
enum PType { T_APT, T_HOUSE, T_MANSION, T_PENTHOUSE, T_OFFICE, T_CLUB, T_METH, T_WEED, T_COKE, T_CASH, T_DOCS, T_BUNKER, T_HANGAR,
             T_FACILITY, T_NIGHTCLUB, T_ARCADE, T_AUTOSHOP, T_AGENCY, T_VEHWARE, T_KOSATKA, T_VILLA, T_GARAGE, T_DATACENTER,
             T_SALVAGE, T_BAIL, T_GARMENT, T_CARWASH, T_WEEDSHOP, T_HELITOURS, T_ARENAWS, T_WAREHOUSE, T_COUNT };
struct TypeInfo { const char *label; int blip, colour, garage; bool residence, business; };
static const TypeInfo TYPES[T_COUNT] = {
    {"Apartments", 40, 0, 10, true, false}, {"Houses", 40, 0, 10, true, false}, {"Mansions", 40, 46, 20, true, false},
    {"Casino Penthouse", 40, 46, 10, true, false}, {"Executive Offices", 475, 0, 60, false, true}, {"MC Clubhouses", 492, 0, 10, false, false},
    {"Meth Labs", 499, 0, 0, false, true}, {"Weed Farms", 496, 0, 0, false, true}, {"Cocaine Lockups", 497, 0, 0, false, true},
    {"Counterfeit Cash Factories", 500, 0, 0, false, true}, {"Document Forgery Offices", 498, 0, 0, false, true},
    {"Bunkers", 557, 0, 0, false, true}, {"Hangars", 569, 0, 0, false, true}, {"Facilities", 590, 0, 0, false, false},
    {"Nightclubs", 614, 0, 20, false, true}, {"Arcades", 740, 0, 0, false, true}, {"Auto Shops", 779, 0, 10, false, true},
    {"Agency", 826, 0, 20, false, true}, {"Vehicle Warehouses", 524, 0, 0, false, true}, {"Kosatka", 760, 0, 0, false, false},
    {"Isla Neon Villa", 40, 27, 8, true, false}, {"Garages", 357, 0, 10, false, false},
    {"Data Centers", 521, 3, 15, false, true},
    {"Salvage Yards", 68, 0, 10, false, true}, {"Bail Offices", 303, 0, 0, false, true}, {"Garment Factory", 521, 0, 0, false, true},
    {"Hands On Car Wash", 100, 0, 0, false, true}, {"Smoke on the Water", 140, 2, 0, false, true}, {"Higgins Helitours", 43, 0, 0, false, true},
    {"Arena Workshop", 643, 17, 0, false, false},
    {"Special Cargo Warehouses", 473, 0, 0, false, true},   // bought on the SecuroServ computer (BizApps.inc), like GTA Online
};
// ob: index in O_BUILDINGS (GTA Online building: exact entrances/exits), -1 = Gori Online's own placement.
// cars: garage size (0 = the type's default).
struct PropDef { const char *name; uint8_t type; int price; float x, y, z, h; const char *interior; int ob = -1; int cars = 0; };
#include "online_props.h"
#include "NewApartments.inc"   // new apartments in empty buildings (IslandBuilder Apartments.cs)
// Gori Online's own properties (businesses, bunkers, mansions...). Apartments, houses, garages,
// offices and MC clubhouses come from GTA Online's data (online_props.h).
static const PropDef PROPS_GORI[] = {
    {"The Vinewood Residence", T_MANSION, 11500000, 543.85f, 712.75f, 201.0f, 0.f, "mansion1"},
    {"Richman Villa", T_MANSION, 12800000, -1630.43f, 470.85f, 128.0f, 0.f, "mansion2"},
    {"The Tongva Estate", T_MANSION, 11800000, -2601.71f, 1874.83f, 166.0f, 0.f, "mansion3"},
    {"Diamond Casino Penthouse", T_PENTHOUSE, 1500000, 924.4f, 46.9f, 81.1f, 60.f, "penthouse"},
    // GTA Online's 22 Special Cargo warehouses (appsecuroserv.ysc: positions, sizes; prices: GTA Online)
    {"Pacific Bait Storage", T_WAREHOUSE, 376000, 50.92f, -2560.01f, 6.00f, 0.f, "whs_s"},
    {"White Widow Garage", T_WAREHOUSE, 360000, -1074.85f, -1257.45f, 5.69f, 0.f, "whs_s"},
    {"Celltowa Unit", T_WAREHOUSE, 318000, 901.94f, -1019.43f, 34.97f, 0.f, "whs_s"},
    {"Convenience Store Lockup", T_WAREHOUSE, 250000, 253.90f, -1955.93f, 22.95f, 0.f, "whs_s"},
    {"Foreclosed Garage", T_WAREHOUSE, 400000, -426.28f, 180.90f, 80.69f, 0.f, "whs_s"},
    {"Xero Gas Factory", T_WAREHOUSE, 2365000, -1050.36f, -2023.94f, 12.16f, 0.f, "whs_l"},
    {"Derriere Lingerie Backlot", T_WAREHOUSE, 902000, -1275.38f, -815.43f, 17.11f, 0.f, "whs_m"},
    {"Bilgeco Warehouse", T_WAREHOUSE, 2825000, -883.29f, -2730.54f, 12.83f, 0.f, "whs_l"},
    {"Pier 400 Utility Building", T_WAREHOUSE, 392000, 269.68f, -3016.28f, 5.72f, 0.f, "whs_s"},
    {"GEE Warehouse", T_WAREHOUSE, 880000, 1561.40f, -2138.70f, 77.53f, 0.f, "whs_m"},
    {"LS Marine Building 3", T_WAREHOUSE, 994000, -303.79f, -2698.39f, 6.00f, 0.f, "whs_m"},
    {"Railyard Warehouse", T_WAREHOUSE, 1017000, 507.21f, -649.59f, 24.75f, 0.f, "whs_m"},
    {"Fridgit Annexe", T_WAREHOUSE, 925000, -525.82f, -1775.87f, 21.35f, 0.f, "whs_m"},
    {"Disused Factory Outlet", T_WAREHOUSE, 971000, -291.64f, -1352.56f, 31.31f, 0.f, "whs_m"},
    {"Discount Retail Unit", T_WAREHOUSE, 948000, 348.64f, 324.43f, 104.32f, 0.f, "whs_m"},
    {"Logistics Depot", T_WAREHOUSE, 2600000, 922.08f, -1561.30f, 29.75f, 0.f, "whs_l"},
    {"Darnell Bros Warehouse", T_WAREHOUSE, 3500000, 763.74f, -885.21f, 25.09f, 0.f, "whs_l"},
    {"Wholesale Furniture", T_WAREHOUSE, 1900000, 1043.88f, -2172.60f, 30.47f, 0.f, "whs_l"},
    {"Cypress Warehouses", T_WAREHOUSE, 3265000, 1001.66f, -2511.88f, 28.30f, 0.f, "whs_l"},
    {"West Vinewood Backlot", T_WAREHOUSE, 2135000, -263.14f, 194.34f, 85.18f, 0.f, "whs_l"},
    {"Old Power Station", T_WAREHOUSE, 1000000, 547.81f, -1942.51f, 23.99f, 0.f, "whs_m"},
    {"Walker & Sons Warehouse", T_WAREHOUSE, 3040000, 88.97f, -2216.88f, 6.06f, 0.f, "whs_l"},
    {"Mile High Data Center", T_DATACENTER, 3450000, DC_ENTRANCE[0], DC_ENTRANCE[1], DC_ENTRANCE[2], DC_ENTRANCE[3], "datacenter"},   // the MHC annex (IslandBuilder Annex.cs)
    {"Mile High Club Office", T_OFFICE, 5650000, MHC_ENTRANCE[0], MHC_ENTRANCE[1], MHC_ENTRANCE[2], MHC_ENTRANCE[3], "office_mhc"},
    {"Meth Lab: Paleto Bay", T_METH, 910000, 51.6f, 6486.0f, 31.4f, 315.f, "meth"},
    {"Weed Farm: Mount Chiliad", T_WEED, 715000, 416.5f, 6520.4f, 27.7f, 270.f, "weed"},
    {"Cocaine Lockup: Elysian Island", T_COKE, 1500000, -253.6f, -2591.0f, 6.0f, 90.f, "coke"},
    {"Counterfeit Cash Factory: Cypress Flats", T_CASH, 1015000, 671.2f, -2667.6f, 6.1f, 90.f, "cash"},
    {"Document Forgery: Grand Senora", T_DOCS, 735000, 379.6f, 3584.5f, 33.3f, 80.f, "docs"},
    {"Bunker: Chumash", T_BUNKER, 2375000, -3180.466f, 1374.192f, 19.96f, 0.f, "bunker"},
    {"Bunker: Paleto Forest", T_BUNKER, 1650000, -783.0755f, 5934.686f, 24.31f, 0.f, "bunker"},
    {"Bunker: Raton Canyon", T_BUNKER, 1550000, -391.3216f, 4363.728f, 58.66f, 0.f, "bunker"},
    {"Bunker: Grand Senora Oilfields", T_BUNKER, 1500000, 481.0465f, 2995.135f, 43.97f, 0.f, "bunker"},
    {"Bunker: Route 68", T_BUNKER, 1475000, 24.43542f, 2959.705f, 58.36f, 0.f, "bunker"},
    {"Bunker: Grand Senora Desert", T_BUNKER, 1165000, 848.6175f, 2996.567f, 45.82f, 0.f, "bunker"},
    {"Bunker: Smoke Tree Road", T_BUNKER, 1375000, 2126.785f, 3335.04f, 48.21f, 0.f, "bunker"},
    {"Bunker: Thomson Scrapyard", T_BUNKER, 1450000, 2493.654f, 3140.399f, 51.29f, 0.f, "bunker"},
    {"Bunker: Farmhouse", T_BUNKER, 1650000, 1570.372f, 2254.549f, 78.89f, 0.f, "bunker"},
    {"Bunker: Grapeseed", T_BUNKER, 1700000, 1823.961f, 4708.14f, 42.50f, 0.f, "bunker"},
    {"Bunker: Lago Zancudo", T_BUNKER, 1750000, -3058.714f, 3329.19f, 12.58f, 0.f, "bunker"},
    {"Hangar: LSIA A17", T_HANGAR, 1200000, -1148.3f, -3406.4f, 13.9f, 330.f, "hangar"},
    {"Hangar: Fort Zancudo A2", T_HANGAR, 3250000, -2022.6f, 3154.1f, 32.8f, 240.f, "hangar"},
    {"Facility: Grand Senora Desert", T_FACILITY, 1749600, 1286.924f, 2846.06f, 49.39f, 0.f, "facility"},
    {"Facility: Route 68", T_FACILITY, 1813500, 18.633f, 2610.834f, 86.0f, 0.f, "facility"},
    {"Facility: Sandy Shores", T_FACILITY, 1960000, 2768.574f, 3919.924f, 45.82f, 0.f, "facility"},
    {"Facility: Mount Gordo", T_FACILITY, 2165000, 3406.90f, 5504.77f, 26.28f, 0.f, "facility"},
    {"Facility: Paleto Bay", T_FACILITY, 2287500, 1.90f, 6832.18f, 15.82f, 0.f, "facility"},
    {"Facility: Lago Zancudo", T_FACILITY, 2595000, -2231.53f, 2418.42f, 12.18f, 0.f, "facility"},
    {"Facility: Zancudo River", T_FACILITY, 2970000, -6.92f, 3327.0f, 41.63f, 0.f, "facility"},
    {"Facility: Ron Alternates Wind Farm", T_FACILITY, 2285000, 2073.62f, 1748.77f, 104.51f, 0.f, "facility"},
    {"Facility: Land Act Reservoir", T_FACILITY, 2450000, 1874.35f, 284.34f, 164.31f, 0.f, "facility"},
    {"Nightclub: La Mesa", T_NIGHTCLUB, 1500000, 760.0f, -1337.0f, 27.0f, 0.f, "nightclub"},
    {"Nightclub: Mission Row", T_NIGHTCLUB, 1600000, 348.0f, -979.0f, 30.0f, 0.f, "nightclub"},
    {"Nightclub: Strawberry", T_NIGHTCLUB, 1525000, -118.0f, -1260.0f, 30.0f, 0.f, "nightclub"},
    {"Nightclub: West Vinewood", T_NIGHTCLUB, 1700000, 9.0f, 221.0f, 109.0f, 0.f, "nightclub"},
    {"Nightclub: Cypress Flats", T_NIGHTCLUB, 1080000, 868.0f, -2098.0f, 31.0f, 0.f, "nightclub"},
    {"Nightclub: Del Perro", T_NIGHTCLUB, 1395000, -1287.0f, -647.0f, 27.0f, 0.f, "nightclub"},
    {"Nightclub: LSIA", T_NIGHTCLUB, 1135000, -680.0f, -2461.0f, 14.0f, 0.f, "nightclub"},
    {"Nightclub: Elysian Island", T_NIGHTCLUB, 1045000, 192.0f, -3168.0f, 6.0f, 0.f, "nightclub"},
    {"Nightclub: Downtown Vinewood", T_NIGHTCLUB, 1670000, 373.0f, 254.0f, 103.0f, 0.f, "nightclub"},
    {"Nightclub: Vespucci Canals", T_NIGHTCLUB, 1200000, -1171.0f, -1150.0f, 6.0f, 0.f, "nightclub"},
    {"Arcade: Pixel Pete's (Paleto Bay)", T_ARCADE, 1235000, -245.8f, 6212.6f, 31.5f, 45.f, "arcade"},
    {"Arcade: Videogeddon (La Mesa)", T_ARCADE, 1500000, 758.7f, -816.0f, 26.3f, 90.f, "arcade"},
    {"Auto Shop: La Mesa", T_AUTOSHOP, 1830000, 888.0f, -890.0f, 26.0f, 90.f, "autoshop"},
    {"Auto Shop: Strawberry", T_AUTOSHOP, 1670000, -66.0f, -1307.0f, 29.2f, 180.f, "autoshop"},
    {"Agency: Rockford Hills", T_AGENCY, 2830000, -1012.0f, -414.0f, 39.6f, 210.f, "agency"},
    // The Chop Shop / Bottom Dollar Bounties / Agents of Sabotage / Money Fronts (Ventures.inc): freemode.ysc's entrance locates
    {"Salvage Yard: Paleto Bay", T_SALVAGE, 1640000, -190.38f, 6268.67f, 31.49f, 131.3f, "salvage"},
    {"Salvage Yard: Sandy Shores", T_SALVAGE, 1750000, 2514.51f, 4110.80f, 38.61f, 154.3f, "salvage"},
    {"Salvage Yard: La Puerta", T_SALVAGE, 2375000, -510.21f, -1741.12f, 19.35f, 55.0f, "salvage"},
    {"Salvage Yard: Strawberry", T_SALVAGE, 2000000, -11.71f, -1311.69f, 29.30f, 90.0f, "salvage"},
    {"Salvage Yard: Murrieta Heights", T_SALVAGE, 2140000, 1191.19f, -1264.92f, 35.33f, 355.1f, "salvage"},
    {"Bail Office: Mission Row", T_BAIL, 1700000, 489.3f, -932.6f, 26.8f, 90.f, "bailoffice"},
    {"Bail Office: West Vinewood", T_BAIL, 1700000, 81.4f, 20.8f, 69.1f, 160.f, "bailoffice"},
    {"Bail Office: Del Perro", T_BAIL, 1750000, -1417.1f, -658.4f, 28.7f, 30.f, "bailoffice"},
    {"Bail Office: Davis", T_BAIL, 1600000, 137.3f, -1696.0f, 29.3f, 140.f, "bailoffice"},
    {"Bail Office: Paleto Bay", T_BAIL, 1550000, -58.6f, 6502.3f, 31.6f, 225.f, "bailoffice"},
    {"Garment Factory: La Mesa", T_GARMENT, 1950000, 716.8f, -962.0f, 30.4f, 180.f, "garment"},
    {"Hands On Car Wash: Strawberry", T_CARWASH, 1400000, 25.6f, -1409.1f, 29.8f, 270.f, "carwash"},
    {"Smoke on the Water: Vespucci", T_WEEDSHOP, 715000, -1163.7f, -1566.5f, 4.95f, 35.f, "weedshop"},
    {"Higgins Helitours: Vespucci Heliport", T_HELITOURS, 2050000, -758.7f, -1515.3f, 5.36f, 110.f, "helitours"},
    {"Arena Workshop: Maze Bank Arena", T_ARENAWS, 1595000, -366.75f, -1864.03f, 19.53f, 160.f, "arenaws"},   // ArenaWar.inc / ModShop.inc
    {"Vehicle Warehouse: La Mesa", T_VEHWARE, 1500000, 812.5f, -2380.0f, 29.6f, 270.f, "vehware"},
    {"Kosatka", T_KOSATKA, 2200000, 0.f, 0.f, 0.f, 0.f, "kosatka"},
    {"Isla Neon Villa", T_VILLA, 0, ISLAND_VILLADOOR[0], ISLAND_VILLADOOR[1], ISLAND_VILLADOOR[2], ISLAND_VILLADOOR[3], "islavilla"},
};
static std::vector<PropDef> PROPS;
static int NPROPS = 0;
static void BuildProps() {
    if (NPROPS) return;
    for (int i = 0; i < NO_PROPS; ++i) PROPS.push_back(O_PROPS[i]);
    for (const PropDef &p : PROPS_GORI) PROPS.push_back(p);
    NaptAddProps(PROPS);
    NPROPS = (int)PROPS.size();
}
static const OBuilding *BuildingOf(int i) { return i >= 0 && i < NPROPS && PROPS[i].ob >= 0 ? BldAt(PROPS[i].ob) : nullptr; }
// first entrance of a kind (1 walk-in, 2 garage) of an Online building
static const OEntrance *EntranceOf(const OBuilding *b, int kind) {
    if (b) for (int k = 0; k < b->ne; ++k) if (b->e[k].kind == kind) return &b->e[k];
    return nullptr;
}
static const InteriorDef *FindInteriorOnline(const char *id) {
    for (const InteriorDef &d : O_INTERIORS) if (!std::strcmp(d.id, id)) return &d;
    return FindInteriorNapt(id);
}
static int FindProp(const std::string &name) { for (int i = 0; i < NPROPS; ++i) if (name == PROPS[i].name) return i; return -1; }

// ---------------------------------------------------------------- State + saving
// Vehicles you own. prop = the property garage it lives in ("" = mechanic storage, "@air" hangar, "@sea" marina)
struct GVeh { std::string prop, model, plate, data; };
static std::vector<GVeh> g_garage;
static std::vector<std::string> g_props;       // owned property names
static std::string g_spawnProp;
static int g_rp = 0, g_snacks[3] = {5, 2, 1}, g_char = 1, g_playMin = 0, g_heistReadyAt = 0;
static bool g_starter = false, g_optMpMap = true, g_optIsland = true;
struct BizState { std::string prop; float supplies = 0.f, stock = 0.f; int safe = 0; };
static std::vector<BizState> g_biz;
static const int NHEISTS = 8;
static int g_heistStage[NHEISTS] = {0};
static int g_heistCut[NHEISTS] = {25, 25, 25, 25, 25, 25, 25, 25};
static int g_lastHeistPay = -1;   // the leader's cut of the finale just passed (its results show it, not the whole take)   // the crew's cut of each heist's finale, % per member (its lobby)
// Island colours (logical colour -> stunt prop tint index), tunable in GoriOnline.ini ("tints=")
static int g_bestTrial = 0;   // Isla Neon time trial record (ms)
static int g_chips = 0;       // Diamond Casino chips
static long long g_wheelNext = 0;   // Lucky Wheel: next spin allowed (unix time)
static std::map<std::string, int> g_cust;
static std::string g_yachtName;   // the Super Yacht's name (Docktease)   // property upgrades: "Property|Category" -> option
static std::vector<std::string> g_unsets;   // entity sets LoadInterior switches off (the alternatives of an upgrade)

static bool Owns(const std::string &name) { for (auto &o : g_props) if (o == name) return true; return false; }
static BizState *BizOf(const std::string &prop) { for (auto &b : g_biz) if (b.prop == prop) return &b; return nullptr; }

// Two profiles: the story characters share GoriOnline.ini; GTA Local Online's freemode "YOU"
// character has its own (properties, businesses, heists, RP...), kept in its server profile through
// LocalOnline.asi (LocalOnline_GetData/SetData, key "gorionline"). g_profileYou = which one is loaded.
static bool g_profileYou = false;
static bool YouActive();
static bool YouSetData(const std::string &text);
static bool YouGetData(std::string &text);
static std::string g_outfit[6];   // the wardrobe's saved outfits (AptActs.inc)
static std::string Serialize() {
    std::string out;
    char b[512];
    std::snprintf(b, sizeof b, "rp=%d\nsnacks=%d,%d,%d\nchar=%d\nstarter=%d\nspawn=%s\nplaymin=%d\nheistcd=%d\nmpmap=%d\nisland=%d\nbesttrial=%d\n",
                  g_rp, g_snacks[0], g_snacks[1], g_snacks[2], g_char, g_starter ? 1 : 0, g_spawnProp.c_str(), g_playMin, g_heistReadyAt,
                  g_optMpMap ? 1 : 0, g_optIsland ? 1 : 0, g_bestTrial);
    out += b;
    std::snprintf(b, sizeof b, "chips=%d\nwheelnext=%lld\n", g_chips, g_wheelNext);
    out += b;
    if (!g_yachtName.empty()) out += "yachtname=" + g_yachtName + "\n";
    for (auto &c : g_cust) out += "cust=" + c.first + "|" + std::to_string(c.second) + "\n";
    for (int i = 0; i < NHEISTS; ++i) if (g_heistStage[i]) out += "heist=" + std::to_string(i) + "|" + std::to_string(g_heistStage[i]) + "\n";
    for (auto &p : g_props) out += "prop=" + p + "\n";
    for (auto &z : g_biz) { std::snprintf(b, sizeof b, "biz=%s|%.2f|%.2f|%d\n", z.prop.c_str(), z.supplies, z.stock, z.safe); out += b; }
    for (auto &c : g_garage) out += "veh=" + c.prop + "|" + c.model + "|" + c.plate + "|" + c.data + "\n";
    for (int i = 0; i < 6; ++i) if (!g_outfit[i].empty()) out += "outfit=" + std::to_string(i) + "|" + g_outfit[i] + "\n";
    return out;
}
static void Save() {
    std::string text = Serialize();
    if (g_profileYou) { if (YouActive()) YouSetData(text); return; }   // never written into the story .ini
    FILE *f = std::fopen(g_iniPath.c_str(), "w");
    if (!f) return;
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}
static std::string Renamed(const std::string &old) {     // names used by the first version of the mod
    static const char *map[][2] = {{"Eclipse Towers", "Eclipse Towers, Apt 3"}, {"Del Perro Heights", "Del Perro Heights, Apt 4"},
        {"Richards Majestic", "Richards Majestic, Apt 2"}, {"4 Integrity Way", "4 Integrity Way, Apt 28"}, {"Tinsel Towers", "Tinsel Towers, Apt 42"},
        // v8: GTA Online's own names and buildings
        {"0115 Bay City Ave", "0115 Bay City Ave, Apt 45"}, {"2677 Whispymound Drive", "3677 Whispymound Drive"},
        {"2133 Mad Wayne Thunder Drive", "2113 Mad Wayne Thunder Drive"}, {"Clubhouse: Paleto Bay", "Paleto Bay Northeast Clubhouse"},
        {"Clubhouse: Sandy Shores", "Sandy Shores Clubhouse"}, {"Clubhouse: East Vinewood", "Downtown Vinewood Clubhouse"}};
    for (auto &m : map) if (old == m[0]) return m[1];
    return old;
}
static void LoadText(const std::string &text) {
    size_t at = 0;
    while (at < text.size()) {
        size_t nl = text.find('\n', at);
        std::string s = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = nl == std::string::npos ? text.size() : nl + 1;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string k = s.substr(0, eq), v = s.substr(eq + 1);
        if (k == "rp") g_rp = std::atoi(v.c_str());
        else if (k == "snacks") { auto p = Split(v, ','); for (int i = 0; i < 3 && i < (int)p.size(); ++i) g_snacks[i] = std::atoi(p[i].c_str()); }
        else if (k == "char") g_char = std::atoi(v.c_str());
        else if (k == "starter") g_starter = v == "1";
        else if (k == "spawn") g_spawnProp = Renamed(v);
        else if (k == "playmin") g_playMin = std::atoi(v.c_str());
        else if (k == "heistcd") g_heistReadyAt = std::atoi(v.c_str());
        else if (k == "mpmap") g_optMpMap = v != "0";
        else if (k == "island") g_optIsland = v != "0";
        else if (k == "besttrial") g_bestTrial = std::atoi(v.c_str());
        else if (k == "chips") g_chips = std::atoi(v.c_str());
        else if (k == "cust") {
            size_t q = v.rfind('|'), c = v.find('|');
            if (q != std::string::npos) g_cust[Renamed(v.substr(0, c)) + v.substr(c, q - c)] = std::atoi(v.c_str() + q + 1);
        }
        else if (k == "wheelnext") g_wheelNext = std::atoll(v.c_str());
        else if (k == "yachtname") g_yachtName = v;
        else if (k == "outfit") { size_t b = v.find('|'); int i = std::atoi(v.c_str()); if (b != std::string::npos && i >= 0 && i < 6) g_outfit[i] = v.substr(b + 1); }
        else if (k == "heist") { auto p = Split(v, '|'); int i = std::atoi(p[0].c_str()); if (i >= 0 && i < NHEISTS && p.size() > 1) g_heistStage[i] = std::atoi(p[1].c_str()); }
        else if (k == "prop") { std::string n = Renamed(v); if (FindProp(n) >= 0 && !Owns(n)) g_props.push_back(n); }
        else if (k == "biz") {
            auto p = Split(v, '|');
            if (p.size() < 4 || BizOf(p[0])) continue;
            BizState b; b.prop = Renamed(p[0]); b.supplies = (float)std::atof(p[1].c_str()); b.stock = (float)std::atof(p[2].c_str()); b.safe = std::atoi(p[3].c_str());
            g_biz.push_back(b);
        }
        else if (k == "veh") {
            auto p = Split(v, '|');
            if (p.size() < 4) continue;
            GVeh g; g.prop = Renamed(p[0]); g.model = p[1]; g.plate = p[2]; g.data = p[3];
            g_garage.push_back(g);
        }
        else if (k == "car") {    // first versions: model|primary|secondary|plate|mods
            auto p = Split(v, '|');
            GVeh g; g.prop = "?"; g.model = p[0];
            g.plate = p.size() > 3 ? p[3] : "GORI";
            while (!g.plate.empty() && g.plate.back() == ' ') g.plate.pop_back();
            while (!g.plate.empty() && g.plate.front() == ' ') g.plate.erase(g.plate.begin());
            if (p.size() > 2 && !p[1].empty() && p[1] != "-1") g.data = "c=" + p[1] + "," + p[2];
            if (p.size() > 4 && !p[4].empty()) g.data += (g.data.empty() ? "" : ";") + std::string("m=") + p[4];
            g_garage.push_back(g);
        }
    }
}
static void Load() {
    std::string text;
    if (g_profileYou) { YouGetData(text); LoadText(text); return; }
    FILE *f = std::fopen(g_iniPath.c_str(), "rb");
    if (!f) return;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    std::fclose(f);
    LoadText(text);
}
// Everything a profile holds, back to a brand-new player (the display options are not per profile).
static void ResetState() {
    g_garage.clear(); g_props.clear(); g_spawnProp.clear(); g_biz.clear(); g_cust.clear(); g_unsets.clear(); g_yachtName.clear();
    g_rp = 0; g_snacks[0] = 5; g_snacks[1] = 2; g_snacks[2] = 1; g_playMin = 0; g_heistReadyAt = 0; g_starter = false;
    for (int i = 0; i < NHEISTS; ++i) g_heistStage[i] = 0;
    g_bestTrial = 0; g_chips = 0; g_wheelNext = 0;
    for (auto &o : g_outfit) o.clear();
}

// Cash of the current story character (Michael/Franklin/Trevor), like the SP HUD shows
static void TrackCharacter() {
    uint32_t m = (uint32_t)N<int>(N_GET_ENTITY_MODEL, Me());
    int c = m == Joaat("player_zero") ? 0 : m == Joaat("player_one") ? 1 : m == Joaat("player_two") ? 2 : -1;
    if (c >= 0 && c != g_char && !g_profileYou) { g_char = c; Save(); }
}
static uint32_t CashStat() { char b[32]; std::snprintf(b, sizeof b, "SP%d_TOTAL_CASH", g_char); return Joaat(b); }
// GTA Local Online: while its freemode "YOU" character is the player, money belongs to that
// character (LocalOnline.asi keeps it), never to the story character. Resolved lazily by name.
struct LocalOnlineApi {
    bool (*isYou)() = nullptr;
    long long (*getMoney)() = nullptr;
    bool (*addMoney)(long long, const char *) = nullptr;
    long long (*getBank)() = nullptr;                              // API v2
    bool (*spend)(long long, const char *, bool) = nullptr;       // API v2
    int (*getData)(const char *, char *, int) = nullptr;           // API v3
    bool (*setData)(const char *, const char *) = nullptr;         // API v3
    bool (*changeAccount)(int, long long, const char *) = nullptr; // deposit (2, +) / withdraw (2, -)
    long long (*getWallet)() = nullptr;                            // API v2
    void (*requestLedger)() = nullptr;                             // API v4: Maze Bank transaction log
    int (*ledgerRevision)() = nullptr;
    int (*getLedger)(char *, int) = nullptr;
    bool (*jobOpen)(const char *, float, float, float, int) = nullptr;   // API v5: co-op jobs (Coop.inc)
    void (*jobUpdate)(int, int, float, float, float, const char *) = nullptr;
    void (*jobJoin)(unsigned) = nullptr;
    int (*jobRevision)() = nullptr;
    int (*getJobs)(char *, int) = nullptr;
    int (*getJobState)(char *, int) = nullptr;
    bool (*jobOpenKind)(const char *, float, float, float, int, int) = nullptr;   // API v6: races (Races.inc)
    void (*raceProgress)(int, int) = nullptr;
    int (*getRaceStandings)(char *, int) = nullptr;
    void (*setPlayerInfo)(int, int) = nullptr;                     // API v7: rank + flags (passive, in an activity), the players, the session
    int (*getPlayers)(char *, int) = nullptr;
    unsigned (*getSession)() = nullptr;
    int (*getPlayerName)(char *, int) = nullptr;
    void (*highlightPlayer)(unsigned, int) = nullptr;
    bool (*adoptOutfit)() = nullptr;                               // API v8: the outfit worn now becomes the character's (AptActs.inc wardrobe)
    bool (*sendPlayerMessage)(unsigned, const char *, const char *) = nullptr;   // API v9: player messages (Buzzer.inc)
    int (*takePlayerMessages)(char *, int) = nullptr;
    bool tried = false;
};
static LocalOnlineApi &LO() {
    static LocalOnlineApi api;
    if (!api.tried) {
        api.tried = true;
        if (HMODULE h = GetModuleHandleA("LocalOnline.asi")) {
            api.isYou = (bool (*)())GetProcAddress(h, "LocalOnline_IsYouActive");
            api.getMoney = (long long (*)())GetProcAddress(h, "LocalOnline_GetMoney");
            api.addMoney = (bool (*)(long long, const char *))GetProcAddress(h, "LocalOnline_AddMoney");
            if (!api.isYou || !api.getMoney || !api.addMoney) api.isYou = nullptr;
            api.getBank = (long long (*)())GetProcAddress(h, "LocalOnline_GetBank");
            api.spend = (bool (*)(long long, const char *, bool))GetProcAddress(h, "LocalOnline_Spend");
            api.getData = (int (*)(const char *, char *, int))GetProcAddress(h, "LocalOnline_GetData");
            api.setData = (bool (*)(const char *, const char *))GetProcAddress(h, "LocalOnline_SetData");
            api.changeAccount = (bool (*)(int, long long, const char *))GetProcAddress(h, "LocalOnline_ChangeAccount");
            api.getWallet = (long long (*)())GetProcAddress(h, "LocalOnline_GetWallet");
            api.requestLedger = (void (*)())GetProcAddress(h, "LocalOnline_RequestLedger");
            api.ledgerRevision = (int (*)())GetProcAddress(h, "LocalOnline_LedgerRevision");
            api.getLedger = (int (*)(char *, int))GetProcAddress(h, "LocalOnline_GetLedger");
            api.jobOpen = (bool (*)(const char *, float, float, float, int))GetProcAddress(h, "LocalOnline_JobOpen");
            api.jobUpdate = (void (*)(int, int, float, float, float, const char *))GetProcAddress(h, "LocalOnline_JobUpdate");
            api.jobJoin = (void (*)(unsigned))GetProcAddress(h, "LocalOnline_JobJoin");
            api.jobRevision = (int (*)())GetProcAddress(h, "LocalOnline_JobRevision");
            api.getJobs = (int (*)(char *, int))GetProcAddress(h, "LocalOnline_GetJobs");
            api.getJobState = (int (*)(char *, int))GetProcAddress(h, "LocalOnline_GetJobState");
            if (!api.jobOpen || !api.jobUpdate || !api.jobJoin || !api.jobRevision || !api.getJobs || !api.getJobState)
                api.jobOpen = nullptr, api.getJobs = nullptr, api.jobRevision = nullptr;
            api.jobOpenKind = (bool (*)(const char *, float, float, float, int, int))GetProcAddress(h, "LocalOnline_JobOpenKind");
            api.raceProgress = (void (*)(int, int))GetProcAddress(h, "LocalOnline_RaceProgress");
            api.getRaceStandings = (int (*)(char *, int))GetProcAddress(h, "LocalOnline_GetRaceStandings");
            if (!api.jobOpenKind || !api.raceProgress || !api.getRaceStandings) api.jobOpenKind = nullptr;
            api.setPlayerInfo = (void (*)(int, int))GetProcAddress(h, "LocalOnline_SetPlayerInfo");
            api.getPlayers = (int (*)(char *, int))GetProcAddress(h, "LocalOnline_GetPlayers");
            api.getSession = (unsigned (*)())GetProcAddress(h, "LocalOnline_GetSession");
            api.getPlayerName = (int (*)(char *, int))GetProcAddress(h, "LocalOnline_GetPlayerName");
            api.highlightPlayer = (void (*)(unsigned, int))GetProcAddress(h, "LocalOnline_HighlightPlayer");
            api.adoptOutfit = (bool (*)())GetProcAddress(h, "LocalOnline_AdoptOutfit");
            api.sendPlayerMessage = (bool (*)(unsigned, const char *, const char *))GetProcAddress(h, "LocalOnline_SendPlayerMessage");
            api.takePlayerMessages = (int (*)(char *, int))GetProcAddress(h, "LocalOnline_TakePlayerMessages");
            if (!api.sendPlayerMessage || !api.takePlayerMessages) api.sendPlayerMessage = nullptr, api.takePlayerMessages = nullptr;
        }
    }
    return api;
}
static bool YouActive() { return LO().isYou && LO().isYou(); }
static bool YouSetData(const std::string &text) { return LO().setData && LO().setData("gorionline", text.c_str()); }
static bool YouGetData(std::string &text) {
    text.clear();
    if (!LO().getData) return false;
    int n = LO().getData("gorionline", nullptr, 0);
    if (n <= 0) return false;
    std::string buf((size_t)n + 1, '\0');
    LO().getData("gorionline", &buf[0], n + 1);
    buf.resize((size_t)n);
    text = buf;
    return true;
}
// Like GTA Online: the internet and phone pay from the bank; shops take cash first, then the bank.
static bool g_payFromBank = false;
// what is being paid for, for the Maze Bank transaction log (GTA Online's MONEY_SPENT_* categories)
static const char *g_payWhat = nullptr;
struct PayWhat { const char *old; PayWhat(const char *w) : old(g_payWhat) { g_payWhat = w; } ~PayWhat() { g_payWhat = old; } };
static long long BankBalance() { return YouActive() && LO().getBank ? LO().getBank() : -1; }
static int GetCash() {
    if (YouActive()) { long long m = LO().getMoney(); return m > 2147483000LL ? 2147483000 : (int)m; }
    int v = 0; N<int>(N_STAT_GET_INT, CashStat(), &v, -1); return v;
}
static void SetCash(int v) {
    if (YouActive()) { LO().addMoney((long long)(v < 0 ? 0 : v) - LO().getMoney(), "Gori Online"); return; }
    N<int>(N_STAT_SET_INT, CashStat(), v < 0 ? 0 : v, 1);
}
static void AddCash(int v) { long long t = (long long)GetCash() + v; SetCash(t > 2147483000LL ? 2147483000 : (int)t); }
static bool Pay(int amount) {
    if (GetCash() < amount) {
        if (YouActive() && g_payFromBank) Notify("~r~Insufficient funds~s~ in your Maze Bank account. You need " + Money(amount) + ".");
        else Notify("~r~Not enough cash.~s~ You need " + Money(amount) + ".");
        Sound("ERROR", "HUD_FRONTEND_DEFAULT_SOUNDSET");
        return false;
    }
    if (YouActive() && LO().spend) {
        std::string why = std::string("Gori Online: ") + (g_payWhat ? g_payWhat : g_payFromBank ? "internet" : "shop");
        if (!LO().spend(amount, why.c_str(), g_payFromBank)) {
            Notify("~r~Payment declined.~s~"); Sound("ERROR", "HUD_FRONTEND_DEFAULT_SOUNDSET"); return false;
        }
    } else AddCash(-amount);
    Sound("PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET");
    return true;
}

// ---------------------------------------------------------------- RP / rank
// GTA Online's RP table (freemode.ysc func_2211: ranks 1-99, then +28,500 + 50 per rank)
static const int ONLINE_RP[100] = {0,0,800,2100,3800,6100,9500,12500,16000,19800,24000,28500,33400,38700,44200,50200,56400,63000,69900,77100,84700,92500,100700,109200,118000,127100,136500,146200,156200,166500,177100,188000,199200,210700,222400,234500,246800,259400,272300,285500,299000,312700,326800,341000,355600,370500,385600,401000,416600,432600,448800,465200,482000,499000,516300,533800,551600,569600,588000,606500,625400,644500,683400,683400,703300,723400,743800,764500,785400,806500,827900,849600,871500,893600,916000,938700,961600,984700,1008100,1031800,1055700,1079800,1104200,1128800,1153700,1178800,1204200,1229800,1255600,1281700,1308100,1334600,1361400,1388500,1415800,1443300,1471100,1499100,1527300,1555800};
static int RpFor(int level) {
    if (level <= 1) return 0;
    if (level > 8000) level = 8000;
    if (level <= 99) return ONLINE_RP[level];
    long long n = level - 99;
    long long rp = 1555800LL + n * 28500LL + (n * (n + 1) / 2) * 50LL;
    return rp > 2000000000LL ? 2000000000 : (int)rp;
}
static int LevelOf(int rp) {
    int lo = 1, hi = 8000;
    while (lo < hi) { int mid = (lo + hi + 1) / 2; if (RpFor(mid) <= rp) lo = mid; else hi = mid - 1; }
    return lo;
}
static int g_rankShowUntil = 0, g_lastLevel = 1;
static bool g_menuOpen = false;
// GTA Online's rank bar: script HUD movie 19 (MP_RANK_BAR), driven like freemode.ysc func_15504..15508
static int g_rbFrom = -1, g_rbTo = -1, g_rbMs = 0, g_rbAskedAt = 0;
static bool RankBarReady() {
    if (NB(N<int>(N_HAS_SCALEFORM_SCRIPT_HUD_MOVIE_LOADED, 19))) return true;
    N(N_REQUEST_SCALEFORM_SCRIPT_HUD_MOVIE, 19);
    if (!g_rbAskedAt) g_rbAskedAt = Now();
    return false;
}
static void RankBarCall(const char *method, std::initializer_list<int> args) {
    N(N_BEGIN_SCALEFORM_SCRIPT_HUD_MOVIE_METHOD, 19, method);
    for (int v : args) N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, v);
    N(N_END_SCALEFORM_MOVIE_METHOD);
}
static void RankBarShow(int fromRp, int toRp, int ms) {
    if (!RankBarReady()) { g_rbFrom = fromRp; g_rbTo = toRp; g_rbMs = ms; return; }   // shown by RankTick once loaded
    g_rbFrom = g_rbTo = -1;
    RankBarCall("SET_COLOUR", {116, 123});                                           // HUD_COLOUR_FREEMODE / _DARK
    if (fromRp != toRp) RankBarCall("OVERRIDE_ANIMATION_SPEED", {2000});
    RankBarCall("OVERRIDE_ONSCREEN_DURATION", {ms});
    int l0 = LevelOf(fromRp), l1 = LevelOf(toRp);
    for (int l = l0; l <= l1 && l < l0 + 20; ++l)                                    // one segment per rank crossed, like Online
        RankBarCall("SET_RANK_SCORES", {RpFor(l), RpFor(l + 1), l == l0 ? fromRp : RpFor(l), l == l1 ? toRp : RpFor(l + 1), l, 100});
}
static int g_zHeldSince = 0;
static void RankTick() {
    if (g_rbFrom >= 0 && RankBarReady()) RankBarShow(g_rbFrom, g_rbTo, g_rbMs);
    // Z / d-pad down (INPUT_MULTIPLAYER_INFO): GTA Online shows the rank bar while you hold it
    bool z = NB(N<int>(N_IS_CONTROL_PRESSED, 0, 20)) || NB(N<int>(N_IS_DISABLED_CONTROL_PRESSED, 0, 20));
    if (z && !g_menuOpen) {
        if (!g_zHeldSince || Now() - g_zHeldSince > 3500) { g_zHeldSince = Now(); RankBarShow(g_rp, g_rp, 4000); }
    } else g_zHeldSince = 0;
}
static void GiveRP(int amount, const char *reason) {
    int before = g_rp;
    g_rp += amount;
    g_rankShowUntil = Now() + 5000;
    RankBarShow(before, g_rp, 5000);
    int lvl = LevelOf(g_rp);
    if (lvl > g_lastLevel) { g_lastLevel = lvl; Subtitle("~b~RANK UP~s~ - Rank " + std::to_string(lvl), 4000); Sound("RANK_UP", "HUD_AWARDS"); }
    if (reason) Notify("~b~+" + std::to_string(amount) + " RP~s~ " + reason);
    Save();
}
static void DrawRank(bool force) {
    if (!force && Now() > g_rankShowUntil) return;
    if (NB(N<int>(N_HAS_SCALEFORM_SCRIPT_HUD_MOVIE_LOADED, 19)) || (g_rbAskedAt && Now() - g_rbAskedAt < 5000)) return;   // the real rank bar is shown
    int lvl = LevelOf(g_rp), from = RpFor(lvl), to = RpFor(lvl + 1);
    float frac = to > from ? (float)(g_rp - from) / (float)(to - from) : 0.f;
    Rect(0.5f, 0.04f, 0.32f, 0.012f, 0, 0, 0, 160);
    Rect(0.5f - 0.16f + 0.16f * frac, 0.04f, 0.32f * frac, 0.012f, 70, 170, 255, 230);
    Text(std::to_string(lvl), 0.325f, 0.022f, 0.55f, 7, 255, 255, 255, 255, 0);
    Text(std::to_string(lvl + 1), 0.675f, 0.022f, 0.55f, 7, 255, 255, 255, 255, 0);
    Text(std::to_string(g_rp) + " / " + std::to_string(to) + " RP", 0.5f, 0.05f, 0.3f, 0, 255, 255, 255, 255, 0);
}

// ---------------------------------------------------------------- Teleport + interiors
static void Teleport(float x, float y, float z, float heading) {
    int me = Me();
    int mover = InAnyVehicle() ? MyVehicle() : me;
    bool sky = g_jobSky;   // in the middle of Online's job transition: the flight hides the move
    if (!sky) { N(N_DO_SCREEN_FADE_OUT, 500); Wait(600); }
    N(N_SET_ENTITY_COORDS, mover, x, y, z + 50.f, 0, 0, 0, 0);
    N(N_REQUEST_COLLISION_AT_COORD, x, y, z);
    float gz = z;
    for (int i = 0; i < 40; ++i) {
        Wait(50);
        float out = 0.f;
        if (NB(N<int>(N_GET_GROUND_Z_FOR_3D_COORD, x, y, z + 50.f, &out, 0, 0))) { gz = out; break; }
    }
    N(N_SET_ENTITY_COORDS, mover, x, y, gz + 0.5f, 0, 0, 0, 0);
    N(N_SET_ENTITY_HEADING, mover, heading);
    Wait(300);
    if (!sky) N(N_DO_SCREEN_FADE_IN, 500);
}
// GTA Online's points carry the right height (doors under canopies, garages under buildings): only
// snap to the floor right below them, never to whatever is highest above.
static void TeleportExact(float x, float y, float z, float heading) {
    int me = Me();
    int mover = InAnyVehicle() ? MyVehicle() : me;
    N(N_DO_SCREEN_FADE_OUT, 500);
    Wait(600);
    N(N_SET_ENTITY_COORDS, mover, x, y, z + 1.0f, 0, 0, 0, 0);
    N(N_FREEZE_ENTITY_POSITION, mover, 1);
    N(N_REQUEST_COLLISION_AT_COORD, x, y, z);
    for (int i = 0; i < 40 && !NB(N<int>(N_HAS_COLLISION_LOADED_AROUND_ENTITY, mover)); ++i) Wait(50);
    float gz = 0.f;
    if (!NB(N<int>(N_GET_GROUND_Z_FOR_3D_COORD, x, y, z + 1.5f, &gz, 0, 0)) || std::fabs(gz - z) > 2.f) gz = z;
    N(N_SET_ENTITY_COORDS, mover, x, y, gz + 0.5f, 0, 0, 0, 0);
    N(N_SET_ENTITY_HEADING, mover, heading);
    N(N_FREEZE_ENTITY_POSITION, mover, 0);
    Wait(300);
    N(N_DO_SCREEN_FADE_IN, 500);
}
static int LoadInterior(const InteriorDef &d) {
    if (!std::strcmp(d.id, "islavilla")) { g_unsets.clear(); return 0; }   // the island mansion is our own geometry, not an MLO
    for (auto &ipl : Split(d.ipls, ';')) if (!ipl.empty() && !NB(N<int>(N_IS_IPL_ACTIVE, ipl.c_str()))) N(N_REQUEST_IPL, ipl.c_str());
    int id = 0;
    for (int i = 0; i < 160; ++i) {   // up to 8 s: streaming in from across the map
        id = N<int>(N_GET_INTERIOR_AT_COORDS, d.x, d.y, d.z);
        if (id && NB(N<int>(N_IS_VALID_INTERIOR, id))) break;
        id = 0; Wait(50);
    }
    if (!id) { Log("interior %s: not found at %.1f %.1f %.1f", d.id, d.x, d.y, d.z); return 0; }
    N(N_PIN_INTERIOR_IN_MEMORY, id);
    for (auto &u : g_unsets) if (NB(N<int>(N_IS_INTERIOR_ENTITY_SET_ACTIVE, id, u.c_str()))) N(N_DEACTIVATE_INTERIOR_ENTITY_SET, id, u.c_str());
    g_unsets.clear();
    for (auto &s : Split(d.sets, ';')) {
        if (s.empty()) continue;
        if (s[0] == '!') {   // "!name": off here (MLOs dressed as two businesses)
            if (NB(N<int>(N_IS_INTERIOR_ENTITY_SET_ACTIVE, id, s.c_str() + 1))) N(N_DEACTIVATE_INTERIOR_ENTITY_SET, id, s.c_str() + 1);
            continue;
        }
        auto p = Split(s, ':');
        if (!NB(N<int>(N_IS_INTERIOR_ENTITY_SET_ACTIVE, id, p[0].c_str()))) N(N_ACTIVATE_INTERIOR_ENTITY_SET, id, p[0].c_str());
        if (p.size() > 1) N(N_SET_INTERIOR_ENTITY_SET_TINT_INDEX, id, p[0].c_str(), std::atoi(p[1].c_str()));
    }
    N(N_REFRESH_INTERIOR, id);
    return id;
}
static bool TeleportInside(const InteriorDef &d) {
    int me = Me();
    if (InAnyVehicle()) { N(N_TASK_LEAVE_VEHICLE, me, MyVehicle(), 16); Wait(300); }
    N(N_DO_SCREEN_FADE_OUT, 400);
    Wait(450);
    // go there first (frozen, faded out) so the area and its interior stream in, then look for the interior: from far away
    // (a respawn at the hospital) it is not loaded yet and would not be found
    N(N_SET_ENTITY_COORDS, me, d.x, d.y, d.z + 1.0f, 0, 0, 0, 0);
    N(N_FREEZE_ENTITY_POSITION, me, 1);
    N(N_REQUEST_COLLISION_AT_COORD, d.x, d.y, d.z);
    int id = LoadInterior(d);
    for (int i = 0; id && i < 80 && !NB(N<int>(N_IS_INTERIOR_READY, id)); ++i) Wait(50);
    N(N_REQUEST_COLLISION_AT_COORD, d.x, d.y, d.z);
    Wait(500);
    float gz = GroundAt(d.x, d.y, d.z + 1.5f, -1000.f);
    bool floor = gz > d.z - 4.f && gz < d.z + 1.6f;
    if (floor) N(N_SET_ENTITY_COORDS, me, d.x, d.y, gz + 1.0f, 0, 0, 0, 0);
    if (!id && !floor) { N(N_FREEZE_ENTITY_POSITION, me, 0); return false; }   // nothing to stand on (the caller takes the player out; the screen stays black)
    N(N_SET_ENTITY_HEADING, me, d.h);
    N(N_FREEZE_ENTITY_POSITION, me, 0);
    N(N_SET_GAMEPLAY_CAM_RELATIVE_HEADING, 0.f);
    Wait(200);
    N(N_DO_SCREEN_FADE_IN, 500);
    return true;
}

// ---------------------------------------------------------------- Isla Neón (the map itself is the islaneon DLC built by IslandBuilder)
static const float ISLAND_CX = ISLAND_CENTER[0], ISLAND_CY = ISLAND_CENTER[1];
static int g_islBlip = 0;
static bool IslandNear(const Vec3 &p, float extra = 0.f) { return Dist2(p, V3(ISLAND_CX, ISLAND_CY, 0.f)) < ISLAND_RADIUS + extra; }
static void IslandArrive(float x, float y, float z, float heading) {
    Teleport(x, y, z, heading);
    Vec3 p = Pos(Me());
    Log("island arrive: wanted z %.1f, now at %.1f %.1f %.1f, ground %.1f", z, p.x, p.y, p.z, GroundAt(p.x, p.y, p.z + 5.f, -999.f));
}

// ---------------------------------------------------------------- Vehicle catalogue (Online websites)
struct CatCar { std::string model, name; uint32_t hash; int cls, price; bool weapon; };
static std::vector<CatCar> g_cat;
static std::map<uint32_t, int> g_catByHash;
static const char *CLASS_NAMES[] = {"Compacts", "Sedans", "SUVs", "Coupes", "Muscle", "Sports Classics", "Sports", "Super", "Motorcycles", "Off-Road",
    "Industrial", "Utility", "Vans", "Cycles", "Boats", "Helicopters", "Planes", "Service", "Emergency", "Military", "Commercial", "Trains", "Open Wheel"};
enum Site { SITE_LEGENDARY, SITE_SSASA, SITE_WARSTOCK, SITE_ELITAS, SITE_DOCKTEASE, SITE_COUNT };
static const char *SITE_NAMES[SITE_COUNT] = {"Legendary Motorsport", "Southern S.A. Super Autos", "Warstock Cache & Carry", "Elitas Travel", "Docktease"};
// Add-on vehicles installed in mods/update/x64/dlcpacks (2026-10-18, from gta5-mods.com): price and website by the
// nearest vanilla Online car. They are not in the game's DLC vehicle list (no shop data), so they are listed here.
struct AddonRow { const char *model; int cls, price, site; };
static const AddonRow ADDON_ROWS[] = {
    {"knowayest", 17, 925000, SITE_SSASA},       // Knoway Ocelot E-Stride (self-driving taxi; Knoway.inc)
    {"warrenerc", 1, 1190000, SITE_SSASA},       // Vulcar Warrener Custom (Warrener HKR $1,260,000)
    {"batic", 8, 330000, SITE_SSASA},            // Pegassi Bati 801 Custom
    {"sxkim38", 6, 1450000, SITE_LEGENDARY},     // Kimera EVO38 (Tropos Rallye)
    {"cometmans", 6, 1950000, SITE_LEGENDARY},   // Pfister Comet EVO Monarch (Comet S2 $1,878,000)
    {"ventoux", 7, 2850000, SITE_LEGENDARY},     // Truffade Ventoux (Thrax $2,325,000)
    {"schaft", 6, 135000, SITE_SSASA},           // Benefactor Schafter Restyling (Schafter V12 $116,000)
    {"lexicorn", 6, 1650000, SITE_LEGENDARY},    // Coil Lexicorn (Coil Raiden $1,375,000)
    {"kuruma10", 6, 95000, SITE_SSASA},          // Karin Kuruma 2010 base model (Kuruma $126,350)
    {"tiberius", 7, 2450000, SITE_LEGENDARY},    // Truffade Tiberius (Nero / Thrax)
};
static std::map<uint32_t, int> g_siteOverride;
static int SiteOf(const CatCar &c) {
    auto ov = g_siteOverride.find(c.hash);
    if (ov != g_siteOverride.end()) return ov->second;
    if (c.cls == 14) return SITE_DOCKTEASE;
    if (c.cls == 15 || c.cls == 16) return SITE_ELITAS;
    if (c.weapon || c.cls == 18 || c.cls == 19) return SITE_WARSTOCK;
    if (c.cls == 7 || c.cls == 22 || ((c.cls == 5 || c.cls == 6) && c.price >= 400000)) return SITE_LEGENDARY;
    return SITE_SSASA;
}
static std::string Label(const char *label) {
    if (!label || !*label) return "";
    const char *t = N<const char *>(N_GET_FILENAME_FOR_AUDIO_CONVERSATION, label);
    if (!t || !*t || !std::strcmp(t, "NULL")) return "";
    return t;
}
static void BuildCatalog() {
    static const int base[] = {18000, 30000, 70000, 90000, 60000, 450000, 300000, 1700000, 60000, 120000, 150000, 80000, 45000, 1000, 250000,
                               1500000, 1600000, 60000, 150000, 1800000, 150000, 0, 3000000};
    auto add = [](const std::string &model, uint32_t h, int cls, int price, bool weapon) {
        if (g_catByHash.count(h) || cls == 21) return;
        if (!NB(N<int>(N_IS_MODEL_IN_CDIMAGE, h)) || !NB(N<int>(N_IS_MODEL_A_VEHICLE, h))) return;
        CatCar c; c.model = model; c.hash = h; c.cls = cls; c.price = price; c.weapon = weapon;
        const char *dn = N<const char *>(N_GET_DISPLAY_NAME_FROM_VEHICLE_MODEL, h);
        std::string name = Label(dn);
        if (name.empty()) name = dn && *dn && std::strcmp(dn, "CARNOTFOUND") ? dn : model;
        std::string make = Label(N<const char *>(N_GET_MAKE_NAME_FROM_VEHICLE_MODEL, h));
        c.name = make.empty() || name.rfind(make, 0) == 0 ? name : make + " " + name;
        g_catByHash[h] = (int)g_cat.size();
        g_cat.push_back(c);
    };
    for (const VehRow &r : VEH_ROWS) add(r.model, Joaat(r.model), r.cls, r.price, r.weapon != 0);
    int addons = 0;
    for (const AddonRow &r : ADDON_ROWS) {
        uint32_t h = Joaat(r.model);
        if (!g_catByHash.count(h)) { add(r.model, h, r.cls, r.price, false); if (g_catByHash.count(h)) ++addons; }
        g_siteOverride[h] = r.site;
    }
    Log("catalogue: %d add-on vehicles", addons);
    int n = N<int>(N_GET_NUM_DLC_VEHICLES), extra = 0;
    for (int i = 0; i < n; ++i) {
        uint32_t h = (uint32_t)N<int>(N_GET_DLC_VEHICLE_MODEL, i);
        if (!h || g_catByHash.count(h)) continue;
        int cls = N<int>(N_GET_VEHICLE_CLASS_FROM_NAME, h);
        char hex[16]; std::snprintf(hex, sizeof hex, "0x%08X", h);
        add(hex, h, cls, cls >= 0 && cls <= 22 ? base[cls] : 100000, false);
        extra++;
    }
    std::vector<CatCar> sorted = g_cat;
    std::sort(sorted.begin(), sorted.end(), [](const CatCar &a, const CatCar &b) { return a.price != b.price ? a.price < b.price : a.name < b.name; });
    g_cat = sorted;
    g_catByHash.clear();
    for (int i = 0; i < (int)g_cat.size(); ++i) g_catByHash[g_cat[i].hash] = i;
    Log("catalogue: %d vehicles (%d from the DLC list, %d DLC vehicles in the game)", (int)g_cat.size(), extra, n);
}
static const CatCar *FindCar(const std::string &model) {
    auto it = g_catByHash.find(ModelHash(model));
    return it == g_catByHash.end() ? nullptr : &g_cat[it->second];
}
static std::string CarName(const std::string &model) { const CatCar *c = FindCar(model); return c ? c->name : model; }
static bool AirOrSea(int cls) { return cls == 14 || cls == 15 || cls == 16; }
// Each property garage is the 10-car garage under the city (like Online's high-end garage)
static int GarageSlots(int pi) {
    if (pi < 0) return 0;
    if (!std::strcmp(PROPS[pi].name, "Mile High Club Office")) return 30;
    return PROPS[pi].cars ? PROPS[pi].cars : TYPES[PROPS[pi].type].garage;
}
static int CountIn(const std::string &prop) { int n = 0; for (auto &g : g_garage) if (g.prop == prop) n++; return n; }
static const int MECHANIC_SLOTS = 10;
static std::string FreeGarage() {    // "!" when everything is full
    for (auto &o : g_props) { int i = FindProp(o); if (i >= 0 && GarageSlots(i) > 0 && CountIn(o) < GarageSlots(i)) return o; }
    return CountIn("") < MECHANIC_SLOTS ? "" : "!";
}
static int GarageCapacity() {
    int cap = MECHANIC_SLOTS;
    for (auto &o : g_props) { int i = FindProp(o); if (i >= 0) cap += GarageSlots(i); }
    return cap;
}
static std::string GarageLabel(const std::string &p) { return p.empty() ? "Mechanic" : p == "@air" ? "Hangar" : p == "@sea" ? "Marina" : p; }
static void AssignGarages() {    // vehicles from older saves get a home
    for (auto &g : g_garage) {
        if (g.prop != "?") continue;
        const CatCar *c = FindCar(g.model);
        if (c && AirOrSea(c->cls)) { g.prop = c->cls == 14 ? "@sea" : "@air"; continue; }
        g.prop = "";
        std::string f = FreeGarage();
        g.prop = f == "!" ? "" : f;
    }
}

// ---------------------------------------------------------------- Personal vehicle
static int g_personal = 0, g_personalBlip = 0, g_personalIdx = -1;

// Everything LS Customs can change, as "key=value;..." (see ApplyVehicle)
static std::string CaptureVehicle(int v) {
    std::string d;
    auto add = [&](const char *k, std::initializer_list<int> vals) {
        d += (d.empty() ? "" : ";") + std::string(k) + "=";
        bool first = true;
        for (int x : vals) { d += (first ? "" : ",") + std::to_string(x); first = false; }
    };
    int a = 0, b = 0, c = 0;
    N(N_GET_VEHICLE_COLOURS, v, &a, &b); add("c", {a, b});
    N(N_GET_VEHICLE_EXTRA_COLOURS, v, &a, &b); add("x", {a, b});
    N(N_GET_VEHICLE_EXTRA_COLOUR_5, v, &a); N(N_GET_VEHICLE_EXTRA_COLOUR_6, v, &b); add("ic", {a, b});
    if (NB(N<int>(N_GET_IS_VEHICLE_PRIMARY_COLOUR_CUSTOM, v))) { N(N_GET_VEHICLE_CUSTOM_PRIMARY_COLOUR, v, &a, &b, &c); add("r1", {a, b, c}); }
    if (NB(N<int>(N_GET_IS_VEHICLE_SECONDARY_COLOUR_CUSTOM, v))) { N(N_GET_VEHICLE_CUSTOM_SECONDARY_COLOUR, v, &a, &b, &c); add("r2", {a, b, c}); }
    add("wt", {N<int>(N_GET_VEHICLE_WHEEL_TYPE, v)});
    add("tn", {N<int>(N_GET_VEHICLE_WINDOW_TINT, v)});
    add("ne", {N<int>(N_GET_VEHICLE_NEON_ENABLED, v, 0) & 1, N<int>(N_GET_VEHICLE_NEON_ENABLED, v, 1) & 1, N<int>(N_GET_VEHICLE_NEON_ENABLED, v, 2) & 1, N<int>(N_GET_VEHICLE_NEON_ENABLED, v, 3) & 1});
    N(N_GET_VEHICLE_NEON_COLOUR, v, &a, &b, &c); add("nc", {a, b, c});
    N(N_GET_VEHICLE_TYRE_SMOKE_COLOR, v, &a, &b, &c); add("ts", {a, b, c});
    add("xc", {N<int>(N_GET_VEHICLE_XENON_LIGHT_COLOR_INDEX, v)});
    add("lv", {N<int>(N_GET_VEHICLE_LIVERY, v)});
    add("pt", {N<int>(N_GET_VEHICLE_NUMBER_PLATE_TEXT_INDEX, v)});
    int exists = 0, on = 0;
    for (int i = 0; i <= 14; ++i) if (NB(N<int>(N_DOES_EXTRA_EXIST, v, i))) { exists |= 1 << i; if (NB(N<int>(N_IS_VEHICLE_EXTRA_TURNED_ON, v, i))) on |= 1 << i; }
    add("ex", {exists, on});
    add("ct", {N<int>(N_GET_VEHICLE_MOD_VARIATION, v, 23) & 1});
    add("bp", {NB(N<int>(N_GET_VEHICLE_TYRES_CAN_BURST, v)) ? 0 : 1});
    std::string mods, toggles;
    for (int t = 0; t <= 49; ++t) {
        if (t >= 17 && t <= 22) { if (NB(N<int>(N_IS_TOGGLE_MOD_ON, v, t))) toggles += (toggles.empty() ? "" : ",") + std::to_string(t); continue; }
        int i = N<int>(N_GET_VEHICLE_MOD, v, t);
        if (i >= 0) mods += (mods.empty() ? "" : ",") + std::to_string(t) + ":" + std::to_string(i);
    }
    d += ";m=" + mods + ";tg=" + toggles;
    return d;
}
static void ApplyVehicle(int v, const std::string &d) {
    std::map<std::string, std::vector<std::string>> kv;
    for (auto &e : Split(d, ';')) { size_t q = e.find('='); if (q != std::string::npos) kv[e.substr(0, q)] = Split(e.substr(q + 1), ','); }
    auto I = [&](const char *k, size_t i, int def) { auto it = kv.find(k); return it != kv.end() && i < it->second.size() && !it->second[i].empty() ? std::atoi(it->second[i].c_str()) : def; };
    auto has = [&](const char *k) { return kv.count(k) > 0; };
    N(N_SET_VEHICLE_MOD_KIT, v, 0);
    if (has("wt")) N(N_SET_VEHICLE_WHEEL_TYPE, v, I("wt", 0, 0));
    if (has("c")) N(N_SET_VEHICLE_COLOURS, v, I("c", 0, 0), I("c", 1, 0));
    if (has("x")) N(N_SET_VEHICLE_EXTRA_COLOURS, v, I("x", 0, 0), I("x", 1, 0));
    if (has("ic")) { N(N_SET_VEHICLE_EXTRA_COLOUR_5, v, I("ic", 0, 0)); N(N_SET_VEHICLE_EXTRA_COLOUR_6, v, I("ic", 1, 0)); }
    if (has("r1")) N(N_SET_VEHICLE_CUSTOM_PRIMARY_COLOUR, v, I("r1", 0, 0), I("r1", 1, 0), I("r1", 2, 0));
    if (has("r2")) N(N_SET_VEHICLE_CUSTOM_SECONDARY_COLOUR, v, I("r2", 0, 0), I("r2", 1, 0), I("r2", 2, 0));
    bool custom = I("ct", 0, 0) == 1;
    if (has("m"))
        for (auto &e : kv["m"]) {
            auto p = Split(e, ':');
            if (p.size() != 2) continue;
            int t = std::atoi(p[0].c_str()), i = std::atoi(p[1].c_str());
            if (t >= 17 && t <= 22) N(N_TOGGLE_VEHICLE_MOD, v, t, i == 1);   // old saves kept toggles here
            else N(N_SET_VEHICLE_MOD, v, t, i, custom ? 1 : 0);
        }
    if (has("tg")) for (auto &e : kv["tg"]) if (!e.empty()) N(N_TOGGLE_VEHICLE_MOD, v, std::atoi(e.c_str()), 1);
    if (has("tn")) N(N_SET_VEHICLE_WINDOW_TINT, v, I("tn", 0, 0));
    if (has("ne")) for (int i = 0; i < 4; ++i) N(N_SET_VEHICLE_NEON_ENABLED, v, i, I("ne", i, 0));
    if (has("nc")) N(N_SET_VEHICLE_NEON_COLOUR, v, I("nc", 0, 255), I("nc", 1, 0), I("nc", 2, 255));
    if (has("ts")) N(N_SET_VEHICLE_TYRE_SMOKE_COLOR, v, I("ts", 0, 255), I("ts", 1, 255), I("ts", 2, 255));
    if (has("xc")) N(N_SET_VEHICLE_XENON_LIGHT_COLOR_INDEX, v, I("xc", 0, -1));
    if (has("lv") && I("lv", 0, -1) >= 0) N(N_SET_VEHICLE_LIVERY, v, I("lv", 0, 0));
    if (has("pt")) N(N_SET_VEHICLE_NUMBER_PLATE_TEXT_INDEX, v, I("pt", 0, 0));
    if (has("ex")) { int ex = I("ex", 0, 0), on = I("ex", 1, 0); for (int i = 0; i <= 14; ++i) if (ex & (1 << i)) N(N_SET_VEHICLE_EXTRA, v, i, (on & (1 << i)) ? 0 : 1); }
    if (has("bp")) N(N_SET_VEHICLE_TYRES_CAN_BURST, v, I("bp", 0, 0) ? 0 : 1);
}
static int SpawnOwned(int idx, Vec3 pos, float heading) {
    const GVeh &g = g_garage[idx];
    uint32_t m = ModelHash(g.model);
    if (!LoadModel(m)) return 0;
    int v = N<int>(N_CREATE_VEHICLE, m, pos.x, pos.y, pos.z, heading, 0, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
    if (!v) return 0;
    N(N_SET_ENTITY_AS_MISSION_ENTITY, v, 1, 1);
    N(N_SET_VEHICLE_HAS_BEEN_OWNED_BY_PLAYER, v, 1);
    ApplyVehicle(v, g.data);
    N(N_SET_VEHICLE_NUMBER_PLATE_TEXT, v, g.plate.empty() ? "GORI" : g.plate.c_str());
    N(N_SET_VEHICLE_DIRT_LEVEL, v, 0.f);
    return v;
}
static std::string PlateOf(int v) { const char *p = N<const char *>(N_GET_VEHICLE_NUMBER_PLATE_TEXT, v); std::string s = p ? p : "GORI"; while (!s.empty() && s.back() == ' ') s.pop_back(); while (!s.empty() && s.front() == ' ') s.erase(s.begin()); return s; }
// Copies the current state of the personal vehicle into its garage record; true if anything changed
static bool SyncPersonal() {
    if (!Exists(g_personal) || Dead(g_personal) || g_personalIdx < 0 || g_personalIdx >= (int)g_garage.size()) return false;
    std::string d = CaptureVehicle(g_personal), plate = PlateOf(g_personal);
    GVeh &g = g_garage[g_personalIdx];
    if (d == g.data && plate == g.plate) return false;
    g.data = d; g.plate = plate;
    return true;
}
static int g_lastSync = 0;
static void PersonalSyncTick() {
    if (Now() - g_lastSync < 3000) return;
    g_lastSync = Now();
    if (SyncPersonal()) Save();
}
static void StorePersonal(bool keep) {
    if (!Exists(g_personal)) { g_personal = 0; RemoveBlip(g_personalBlip); return; }
    if (keep && SyncPersonal()) Save();
    RemoveBlip(g_personalBlip);
    int me = Me();
    if (InAnyVehicle() && MyVehicle() == g_personal) { N(N_TASK_LEAVE_VEHICLE, me, g_personal, 16); Wait(800); }
    N(N_SET_ENTITY_AS_MISSION_ENTITY, g_personal, 1, 1);
    N(N_DELETE_VEHICLE, &g_personal);
    g_personal = 0;
}
static void MakePersonal(int v, int idx) {
    g_personal = v; g_personalIdx = idx;
    const CatCar *def = FindCar(g_garage[idx].model);
    int cls = def ? def->cls : -1;
    int sprite = cls == 15 ? 43 : cls == 16 ? 423 : cls == 14 ? 427 : cls == 8 ? 226 : 225;
    RemoveBlip(g_personalBlip);
    g_personalBlip = AddBlip(v, sprite, 3, "Personal Vehicle");
}
static void RequestVehicle(int idx) {
    StorePersonal(true);
    const CatCar *def = FindCar(g_garage[idx].model);
    int cls = def ? def->cls : -1;
    bool air = cls == 15 || cls == 16, boat = cls == 14;
    int me = Me();
    Vec3 mp = Pos(me);
    float heading = N<float>(N_GET_ENTITY_HEADING, me);
    Vec3 pos = mp;
    bool island = IslandNear(mp, 1500.f);
    if (air && island) { pos = V3(ISLAND_RUNWAYA[0], ISLAND_RUNWAYA[1], ISLAND_RUNWAYA[2] + 0.5f); heading = ISLAND_RUNWAYA[3]; }
    else if (boat && island) { pos = V3(ISLAND_PIEREND[0] + 12.f, ISLAND_PIEREND[1], 0.5f); heading = ISLAND_PIEREND[3]; }
    else if (air) { pos = V3(-1146.0f, -3379.0f, 13.9f); heading = 330.f; }
    else if (boat) { pos = V3(-798.0f, -1500.0f, 0.5f); heading = 110.f; }
    else {
        Vec3 fwd = N<Vec3>(N_GET_ENTITY_FORWARD_VECTOR, me);
        Vec3 out{}; float h = heading;
        if (!island && NB(N<int>(N_GET_CLOSEST_VEHICLE_NODE_WITH_HEADING, mp.x + fwd.x * 25.f, mp.y + fwd.y * 25.f, mp.z, &out, &h, 1, 3.0f, 0.0f))) { pos = out; heading = h; }
        else pos = V3(mp.x + fwd.x * 6.f, mp.y + fwd.y * 6.f, mp.z + 0.5f);
    }
    int v = SpawnOwned(idx, pos, heading);
    if (!v) { Notify("~r~Delivery failed, try again."); return; }
    if (!boat) N<int>(N_SET_VEHICLE_ON_GROUND_PROPERLY, v, 5.0f);
    MakePersonal(v, idx);
    if (air) Notify(island ? "Your aircraft is waiting on the ~b~Isla Neon airstrip~s~." : "Your aircraft is waiting at ~b~LSIA~s~.");
    else if (boat) Notify(island ? "Your boat is waiting at the ~b~Isla Neon pier~s~." : "Your boat is waiting at the ~b~Puerto Del Sol marina~s~.");
    else Notify("Your mechanic left your vehicle ~b~nearby~s~.");
}

// ---------------------------------------------------------------- Properties in the world
static int g_inside = -1;                 // property we are inside of
static std::map<int, Vec3> g_extSnap;     // entrances moved onto walkable ground
static std::map<int, int> g_propBlip;

static Vec3 PropSeed(int i) { return V3(PROPS[i].x, PROPS[i].y, PROPS[i].z); }
static Vec3 PropExt(int i) {
    if (const OBuilding *b = BuildingOf(i)) {   // GTA Online's walk-in marker (garages: the garage door)
        const OEntrance *e = EntranceOf(b, 1);
        if (!e) e = EntranceOf(b, 2);
        if (e) return V3(e->mx, e->my, e->mz);
    }
    auto it = g_extSnap.find(i); return it == g_extSnap.end() ? PropSeed(i) : it->second;
}
static const InteriorDef *PropInterior(int i) { return FindInterior(PROPS[i].interior); }
static void RefreshPropertyBlips() {
    for (auto &kv : g_propBlip) { int b = kv.second; RemoveBlip(b); }
    g_propBlip.clear();
    std::set<int> buildings;
    for (auto &o : g_props) {
        int i = FindProp(o);
        if (i < 0 || PROPS[i].type == T_KOSATKA) continue;
        if (PROPS[i].ob >= 0 && !buildings.insert(PROPS[i].ob).second && g_spawnProp != o) continue;   // one blip per building
        const TypeInfo &t = TYPES[PROPS[i].type];
        g_propBlip[i] = CoordBlip(PropExt(i), t.blip, g_spawnProp == o ? 2 : t.colour, PROPS[i].name, true);
    }
}
static int SpawnGarage(int pi, int page, bool movePlayer);
static InteriorDef CustomisedInterior(int pi, const InteriorDef &d, std::string &ipls, std::string &sets, std::vector<std::string> &unsets);
// ---------------------------------------------------------------- GTA Online interiors + cutscenes
// O_PROPS rows come first in PROPS, so PROPS[i] for i < NO_PROPS has its GTA Online interior frame.
static const OInterior *PIntOf(int i) {
    if (i >= 0 && i < NO_PROPS) return &O_PINT[i];
    int k = NaptIndexOfProp(i);   // the new apartments: their template's frame, moved (NewApartments.inc)
    return k >= 0 ? &g_naptPint[k] : nullptr;
}
// GTA Online's func_48: an interior point of the template (metadata block, read by the game itself)
// moved from the template's origin to this property's origin. Point 6 = where you appear when you
// come in, points 1-2 = the exit area at the apartment's front door.
static bool OnlinePoint(int i, int id, Vec3 &p, float &h, float *pitch = nullptr) {
    const OInterior *t = PIntOf(i);
    if (!t) return false;
    h = 0.f;
    if (pitch) *pitch = 0.f;
    if (t->lit) {
        if (id == 6) { p = V3(t->spx, t->spy, t->spz); h = t->sph; return true; }
        if (id == 1) { p = V3(t->ax, t->ay, t->az); return true; }
        if (id == 2) { p = V3(t->bx, t->by, t->bz); return true; }
        return false;
    }
    if (t->map < 0) return false;
    struct { float x, p0, y, p1, z, p2; } pos = {}, rot = {};
    if (!NB(N<int>(N_GET_BASE_ELEMENT_LOCATION_FROM_METADATA_BLOCK, &pos, &rot, id, t->map)) || (pos.x == 0.f && pos.y == 0.f)) return false;
    float d = (t->orot - t->trot) * 3.14159265f / 180.f, c = std::cos(d), sn = std::sin(d);
    float vx = pos.x - t->tx, vy = pos.y - t->ty;
    p = V3(t->ox + vx * c - vy * sn, t->oy + vx * sn + vy * c, t->oz + pos.z - t->tz);
    h = rot.z + (t->orot - t->trot);
    if (pitch) *pitch = rot.x;
    return true;
}
static bool InOnlineExitArea(int i, int ped) {
    Vec3 a, b; float h;
    if (!OnlinePoint(i, 1, a, h) || !OnlinePoint(i, 2, b, h)) return false;
    return NB(N<int>(N_IS_ENTITY_IN_ANGLED_AREA, ped, a.x, a.y, a.z, b.x, b.y, b.z, PIntOf(i)->w, 0, 1, 0));
}
static const OScene *SceneOf(int bid, int kind) {
    for (int k = 0; k < NO_SCENES; ++k) if (O_SCENES[k].bid == bid && O_SCENES[k].kind == kind) return &O_SCENES[k];
    return nullptr;
}
// scripted cameras (GTA Online's func_6691: a -> b interpolation with hand shake)
static int g_scCamA = 0, g_scCamB = 0;
static int SceneCam(float x, float y, float z, float rx, float ry, float rz, float fov) {
    int c = N<int>(N_CREATE_CAM, "DEFAULT_SCRIPTED_CAMERA", 0);
    N(N_SET_CAM_COORD, c, x, y, z);
    N(N_SET_CAM_ROT, c, rx, ry, rz, 2);
    N(N_SET_CAM_FOV, c, fov);
    return c;
}
static void SceneEnd(bool blend) {
    N(N_RENDER_SCRIPT_CAMS, 0, blend ? 1 : 0, blend ? 1000 : 0, 1, 0, 0);
    if (g_scCamA) N(N_DESTROY_CAM, g_scCamA, 0);
    if (g_scCamB) N(N_DESTROY_CAM, g_scCamB, 0);
    g_scCamA = g_scCamB = 0;
}
static void SceneShot(const OScene &sc) {
    SceneEnd(false);
    g_scCamA = SceneCam(sc.ax, sc.ay, sc.az, sc.arx, sc.ary, sc.arz, sc.fov);
    g_scCamB = SceneCam(sc.bx, sc.by, sc.bz, sc.brx, sc.bry, sc.brz, sc.fov);
    N(N_SHAKE_CAM, g_scCamA, "HAND_SHAKE", sc.shake);
    N(N_SHAKE_CAM, g_scCamB, "HAND_SHAKE", sc.shake);
    if (sc.dur > 0.1f) N(N_SET_CAM_ACTIVE_WITH_INTERP, g_scCamB, g_scCamA, (int)(sc.dur * 1000.f), 1, 1);
    else N(N_SET_CAM_ACTIVE, g_scCamA, 1);
    N(N_RENDER_SCRIPT_CAMS, 1, 0, 3000, 1, 0, 0);
}
static void SceneStill(const OCamShot &c) {   // one fixed shot (the building cameras f_1922)
    SceneEnd(false);
    g_scCamA = SceneCam(c.x, c.y, c.z, c.rx, c.ry, c.rz, c.fov > 1.f ? c.fov : 50.f);
    N(N_SHAKE_CAM, g_scCamA, "HAND_SHAKE", 0.25f);
    N(N_SET_CAM_ACTIVE, g_scCamA, 1);
    N(N_RENDER_SCRIPT_CAMS, 1, 0, 3000, 1, 0, 0);
}
static void SceneLookAt(Vec3 from, Vec3 at, float fov) {   // generic shot for Gori Online's own properties
    SceneEnd(false);
    g_scCamA = N<int>(N_CREATE_CAM, "DEFAULT_SCRIPTED_CAMERA", 0);
    N(N_SET_CAM_COORD, g_scCamA, from.x, from.y, from.z);
    N(N_POINT_CAM_AT_COORD, g_scCamA, at.x, at.y, at.z);
    N(N_SET_CAM_FOV, g_scCamA, fov);
    N(N_SHAKE_CAM, g_scCamA, "HAND_SHAKE", 0.2f);
    N(N_SET_CAM_ACTIVE, g_scCamA, 1);
    N(N_RENDER_SCRIPT_CAMS, 1, 0, 3000, 1, 0, 0);
}
// runs the scene for up to ms (ends earlier when done() says so); the player only watches
static void SceneRun(int ms, const std::function<bool()> &done, int minMs = 1200) {
    int until = Now() + ms, minEnd = Now() + minMs;
    while (Now() < until && !(Now() > minEnd && done && done())) {
        N(N_HIDE_HUD_AND_RADAR_THIS_FRAME);
        N(N_DISABLE_ALL_CONTROL_ACTIONS, 0);
        Wait(0);
    }
}
static void GoToProperty(int i);
static bool HasShot(const OCamShot &c) { return c.x != 0.f || c.y != 0.f; }
static Vec3 Ahead(Vec3 p, float heading, float d) { float r = heading * 3.14159265f / 180.f; return V3(p.x - std::sin(r) * d, p.y + std::cos(r) * d, p.z); }
static Vec3 CamBehind(Vec3 p, float heading, float back, float side, float up) {
    float r = heading * 3.14159265f / 180.f;
    return V3(p.x + std::sin(r) * back + std::cos(r) * side, p.y - std::cos(r) * back + std::sin(r) * side, p.z + up);
}
static void WalkRoute(const float r[3][3]) {
    int me = Me();
    N(N_TASK_FLUSH_ROUTE);
    for (int k = 0; k < 3; ++k) N(N_TASK_EXTEND_ROUTE, r[k][0], r[k][1], r[k][2]);
    N(N_TASK_FOLLOW_POINT_ROUTE, me, 1.0f, 0);
}
static void WalkTo(Vec3 p, float heading) { N(N_TASK_GO_STRAIGHT_TO_COORD, Me(), p.x, p.y, p.z, 1.0f, 8000, heading, 0.25f); }
static void DriveTo(int veh, Vec3 p, float speed) {
    N(N_TASK_VEHICLE_DRIVE_TO_COORD, Me(), veh, p.x, p.y, p.z, speed, 0, N<int>(N_GET_ENTITY_MODEL, veh), 262144, 1.0f, 100.f);
}

// walking up to the building door (GTA Online's entry cutscene, am_mp_property_ext func_6689)
static void CutsceneWalkIn(int i) {
    const OBuilding *b = BuildingOf(i);
    const OEntrance *we = EntranceOf(b, 1);
    Vec3 mp = Pos(Me());
    if (InAnyVehicle()) return;
    if (b && we) {
        if (std::hypot(mp.x - we->mx, mp.y - we->my) > 12.f) return;        // fast travel etc.: no walk-in
        const OScene *sc = SceneOf(b->id, 1);
        if (!sc) return;
        SceneShot(*sc);
        WalkRoute(sc->r);
        Vec3 last = V3(sc->r[2][0], sc->r[2][1], sc->r[2][2]);
        (void)last;
        SceneRun((int)((sc->dur + sc->extra) * 1000.f), nullptr, (int)((sc->dur + sc->extra) * 1000.f));
    } else {                                                              // Gori Online's own properties
        Vec3 e = PropExt(i);
        if (std::hypot(mp.x - e.x, mp.y - e.y) > 12.f) return;
        float h = N<float>(N_GET_ENTITY_HEADING, Me());
        Vec3 at = Ahead(mp, h, 4.f);
        at.z += 0.8f;
        SceneLookAt(CamBehind(mp, h, 3.8f, 1.1f, 1.7f), at, 50.f);   // behind and above: the way you came is clear
        WalkTo(Ahead(mp, h, 3.f), h);
        SceneRun(2600, nullptr, 2600);
    }
    N(N_DO_SCREEN_FADE_OUT, 400);
    Wait(450);
    N(N_CLEAR_PED_TASKS, Me());
    SceneEnd(false);
}
// stepping out of the building door onto the street (GTA Online puts you at f_146[0].f_8)
static void CutsceneWalkOut(int i) {
    const OBuilding *b = BuildingOf(i);
    const OEntrance *we = EntranceOf(b, 1);
    int me = Me();
    if (b && we) {
        const OScene *sc = SceneOf(b->id, 1);
        Vec3 to = V3(b->fx, b->fy, b->fz);
        bool route = sc && (sc->r[0][0] != sc->r[2][0] || sc->r[0][1] != sc->r[2][1]);   // camera-only scenes have no route
        Vec3 from = route ? V3(sc->r[1][0], sc->r[1][1], sc->r[1][2]) : V3(we->sx, we->sy, we->sz);
        Vec3 st = V3(we->sx, we->sy, we->sz);
        float h0 = route ? std::atan2(-(sc->r[0][0] - from.x), sc->r[0][1] - from.y) * 180.f / 3.14159265f : b->fh;
        if (std::hypot(to.x - st.x, to.y - st.y) > 8.f) to = Ahead(st, std::atan2(-(st.x - sc->r[0][0]), st.y - sc->r[0][1]) * 180.f / 3.14159265f, 3.f);   // far street spot: just step out
        TeleportExact(from.x, from.y, from.z, h0);   // fades in: cover it with the shot
        N(N_DO_SCREEN_FADE_OUT, 0);
        if (HasShot(b->cam[0])) SceneStill(b->cam[0]);
        else if (sc) SceneShot(*sc);
        else SceneLookAt(CamBehind(to, b->fh, -5.f, 1.5f, 1.3f), from, 45.f);
        N(N_DO_SCREEN_FADE_IN, 500);
        N(N_TASK_FLUSH_ROUTE);
        if (route) N(N_TASK_EXTEND_ROUTE, sc->r[0][0], sc->r[0][1], sc->r[0][2]);
        N(N_TASK_EXTEND_ROUTE, to.x, to.y, to.z);
        N(N_TASK_FOLLOW_POINT_ROUTE, me, 1.0f, 0);
        SceneRun(5000, [to] { return std::hypot(Pos(Me()).x - to.x, Pos(Me()).y - to.y) < 0.8f; }, 3000);
        N(N_SET_GAMEPLAY_CAM_RELATIVE_HEADING, 0.f);
        SceneEnd(true);
        return;
    }
    GoToProperty(i);                                                      // Gori Online's own properties
    Vec3 p = Pos(me);
    float h = N<float>(N_GET_ENTITY_HEADING, me);
    if (PROPS[i].type >= T_SALVAGE) {   // Online's door points: step out towards the nearest pavement (the navmesh), not through the shop front
        Vec3 st{};
        if (!NB(N<int>(N_GET_SAFE_COORD_FOR_PED, p.x, p.y, p.z, 1, &st, 16)) || Dist(st, p) < 1.5f || Dist(st, p) > 12.f) { SceneEnd(true); return; }
        h = std::atan2(-(st.x - p.x), st.y - p.y) * 180.f / 3.14159265f;
        N(N_SET_ENTITY_HEADING, me, h);
        SceneLookAt(CamBehind(p, h, -6.f, 1.5f, 2.6f), V3(p.x, p.y, p.z + 0.3f), 45.f);
        WalkTo(st, h);
        SceneRun(2400, nullptr);
        N(N_SET_GAMEPLAY_CAM_RELATIVE_HEADING, 0.f);
        SceneEnd(true);
        return;
    }
    SceneLookAt(CamBehind(p, h, -6.f, 1.5f, 2.6f), V3(p.x, p.y, p.z + 0.3f), 45.f);
    WalkTo(Ahead(p, h, 3.f), h);
    SceneRun(2400, nullptr);
    N(N_SET_GAMEPLAY_CAM_RELATIVE_HEADING, 0.f);
    SceneEnd(true);
}
// driving up to the garage door (building garage camera f_1922[1])
// walking into the garage on foot
// driving out of the garage (GTA Online's func_6746 / func_6763); the screen is faded out on entry
// walking out of the garage door (GTA Online's func_6769 / func_6773)
static float HeadingTo(Vec3 a, Vec3 b) { return std::atan2(-(b.x - a.x), b.y - a.y) * 180.f / 3.14159265f; }
static Vec3 RoutePt(const OScene &sc, int k) { return V3(sc.r[k][0], sc.r[k][1], sc.r[k][2]); }
// driving into the garage (GTA Online's func_6745/func_6763: the car goes down/through to the shutter)
static void CutsceneDriveIn(int i, int veh) {
    const OBuilding *b = BuildingOf(i);
    const OEntrance *ge = EntranceOf(b, 2);
    const OScene *sc = b ? SceneOf(b->id, 2) : nullptr;
    Vec3 vp = Pos(veh);
    if (sc && std::hypot(vp.x - sc->sx, vp.y - sc->sy) < 25.f) {
        Vec3 to = RoutePt(*sc, 0);
        SceneShot(*sc);
        DriveTo(veh, to, 5.f);
        SceneRun(std::min(8000, (int)(sc->dur * 1000.f)), [veh, to] { Vec3 p = Pos(veh); return std::hypot(p.x - to.x, p.y - to.y) < 3.f; }, 3500);
    } else if (b && ge) {
        if (std::hypot(vp.x - ge->mx, vp.y - ge->my) > 20.f) return;
        Vec3 door = V3(ge->dx, ge->dy, ge->dz);
        Vec3 past = Ahead(door, HeadingTo(vp, door), 4.f);
        if (HasShot(b->cam[1])) SceneStill(b->cam[1]);
        else SceneLookAt(CamBehind(vp, N<float>(N_GET_ENTITY_HEADING, veh), 7.f, 3.f, 2.f), door, 45.f);
        DriveTo(veh, past, 4.f);
        SceneRun(3500, [veh, past] { Vec3 p = Pos(veh); return std::hypot(p.x - past.x, p.y - past.y) < 2.5f; }, 2800);
    } else {
        float h = N<float>(N_GET_ENTITY_HEADING, veh);
        SceneLookAt(CamBehind(vp, h, 7.f, 2.5f, 2.f), Ahead(vp, h, 6.f), 45.f);
        DriveTo(veh, Ahead(vp, h, 10.f), 4.f);
        SceneRun(2400, nullptr, 2400);
    }
    N(N_DO_SCREEN_FADE_OUT, 400);
    Wait(450);
    SceneEnd(false);
}
// walking into the garage (GTA Online's func_6769/func_6773)
static void CutsceneWalkToGarage(int i) {
    const OBuilding *b = BuildingOf(i);
    const OEntrance *ge = EntranceOf(b, 2);
    const OScene *sc = b ? SceneOf(b->id, 3) : nullptr;
    Vec3 mp = Pos(Me());
    if (!b || !ge || std::hypot(mp.x - ge->mx, mp.y - ge->my) > 12.f) return;
    if (sc) {
        SceneShot(*sc);
        WalkRoute(sc->r);
        SceneRun((int)(sc->dur * 1000.f), nullptr, (int)(sc->dur * 1000.f));
    } else {
        Vec3 door = V3(ge->dx, ge->dy, ge->dz);
        Vec3 past = Ahead(door, HeadingTo(mp, door), 2.f);
        if (HasShot(b->cam[1])) SceneStill(b->cam[1]);
        else SceneLookAt(CamBehind(mp, N<float>(N_GET_ENTITY_HEADING, Me()), 4.f, 2.f, 1.2f), door, 45.f);
        WalkTo(past, HeadingTo(mp, door));
        SceneRun(3000, nullptr, 3000);
    }
    N(N_DO_SCREEN_FADE_OUT, 400);
    Wait(450);
    N(N_CLEAR_PED_TASKS, Me());
    SceneEnd(false);
}
// driving out of the garage: the way in, backwards (shutter -> street); the screen is faded out on entry
static void CutsceneDriveOut(int pi, int v) {
    const OBuilding *b = BuildingOf(pi);
    const OEntrance *ge = EntranceOf(b, 2);
    const OScene *sc = b ? SceneOf(b->id, 2) : nullptr;
    Vec3 from, to;
    if (sc) { from = RoutePt(*sc, 0); to = V3(sc->sx, sc->sy, sc->sz); }
    else if (b && ge) { from = V3(ge->sx, ge->sy, ge->sz); to = V3(b->vx, b->vy, b->vz); }
    else { from = Pos(v); to = Ahead(from, N<float>(N_GET_ENTITY_HEADING, v), 12.f); }
    float h = HeadingTo(from, to);
    N(N_SET_ENTITY_COORDS, v, from.x, from.y, from.z + 0.3f, 0, 0, 0, 0);
    N(N_SET_ENTITY_HEADING, v, h);
    N(N_REQUEST_COLLISION_AT_COORD, to.x, to.y, to.z);
    Wait(300);
    N<int>(N_SET_VEHICLE_ON_GROUND_PROPERLY, v, 5.0f);
    N(N_SET_VEHICLE_ENGINE_ON, v, 1, 1, 0);
    if (sc) SceneShot(*sc);
    else if (b && HasShot(b->cam[1])) SceneStill(b->cam[1]);
    else { Vec3 c = Ahead(to, h, 4.f); c.z += 1.5f; SceneLookAt(c, from, 45.f); }
    N(N_DO_SCREEN_FADE_IN, 500);
    DriveTo(v, to, 6.f);
    SceneRun(sc ? std::min(7000, (int)(sc->dur * 1000.f)) : 4000, [v, to] { Vec3 p = Pos(v); return std::hypot(p.x - to.x, p.y - to.y) < 3.f; }, 3000);
    if (b && sc) {   // then on to GTA Online's street exit point, under player control
        N(N_CLEAR_PED_TASKS, Me());
    }
    N(N_CLEAR_PED_TASKS, Me());
    N(N_SET_GAMEPLAY_CAM_RELATIVE_HEADING, 0.f);
    SceneEnd(true);
}
// walking out of the garage: the way in, backwards
static void CutsceneGarageWalkOut(int pi) {
    const OBuilding *b = BuildingOf(pi);
    const OScene *sc = b ? SceneOf(b->id, 3) : nullptr;
    const OEntrance *ge = EntranceOf(b, 2);
    if (!b) { GoToProperty(pi); return; }
    int me = Me();
    if (sc) {
        Vec3 from = RoutePt(*sc, 2), to = V3(sc->sx, sc->sy, sc->sz);
        TeleportExact(from.x, from.y, from.z, HeadingTo(from, RoutePt(*sc, 1)));
        N(N_DO_SCREEN_FADE_OUT, 0);
        SceneShot(*sc);
        N(N_DO_SCREEN_FADE_IN, 500);
        N(N_TASK_FLUSH_ROUTE);
        N(N_TASK_EXTEND_ROUTE, sc->r[1][0], sc->r[1][1], sc->r[1][2]);
        N(N_TASK_EXTEND_ROUTE, sc->r[0][0], sc->r[0][1], sc->r[0][2]);
        N(N_TASK_EXTEND_ROUTE, to.x, to.y, to.z);
        N(N_TASK_FOLLOW_POINT_ROUTE, me, 1.0f, 0);
        SceneRun(std::max(4000, (int)(sc->dur * 1000.f)), [to] { return std::hypot(Pos(Me()).x - to.x, Pos(Me()).y - to.y) < 0.8f; }, 3000);
    } else if (ge) {
        Vec3 st = V3(ge->sx, ge->sy, ge->sz), to = V3(b->fx, b->fy, b->fz);
        TeleportExact(st.x, st.y, st.z, HeadingTo(st, to));
        N(N_DO_SCREEN_FADE_OUT, 0);
        if (HasShot(b->cam[1])) SceneStill(b->cam[1]); else SceneLookAt(CamBehind(st, b->fh, -5.f, 1.5f, 1.3f), st, 45.f);
        N(N_DO_SCREEN_FADE_IN, 500);
        WalkTo(to, b->fh);
        SceneRun(3500, [to] { return std::hypot(Pos(Me()).x - to.x, Pos(Me()).y - to.y) < 0.6f; }, 2500);
    } else { GoToProperty(pi); return; }
    N(N_CLEAR_PED_TASKS, me);
    N(N_SET_GAMEPLAY_CAM_RELATIVE_HEADING, 0.f);
    SceneEnd(true);
}

static void EnterProperty(int i) {
    const InteriorDef *d0 = PropInterior(i);
    if (!d0) return;
    std::string ipls, sets;
    InteriorDef cd = CustomisedInterior(i, *d0, ipls, sets, g_unsets);
    if (!std::strcmp(d0->id, "datacenter")) {
        BizState *b = BizOf(PROPS[i].name);
        int prod = b ? (int)std::ceil(b->stock / 20.f) : 0, sup = b ? (int)std::ceil(b->supplies / 20.f) : 0;
        for (int k = 1; k <= 5; ++k) {
            char pb[24], sb[24]; std::snprintf(pb, sizeof pb, "dc_product_%02d", k); std::snprintf(sb, sizeof sb, "dc_supplies_%02d", k);
            if (k <= prod) sets += std::string(";") + pb; else g_unsets.push_back(pb);
            if (k <= sup) sets += std::string(";") + sb; else g_unsets.push_back(sb);
        }
        cd.sets = sets.c_str();
    }
    if (!std::strcmp(d0->id, "office_mhc")) {
        long long money = BankBalance(); if (money < 0) money = GetCash();
        int lvl = (int)std::min(10LL, std::max(0LL, money / 1000000));
        for (int k = 1; k <= 10; ++k) { char b[16]; std::snprintf(b, sizeof b, "cash_set_%02d", k); if (k <= lvl) sets += std::string(";") + b; else g_unsets.push_back(b); }
        // the swag displays fill up with the special cargo in your warehouses (one more piece every 10 crates)
        static const char *SWAG[] = {"swag_art", "swag_jewelwatch", "swag_booze_cigs", "swag_electronic", "swag_guns", "swag_silver",
                                     "swag_gems", "swag_furcoats", "swag_ivory", "swag_med", "swag_drugstatue", "swag_pills"};
        int crates = 0;
        for (int k = 0; k < NPROPS; ++k) if (PROPS[k].type == T_WAREHOUSE && Owns(PROPS[k].name)) { BizState *b = BizOf(PROPS[k].name); if (b) crates += (int)b->stock; }
        int nswag = std::min(12, (crates + 9) / 10);
        for (int k = 0; k < 12; ++k) { if (k < nswag) sets += std::string(";") + SWAG[k]; else g_unsets.push_back(SWAG[k]); }
        cd.sets = sets.c_str();
    }
    Vec3 sp; float sh;
    bool onlineSpot = OnlinePoint(i, 6, sp, sh);
    if (onlineSpot && std::hypot(sp.x - d0->x, sp.y - d0->y) > 60.f) {   // the metadata block read was another template's: not this interior
        Log("interior %s: online spot %.1f %.1f %.1f is %.0f m away, using the interior's own", d0->id, sp.x, sp.y, sp.z, std::hypot(sp.x - d0->x, sp.y - d0->y));
        onlineSpot = false;
    }
    if (onlineSpot) {   // GTA Online's spot at the front door, facing away from the door
        cd.x = sp.x; cd.y = sp.y; cd.z = sp.z; cd.h = std::fmod(sh + 180.f, 360.f);
        Vec3 da, db; float dh;
        if (OnlinePoint(i, 1, da, dh) && OnlinePoint(i, 2, db, dh)) {
            Vec3 door = V3((da.x + db.x) * 0.5f, (da.y + db.y) * 0.5f, sp.z);
            if (std::hypot(sp.x - door.x, sp.y - door.y) > 0.3f) cd.h = std::atan2(-(sp.x - door.x), sp.y - door.y) * 180.f / 3.14159265f;
        }
    }
    const InteriorDef *d = &cd;
    CutsceneWalkIn(i);
    if (!TeleportInside(*d)) {   // never leave the player in the air: back to the front door
        Log("interior %s: no interior or floor at %.1f %.1f %.1f, back outside", d->id, d->x, d->y, d->z);
        GoToProperty(i);
        return;
    }
    g_inside = i;
    if (onlineSpot) {   // walking in from the front door
        Vec3 cp; float ch, pitch;
        float h = cd.h;
        (void)cp; (void)ch; (void)pitch;
        Vec3 c = CamBehind(sp, h, 0.55f, 0.45f, 0.75f), at = Ahead(sp, h, 6.f);
        at.z += 0.4f;
        SceneLookAt(c, at, 55.f);
        WalkTo(Ahead(sp, h, 1.5f), h);
        SceneRun(2200, nullptr, 1800);
        N(N_SET_GAMEPLAY_CAM_RELATIVE_HEADING, 0.f);
        SceneEnd(true);
    }
    if (PROPS[i].type == T_HANGAR) SpawnGarage(i, 0, false);      // your aircraft are parked inside, like Online
    if (TYPES[PROPS[i].type].business) Notify("Open the interaction menu (~b~M~s~) to manage this business.");
}
// Ventures.inc's businesses use Online's door locates, which can sit under an awning or just inside the shop front
// (no collision there in story mode): the first visit looks for real ground around the point and keeps that spot
// as the entrance (g_extSnap, like SnapEntrance does for Gori's own properties).
static Vec3 VentureDoor(int i) {
    auto it = g_extSnap.find(i);
    if (it != g_extSnap.end()) return it->second;
    Vec3 p = V3(PROPS[i].x, PROPS[i].y, PROPS[i].z);
    auto groundNear = [&](float x, float y, float &gz) {
        return NB(N<int>(N_GET_GROUND_Z_FOR_3D_COORD, x, y, p.z + 1.0f, &gz, 0, 0)) && std::fabs(gz - (p.z - 1.0f)) < 1.6f;
    };
    float gz = 0.f;
    if (groundNear(p.x, p.y, gz)) { g_extSnap[i] = V3(p.x, p.y, gz + 1.0f); return g_extSnap[i]; }
    for (float r = 1.5f; r <= 7.f; r += 1.f)
        for (int k = 0; k < 16; ++k) {
            float a = k * 3.14159265f / 8.f, x = p.x + std::cos(a) * r, y = p.y + std::sin(a) * r;
            if (groundNear(x, y, gz)) { g_extSnap[i] = V3(x, y, gz + 1.0f); Log("venture door %s: moved %.1f m to %.2f %.2f %.2f", PROPS[i].name, r, x, y, gz); return g_extSnap[i]; }
        }
    Log("venture door %s: no ground found near the door", PROPS[i].name);
    return p;
}
static void VentureGoTo(int i) {
    int me = Me(), mover = InAnyVehicle() ? MyVehicle() : me;
    Vec3 p = V3(PROPS[i].x, PROPS[i].y, PROPS[i].z);
    N(N_DO_SCREEN_FADE_OUT, 500); Wait(600);
    N(N_SET_ENTITY_COORDS, mover, p.x, p.y, p.z + 1.0f, 0, 0, 0, 0);
    N(N_FREEZE_ENTITY_POSITION, mover, 1);
    N(N_REQUEST_COLLISION_AT_COORD, p.x, p.y, p.z);
    for (int k = 0; k < 60 && !NB(N<int>(N_HAS_COLLISION_LOADED_AROUND_ENTITY, mover)); ++k) Wait(50);
    Wait(300);
    Vec3 d = VentureDoor(i);
    float h = Dist(d, p) > 0.5f ? std::atan2(-(p.x - d.x), p.y - d.y) * 180.f / 3.14159265f : PROPS[i].h;   // facing the door
    N(N_SET_ENTITY_COORDS, mover, d.x, d.y, d.z - 0.5f, 0, 0, 0, 0);
    N(N_SET_ENTITY_HEADING, mover, h);
    N(N_FREEZE_ENTITY_POSITION, mover, 0);
    Wait(300);
    N(N_DO_SCREEN_FADE_IN, 500);
}
static void GoToProperty(int i) {
    if (const OBuilding *b = BuildingOf(i)) { TeleportExact(b->fx, b->fy, b->fz, b->fh); return; }   // where GTA Online puts you outside
    Vec3 e = PropExt(i);
    if (PROPS[i].type == T_VILLA) IslandArrive(e.x, e.y, e.z, PROPS[i].h);
    else if (PROPS[i].type == T_KOSATKA) EnterProperty(i);
    else if (PROPS[i].type >= T_SALVAGE) VentureGoTo(i);   // Online's own door points (Ventures.inc)
    else Teleport(e.x, e.y, e.z, PROPS[i].h);
}
static void ExitProperty() {
    int i = g_inside;
    g_inside = -1;
    if (i < 0) return;
    if (PROPS[i].type == T_KOSATKA) { Teleport(-1600.f, -1050.f, 13.0f, 230.f); return; }   // Del Perro pier
    if (PROPS[i].type == T_VILLA) { GoToProperty(i); return; }
    CutsceneWalkOut(i);
}
static void SnapEntrance(int i) {
    if (BuildingOf(i)) return;   // GTA Online's points are exact
    if (g_extSnap.count(i) || PROPS[i].type == T_VILLA || PROPS[i].type == T_KOSATKA || PROPS[i].type >= T_SALVAGE) return;
    Vec3 seed = PropSeed(i), out{};
    if (!std::strcmp(PROPS[i].interior, "office_mhc") || PROPS[i].type == T_DATACENTER) {   // our own doors on the new plaza: the navmesh there is still the old site's
        g_extSnap[i] = V3(seed.x, seed.y, GroundAt(seed.x, seed.y, seed.z + 3.f, seed.z - 1.f) + 1.0f);
        return;
    }
    if (NB(N<int>(N_GET_SAFE_COORD_FOR_PED, seed.x, seed.y, seed.z, 1, &out, 28)) && Dist(out, seed) < 30.f) {
        g_extSnap[i] = V3(out.x, out.y, out.z + 1.0f);
    } else {
        float gz = GroundAt(seed.x, seed.y, seed.z + 5.f, seed.z - 1.0f);
        g_extSnap[i] = V3(seed.x, seed.y, gz + 1.0f);
    }
    auto b = g_propBlip.find(i);
    if (b != g_propBlip.end()) { Vec3 p = g_extSnap[i]; N(N_SET_BLIP_COORDS, b->second, p.x, p.y, p.z); }
}
static void EnterGarage(int pi, int veh, int page = 0);
static void StoreAircraft(int pi, int v);
static int GarageSlots(int pi);
// GTA Online's entrance corona: blue cylinder 0.75 m wide, 0.25 m below the ground under the marker.
static std::map<long long, float> g_markerGround;
static void OnlineMarker(const OEntrance &e) {
    long long key = (long long)(e.mx * 10.f) * 1000003LL + (long long)(e.my * 10.f);
    auto it = g_markerGround.find(key);
    if (it == g_markerGround.end()) {
        float gz = 0.f;
        if (!NB(N<int>(N_GET_GROUND_Z_FOR_3D_COORD, e.mx + 0.5f, e.my, e.mz + 0.5f, &gz, 0, 0)) || gz < e.mz - 3.f) gz = e.mz;
        it = g_markerGround.emplace(key, gz).first;
    }
    N(N_DRAW_MARKER, 1, e.mx, e.my, it->second - 0.25f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.75f, 0.75f, 0.75f, 93, 182, 229, 255, 0, 0, 2, 0, (const char *)nullptr, (const char *)nullptr, 0);
}
static int g_unitChoice = 0;   // which owned unit of a building the entrance prompt offers
static void OnlineBuildingsTick(Vec3 mp, bool onFoot) {
    // owned properties grouped by GTA Online building
    std::map<int, std::vector<int>> owned;
    for (auto &o : g_props) { int i = FindProp(o); if (i >= 0 && PROPS[i].ob >= 0) owned[PROPS[i].ob].push_back(i); }
    for (auto &kv : owned) {
        const OBuilding &b = *BldAt(kv.first);
        const std::vector<int> &units = kv.second;
        for (int k = 0; k < b.ne; ++k) {
            const OEntrance &e = b.e[k];
            Vec3 m = V3(e.mx, e.my, e.mz);
            float d = Dist(mp, m);
            if (d > 60.f) continue;
            bool garageDoor = e.kind == 2;
            if (onFoot || garageDoor) OnlineMarker(e);
            if (g_menuOpen) continue;
            bool inside = false;
            if (!onFoot && garageDoor)   // GTA Online: driving into the garage entrance area
                inside = NB(N<int>(N_IS_ENTITY_IN_ANGLED_AREA, MyVehicle(), e.ax, e.ay, e.az, e.bx, e.by, e.bz, e.w, 0, 1, 0)) || d < 3.5f;
            else if (onFoot) inside = std::hypot(mp.x - e.mx, mp.y - e.my) < 1.1f && std::fabs(mp.z - e.mz) < 2.2f;   // the ped stands ~1 m above the marker
            if (!inside) continue;
            // several owned units in this building: left/right picks one (GTA Online asks with a menu)
            std::vector<int> pick;
            for (int i : units) if (garageDoor ? GarageSlots(i) > 0 : (PropInterior(i) != nullptr)) pick.push_back(i);
            if (pick.empty()) { if (!garageDoor || onFoot) for (int i : units) if (GarageSlots(i) > 0) pick.push_back(i); }
            if (pick.empty()) continue;
            if ((int)pick.size() > 1) {
                if (JustPressed(174)) g_unitChoice = (g_unitChoice + (int)pick.size() - 1) % (int)pick.size();
                if (JustPressed(175)) g_unitChoice = (g_unitChoice + 1) % (int)pick.size();
            }
            int i = pick[g_unitChoice % pick.size()];
            std::string more = pick.size() > 1 ? "~n~~INPUT_CELLPHONE_LEFT~ ~INPUT_CELLPHONE_RIGHT~ other properties here (" + std::to_string(g_unitChoice % pick.size() + 1) + "/" + std::to_string(pick.size()) + ")" : "";
            bool garageOnly = PROPS[i].type == T_GARAGE || !PropInterior(i);
            if (!onFoot) {
                Help(std::string("Press ~INPUT_CONTEXT~ to park in the garage of ~b~") + PROPS[i].name + "~s~." + more);
                if (JustPressed(51)) { EnterGarage(i, MyVehicle()); return; }
            } else if (garageDoor || garageOnly) {
                Help(std::string("Press ~INPUT_CONTEXT~ to enter the garage of ~b~") + PROPS[i].name + "~s~." + more);
                if (JustPressed(51)) { EnterGarage(i, 0); return; }
            } else {
                Help(std::string("Press ~INPUT_CONTEXT~ to enter ~b~") + PROPS[i].name + "~s~." + more);
                if (JustPressed(51)) { EnterProperty(i); return; }
            }
        }
    }
}
// the office computer (Internet.inc): sit down at the desk, the SecuroServ desktop opens; closing it stands you up
static void OpenSecuroServ(int pi);
static bool SecuroServTick(int pi, const Vec3 &mp, bool onFoot);
static bool DataTerminalTick(int pi, const Vec3 &mp, bool onFoot);
static void NaptResolveTick(Vec3 mp);
static void AptActsFrame();
static bool AptActsTick(Vec3 mp, bool onFoot);
static void PropertiesTick() {
    int me = Me(); Vec3 mp = Pos(me);
    NaptResolveTick(mp);
    AptActsFrame();
    bool onFoot = !InAnyVehicle();
    if (g_inside >= 0) {
        const InteriorDef *d = PropInterior(g_inside);
        Vec3 ex = V3(d->x, d->y, d->z);
        if (Dist(mp, ex) > 250.f) { g_inside = -1; return; }   // left by fast travel, death...
        if (AptActsTick(mp, onFoot)) return;                     // the apartment's activities (AptActs.inc)
        Vec3 da; float dh;
        if (OnlinePoint(g_inside, 1, da, dh)) {   // GTA Online: walk to the front door to leave
            if (onFoot && !g_menuOpen && InOnlineExitArea(g_inside, me)) {
                bool gar = GarageSlots(g_inside) > 0;
                Help(std::string("Press ~INPUT_CONTEXT~ to exit to the street.") + (gar ? "~n~Press ~INPUT_CELLPHONE_RIGHT~ to go to the garage." : ""));
                if (JustPressed(51)) { ExitProperty(); return; }
                if (gar && JustPressed(175)) { int pi = g_inside; g_inside = -1; EnterGarage(pi, 0); return; }
            }
            return;
        }
        if (!std::strcmp(PROPS[g_inside].interior, "office_mhc") && SecuroServTick(g_inside, mp, onFoot)) return;
        if (PROPS[g_inside].type == T_DATACENTER && DataTerminalTick(g_inside, mp, onFoot)) return;
        float de = Dist(mp, ex);
        if (de < 30.f) Marker(ex, 240, 200, 80);
        if (de < 1.5f && onFoot && !g_menuOpen) {
            bool gar = GarageSlots(g_inside) > 0;   // the lift: down to the street or to the garage
            Help(std::string("Press ~INPUT_CONTEXT~ to leave.") + (gar ? "~n~Press ~INPUT_CELLPHONE_RIGHT~ to go to the garage." : ""));
            if (JustPressed(51)) ExitProperty();
            else if (gar && JustPressed(175)) { int pi = g_inside; g_inside = -1; EnterGarage(pi, 0); }
        }
        return;
    }
    OnlineBuildingsTick(mp, onFoot);
    for (auto &o : g_props) {
        int i = FindProp(o);
        if (i < 0 || PROPS[i].type == T_KOSATKA || BuildingOf(i)) continue;
        float d = Dist(mp, PROPS[i].type == T_VILLA ? PropSeed(i) : PropExt(i));
        if (d < 150.f) SnapEntrance(i);
        Vec3 e = PropExt(i);
        if (PROPS[i].type == T_VILLA && !onFoot) e = V3(ISLAND_VILLAGARAGE[0], ISLAND_VILLAGARAGE[1], ISLAND_VILLAGARAGE[2]);
        if (PROPS[i].type == T_DATACENTER && !onFoot) e = V3(DC_GARAGE_ENTRANCE[0], DC_GARAGE_ENTRANCE[1], DC_GARAGE_ENTRANCE[2]);   // the garage has its own door
        if (!onFoot && !std::strcmp(PROPS[i].interior, "office_mhc")) e = V3(MHC_GARAGE_ENTRANCE[0], MHC_GARAGE_ENTRANCE[1], MHC_GARAGE_ENTRANCE[2]);   // the office garage's door on the north street
        d = Dist(mp, e);
        if (d < 40.f) Marker(e, 93, 182, 229);
        if (d < (onFoot ? 1.6f : 3.0f) && !g_menuOpen) {
            if (!onFoot) {
                int vcls = N<int>(N_GET_VEHICLE_CLASS, MyVehicle());
                if (PROPS[i].type == T_HANGAR && (vcls == 15 || vcls == 16)) { Help(std::string("Press ~INPUT_CONTEXT~ to store this aircraft in ~b~") + PROPS[i].name + "~s~."); if (JustPressed(51)) { StoreAircraft(i, MyVehicle()); return; } }
                else if (GarageSlots(i) > 0) { Help(std::string("Press ~INPUT_CONTEXT~ to park in the garage of ~b~") + PROPS[i].name + "~s~."); if (JustPressed(51)) { EnterGarage(i, MyVehicle()); return; } }
                else Help("Get out of the vehicle to enter.");
            } else {
                Help(std::string("Press ~INPUT_CONTEXT~ to enter ~b~") + PROPS[i].name + "~s~.");
                if (JustPressed(51)) { EnterProperty(i); return; }
            }
        }
    }
}

// ---------------------------------------------------------------- Property garages (each property type uses its GTA Online garage)
// Slots are in each interior's local frame (found from the interiors' collision by IslandBuilder/tools_slots.py).
struct GSlot { float x, y, z, h; };
struct GLayout { const GSlot *slots; int n; float sx, sy, sz, sh; const char *sets; };
struct GInst { float x, y, z, yaw; const char *ipl; const char *sets; };
static const GSlot SL_10CAR[] = {{-5.0f, -9.0f, 0.6f, 270.f}, {-5.0f, -4.2f, 0.6f, 270.f}, {-5.0f, 0.6f, 0.6f, 270.f}, {-5.0f, 5.4f, 0.6f, 270.f}, {-5.0f, 10.2f, 0.6f, 270.f},
                                 {4.8f, -9.0f, 0.6f, 90.f}, {4.8f, -4.2f, 0.6f, 90.f}, {4.8f, 0.6f, 0.6f, 90.f}, {4.8f, 5.4f, 0.6f, 90.f}, {4.8f, 10.2f, 0.6f, 90.f}};
static const GSlot SL_OFFICE[] = {   // three levels joined by the car lift
    {-8.0f, -1.0f, 0.6f, 180.f}, {-7.0f, -14.5f, 0.6f, 150.f}, {-1.0f, -15.0f, 0.6f, 195.f}, {3.0f, -10.5f, 0.6f, 255.f}, {7.0f, -1.5f, 0.6f, 90.f},
    {4.5f, 3.5f, 0.6f, 150.f}, {-2.5f, -2.5f, 0.6f, 0.f}, {7.0f, -8.5f, 0.6f, 75.f}, {0.5f, 5.0f, 0.6f, 180.f},
    {-10.0f, -13.5f, 5.9f, 180.f}, {-2.5f, -2.0f, 5.9f, 270.f}, {-2.5f, 1.0f, 5.9f, 270.f}, {-2.5f, 4.0f, 5.9f, 270.f}, {-1.0f, -15.0f, 5.9f, 195.f},
    {3.0f, -10.5f, 5.9f, 255.f}, {-6.0f, -15.0f, 5.9f, 165.f}, {7.5f, 1.0f, 5.9f, 150.f}, {7.0f, -8.5f, 5.9f, 75.f},
    {-10.0f, -13.5f, 11.3f, 180.f}, {-2.5f, -2.0f, 11.3f, 270.f}};
static const GSlot SL_AGENCY[] = {
    {-9.5f, -15.5f, 0.6f, 270.f}, {-9.5f, -12.5f, 0.6f, 270.f}, {-9.5f, -7.0f, 0.6f, 270.f}, {-9.5f, -4.0f, 0.6f, 270.f}, {4.5f, -11.5f, 0.6f, 90.f},
    {-9.5f, 1.0f, 0.6f, 270.f}, {4.5f, -7.0f, 0.6f, 90.f}, {4.5f, -4.0f, 0.6f, 90.f}, {-3.0f, 5.0f, 0.6f, 180.f}, {4.5f, 1.0f, 0.6f, 90.f},
    {-9.5f, -15.5f, 5.0f, 270.f}, {-9.5f, -12.5f, 5.0f, 270.f}, {-9.5f, -7.0f, 5.0f, 270.f}, {-9.5f, -4.0f, 5.0f, 270.f}, {4.5f, -11.5f, 5.0f, 90.f},
    {-9.5f, 1.0f, 5.0f, 270.f}, {4.5f, -7.0f, 5.0f, 90.f}, {4.5f, -4.0f, 5.0f, 90.f}, {-3.0f, 5.0f, 5.0f, 180.f}, {4.5f, 1.0f, 5.0f, 90.f}};
static const GSlot SL_MANSION[] = {
    {-7.5f, -26.0f, 0.6f, 270.f}, {8.0f, -25.0f, 0.6f, 0.f}, {-7.5f, -11.0f, 0.6f, 270.f}, {-3.5f, -4.0f, 0.6f, 0.f}, {5.5f, -5.0f, 0.6f, 90.f},
    {-7.5f, 5.5f, 0.6f, 270.f}, {5.5f, -2.0f, 0.6f, 90.f}, {5.5f, 1.0f, 0.6f, 90.f}, {-7.5f, 20.0f, 0.6f, 270.f}, {-7.5f, 23.0f, 0.6f, 270.f},
    {-7.5f, 26.0f, 0.6f, 270.f}, {2.5f, 33.5f, 0.6f, 180.f},
    {-7.5f, -26.0f, 5.0f, 270.f}, {8.0f, -25.0f, 5.0f, 0.f}, {-7.5f, -11.0f, 5.0f, 270.f}, {12.0f, -20.0f, 5.0f, 90.f}, {-3.5f, -4.0f, 5.0f, 0.f},
    {5.5f, -5.0f, 5.0f, 90.f}, {-7.5f, 5.5f, 5.0f, 270.f}, {5.5f, -2.0f, 5.0f, 90.f}};
static const GSlot SL_CLUB[] = {
    {-14.0f, 11.0f, -2.6f, 270.f}, {-14.0f, 14.0f, -2.6f, 270.f}, {-14.0f, 17.0f, -2.6f, 270.f}, {-14.0f, 20.0f, -2.6f, 270.f}, {-14.0f, 23.0f, -2.6f, 270.f},
    {-14.0f, 26.0f, -2.6f, 270.f}, {6.0f, 34.5f, -2.6f, 180.f}, {9.0f, 34.5f, -2.6f, 180.f}, {1.0f, 11.5f, -2.6f, 15.f}, {8.5f, 12.0f, -2.6f, 15.f}};
static const GSlot SL_CASINO[] = {
    {-11.0f, -7.5f, 0.6f, 0.f}, {-8.0f, -7.5f, 0.6f, 0.f}, {-5.0f, -7.5f, 0.6f, 0.f}, {-2.0f, -7.5f, 0.6f, 0.f}, {2.5f, -7.5f, 0.6f, 0.f},
    {5.5f, -7.5f, 0.6f, 0.f}, {8.5f, -7.5f, 0.6f, 0.f}, {11.5f, -7.5f, 0.6f, 0.f}, {14.5f, -7.5f, 0.6f, 0.f}, {17.5f, -7.5f, 0.6f, 0.f}};
static const GSlot SL_HANGAR[] = {
    {-28.f, 14.f, 1.1f, 0.f}, {-14.f, 14.f, 1.1f, 0.f}, {0.f, 14.f, 1.1f, 0.f}, {14.f, 14.f, 1.1f, 0.f}, {27.f, 14.f, 1.1f, 0.f},
    {-28.f, -14.f, 1.1f, 0.f}, {-14.f, -14.f, 1.1f, 0.f}, {14.f, -14.f, 1.1f, 0.f}, {27.f, -14.f, 1.1f, 0.f},
    {-28.f, -28.f, 1.1f, 0.f}, {-14.f, -28.f, 1.1f, 0.f}, {0.f, -28.f, 1.1f, 0.f}, {14.f, -28.f, 1.1f, 0.f}, {27.f, -28.f, 1.1f, 0.f},
    {-12.f, 30.f, 1.1f, 0.f}, {8.f, 30.f, 1.1f, 0.f}, {-12.f, 46.f, 1.1f, 0.f}, {8.f, 46.f, 1.1f, 0.f}, {-28.f, 0.f, 1.1f, 0.f}, {27.f, 0.f, 1.1f, 0.f}};
static const GSlot SL_VILLA[] = {
    {0.f, 0.f, 0.75f, 90.f},
    {-10.5f, 7.4f, 0.6f, 180.f}, {-3.5f, 7.4f, 0.6f, 180.f}, {3.5f, 7.4f, 0.6f, 180.f}, {10.5f, 7.4f, 0.6f, 180.f},
    {-3.5f, -7.4f, 0.6f, 0.f}, {3.5f, -7.4f, 0.6f, 0.f}, {10.5f, -7.4f, 0.6f, 0.f}};
#define NSL(a) (int)(sizeof(a) / sizeof(a[0]))
static const GLayout GL_10CAR = {SL_10CAR, NSL(SL_10CAR), 0.f, 12.8f, 1.0f, 180.f, ""};
static const GLayout GL_OFFICE = {SL_OFFICE, NSL(SL_OFFICE), 1.0f, -5.0f, 1.0f, 0.f, ""};
static const GLayout GL_AGENCY = {SL_AGENCY, NSL(SL_AGENCY), -2.5f, -10.0f, 1.0f, 0.f, "entity_set_workshop_wall;entity_set_workshop_lights;entity_set_wallpaper_01;entity_set_art_1;entity_set_tints:1"};
static const GLayout GL_MANSION = {SL_MANSION, NSL(SL_MANSION), 0.5f, -15.0f, 1.0f, 0.f, ""};
static const GLayout GL_CLUB = {SL_CLUB, NSL(SL_CLUB), -6.0f, 20.0f, -2.2f, 0.f, "int02_ba_storage_blocker"};
static const GLayout GL_CASINO = {SL_CASINO, NSL(SL_CASINO), 0.f, 10.0f, 1.0f, 180.f, ""};
static const GLayout GL_HANGAR = {SL_HANGAR, NSL(SL_HANGAR), 0.f, 0.f, 1.5f, 0.f, ""};
static const GLayout GL_VILLA = {SL_VILLA, NSL(SL_VILLA), -12.5f, 0.f, 1.0f, 270.f, ""};      // no MLO: our own geometry (IslandBuilder VillaGarage.cs)
static const float GAR_O[3] = {228.61f, -992.05f, -100.0f};    // the classic 10-car garage

// garage of property pi, floor (page) p: layout, placement, vehicles per page and number of pages
static std::string OfficeGarageSets(int pi, int floor, std::vector<std::string> &unsets);
static bool GarageOf(int pi, int page, const GLayout *&L, GInst &I, int &per, int &pages, std::string &key) {
    const PropDef &p = PROPS[pi];
    key = p.name; pages = 1;
    std::string n = p.name;
    switch (p.type) {
    case T_OFFICE: {
        if (n == "Mile High Club Office") {   // Rockstar's office garage (imp_impexp_int_01) on three floors under the tower, like every office
            static std::string msets[3];
            int f = std::min(page, 2);
            msets[f] = OfficeGarageSets(pi, f, g_unsets);
            L = &GL_OFFICE; I = {MHC_GARAGE[f][0], MHC_GARAGE[f][1], MHC_GARAGE[f][2], MHC_GARAGE[f][3], MHC_GARAGE_IPL[f], msets[f].c_str()};
            per = 20; pages = 3; return true;
        }
        static const char *pre[4] = {"imp_dt1_11_cargarage_", "imp_dt1_02_cargarage_", "imp_sm_13_cargarage_", "imp_sm_15_cargarage_"};
        static const float pos[4][3][4] = {
            {{-84.219f, -823.085f, 221.f, -70.f}, {-69.863f, -824.75f, 221.f, 200.f}, {-80.432f, -813.254f, 221.f, 20.f}},
            {{-191.013f, -579.143f, 135.f, 263.8f}, {-117.499f, -568.113f, 135.f, 249.f}, {-136.078f, -630.185f, 135.f, 9.f}},
            {{-1581.112f, -567.245f, 85.5f, -36.1f}, {-1568.739f, -562.046f, 85.5f, 53.9f}, {-1563.557f, -574.431f, 85.5f, 143.9f}},
            {{-1388.84f, -478.74f, 56.1f, 261.3f}, {-1388.86f, -478.757f, 48.1f, 261.3f}, {-1374.682f, -474.359f, 56.1f, 81.1f}}};
        int b = n.find("Maze Bank West") == 0 ? 3 : n.find("Arcadius") == 0 ? 1 : n.find("Lombank") == 0 ? 2 : 0;
        static std::string ipl[3], sets[3];
        int f = std::min(page, 2);
        ipl[f] = std::string(pre[b]) + (char)('a' + f);
        sets[f] = OfficeGarageSets(pi, f, g_unsets);
        L = &GL_OFFICE; I = {pos[b][f][0], pos[b][f][1], pos[b][f][2], pos[b][f][3], ipl[f].c_str(), sets[f].c_str()};
        per = 20; pages = 3; return true;
    }
    case T_DATACENTER: L = &GL_OFFICE; I = {DC_GARAGE[0], DC_GARAGE[1], DC_GARAGE[2], DC_GARAGE[3], DC_GARAGE_IPL, "garage_decor_03;lighting_option03;numbering_style05_n1"}; per = 20; return true;
    case T_AGENCY: L = &GL_AGENCY; I = {-1070.f, -70.f, -100.f, 0.f, "sf_int_placement_sec_interior_2_dlc_garage_sec_milo_", ""}; per = 20; return true;
    case T_NIGHTCLUB: {
        static std::string sets;
        sets = "int02_ba_floor0" + std::to_string(std::min(page, 3) + 2);
        L = &GL_CLUB; I = {-1505.782f, -3012.587f, -80.f, 0.f, "ba_int_placement_ba_interior_1_dlc_int_02_ba_milo_", sets.c_str()}; per = 10; pages = 2; return true;
    }
    case T_PENTHOUSE: L = &GL_CASINO; I = {1295.f, 230.f, -50.f, 0.f, "vw_casino_garage", ""}; per = 10; return true;
    case T_MANSION: {
        const char *in = p.interior;
        if (!std::strcmp(in, "mansion1")) I = {548.696f, 766.887f, 186.076f, 51.7f, "m25_2_ch2_04_mansion_interior_c", ""};
        else if (!std::strcmp(in, "mansion2")) I = {-1679.877f, 493.596f, 112.935f, -17.8f, "m25_2_ch1_06e_mansion_interior_b", ""};
        else I = {-2568.934f, 1920.203f, 151.09f, 84.1f, "m25_2_ch1_09_mansion_interior_c", ""};
        L = &GL_MANSION; per = 20; return true;
    }
    case T_VILLA: L = &GL_VILLA; I = {ISLAND_GARINT[0], ISLAND_GARINT[1], ISLAND_GARINT[2], 0.f, "", ""}; per = 8; return true;
    case T_HANGAR: L = &GL_HANGAR; I = {-1266.802f, -3014.837f, -50.f, 0.f, "", ""}; per = 20; key = "@air"; return true;
    default: {
        int cap = GarageSlots(pi);
        if (cap <= 0) return false;
        L = &GL_10CAR; I = {GAR_O[0], GAR_O[1], GAR_O[2], 0.f, "", ""}; per = 10; pages = (cap + 9) / 10; return true;
    }
    }
}
static Vec3 GLocal(const GInst &I, float lx, float ly, float lz) {
    float r = I.yaw * 3.14159265f / 180.f, c = std::cos(r), s = std::sin(r);
    return V3(I.x + lx * c - ly * s, I.y + lx * s + ly * c, I.z + lz);
}
static const int GARMAX = 20;
static int g_garProp = -1, g_garPage = 0, g_garCar[GARMAX] = {0}, g_garIdx[GARMAX] = {0};
static Vec3 g_garSpawn = {};
static void ClearGarageCars(int keep) { for (int k = 0; k < GARMAX; ++k) { if (g_garCar[k] && g_garCar[k] != keep) DeleteEntity(g_garCar[k]); g_garCar[k] = 0; } }
static std::string ModelNameOf(int v) {
    uint32_t h = (uint32_t)N<int>(N_GET_ENTITY_MODEL, v);
    auto it = g_catByHash.find(h);
    if (it != g_catByHash.end()) return g_cat[it->second].model;
    char hex[16]; std::snprintf(hex, sizeof hex, "0x%08X", h);
    return hex;
}
// Drives a vehicle into the garage of property pi: your personal vehicle goes home, any other car takes a free slot
static bool StoreVehicleIn(int pi, int v) {
    std::string prop = PROPS[pi].name;
    int cls = N<int>(N_GET_VEHICLE_CLASS, v), cap = GarageSlots(pi);
    std::string full = "~r~This garage is full~s~ (" + std::to_string(cap) + " vehicles).";
    if (AirOrSea(cls)) { Notify("~r~Aircraft and boats can't go in a garage."); return false; }
    if (v == g_personal && g_personalIdx >= 0) {
        SyncPersonal();
        GVeh &g = g_garage[g_personalIdx];
        if (g.prop != prop) {
            if (CountIn(prop) >= cap) { Notify(full); return false; }
            g.prop = prop;
        }
        RemoveBlip(g_personalBlip);
        g_personal = 0;
    } else {
        if (CountIn(prop) >= cap) { Notify(full + " Sell one in the Vehicles menu."); return false; }
        GVeh g; g.prop = prop; g.model = ModelNameOf(v); g.plate = PlateOf(v); g.data = CaptureVehicle(v);
        g_garage.push_back(g);
        GiveRP(100, "vehicle stored");
        Notify("~g~" + CarName(g.model) + "~s~ stored in your garage.");
    }
    Save();
    return true;
}
// Loads the garage interior of property pi (floor page) and parks its vehicles in it; the player appears at the spawn point
static int SpawnGarage(int pi, int page, bool movePlayer) {
    const GLayout *L; GInst I; int per, pages; std::string key;
    if (!GarageOf(pi, page, L, I, per, pages, key)) return 0;
    int me = Me();
    ClearGarageCars(0);
    Vec3 sp = GLocal(I, L->sx, L->sy, L->sz);
    InteriorDef d = {"garage", sp.x, sp.y, sp.z, 0.f, I.ipl, ""};
    std::string sets = std::string(L->sets) + (L->sets[0] && I.sets[0] ? ";" : "") + I.sets;
    d.sets = sets.c_str();
    if (movePlayer) { N(N_SET_ENTITY_COORDS, me, sp.x, sp.y, sp.z, 0, 0, 0, 0); N(N_FREEZE_ENTITY_POSITION, me, 1); }
    int id = L == &GL_VILLA ? 0 : LoadInterior(d);
    for (int i = 0; id && i < 80 && !NB(N<int>(N_IS_INTERIOR_READY, id)); ++i) Wait(50);
    N(N_REQUEST_COLLISION_AT_COORD, sp.x, sp.y, sp.z);
    Wait(300);
    int k = 0, seen = 0, total = 0;
    for (int i = 0; i < (int)g_garage.size(); ++i) {
        if (g_garage[i].prop != key || (i == g_personalIdx && Exists(g_personal))) continue;
        if (key == "@air") { const CatCar *c = FindCar(g_garage[i].model); if (c && c->cls == 14) continue; }   // boats stay at the marina
        total++;
        if (seen++ < page * per || k >= std::min(per, std::min(L->n, GARMAX))) continue;
        const GSlot &s = L->slots[k];
        Vec3 p = GLocal(I, s.x, s.y, s.z);
        int v = SpawnOwned(i, p, s.h + I.yaw);
        if (v) { N<int>(N_SET_VEHICLE_ON_GROUND_PROPERLY, v, 5.0f); N(N_SET_VEHICLE_ENGINE_ON, v, 0, 1, 1); if (L == &GL_VILLA && k == 0) N(N_FREEZE_ENTITY_POSITION, v, 1); g_garCar[k] = v; g_garIdx[k] = i; k++; }
    }
    if (movePlayer) { N(N_SET_ENTITY_HEADING, me, L->sh + I.yaw); N(N_FREEZE_ENTITY_POSITION, me, 0); }
    g_garProp = pi; g_garPage = page; g_garSpawn = sp;
    Log("garage %s page %d: %d/%d vehicles (interior %d)", PROPS[pi].name, page, k, total, id);
    return k;
}
static void EnterGarage(int pi, int veh, int page) {
    int me = Me();
    if (veh) CutsceneDriveIn(pi, veh);
    else if (g_inside < 0 && g_garProp < 0) CutsceneWalkToGarage(pi);
    N(N_DO_SCREEN_FADE_OUT, 400);
    Wait(450);
    if (veh) {
        if (!StoreVehicleIn(pi, veh)) { N(N_DO_SCREEN_FADE_IN, 300); return; }
        N(N_CLEAR_PED_TASKS_IMMEDIATELY, me);
        N(N_SET_ENTITY_AS_MISSION_ENTITY, veh, 1, 1);
        N(N_DELETE_VEHICLE, &veh);
        // a stored car shows up on the floor where it landed
        const GLayout *L; GInst I; int per = 10, pages = 1; std::string key;
        if (GarageOf(pi, 0, L, I, per, pages, key)) page = std::min(pages - 1, std::max(0, (CountIn(PROPS[pi].name) - 1) / per));
    }
    int k = SpawnGarage(pi, page, true);
    Wait(200);
    N(N_DO_SCREEN_FADE_IN, 500);
    const GLayout *L; GInst I; int per = 10, pages = 1; std::string key;
    GarageOf(pi, page, L, I, per, pages, key);
    Notify(std::string("~b~") + PROPS[pi].name + "~s~ garage" + (pages > 1 ? " floor " + std::to_string(page + 1) : "") + ": " + std::to_string(k) +
           " vehicles. Get in one and drive off (or press E) to leave with it.");
}
static void LeaveGarageOnFoot(bool toInterior) {
    int pi = g_garProp;
    ClearGarageCars(0);
    g_garProp = -1;
    if (toInterior) EnterProperty(pi); else CutsceneGarageWalkOut(pi);
}
static void DriveOut(int k) {
    int v = g_garCar[k], idx = g_garIdx[k], pi = g_garProp;
    bool air = PROPS[pi].type == T_HANGAR;
    N(N_DO_SCREEN_FADE_OUT, 400);
    Wait(450);
    if (Exists(g_personal) && g_personal != v) { if (SyncPersonal()) Save(); RemoveBlip(g_personalBlip); N(N_SET_ENTITY_AS_MISSION_ENTITY, g_personal, 1, 1); N(N_DELETE_VEHICLE, &g_personal); }
    g_garCar[k] = 0;
    ClearGarageCars(0);
    g_garProp = -1;
    if (g_inside == pi) g_inside = -1;
    N(N_FREEZE_ENTITY_POSITION, v, 0);
    MakePersonal(v, idx);
    Vec3 e = PropExt(pi); float h = PROPS[pi].h;
    const OBuilding *ob = BuildingOf(pi);
    if (ob) { CutsceneDriveOut(pi, v); return; }   // GTA Online's garage exit
    else if (PROPS[pi].type == T_VILLA) { e = V3(ISLAND_VILLACAR[0], ISLAND_VILLACAR[1], ISLAND_VILLACAR[2]); h = ISLAND_VILLACAR[3]; }
    else if (PROPS[pi].type == T_DATACENTER) { e = V3(DC_GARAGE_ENTRANCE[0], DC_GARAGE_ENTRANCE[1], DC_GARAGE_ENTRANCE[2]); h = DC_GARAGE_ENTRANCE[3]; }
    else if (!std::strcmp(PROPS[pi].interior, "office_mhc")) { e = V3(MHC_GARAGE_ENTRANCE[0], MHC_GARAGE_ENTRANCE[1], MHC_GARAGE_ENTRANCE[2]); h = MHC_GARAGE_ENTRANCE[3]; }
    else if (air) { float r = h * 3.14159265f / 180.f; e = V3(e.x - std::sin(r) * 25.f, e.y + std::cos(r) * 25.f, e.z); }   // out on the apron
    else { Vec3 out{}; float oh = h; if (NB(N<int>(N_GET_CLOSEST_VEHICLE_NODE_WITH_HEADING, e.x, e.y, e.z, &out, &oh, 1, 3.0f, 0.0f)) && Dist(out, e) < 70.f) { e = out; h = oh; } }
    N(N_SET_ENTITY_COORDS, v, e.x, e.y, e.z + (air ? 1.5f : 0.5f), 0, 0, 0, 0);
    N(N_SET_ENTITY_HEADING, v, h);
    N(N_REQUEST_COLLISION_AT_COORD, e.x, e.y, e.z);
    Wait(500);
    N<int>(N_SET_VEHICLE_ON_GROUND_PROPERLY, v, 5.0f);
    N(N_SET_VEHICLE_ENGINE_ON, v, 1, 1, 0);
    Wait(200);
    N(N_DO_SCREEN_FADE_IN, 500);
}
// flying (or taxiing) into your hangar: the aircraft is parked inside with the rest, like Online
static void StoreAircraft(int pi, int v) {
    N(N_DO_SCREEN_FADE_OUT, 400);
    Wait(450);
    if (v == g_personal && g_personalIdx >= 0) { SyncPersonal(); g_garage[g_personalIdx].prop = "@air"; RemoveBlip(g_personalBlip); g_personal = 0; }
    else { GVeh g; g.prop = "@air"; g.model = ModelNameOf(v); g.plate = PlateOf(v); g.data = CaptureVehicle(v); g_garage.push_back(g); GiveRP(100, "aircraft stored"); }
    Save();
    N(N_CLEAR_PED_TASKS_IMMEDIATELY, Me());
    N(N_SET_ENTITY_AS_MISSION_ENTITY, v, 1, 1);
    N(N_DELETE_VEHICLE, &v);
    N(N_DO_SCREEN_FADE_IN, 300);
    EnterProperty(pi);
}
static void OpenGarageMenu();
static void GarageTick() {
    if (g_garProp < 0) return;
    int me = Me(); Vec3 mp = Pos(me);
    if (PROPS[g_garProp].type == T_VILLA && Exists(g_garCar[0]) && MyVehicle() != g_garCar[0]) N(N_SET_ENTITY_HEADING, g_garCar[0], std::fmod(Now() / 50.f, 360.f));
    if (Dist(mp, g_garSpawn) > 120.f || Dead(me)) { ClearGarageCars(0); g_garProp = -1; return; }
    int v = MyVehicle();
    if (v) {
        for (int k = 0; k < GARMAX; ++k) if (g_garCar[k] && g_garCar[k] == v) {
            static int movingSince = 0;
            bool moving = N<float>(N_GET_ENTITY_SPEED, v) > 2.0f;
            if (!moving) movingSince = 0; else if (!movingSince) movingSince = Now();
            if ((moving && Now() - movingSince > 700) || JustPressed(51)) { movingSince = 0; DriveOut(k); return; }
            Help("Drive off (or press ~INPUT_CONTEXT~) to leave with this vehicle.");
        }
        return;
    }
    if (PROPS[g_garProp].type == T_HANGAR) return;     // the hangar's own exit is the property exit
    if (Dist(mp, g_garSpawn) < 25.f) Marker(g_garSpawn, 93, 182, 229);
    if (g_menuOpen) return;
    if (Dist(mp, g_garSpawn) < 1.5f) {
        Help("Press ~INPUT_CONTEXT~ for the garage elevator.");
        if (JustPressed(51)) OpenGarageMenu();
    }
}

// ---------------------------------------------------------------- Businesses
struct BizInfo { uint8_t type; const char *product; int fullValue; int fillMinutes; const char *vehicle; };
static const BizInfo BIZ[] = {
    {T_METH, "Meth", 357000, 90, "speedo"}, {T_WEED, "Weed", 252000, 75, "rumpo"}, {T_COKE, "Cocaine", 504000, 100, "mule"},
    {T_CASH, "Counterfeit Cash", 294000, 80, "boxville"}, {T_DOCS, "Forged Documents", 162000, 60, "speedo"},
    {T_BUNKER, "Weapons", 1050000, 120, "mule"}, {T_DATACENTER, "Encrypted Data", 640000, 110, "pony"},
};
static const BizInfo *ProductionOf(int type) { for (const BizInfo &b : BIZ) if (b.type == type) return &b; return nullptr; }
static const int SUPPLY_COST = 75000;
static int CargoMax(int type) { return type == T_VEHWARE ? 10 : 50; }
static int CargoValue(int type) { return type == T_OFFICE ? 22000 : type == T_HANGAR ? 10000 : 90000; }
static bool IsCargo(int type) { return type == T_OFFICE || type == T_HANGAR || type == T_VEHWARE || type == T_WAREHOUSE; }
// Special Cargo warehouses (GTA Online): 16 / 42 / 111 crates; a sale pays per crate more the more crates go at once
static int CargoMaxP(int pi) {
    if (PROPS[pi].type != T_WAREHOUSE) return CargoMax(PROPS[pi].type);
    std::string in = PROPS[pi].interior; return in == "whs_s" ? 16 : in == "whs_m" ? 42 : 111;
}
static int CrateValue(int n) {   // GTA Online's price per crate for a sale of n crates
    static const int T[][2] = {{1, 10000}, {2, 11000}, {3, 12000}, {5, 13000}, {7, 14000}, {9, 15000}, {14, 16000}, {19, 17000}, {24, 17500},
                               {29, 18000}, {34, 18500}, {39, 19000}, {44, 19500}, {1000, 20000}};
    for (auto &t : T) if (n <= t[0]) return t[1];
    return 20000;
}
static int CargoTotal(int pi, int n) {   // what n units sell for
    int type = PROPS[pi].type;
    return type == T_WAREHOUSE ? n * CrateValue(n) : type == T_VEHWARE ? CargoValue(type) : n * CargoValue(type);
}
static void SafeIncome(int type, int &perDay, int &cap) {
    perDay = type == T_NIGHTCLUB ? 30000 : type == T_ARCADE ? 5000 : type == T_AGENCY ? 20000 : type == T_AUTOSHOP ? 8000 : type == T_DATACENTER ? 12000 :
             type == T_SALVAGE ? 18000 : type == T_BAIL ? 15000 : type == T_GARMENT ? 12000 : type == T_CARWASH ? 9000 : type == T_WEEDSHOP ? 11000 :
             type == T_HELITOURS ? 14000 : 0;   // the salvage yard's tow truck, the bail office's staff, the money fronts' takings
    cap = type == T_NIGHTCLUB ? 300000 : type == T_ARCADE ? 100000 : type == T_AGENCY ? 250000 : type == T_DATACENTER ? 120000 : perDay > 0 && type >= T_SALVAGE ? perDay * 10 : 80000;   // the data center's crypto wallet
}
static int g_lastMinute = 0;
static float DcSpeed(int pi);   // the data center's upgrades (Internet.inc's CUSTOM)
// the data center's staff and research (BizApps.inc, its DataNet computer like the Bunker's Disruption Logistics)
static float DcMfgShare(int pi);     // 1 all staff on production, 0.5 both, 0 research only
static float DcHackBonus(int pi);    // research unlocks: hacking contract pay multiplier
static int DcHackSeconds(int pi), DcHackWanted(int pi);
static float DcSellBonus(int pi);
static bool g_dcSellFar = false;     // "sell to Los Santos": further buyers, +50%
static void BusinessTick() {
    int now = Now();
    if (!g_lastMinute) g_lastMinute = now;
    if (now - g_lastMinute < 60000) return;
    g_lastMinute += 60000;
    g_playMin++;
    for (auto &b : g_biz) {
        int i = FindProp(b.prop);
        if (i < 0) continue;
        int type = PROPS[i].type;
        if (const BizInfo *p = ProductionOf(type)) {
            float step = 100.f / (float)p->fillMinutes * (type == T_DATACENTER ? DcSpeed(i) * DcMfgShare(i) : 1.f);
            if (b.supplies > 0.f && b.stock < 100.f) {
                b.stock = std::min(100.f, b.stock + step);
                b.supplies = std::max(0.f, b.supplies - step * 2.f);
                if (b.supplies <= 0.f) Notify(std::string("~y~") + PROPS[i].name + "~s~ has run out of supplies.");
                if (b.stock >= 100.f) Notify(std::string("~g~") + PROPS[i].name + "~s~ is full of product. Time to sell.");
            }
        }
        int perDay, cap;
        SafeIncome(type, perDay, cap);
        if (perDay && g_playMin % 48 == 0 && b.safe < cap) {
            b.safe = std::min(cap, b.safe + perDay);
            Notify(std::string("Income added to the safe of ~b~") + PROPS[i].name + "~s~: " + Money(perDay));
        }
    }
    if (g_playMin % 5 == 0) Save();
}
static void BuyProperty(int i, int price = -1) {   // price: the website's total (style and extras included)
    PayWhat what("property");
    const PropDef &p = PROPS[i];
    if (Owns(p.name)) return;
    if (!Pay(price >= 0 ? price : p.price)) return;
    g_props.push_back(p.name);
    int t = p.type;
    if (TYPES[t].business || t == T_CLUB || t == T_FACILITY) { BizState b; b.prop = p.name; if (ProductionOf(t)) b.supplies = 100.f; g_biz.push_back(b); }
    if (TYPES[t].residence && g_spawnProp.empty()) g_spawnProp = p.name;
    Save();
    RefreshPropertyBlips();
    GiveRP(1000, "property purchased");
    Shard("~g~PROPERTY PURCHASED", p.name);
}

// ---------------------------------------------------------------- Missions (jobs, sell missions, heists)
enum StepType { S_GOTO, S_PICKUP, S_STEAL, S_DELIVER, S_HOLD, S_ESCAPE };
struct Step {
    StepType type = S_GOTO;
    std::string text;
    Vec3 pos{};
    float radius = 6.f, heading = 0.f;
    bool snapRoad = false, snapFoot = false;   // move the target to the nearest road / walkable spot
    std::string vehicle;                       // S_STEAL
    int enemies = 0;
    const char *enemyModel = "g_m_y_mexgoon_01", *enemyWeapon = "weapon_carbinerifle";
    int chasers = 0, wanted = 0, seconds = 0, pickups = 1;
    const char *pickupModel = "prop_box_wood02a";
    float escapeDist = 0.f;
    bool needMissionVehicle = false;
    std::string ally;                          // a ped that follows you and must survive
    std::function<void()> onStart, onDone;
};
struct Mission {
    std::string name;
    std::vector<Step> steps;
    int timeLimitMs = 0;
    std::function<void()> onSuccess, onFail;
    bool keepVehicle = false;
    int coopPay = -1;   // what each co-op member gets when it passes (-1: by its length)
    bool corona = false; int resultCash = 0, resultRp = 0;   // started from a lobby: the results board at the end
};
static Mission g_m;
static bool g_mActive = false, g_stepInit = false, g_enemiesUp = false, g_snapped = false;
static int g_mKills = 0;                                                        // enemies you killed (the results board)
static std::function<void(bool, const std::string &, const Mission &)> g_missionEnd;   // Corona.inc: the results
static size_t g_mStep = 0;
static int g_mEnds = 0, g_mBlip = 0, g_mVeh = 0, g_mVehBlip = 0, g_mAlly = 0, g_holdMs = 0, g_holdLast = 0, g_holding = 0, g_collected = 0;
static std::vector<int> g_mPeds, g_mCars, g_mObjs, g_mPedBlips, g_mObjBlips;
static uint32_t g_hostile = 0;
#include "Coop.inc"

static void HostileGroup() {
    if (g_hostile) return;
    N<int>(N_ADD_RELATIONSHIP_GROUP, "GORI_HOSTILE", &g_hostile);
    N(N_SET_RELATIONSHIP_BETWEEN_GROUPS, 5, g_hostile, Joaat("PLAYER"));
    N(N_SET_RELATIONSHIP_BETWEEN_GROUPS, 5, Joaat("PLAYER"), g_hostile);
}
static int SpawnEnemy(uint32_t model, uint32_t weapon, float x, float y, float z) {
    int ped = N<int>(N_CREATE_PED, 4, model, x, y, z, (float)Rand(360), 0, 0);
    if (!ped) return 0;
    N(N_SET_ENTITY_AS_MISSION_ENTITY, ped, 1, 1);
    N(N_GIVE_WEAPON_TO_PED, ped, weapon, 500, 0, 1);
    N(N_SET_PED_RELATIONSHIP_GROUP_HASH, ped, g_hostile);
    N(N_SET_PED_COMBAT_ABILITY, ped, 1);
    N(N_SET_PED_ACCURACY, ped, 22);
    N(N_SET_PED_COMBAT_ATTRIBUTES, ped, 46, 1);
    N(N_SET_PED_FLEE_ATTRIBUTES, ped, 0, 0);
    N(N_SET_PED_DROPS_WEAPONS_WHEN_DEAD, ped, 0);
    N(N_TASK_COMBAT_HATED_TARGETS_AROUND_PED, ped, 150.f, 0);
    int b = N<int>(N_ADD_BLIP_FOR_ENTITY, ped);
    N(N_SET_BLIP_COLOUR, b, 1);
    N(N_SET_BLIP_SCALE, b, 0.7f);
    g_mPeds.push_back(ped);
    g_mPedBlips.push_back(b);
    return ped;
}
static void SpawnEnemies(const Step &s) {
    HostileGroup();
    uint32_t pm = Joaat(s.enemyModel), wm = Joaat(s.enemyWeapon);
    if (!LoadModel(pm)) return;
    for (int i = 0; i < s.enemies; ++i) {
        float a = (float)Rand(6283) / 1000.f, r = 5.f + (float)Rand(14);
        float x = s.pos.x + std::cos(a) * r, y = s.pos.y + std::sin(a) * r, z = s.pos.z;
        Vec3 out{};
        if (NB(N<int>(N_GET_SAFE_COORD_FOR_PED, x, y, z, 1, &out, 16)) && Dist(out, s.pos) < 40.f) { x = out.x; y = out.y; z = out.z; }
        else z = GroundAt(x, y, z + 10.f, z);
        SpawnEnemy(pm, wm, x, y, z + 1.0f);
    }
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, pm);
}
static void SpawnChasers(int count) {
    HostileGroup();
    uint32_t cm = Joaat("granger"), pm = Joaat("g_m_y_mexgoon_01"), wm = Joaat("weapon_microsmg");
    if (!LoadModel(cm) || !LoadModel(pm)) return;
    int me = Me(); Vec3 p = Pos(me);
    for (int i = 0; i < count; ++i) {
        float a = (float)Rand(6283) / 1000.f;
        Vec3 out{}; float h = 0.f;
        if (!NB(N<int>(N_GET_CLOSEST_VEHICLE_NODE_WITH_HEADING, p.x + std::cos(a) * 160.f, p.y + std::sin(a) * 160.f, p.z, &out, &h, 1, 3.0f, 0.0f))) continue;
        int car = N<int>(N_CREATE_VEHICLE, cm, out.x, out.y, out.z, h, 0, 0, 0);
        if (!car) continue;
        N(N_SET_ENTITY_AS_MISSION_ENTITY, car, 1, 1);
        g_mCars.push_back(car);
        for (int seat = -1; seat <= 0; ++seat) {
            int ped = N<int>(N_CREATE_PED_INSIDE_VEHICLE, car, 4, pm, seat, 0, 0);
            if (!ped) continue;
            N(N_SET_ENTITY_AS_MISSION_ENTITY, ped, 1, 1);
            N(N_GIVE_WEAPON_TO_PED, ped, wm, 500, 0, 1);
            N(N_SET_PED_RELATIONSHIP_GROUP_HASH, ped, g_hostile);
            N(N_SET_PED_ACCURACY, ped, 15);
            if (seat == -1) N(N_TASK_VEHICLE_CHASE, ped, me); else N(N_TASK_COMBAT_PED, ped, me, 0, 16);
            N(N_SET_PED_KEEP_TASK, ped, 1);
            int b = N<int>(N_ADD_BLIP_FOR_ENTITY, ped);
            N(N_SET_BLIP_COLOUR, b, 1); N(N_SET_BLIP_SCALE, b, 0.7f);
            g_mPeds.push_back(ped); g_mPedBlips.push_back(b);
        }
    }
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, cm); N(N_SET_MODEL_AS_NO_LONGER_NEEDED, pm);
    Notify("~r~Rivals~s~ are coming for your cargo!");
}
static void ClearStepThings() {
    RemoveBlip(g_mBlip);
    for (int &b : g_mObjBlips) RemoveBlip(b);
    g_mObjBlips.clear();
    for (int &o : g_mObjs) DeleteEntity(o);
    g_mObjs.clear();
}
static void CleanupMission() {
    ClearStepThings();
    for (int &b : g_mPedBlips) RemoveBlip(b);
    g_mPedBlips.clear();
    for (int &p : g_mPeds) ForgetEntity(p);
    for (int &c : g_mCars) ForgetEntity(c);
    g_mPeds.clear(); g_mCars.clear();
    RemoveBlip(g_mVehBlip);
    if (g_mVeh) {
        if (g_m.keepVehicle) ForgetEntity(g_mVeh);
        else {
            if (MyVehicle() == g_mVeh) { N(N_TASK_LEAVE_VEHICLE, Me(), g_mVeh, 0); Wait(1500); }
            DeleteEntity(g_mVeh);
        }
    }
    if (g_mAlly) { N(N_REMOVE_PED_FROM_GROUP, g_mAlly); ForgetEntity(g_mAlly); }
    if (g_holding) N(N_CLEAR_PED_TASKS, Me());
    g_holding = 0;
    g_mActive = false;
}
// a reachable spot near p: walkable, flat, outside interiors and water (GET_SAFE_COORD_FOR_PED flags 4|8|16), and not on a
// roof far above the nearest road; otherwise the pavement by that road
static Vec3 ReachableSpot(Vec3 p) {
    Vec3 out{}, node{};
    bool haveNode = NB(N<int>(N_GET_CLOSEST_VEHICLE_NODE, p.x, p.y, p.z, &node, 1, 3.0f, 0.0f));
    float nodeD = haveNode ? std::hypot(node.x - p.x, node.y - p.y) : 1e9f;
    if (NB(N<int>(N_GET_SAFE_COORD_FOR_PED, p.x, p.y, p.z, 1, &out, 28)) && std::hypot(out.x - p.x, out.y - p.y) < 60.f &&
        (!haveNode || nodeD > 120.f || std::fabs(out.z - node.z) < 15.f)) return V3(out.x, out.y, out.z + 1.0f);
    if (haveNode && NB(N<int>(N_GET_SAFE_COORD_FOR_PED, node.x, node.y, node.z, 1, &out, 1 | 4 | 8))) return V3(out.x, out.y, out.z + 1.0f);
    if (haveNode) return V3(node.x, node.y, node.z + 1.0f);
    return p;
}
static void StartMission(const Mission &m) {
    if (g_mActive) { Notify("~r~Finish or quit your current job first."); return; }
    g_m = m; g_mActive = true; g_mStep = 0; g_stepInit = false; g_mVeh = 0; g_mAlly = 0; g_mKills = 0;
    JobEnter();
    g_mEnds = m.timeLimitMs ? Now() + m.timeLimitMs : 0;
    Shard("~y~" + m.name, m.steps.empty() ? "" : m.steps[0].text);
    Log("mission start: %s (%d steps)", m.name.c_str(), (int)m.steps.size());
    CoopOpen(m.name, m.steps.empty() ? Pos(Me()) : m.steps[0].pos, m.coopPay >= 0 ? m.coopPay : 4000 + 3000 * (int)m.steps.size());
}
static void FailMission(const std::string &why) {
    std::function<void()> cb = g_m.onFail;
    Mission done = g_m;
    CoopReport(COOP_FAILED, (int)g_mStep, Pos(Me()), why);
    CleanupMission();
    if (done.corona && g_missionEnd) g_missionEnd(false, why, done);
    else { Shard("~r~MISSION FAILED", why); Sound("ScreenFlash", "MissionFailedSounds"); }
    if (cb) cb();
}
static void PassMission() {
    std::function<void()> cb = g_m.onSuccess;
    Mission done = g_m;
    CoopReport(COOP_PASSED, (int)g_mStep, Pos(Me()), "");
    CleanupMission();
    if (!done.corona) Sound("Mission_Pass_Notify", "DLC_HEISTS_GENERAL_FRONTEND_SOUNDS");
    if (cb) cb();
    if (done.corona && g_missionEnd) g_missionEnd(true, "", done);
}
static void NextStep() {
    Step &s = g_m.steps[g_mStep];
    if (s.onDone) s.onDone();
    if (!g_mActive) return;
    ClearStepThings();
    if (g_holding) { N(N_CLEAR_PED_TASKS, Me()); g_holding = 0; }
    g_mStep++;
    g_stepInit = false;
    if (g_mStep >= g_m.steps.size()) PassMission();
}
static void InitStep(Step &s) {
    g_stepInit = true; g_enemiesUp = false; g_snapped = false; g_holdMs = 0; g_collected = 0; g_holdLast = Now();
    if (s.onStart) s.onStart();
    if (s.wanted) { N(N_SET_PLAYER_WANTED_LEVEL, Player(), s.wanted, 0); N(N_SET_PLAYER_WANTED_LEVEL_NOW, Player(), 0); }
    if (s.chasers) SpawnChasers(s.chasers);
    if (!s.ally.empty() && !g_mAlly) {
        uint32_t am = Joaat(s.ally.c_str());
        if (LoadModel(am)) {
            Vec3 p = Pos(Me());
            g_mAlly = N<int>(N_CREATE_PED, 4, am, p.x + 1.5f, p.y + 1.0f, p.z, 0.f, 0, 0);
            N(N_SET_MODEL_AS_NO_LONGER_NEEDED, am);
            if (g_mAlly) {
                N(N_SET_ENTITY_AS_MISSION_ENTITY, g_mAlly, 1, 1);
                N(N_SET_PED_AS_GROUP_MEMBER, g_mAlly, N<int>(N_GET_PLAYER_GROUP, Player()));
                N(N_SET_ENTITY_MAX_HEALTH, g_mAlly, 500); N(N_SET_ENTITY_HEALTH, g_mAlly, 500, 0, 0);
                N(N_GIVE_WEAPON_TO_PED, g_mAlly, Joaat("weapon_pistol"), 500, 0, 1);
                int b = N<int>(N_ADD_BLIP_FOR_ENTITY, g_mAlly); N(N_SET_BLIP_COLOUR, b, 3); N(N_SET_BLIP_SCALE, b, 0.8f);
            }
        }
    }
    if (s.type == S_STEAL) {
        uint32_t vm = Joaat(s.vehicle.c_str());
        Vec3 p = s.pos; float h = s.heading;
        Vec3 out{}; float oh = h;
        if (NB(N<int>(N_GET_CLOSEST_VEHICLE_NODE_WITH_HEADING, p.x, p.y, p.z, &out, &oh, 1, 3.0f, 0.0f)) && Dist(out, p) < 60.f) { p = out; h = oh; }
        if (LoadModel(vm)) {
            g_mVeh = N<int>(N_CREATE_VEHICLE, vm, p.x, p.y, p.z, h, 0, 0, 0);
            N(N_SET_MODEL_AS_NO_LONGER_NEEDED, vm);
            if (g_mVeh) {
                N(N_SET_ENTITY_AS_MISSION_ENTITY, g_mVeh, 1, 1);
                N<int>(N_SET_VEHICLE_ON_GROUND_PROPERLY, g_mVeh, 5.0f);
                g_mVehBlip = AddBlip(g_mVeh, 225, 2, "Target vehicle");
                s.pos = p;
            }
        }
    } else if (s.type == S_PICKUP) {
        // spawned when the player gets close (needs the ground to exist)
    } else {
        g_mBlip = CoordBlip(s.pos, 1, 5, "Objective", false);
        N(N_SET_BLIP_ROUTE, g_mBlip, 1);
        N(N_SET_BLIP_ROUTE_COLOUR, g_mBlip, 5);
    }
    if (s.type == S_ESCAPE) RemoveBlip(g_mBlip);
    if (s.type == S_DELIVER && g_mVeh && !g_mVehBlip) g_mVehBlip = AddBlip(g_mVeh, 225, 2, "Mission vehicle");
    if (s.type == S_PICKUP) { g_mBlip = CoordBlip(s.pos, 1, 5, "Objective", false); N(N_SET_BLIP_ROUTE, g_mBlip, 1); }
}
static void DrawObjective(const std::string &t) {
    Text(t, 0.5f, 0.905f, 0.45f, 4, 255, 255, 255, 255, 0, 1.0f, true);
    if (g_mEnds) {
        int left = std::max(0, (g_mEnds - Now()) / 1000);
        char b[32]; std::snprintf(b, sizeof b, "%02d:%02d", left / 60, left % 60);
        Rect(0.92f, 0.86f, 0.1f, 0.035f, 0, 0, 0, 160);
        Text(std::string("TIME ") + b, 0.92f, 0.845f, 0.42f, 4, left < 60 ? 230 : 255, left < 60 ? 60 : 255, left < 60 ? 60 : 255, 255, 0);
    }
}
static void MissionTick() {
    if (!g_mActive || g_mStep >= g_m.steps.size()) return;
    int me = Me(); Vec3 mp = Pos(me);
    if (Dead(me)) { FailMission("You died."); return; }
    if (g_mEnds && Now() > g_mEnds) { FailMission("You ran out of time."); return; }
    Step &s = g_m.steps[g_mStep];
    if (!g_stepInit) InitStep(s);
    if (!g_mActive) return;
    if (g_mAlly && (!Exists(g_mAlly) || Dead(g_mAlly))) { g_mAlly = 0; FailMission("Your crew member died."); return; }
    if (g_mVeh && (s.type == S_DELIVER || s.needMissionVehicle) && (!Exists(g_mVeh) || Dead(g_mVeh) || !NB(N<int>(N_IS_VEHICLE_DRIVEABLE, g_mVeh, 0)))) {
        g_mVeh = 0; FailMission("The vehicle was destroyed."); return;
    }
    // dead enemies leave the radar at once (their blips stayed and pointed at bodies)
    for (size_t i = 0; i < g_mPeds.size() && i < g_mPedBlips.size(); ++i)
        if (g_mPedBlips[i] && (!Exists(g_mPeds[i]) || Dead(g_mPeds[i]))) {
            RemoveBlip(g_mPedBlips[i]);
            if (Exists(g_mPeds[i]) && (N<int>(N_GET_PED_SOURCE_OF_DEATH, g_mPeds[i]) == me || NB(N<int>(N_HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY, g_mPeds[i], me, 1)))) ++g_mKills;
        }
    float d = Dist(mp, s.pos);
    // Every target onto a spot you can reach, once the area (collision, navmesh) is streamed in: roads for vehicles,
    // otherwise walkable ground outside closed interiors and water, never a rooftop high above the street
    bool snapAny = s.snapRoad || s.snapFoot || s.type == S_GOTO || s.type == S_PICKUP || s.type == S_HOLD || s.type == S_DELIVER;
    if (!g_snapped && d < 150.f && snapAny) {
        g_snapped = true;
        Vec3 out{}, before = s.pos;
        if (s.snapRoad && NB(N<int>(N_GET_CLOSEST_VEHICLE_NODE, s.pos.x, s.pos.y, s.pos.z, &out, 1, 3.0f, 0.0f)) && Dist(out, s.pos) < 80.f) s.pos = out;
        else if (s.type != S_DELIVER || s.snapFoot) s.pos = ReachableSpot(s.pos);
        if (Dist(before, s.pos) > 2.f) Log("mission: step %d target %.1f %.1f %.1f -> %.1f %.1f %.1f", (int)g_mStep, before.x, before.y, before.z, s.pos.x, s.pos.y, s.pos.z);
        if (g_mBlip) N(N_SET_BLIP_COORDS, g_mBlip, s.pos.x, s.pos.y, s.pos.z);
        d = Dist(mp, s.pos);
    }
    if (s.enemies && !g_enemiesUp && d < 230.f) { g_enemiesUp = true; SpawnEnemies(s); }
    DrawObjective(s.text);
    CoopTrack((int)g_mStep, s.type == S_STEAL && g_mVeh && Exists(g_mVeh) ? Pos(g_mVeh) : s.pos, s.text);
    bool inMissionVeh = g_mVeh && MyVehicle() == g_mVeh;
    switch (s.type) {
    case S_GOTO:
        if (d < 60.f) Marker(s.pos, 240, 200, 80);
        if (d < s.radius) NextStep();
        break;
    case S_STEAL:
        if (g_mVeh && inMissionVeh) { RemoveBlip(g_mVehBlip); NextStep(); }
        break;
    case S_DELIVER: {
        if (s.needMissionVehicle && g_mVeh && !inMissionVeh) {
            if (!g_mVehBlip) g_mVehBlip = AddBlip(g_mVeh, 225, 2, "Mission vehicle");
            DrawObjective("Get back in the ~b~vehicle~s~.");
            break;
        }
        RemoveBlip(g_mVehBlip);
        if (d < 80.f) Marker(s.pos, 240, 200, 80);
        Vec3 vp = s.needMissionVehicle && g_mVeh ? Pos(g_mVeh) : mp;
        if (Dist(vp, s.pos) < s.radius) NextStep();
        break;
    }
    case S_PICKUP:
        if (g_mObjs.empty() && g_collected == 0 && d < 120.f) {
            uint32_t om = Joaat(s.pickupModel);
            if (LoadModel(om)) {
                for (int i = 0; i < s.pickups; ++i) {
                    float a = 6.283f * i / std::max(1, s.pickups), r = s.pickups > 1 ? 2.5f : 0.f;
                    float x = s.pos.x + std::cos(a) * r, y = s.pos.y + std::sin(a) * r;
                    float z = GroundAt(x, y, s.pos.z + 2.f, s.pos.z - 1.f);
                    int o = N<int>(N_CREATE_OBJECT, om, x, y, z, 0, 0, 0);
                    if (!o) continue;
                    N(N_PLACE_OBJECT_ON_GROUND_PROPERLY, o);
                    N(N_FREEZE_ENTITY_POSITION, o, 1);
                    g_mObjs.push_back(o);
                    int b = N<int>(N_ADD_BLIP_FOR_ENTITY, o); N(N_SET_BLIP_COLOUR, b, 2); N(N_SET_BLIP_SCALE, b, 0.8f);
                    g_mObjBlips.push_back(b);
                }
                N(N_SET_MODEL_AS_NO_LONGER_NEEDED, om);
                RemoveBlip(g_mBlip);
                if (g_mObjs.empty()) g_collected = s.pickups;   // could not spawn: don't block the job
            }
        }
        for (size_t i = 0; i < g_mObjs.size(); ++i) {
            if (!g_mObjs[i]) continue;
            Vec3 op = Pos(g_mObjs[i]);
            if (Dist(mp, op) < 2.0f && !InAnyVehicle()) {
                DeleteEntity(g_mObjs[i]);
                RemoveBlip(g_mObjBlips[i]);
                g_collected++;
                Sound("PICK_UP", "HUD_FRONTEND_DEFAULT_SOUNDSET");
            }
        }
        if (g_collected >= s.pickups) NextStep();
        break;
    case S_HOLD: {
        if (d < 50.f) Marker(s.pos, 240, 200, 80);
        int now = Now();
        bool inside = d < s.radius && !InAnyVehicle();
        if (inside) {
            if (!g_holding) { N(N_TASK_START_SCENARIO_IN_PLACE, me, "WORLD_HUMAN_WELDING", 0, 1); g_holding = 1; }
            g_holdMs += now - g_holdLast;
        } else if (g_holding) { N(N_CLEAR_PED_TASKS, me); g_holding = 0; }
        g_holdLast = now;
        float frac = std::min(1.f, g_holdMs / (s.seconds * 1000.f));
        Rect(0.5f, 0.87f, 0.2f, 0.016f, 0, 0, 0, 170);
        Rect(0.4f + 0.1f * frac, 0.87f, 0.2f * frac, 0.016f, 240, 200, 80, 230);
        if (frac >= 1.f) NextStep();
        break;
    }
    case S_ESCAPE: {
        int wanted = N<int>(N_GET_PLAYER_WANTED_LEVEL, Player());
        bool farEnough = s.escapeDist <= 0.f || Dist2(mp, s.pos) > s.escapeDist;
        bool enemiesNear = false;
        for (int p : g_mPeds) if (Exists(p) && !Dead(p) && Dist(Pos(p), mp) < 120.f) enemiesNear = true;
        if (wanted == 0 && farEnough && !enemiesNear) NextStep();
        break;
    }
    }
}

// Mission building blocks
static Step Goto(const std::string &t, Vec3 p, float r = 6.f) { Step s; s.type = S_GOTO; s.text = t; s.pos = p; s.radius = r; return s; }
static Step Pickup(const std::string &t, Vec3 p, int n = 1, const char *model = "prop_box_wood02a") { Step s; s.type = S_PICKUP; s.text = t; s.pos = p; s.pickups = n; s.pickupModel = model; return s; }
static Step Steal(const std::string &t, Vec3 p, float h, const char *vehicle) { Step s; s.type = S_STEAL; s.text = t; s.pos = p; s.heading = h; s.vehicle = vehicle; return s; }
static Step Deliver(const std::string &t, Vec3 p, bool inVehicle = true) { Step s; s.type = S_DELIVER; s.text = t; s.pos = p; s.radius = 8.f; s.needMissionVehicle = inVehicle; s.snapRoad = inVehicle; return s; }
static Step Hold(const std::string &t, Vec3 p, int seconds) { Step s; s.type = S_HOLD; s.text = t; s.pos = p; s.radius = 2.5f; s.seconds = seconds; return s; }
static Step Escape(const std::string &t, Vec3 p = V3(0, 0, 0), float dist = 0.f) { Step s; s.type = S_ESCAPE; s.text = t; s.pos = p; s.escapeDist = dist; return s; }
static Step Enemies(Step s, int n, const char *model, const char *weapon) { s.enemies = n; s.enemyModel = model; s.enemyWeapon = weapon; return s; }

static const float DROPS[][3] = {
    {-147.0f, 6304.0f, 31.5f}, {1961.0f, 3740.0f, 32.3f}, {1705.0f, 4720.0f, 42.1f}, {568.0f, 2735.0f, 42.0f}, {-3170.0f, 1100.0f, 20.8f},
    {-1450.0f, -380.0f, 38.0f}, {-1200.0f, -1400.0f, 4.4f}, {850.0f, -2350.0f, 30.0f}, {870.0f, -1000.0f, 30.0f}, {1150.0f, -650.0f, 57.0f},
    {300.0f, 200.0f, 104.3f}, {-700.0f, -200.0f, 37.0f}, {200.0f, -3000.0f, 6.0f}, {-200.0f, -2500.0f, 6.0f}, {2580.0f, 360.0f, 108.5f}, {-2200.0f, 4250.0f, 47.0f},
};
static const float HOSTILE_SPOTS[][3] = {
    {480.0f, -3060.0f, 6.1f}, {1180.0f, -3110.0f, 5.9f}, {-600.0f, 5300.0f, 70.2f}, {1400.0f, 3600.0f, 34.9f}, {2700.0f, 1550.0f, 24.5f},
    {-1100.0f, 4930.0f, 218.0f}, {2430.0f, 4970.0f, 46.0f}, {-60.0f, -2500.0f, 6.0f}, {870.0f, -2350.0f, 30.0f}, {-480.0f, 6270.0f, 13.0f},
};
static const float PARKING[][4] = {
    {-1149.2f, -1993.5f, 13.2f, 316.f}, {-340.6f, -876.5f, 31.1f, 167.f}, {215.1f, -806.8f, 30.7f, 250.f}, {-1607.9f, -830.2f, 10.0f, 140.f},
    {1128.8f, -779.1f, 57.6f, 0.f}, {-2033.0f, -462.0f, 11.4f, 140.f}, {109.8f, 6608.9f, 31.8f, 315.f}, {1692.0f, 3592.0f, 35.6f, 210.f},
};
static const char *GANGS[][2] = {{"g_m_y_mexgoon_01", "weapon_microsmg"}, {"g_m_y_lost_01", "weapon_sawnoffshotgun"}, {"g_m_y_ballaorig_01", "weapon_pistol"},
                                  {"g_m_y_korean_01", "weapon_smg"}, {"g_m_m_armgoon_01", "weapon_carbinerifle"}, {"g_m_y_salvagoon_01", "weapon_assaultrifle"}};
static const char *STEAL_CARS[] = {"zentorno", "t20", "osiris", "entityxf", "reaper", "fmj", "pfister811", "tempesta", "italigtb", "nero", "seven70", "xa21", "turismor", "cheetah"};
template <size_t K, size_t M> static Vec3 PickSpot(const float (&a)[K][M]) { int i = Rand((int)K); return V3(a[i][0], a[i][1], a[i][2]); }
static Vec3 Safehouse() {
    int i = FindProp(g_spawnProp);
    if (i >= 0 && PROPS[i].type != T_VILLA) return PropExt(i);
    for (auto &o : g_props) { int k = FindProp(o); if (k >= 0 && TYPES[PROPS[k].type].residence && PROPS[k].type != T_VILLA) return PropExt(k); }
    return V3(716.8f, -962.0f, 30.4f);   // Lester's garment factory
}

// ---------------------------------------------------------------- Business missions
static void StealSupplies(int pi) {
    const char *(&gang)[2] = GANGS[Rand(6)];
    Mission m; m.name = "STEAL SUPPLIES";
    Step a = Pickup("Steal the ~g~supplies~s~ from the rival gang.", PickSpot(HOSTILE_SPOTS), 1, "prop_box_wood02a");
    a.snapFoot = true;
    m.steps.push_back(Enemies(a, 4 + Rand(3), gang[0], gang[1]));
    m.steps.push_back(Goto(std::string("Take the supplies to ~y~") + PROPS[pi].name + "~s~.", PropExt(pi), 5.f));
    std::string prop = PROPS[pi].name;
    m.timeLimitMs = 20 * 60000;
    m.onSuccess = [prop] {
        if (BizState *b = BizOf(prop)) b->supplies = std::min(100.f, b->supplies + 40.f);
        AddCash(0); Save(); GiveRP(500, "supplies stolen");
        Shard("~g~SUPPLIES DELIVERED", "Supplies +40%");
    };
    StartMission(m);
}
static void SellProduct(int pi) {
    BizState *b = BizOf(PROPS[pi].name);
    const BizInfo *info = ProductionOf(PROPS[pi].type);
    if (!b || !info) return;
    if (b->stock < 5.f) { Notify("~r~There is no product to sell."); return; }
    int value = (int)(info->fullValue * b->stock / 100.f);
    int drops = b->stock > 50.f ? 2 : 1;
    if (PROPS[pi].type == T_DATACENTER) {   // DataNet: local buyers, or buyers across Los Santos for more (like the Bunker's sale)
        value = (int)(value * DcSellBonus(pi) * (g_dcSellFar ? 1.5f : 1.f));
        if (g_dcSellFar) drops++;
        g_dcSellFar = false;
    }
    Mission m; m.name = std::string("SELL ") + info->product;
    for (auto &c : m.name) c = (char)toupper((unsigned char)c);
    std::string prop = PROPS[pi].name;
    Vec3 ext = PropExt(pi);
    Step get = Goto("Get in the ~b~delivery vehicle~s~.", ext, 3.f);
    std::string veh = info->vehicle;
    get.type = S_STEAL; get.vehicle = veh; get.heading = PROPS[pi].h;
    m.steps.push_back(get);
    for (int i = 0; i < drops; ++i) {
        Step d = Deliver(std::string("Deliver the ") + info->product + " to the ~y~buyer~s~ (" + std::to_string(i + 1) + "/" + std::to_string(drops) + ").", PickSpot(DROPS));
        if (i == 0 && Rand(2)) d.chasers = 1 + Rand(2);
        if (Rand(3) == 0) d.wanted = 2;
        m.steps.push_back(d);
    }
    m.timeLimitMs = 25 * 60000;
    float stock = b->stock;
    m.onSuccess = [prop, value, stock] {
        if (BizState *s = BizOf(prop)) s->stock = std::max(0.f, s->stock - stock);
        AddCash(value); Save(); GiveRP(1500, "product sold");
        Shard("~g~SALE COMPLETE", "You earned " + Money(value));
    };
    m.onFail = [prop] { if (BizState *s = BizOf(prop)) { s->stock = std::max(0.f, s->stock * 0.5f); Save(); } };
    StartMission(m);
}
static void SourceCargo(int pi, int shipment = 0) {   // shipment: crates bought on SecuroServ (warehouses)
    int type = PROPS[pi].type;
    std::string prop = PROPS[pi].name;
    Mission m;
    if (type == T_VEHWARE) {
        m.name = "SOURCE VEHICLE";
        int k = Rand((int)(sizeof PARKING / sizeof *PARKING));
        const char *car = STEAL_CARS[Rand((int)(sizeof STEAL_CARS / sizeof *STEAL_CARS))];
        Step s = Steal("Steal the ~g~high-end vehicle~s~.", V3(PARKING[k][0], PARKING[k][1], PARKING[k][2]), PARKING[k][3], car);
        if (Rand(2)) s = Enemies(s, 3, "g_m_y_korean_01", "weapon_pistol");
        m.steps.push_back(s);
        m.steps.push_back(Deliver(std::string("Take it to your ~y~warehouse~s~."), PropExt(pi)));
        m.onSuccess = [prop] { if (BizState *b = BizOf(prop)) b->stock = std::min(10.f, b->stock + 1.f); Save(); GiveRP(800, "vehicle sourced"); Shard("~g~VEHICLE SOURCED", "Stored in your warehouse"); };
    } else {
        m.name = type == T_OFFICE || type == T_WAREHOUSE ? "SOURCE SPECIAL CARGO" : "SOURCE AIR FREIGHT";
        const char *(&gang)[2] = GANGS[Rand(6)];
        int crates = shipment > 0 ? shipment : type == T_OFFICE ? 1 + Rand(3) : 2 + Rand(3);
        int cap = CargoMaxP(pi);
        Step a = Pickup("Collect the ~g~crates~s~.", PickSpot(HOSTILE_SPOTS), crates, "prop_box_wood02a");
        a.snapFoot = true;
        m.steps.push_back(Enemies(a, 5, gang[0], gang[1]));
        m.steps.push_back(Goto(std::string("Take the cargo to ~y~") + prop + "~s~.", PropExt(pi), 5.f));
        m.onSuccess = [prop, crates, cap] { if (BizState *b = BizOf(prop)) b->stock = std::min((float)cap, b->stock + crates); Save(); GiveRP(700, "cargo sourced"); Shard("~g~CARGO SOURCED", std::to_string(crates) + " crate(s) stored"); };
    }
    m.timeLimitMs = 20 * 60000;
    StartMission(m);
}
static void SellCargo(int pi) {
    int type = PROPS[pi].type;
    BizState *b = BizOf(PROPS[pi].name);
    if (!b || b->stock < 1.f) { Notify("~r~You have nothing to sell yet."); return; }
    std::string prop = PROPS[pi].name;
    Mission m;
    int count = type == T_VEHWARE ? 1 : (int)b->stock;
    int value = CargoTotal(pi, count);
    m.name = type == T_VEHWARE ? "EXPORT VEHICLE" : "SELL CARGO";
    Step get; get.type = S_STEAL; get.text = "Get in the ~b~vehicle~s~."; get.pos = PropExt(pi); get.heading = PROPS[pi].h;
    get.vehicle = type == T_VEHWARE ? STEAL_CARS[Rand((int)(sizeof STEAL_CARS / sizeof *STEAL_CARS))] : type == T_OFFICE || type == T_WAREHOUSE ? "brickade" : "mule";
    m.steps.push_back(get);
    Step d = Deliver("Deliver it to the ~y~buyer~s~.", type == T_VEHWARE ? V3(1200.f, -3000.f, 5.9f) : PickSpot(DROPS));
    if (Rand(2)) d.chasers = 1;
    m.steps.push_back(d);
    m.timeLimitMs = 25 * 60000;
    m.onSuccess = [prop, count, value, type] {
        int pay = value;
        if (BizState *s = BizOf(prop)) s->stock = std::max(0.f, s->stock - count);
        AddCash(pay); Save(); GiveRP(type == T_VEHWARE ? 1000 : 1500, "cargo sold");
        Shard("~g~SALE COMPLETE", "You earned " + Money(pay));
    };
    StartMission(m);
}
// Data center: a client pays to break into a corporate server. Get there, hack it on site while security tries to stop you,
// lose the police. Pays more with better equipment; the stolen data also adds to the stock.
static const float HACK_TARGETS[][3] = {
    {-1047.3f, -233.6f, 39.0f}, {105.1f, -744.6f, 45.8f}, {-68.5f, -800.4f, 44.2f}, {7.1f, -706.6f, 46.0f}, {-1582.0f, -558.0f, 34.9f},
    {253.1f, -366.8f, 44.6f}, {-1379.3f, -503.0f, 33.2f}, {150.6f, -1040.5f, 29.4f}, {-603.6f, -1123.0f, 22.3f}, {1210.5f, -3001.0f, 5.9f},
};
static float DcSpeed(int pi);
static void HackContract(int pi) {
    std::string prop = PROPS[pi].name;
    Mission m; m.name = "HACKING CONTRACT";
    Vec3 t = PickSpot(HACK_TARGETS);
    m.steps.push_back(Goto("Go to the ~y~client's target~s~.", t, 25.f));
    Step h = Hold("Stay by the ~g~server terminal~s~ while the hack runs.", t, DcHackSeconds(pi));
    h.snapFoot = true;
    m.steps.push_back(Enemies(h, 4 + Rand(3), "s_m_m_security_01", "weapon_pistol"));
    Step e = Escape("Lose the ~r~police~s~.");
    e.wanted = DcHackWanted(pi);
    m.steps.push_back(e);
    m.timeLimitMs = 20 * 60000;
    int pay = (int)((55000 + Rand(40000) + (int)((DcSpeed(pi) - 1.f) * 50000)) * DcHackBonus(pi));
    m.onSuccess = [prop, pay] {
        if (BizState *b = BizOf(prop)) b->stock = std::min(100.f, b->stock + 15.f);
        AddCash(pay); Save(); GiveRP(1200, "hacking contract");
        Shard("~g~CONTRACT COMPLETE", "You earned " + Money(pay) + " - data stock +15%");
    };
    StartMission(m);
}
static void ClientJob(int pi) {      // auto shop / agency contracts
    int type = PROPS[pi].type;
    Mission m;
    if (type == T_AUTOSHOP) {
        m.name = "CLIENT JOB";
        Step get; get.type = S_STEAL; get.text = "Get in the ~b~customer's car~s~."; get.pos = PropExt(pi); get.heading = PROPS[pi].h;
        static const char *cars[] = {"sultan2", "jester4", "zr350", "euros", "futo2", "calico", "remus", "rt3000", "comet6", "vectre"};
        get.vehicle = cars[Rand(10)];
        m.steps.push_back(get);
        m.steps.push_back(Deliver("Deliver the car to its ~y~owner~s~ without damage.", PickSpot(DROPS)));
        int pay = 25000 + Rand(20000);
        m.coopPay = pay / 2;
        m.onSuccess = [pay] { AddCash(pay); GiveRP(800, "client job"); Shard("~g~CLIENT JOB COMPLETE", "You earned " + Money(pay)); };
    } else {
        m.name = "SECURITY CONTRACT";
        const char *(&gang)[2] = GANGS[Rand(6)];
        Step a = Goto("Rescue the ~b~client~s~ held by the gang.", PickSpot(HOSTILE_SPOTS), 6.f);
        a.snapFoot = true;
        m.steps.push_back(Enemies(a, 6, gang[0], gang[1]));
        Step e = Escape("Lose the ~r~cops~s~ and take the client to safety.");
        e.ally = "a_m_y_business_02"; e.wanted = 2;
        m.steps.push_back(e);
        m.steps.push_back(Goto("Drop the client at the ~y~Agency~s~.", PropExt(pi), 6.f));
        int pay = 40000 + Rand(30000);
        m.coopPay = pay / 2;
        m.onSuccess = [pay] { AddCash(pay); GiveRP(1000, "security contract"); Shard("~g~CONTRACT COMPLETE", "You earned " + Money(pay)); };
    }
    m.timeLimitMs = 25 * 60000;
    StartMission(m);
}

#include "Ventures.inc"

// ---------------------------------------------------------------- Cayo Perico
static bool g_cayo = false, g_cayoMap = false;
static const Vec3 CAYO = V3(4840.f, -5170.f, 0.f);
static void EnableCayo(bool on) {
    if (on == g_cayo) return;
    g_cayo = on;
    N(N_SET_ISLAND_ENABLED, "HeistIsland", on ? 1 : 0);
    N(N_LOAD_GLOBAL_WATER_FILE, on ? 1 : 0);
    N(N_SET_ALLOW_STREAM_HEIST_ISLAND_NODES, on ? 1 : 0);
    N(N_SET_SCENARIO_GROUP_ENABLED, "Heist_Island_Peds", on ? 1 : 0);
    Log("cayo perico: %s", on ? "on" : "off");
}
static void CayoTick() {
    if (!g_cayo) return;
    Vec3 mp = Pos(Me());
    float d = Dist2(mp, CAYO);
    bool onIsland = d < 2600.f;
    if (onIsland != g_cayoMap) { g_cayoMap = onIsland; N(N_SET_USE_ISLAND_MAP, onIsland ? 1 : 0); }
    if (d > 5000.f && !g_mActive) EnableCayo(false);
}
static void TravelCayo(float x, float y, float z, float h, const char *boat) {
    EnableCayo(true);
    int me = Me();
    N(N_DO_SCREEN_FADE_OUT, 500);
    Wait(600);
    if (boat) {
        uint32_t m = Joaat(boat);
        if (LoadModel(m)) {
            int v = N<int>(N_CREATE_VEHICLE, m, x, y, 0.5f, h, 0, 0, 0);
            N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
            if (v) { N(N_SET_ENTITY_AS_MISSION_ENTITY, v, 1, 1); N(N_SET_PED_INTO_VEHICLE, me, v, -1); ForgetEntity(v); }
        }
        Wait(800);
        N(N_DO_SCREEN_FADE_IN, 500);
    } else {
        N(N_DO_SCREEN_FADE_IN, 0);
        Teleport(x, y, z, h);
    }
}

// ---------------------------------------------------------------- Heists
struct HeistDef { const char *name, *desc; int needType, fee, payout, rp; int stages; const char *stageNames[4]; };
static const HeistDef HEISTS[NHEISTS] = {
    {"The Fleeca Job", "Lester's warm-up: drill a Fleeca vault on the Great Ocean Highway.", -2, 25000, 230000, 3000, 3, {"Scope Out", "Kuruma", "Finale"}},
    {"The Prison Break", "Break Rashkovsky out of Bolingbroke Penitentiary.", -2, 40000, 1000000, 6000, 3, {"Plane", "Uniforms", "Finale"}},
    {"The Humane Labs Raid", "Steal the experimental compound from Humane Labs.", -2, 50000, 1350000, 7000, 3, {"Key Codes", "Insurgents", "Finale"}},
    {"Series A Funding", "Collect the product of three gangs for Trevor.", -2, 50000, 800000, 6000, 4, {"Families", "Vagos", "The Lost", "Finale"}},
    {"The Pacific Standard Job", "Rob the vault of the Pacific Standard bank.", -2, 75000, 1250000, 8000, 4, {"Vans", "Hack", "Bikes", "Finale"}},
    {"The Diamond Casino Heist", "Hit the cashier cage of the Diamond Casino.", T_ARCADE, 100000, 2100000, 9000, 3, {"Keycards", "Getaway", "Finale"}},
    {"The Cayo Perico Heist", "Infiltrate El Rubio's compound from the Kosatka.", T_KOSATKA, 25000, 1900000, 9000, 3, {"Gather Intel", "Plasma Cutter", "Finale"}},
    {"The Kortz Center Heist", "Steal the masterpiece from the Kortz Center exhibition.", -1, 100000, 1600000, 8000, 3, {"Blueprints", "Cutting Tools", "Finale"}},
};
static bool HeistUnlocked(int h, std::string &why) {
    int need = HEISTS[h].needType;
    if (need == -1) return true;
    for (auto &o : g_props) {
        int i = FindProp(o);
        if (i < 0) continue;
        if (need == -2 && (PROPS[i].type == T_APT || PROPS[i].type == T_HOUSE || PROPS[i].type == T_MANSION)) return true;
        if (PROPS[i].type == need) return true;
    }
    why = need == -2 ? "Needs a high-end apartment or house." : std::string("Needs: ") + TYPES[need].label;
    return false;
}
static void HeistStageDone(int h, bool finale) {
    if (finale) {
        g_heistStage[h] = 0;
        g_heistReadyAt = g_playMin + 20;
        // the leader's cut: the take less the crew's (each member was paid its cut by the server), at least 15%
        int crew = 0;
        for (auto &n : Split(g_coop.members, ',')) if (!n.empty()) ++crew;
        int leaderPct = std::max(15, 100 - g_heistCut[h] * crew);
        int pay = HEISTS[h].payout * leaderPct / 100;
        g_lastHeistPay = pay;
        AddCash(pay);
        GiveRP(HEISTS[h].rp, "heist complete");
        Log("heist %s: leader cut %d%% (%d crew at %d%%) = %d", HEISTS[h].name, leaderPct, crew, g_heistCut[h], pay);
    } else {
        g_heistStage[h]++;
        GiveRP(1500, "heist prep done");
        Shard("~g~PREP COMPLETE", std::string(HEISTS[h].name) + ": " + HEISTS[h].stageNames[g_heistStage[h] - 1]);
    }
    Save();
}
static Step Interior(Step s, const char *interiorId) {    // reaching the step teleports you inside
    auto prev = s.onDone;
    s.onDone = [interiorId, prev] { if (prev) prev(); if (const InteriorDef *d = FindInterior(interiorId)) TeleportInside(*d); };
    return s;
}
static void StartHeist(int h) {
    std::string why;
    if (!HeistUnlocked(h, why)) { Notify("~r~" + why); return; }
    if (g_playMin < g_heistReadyAt) { Notify("Lester: \"Lay low for a while. Next job in " + std::to_string(g_heistReadyAt - g_playMin) + " minutes.\""); return; }
    int st = g_heistStage[h];
    bool finale = st == HEISTS[h].stages - 1;
    if (st == 0) { PayWhat what("heist setup (job)"); if (!Pay(HEISTS[h].fee)) return; }
    Mission m;
    m.corona = true; m.resultCash = finale ? HEISTS[h].payout : 0; m.resultRp = HEISTS[h].rp;   // the results board at the end
    m.name = std::string(HEISTS[h].name) + (finale ? "" : std::string(": ") + HEISTS[h].stageNames[st]);
    for (auto &c : m.name) c = (char)toupper((unsigned char)c);
    m.timeLimitMs = 30 * 60000;
    Vec3 safe = Safehouse();
    int k = Rand((int)(sizeof PARKING / sizeof *PARKING));
    Vec3 park = V3(PARKING[k][0], PARKING[k][1], PARKING[k][2]);
    float parkH = PARKING[k][3];
    auto prep = [&](Step s, int enemies, int gang) { s.snapFoot = s.type == S_PICKUP || s.type == S_GOTO; return Enemies(s, enemies, GANGS[gang][0], GANGS[gang][1]); };
    switch (h) {
    case 0:   // Fleeca
        if (st == 0) { m.steps.push_back(Goto("Scope out the ~y~Fleeca~s~ on the Great Ocean Highway.", V3(-2966.f, 482.9f, 15.7f), 10.f)); }
        else if (st == 1) { m.steps.push_back(Enemies(Steal("Steal the ~g~armored Kuruma~s~.", park, parkH, "kuruma2"), 4, "g_m_y_mexgoon_01", "weapon_microsmg"));
                            m.steps.push_back(Deliver("Take it to your ~y~safehouse~s~.", safe)); m.keepVehicle = true; }
        else {
            m.steps.push_back(Goto("Go to the ~y~Fleeca~s~ bank.", V3(-2966.f, 482.9f, 15.7f), 5.f));
            Step d = Hold("~y~Drill~s~ the vault door. Stay close.", V3(-2958.5f, 482.3f, 15.7f), 40); d.wanted = 3; m.steps.push_back(d);
            m.steps.push_back(Pickup("Grab the ~g~cash~s~.", V3(-2957.0f, 484.5f, 15.7f), 1, "prop_money_bag_01"));
            m.steps.push_back(Escape("Lose the ~r~cops~s~."));
        }
        break;
    case 1:   // Prison break
        if (st == 0) { m.steps.push_back(Enemies(Steal("Steal the ~g~Velum~s~ at Sandy Shores airfield.", V3(1708.f, 3254.f, 41.f), 105.f, "velum2"), 4, "g_m_y_lost_01", "weapon_sawnoffshotgun"));
                       m.steps.push_back(Deliver("Land it at the ~y~Grapeseed airstrip~s~.", V3(2133.f, 4783.f, 40.9f))); }
        else if (st == 1) { m.steps.push_back(prep(Pickup("Steal the ~g~prison guard uniforms~s~.", V3(1855.f, 2601.f, 45.6f), 1, "prop_cs_cardbox_01"), 3, 2)); }
        else {
            Step g = Interior(Goto("Get to the ~y~prison gate~s~.", V3(1845.9f, 2585.9f, 45.67f), 5.f), "");
            g.onDone = [] { Teleport(1691.0f, 2565.0f, 45.6f, 0.f); };
            m.steps.push_back(g);
            Step y = Goto("Find ~b~Rashkovsky~s~ in the yard.", V3(1691.0f, 2565.0f, 45.6f), 3.f);
            y.enemies = 8; y.enemyModel = "s_m_m_prisguard_01"; y.enemyWeapon = "weapon_pistol"; y.wanted = 3;
            m.steps.push_back(y);
            Step e = Escape("Escape the prison with Rashkovsky and lose the ~r~cops~s~."); e.ally = "s_m_y_prisoner_01";
            m.steps.push_back(e);
            m.steps.push_back(Deliver("Take Rashkovsky to the ~y~Sandy Shores airfield~s~.", V3(1733.f, 3291.f, 41.1f), false));
        }
        break;
    case 2:   // Humane labs
        if (st == 0) { m.steps.push_back(prep(Pickup("Steal the ~g~key codes~s~ from the Merryweather convoy.", PickSpot(HOSTILE_SPOTS), 1, "prop_laptop_01a"), 5, 4));
                       m.steps.push_back(Goto("Bring them to your ~y~safehouse~s~.", safe, 5.f)); }
        else if (st == 1) { m.steps.push_back(Enemies(Steal("Steal the ~g~Insurgent~s~.", park, parkH, "insurgent"), 5, "s_m_y_blackops_01", "weapon_carbinerifle"));
                            m.steps.push_back(Deliver("Take it to your ~y~safehouse~s~.", safe)); m.keepVehicle = true; }
        else {
            Step g = Goto("Break into ~y~Humane Labs~s~.", V3(3617.f, 3739.f, 28.7f), 8.f);
            g.enemies = 8; g.enemyModel = "s_m_y_blackops_01"; g.enemyWeapon = "weapon_carbinerifle";
            m.steps.push_back(g);
            Step d = Hold("Steal the ~g~compound~s~ from the lab.", V3(3536.7f, 3668.9f, 28.1f), 30); d.snapFoot = true; d.wanted = 4;
            m.steps.push_back(d);
            m.steps.push_back(Escape("Lose the ~r~cops~s~."));
        }
        break;
    case 3: { // Series A
        static const float spots[3][3] = {{100.5f, -1937.0f, 20.8f}, {338.0f, -2034.0f, 21.6f}, {982.0f, -103.0f, 74.8f}};
        static const int gang[3] = {2, 0, 1};
        if (st < 3) {
            Step p = Pickup("Steal the ~g~product~s~ from the gang.", V3(spots[st][0], spots[st][1], spots[st][2]), 1, "prop_drug_package");
            p.snapFoot = true;
            m.steps.push_back(Enemies(p, 6, GANGS[gang[st]][0], GANGS[gang[st]][1]));
            m.steps.push_back(Escape("Lose the ~r~gang~s~ and the cops.", V3(spots[st][0], spots[st][1], 0.f), 150.f));
        } else m.steps.push_back(Goto("Deliver everything to ~y~Trevor~s~ in Sandy Shores.", V3(1395.f, 3613.f, 34.9f), 6.f));
        break;
    }
    case 4:   // Pacific Standard
        if (st == 0) { m.steps.push_back(Steal("Steal the ~g~security van~s~.", park, parkH, "boxville"));
                       m.steps.push_back(Deliver("Leave it near the ~y~Pacific Standard~s~.", V3(220.f, 200.f, 105.5f))); }
        else if (st == 1) { m.steps.push_back(prep(Pickup("Steal the ~g~hacking device~s~.", PickSpot(HOSTILE_SPOTS), 1, "prop_laptop_01a"), 5, 3));
                            m.steps.push_back(Goto("Bring it to your ~y~safehouse~s~.", safe, 5.f)); }
        else if (st == 2) { m.steps.push_back(Enemies(Steal("Steal the ~g~Lectro~s~ getaway bike.", park, parkH, "lectro"), 3, "g_m_y_lost_01", "weapon_pistol"));
                            m.steps.push_back(Deliver("Hide it at your ~y~safehouse~s~.", safe)); }
        else {
            m.steps.push_back(Interior(Goto("Go to the ~y~Pacific Standard~s~.", V3(229.f, 213.f, 105.5f), 5.f), "psbank"));
            Step d = Hold("~y~Hack~s~ the vault.", V3(253.3f, 228.4f, 101.7f), 45);
            d.enemies = 4; d.enemyModel = "s_m_m_security_01"; d.enemyWeapon = "weapon_pistol"; d.wanted = 4; d.snapFoot = true;
            m.steps.push_back(d);
            m.steps.push_back(Pickup("Grab the ~g~cash~s~.", V3(263.f, 214.f, 101.7f), 2, "prop_money_bag_01"));
            Step out = Goto("Get out of the ~y~bank~s~.", V3(247.f, 222.f, 106.3f), 3.f);
            out.onDone = [] { Teleport(229.f, 205.f, 105.5f, 160.f); };
            m.steps.push_back(out);
            m.steps.push_back(Escape("Lose the ~r~cops~s~."));
        }
        break;
    case 5:   // Diamond Casino
        if (st == 0) { m.steps.push_back(prep(Pickup("Steal the ~g~vault keycards~s~.", PickSpot(HOSTILE_SPOTS), 1, "prop_cs_swipe_card"), 5, 3));
                       m.steps.push_back(Goto("Bring them to your ~y~Arcade~s~.", safe, 5.f)); }
        else if (st == 1) { m.steps.push_back(Steal("Steal the ~g~getaway car~s~.", park, parkH, "sultan2"));
                            m.steps.push_back(Deliver("Park it near the ~y~casino~s~.", V3(930.f, 30.f, 80.f))); m.keepVehicle = true; }
        else {
            m.steps.push_back(Interior(Goto("Walk into the ~y~Diamond Casino~s~.", V3(924.4f, 46.9f, 81.1f), 4.f), "casino"));
            Step d = Hold("Break into the ~y~cashier cage~s~.", V3(1117.8f, 219.9f, -49.4f), 35);
            d.enemies = 8; d.enemyModel = "s_m_m_highsec_01"; d.enemyWeapon = "weapon_smg"; d.snapFoot = true;
            m.steps.push_back(d);
            m.steps.push_back(Pickup("Grab the ~g~cash~s~.", V3(1112.f, 222.f, -49.4f), 3, "prop_money_bag_01"));
            Step out = Goto("Leave the ~y~casino~s~.", V3(1089.97f, 206.0f, -49.0f), 3.f);
            out.onDone = [] { Teleport(918.f, 51.f, 80.9f, 330.f); N(N_SET_PLAYER_WANTED_LEVEL, Player(), 4, 0); N(N_SET_PLAYER_WANTED_LEVEL_NOW, Player(), 0); };
            m.steps.push_back(out);
            m.steps.push_back(Escape("Lose the ~r~cops~s~."));
        }
        break;
    case 6:   // Cayo Perico
        if (st == 0) {
            Step go = Goto("Scout ~y~El Rubio's compound~s~ on Cayo Perico.", V3(4975.f, -5690.f, 20.f), 30.f);
            go.onStart = [] { TravelCayo(4300.f, -5300.f, 0.5f, 60.f, "longfin"); };
            m.steps.push_back(go);
            m.steps.push_back(Escape("Get back to sea without being seen.", V3(4975.f, -5690.f, 0.f), 1200.f));
        } else if (st == 1) { m.steps.push_back(prep(Pickup("Steal the ~g~plasma cutter~s~.", PickSpot(HOSTILE_SPOTS), 1, "prop_tool_box_04"), 5, 5));
                              m.steps.push_back(Goto("Bring it to the ~y~Del Perro pier~s~ (Kosatka pickup).", V3(-1600.f, -1050.f, 13.f), 6.f)); }
        else {
            Step go = Goto("Infiltrate ~y~El Rubio's compound~s~.", V3(4990.f, -5717.f, 19.9f), 10.f);
            go.onStart = [] { TravelCayo(4300.f, -5900.f, 0.5f, 40.f, "longfin"); };
            go.enemies = 10; go.enemyModel = "g_m_m_cartelguards_01"; go.enemyWeapon = "weapon_assaultrifle";
            m.steps.push_back(go);
            Step d = Hold("Cut into the ~y~office safe~s~.", V3(5010.f, -5752.f, 28.8f), 40); d.snapFoot = true;
            m.steps.push_back(d);
            Step p = Pickup("Take the ~g~loot~s~.", V3(5010.f, -5752.f, 28.8f), 3, "prop_gold_bar"); p.snapFoot = true;
            m.steps.push_back(p);
            m.steps.push_back(Escape("Escape the island: get out to ~b~open sea~s~.", V3(4840.f, -5170.f, 0.f), 1800.f));
        }
        break;
    case 7:   // Kortz Center
        if (st == 0) { m.steps.push_back(prep(Pickup("Steal the ~g~museum blueprints~s~.", PickSpot(HOSTILE_SPOTS), 1, "prop_cs_documents_01"), 5, 4));
                       m.steps.push_back(Goto("Bring them to your ~y~safehouse~s~.", safe, 5.f)); }
        else if (st == 1) { m.steps.push_back(Enemies(Steal("Steal the ~g~van with the cutting tools~s~.", park, parkH, "speedo"), 3, "g_m_m_armgoon_01", "weapon_carbinerifle"));
                            m.steps.push_back(Deliver("Take it to your ~y~safehouse~s~.", safe)); }
        else {
            m.steps.push_back(Interior(Goto("Get into the ~y~Kortz Center~s~.", V3(-2242.16f, 270.78f, 173.5f), 6.f), "kortz"));
            Step d = Hold("Cut the ~y~display case~s~.", V3(2606.f, 5903.f, -50.f), 35);
            d.enemies = 6; d.enemyModel = "s_m_m_security_01"; d.enemyWeapon = "weapon_pistol"; d.snapFoot = true;
            m.steps.push_back(d);
            Step out = Goto("Get out of the ~y~exhibition~s~.", V3(2603.17f, 5903.76f, -50.f), 3.f);
            out.onDone = [] { Teleport(-2238.f, 265.f, 174.6f, 200.f); N(N_SET_PLAYER_WANTED_LEVEL, Player(), 4, 0); N(N_SET_PLAYER_WANTED_LEVEL_NOW, Player(), 0); };
            m.steps.push_back(out);
            m.steps.push_back(Escape("Lose the ~r~cops~s~."));
        }
        break;
    }
    m.coopPay = finale ? HEISTS[h].payout * g_heistCut[h] / 100 : 5000;   // a crew member's cut (the lobby's Crew Cut)
    m.onSuccess = [h, finale] { HeistStageDone(h, finale); };
    StartMission(m);
}

// ---------------------------------------------------------------- Isla Neón life: traffic, people, parked vehicles, activities
static const int NIROADS = (int)(sizeof ISLAND_ROADS / sizeof *ISLAND_ROADS);
static const int NPEDSPOTS = (int)(sizeof ISLAND_PEDSPOTS / sizeof *ISLAND_PEDSPOTS);
static const int NRING = (int)(sizeof ISLAND_RING / sizeof *ISLAND_RING);
struct IslCar { int veh = 0, ped = 0, road = 0, k = 0, dir = 1, lastMove = 0, lastTask = 0; Vec3 lastPos{}; };
struct IslPed { int ped = 0, target = -1; bool walker = false; };
static std::vector<IslCar> g_islCars;
static std::vector<IslPed> g_islPeds;
static std::vector<int> g_partyPeds;
static int g_islParked[3] = {0, 0, 0}, g_islLast = 0, g_partyEnds = 0;
static const char *ISL_CARS[] = {"bifta", "kalahari", "manana", "peyote", "tornado", "rebel", "dloader", "winky", "verus", "seminole", "issi2", "panto", "blista", "faggio", "surfer", "squaddie"};
static const char *ISL_PEDS[] = {"a_f_y_beach_01", "a_m_y_beach_01", "a_m_y_beach_02", "a_m_y_beach_03", "a_f_m_beach_01", "a_m_m_beach_01", "a_m_y_hipster_01",
                                 "a_f_y_hipster_02", "a_m_y_surfer_01", "a_f_y_tourist_01", "a_m_m_tourist_01", "a_f_y_bevhills_01"};
static const char *ISL_SCENARIOS[] = {"WORLD_HUMAN_STAND_MOBILE", "WORLD_HUMAN_SMOKING", "WORLD_HUMAN_DRINKING", "WORLD_HUMAN_HANG_OUT_STREET", "WORLD_HUMAN_TOURIST_MAP",
                                      "WORLD_HUMAN_MUSICIAN", "WORLD_HUMAN_STAND_IMPATIENT", "WORLD_HUMAN_TOURIST_MOBILE", "WORLD_HUMAN_PARTYING", "WORLD_HUMAN_CHEERING"};
static Vec3 RoadPt(int r, int k) { const float *p = ISLAND_ROADPTS[ISLAND_ROADS[r].first + k]; return V3(p[0], p[1], p[2]); }
static void IslandLifeClear() {
    for (auto &c : g_islCars) { DeleteEntity(c.ped); DeleteEntity(c.veh); }
    for (auto &p : g_islPeds) DeleteEntity(p.ped);
    for (int &v : g_islParked) if (v && v != g_personal && MyVehicle() != v) ForgetEntity(v); else v = 0;
    g_islCars.clear(); g_islPeds.clear();
}
// Next waypoint, 2.8 m to the right of the centre line (cars keep their lane)
static Vec3 LaneTarget(const IslCar &c) {
    int n = ISLAND_ROADS[c.road].count;
    int k2 = std::clamp(c.k + c.dir, 0, n - 1);
    Vec3 a = RoadPt(c.road, c.k), b = RoadPt(c.road, k2);
    float dx = b.x - a.x, dy = b.y - a.y, l = std::sqrt(dx * dx + dy * dy) + 1e-3f;
    return V3(b.x + dy / l * 2.8f, b.y - dx / l * 2.8f, b.z);
}
static void DriveOn(IslCar &c) {
    Vec3 t = LaneTarget(c);
    N(N_TASK_VEHICLE_DRIVE_TO_COORD, c.ped, c.veh, t.x, t.y, t.z, ISLAND_ROADS[c.road].city ? 10.f : 15.f, 0, (uint32_t)N<int>(N_GET_ENTITY_MODEL, c.veh), 16777279, 4.0f, -1.0f);
    c.lastTask = Now();
}
static void NextRoad(IslCar &c) {     // at the end of a road: continue on a road that starts or ends here
    Vec3 end = RoadPt(c.road, c.k);
    std::vector<std::pair<int, int>> options;
    for (int r = 0; r < NIROADS; ++r) {
        int n = ISLAND_ROADS[r].count;
        if (r != c.road && Dist2(RoadPt(r, 0), end) < 8.f) options.push_back({r, 1});
        if (r != c.road && Dist2(RoadPt(r, n - 1), end) < 8.f) options.push_back({r, -1});
    }
    if (options.empty()) { c.dir = -c.dir; return; }
    auto o = options[Rand((int)options.size())];
    c.road = o.first; c.dir = o.second; c.k = o.second == 1 ? 0 : ISLAND_ROADS[o.first].count - 1;
}
static void SpawnIslandCar(const Vec3 &mp) {
    int r = Rand(NIROADS), n = ISLAND_ROADS[r].count;
    if (n < 3) return;
    int k = 1 + Rand(n - 2);
    Vec3 p = RoadPt(r, k);
    float d = Dist2(p, mp);
    if (d < 70.f || d > 380.f) return;
    uint32_t vm = Joaat(ISL_CARS[Rand((int)(sizeof ISL_CARS / sizeof *ISL_CARS))]), pm = Joaat(ISL_PEDS[Rand((int)(sizeof ISL_PEDS / sizeof *ISL_PEDS))]);
    if (!LoadModel(vm) || !LoadModel(pm)) return;
    IslCar c; c.road = r; c.k = k; c.dir = Rand(2) ? 1 : -1;
    Vec3 t = LaneTarget(c), a = RoadPt(r, k);
    float heading = std::atan2(-(t.x - a.x), t.y - a.y) * 57.2958f;
    c.veh = N<int>(N_CREATE_VEHICLE, vm, a.x + (t.x - p.x) * 0.0f, a.y, a.z + 0.3f, heading, 0, 0, 0);
    if (!c.veh) return;
    N(N_SET_ENTITY_AS_MISSION_ENTITY, c.veh, 1, 1);
    c.ped = N<int>(N_CREATE_PED_INSIDE_VEHICLE, c.veh, 4, pm, -1, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, vm); N(N_SET_MODEL_AS_NO_LONGER_NEEDED, pm);
    if (!c.ped) { DeleteEntity(c.veh); return; }
    N(N_SET_ENTITY_AS_MISSION_ENTITY, c.ped, 1, 1);
    N(N_SET_BLOCKING_OF_NON_TEMPORARY_EVENTS, c.ped, 1);
    N(N_SET_PED_KEEP_TASK, c.ped, 1);
    N<int>(N_SET_VEHICLE_ON_GROUND_PROPERLY, c.veh, 5.0f);
    c.lastPos = a; c.lastMove = Now();
    DriveOn(c);
    g_islCars.push_back(c);
}
static void SpawnIslandPed(const Vec3 &mp, bool forceScenario, const char *scenario, Vec3 at) {
    Vec3 p = at;
    if (!forceScenario) {
        const float *s = ISLAND_PEDSPOTS[Rand(NPEDSPOTS)];
        p = V3(s[0] + (Rand(100) - 50) * 0.03f, s[1] + (Rand(100) - 50) * 0.03f, s[2]);
        float d = Dist2(p, mp);
        if (d < 35.f || d > 260.f) return;
    }
    uint32_t pm = Joaat(ISL_PEDS[Rand((int)(sizeof ISL_PEDS / sizeof *ISL_PEDS))]);
    if (!LoadModel(pm)) return;
    int ped = N<int>(N_CREATE_PED, 4, pm, p.x, p.y, p.z, (float)Rand(360), 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, pm);
    if (!ped) return;
    N(N_SET_ENTITY_AS_MISSION_ENTITY, ped, 1, 1);
    IslPed ip; ip.ped = ped;
    if (forceScenario) { N(N_TASK_START_SCENARIO_IN_PLACE, ped, scenario, 0, 1); g_partyPeds.push_back(ped); return; }
    ip.walker = Rand(2) == 0;
    if (!ip.walker) N(N_TASK_START_SCENARIO_IN_PLACE, ped, ISL_SCENARIOS[Rand(8)], 0, 1);
    g_islPeds.push_back(ip);
}
static void WalkOn(IslPed &p) {
    Vec3 pp = Pos(p.ped);
    for (int tries = 0; tries < 12; ++tries) {
        int t = Rand(NPEDSPOTS);
        Vec3 s = V3(ISLAND_PEDSPOTS[t][0], ISLAND_PEDSPOTS[t][1], ISLAND_PEDSPOTS[t][2]);
        float d = Dist2(s, pp);
        if (d > 15.f && d < 90.f) { p.target = t; N(N_TASK_GO_STRAIGHT_TO_COORD, p.ped, s.x, s.y, s.z, 1.0f, -1, 0.f, 0.f); return; }
    }
}
static void IslandParked(const Vec3 &mp) {
    struct Spot { const float *p; const char *model; int colour; };
    static const Spot spots[3] = {{ISLAND_VILLACAR, "comet2", 55}, {ISLAND_PIEREND, "speeder", 111}, {ISLAND_RUNWAYA, "luxor", 111}};
    for (int i = 0; i < 3; ++i) {
        Vec3 p = V3(spots[i].p[0], spots[i].p[1], spots[i].p[2]);
        float d = Dist2(mp, p);
        if (!g_islParked[i] && d < 250.f) {
            uint32_t m = Joaat(spots[i].model);
            if (!LoadModel(m)) continue;
            g_islParked[i] = N<int>(N_CREATE_VEHICLE, m, p.x, p.y, p.z, spots[i].p[3], 0, 0, 0);
            N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
            if (!g_islParked[i]) continue;
            N(N_SET_ENTITY_AS_MISSION_ENTITY, g_islParked[i], 1, 1);
            N(N_SET_VEHICLE_COLOURS, g_islParked[i], spots[i].colour, spots[i].colour);
            N(N_SET_VEHICLE_NUMBER_PLATE_TEXT, g_islParked[i], "GORI");
            if (i != 1) N<int>(N_SET_VEHICLE_ON_GROUND_PROPERLY, g_islParked[i], 5.0f);
        } else if (g_islParked[i] && d > 700.f && MyVehicle() != g_islParked[i]) ForgetEntity(g_islParked[i]);
    }
}
static void IslandLifeTick() {
    if (!g_islBlip) g_islBlip = CoordBlip(V3(ISLAND_CX, ISLAND_CY, 5.f), 307, 27, "Isla Neon", false);
    int now = Now();
    if (now - g_islLast < 400) return;
    g_islLast = now;
    Vec3 mp = Pos(Me());
    if (g_partyEnds && now > g_partyEnds) { for (int &p : g_partyPeds) DeleteEntity(p); g_partyPeds.clear(); g_partyEnds = 0; }
    if (!g_optIsland || !IslandNear(mp, 500.f)) { if (!g_islCars.empty() || !g_islPeds.empty()) IslandLifeClear(); return; }
    IslandParked(mp);
    // traffic
    for (size_t i = 0; i < g_islCars.size();) {
        IslCar &c = g_islCars[i];
        bool gone = !Exists(c.veh) || !Exists(c.ped) || Dead(c.veh) || Dead(c.ped);
        Vec3 vp = gone ? mp : Pos(c.veh);
        if (!gone && Dist(vp, c.lastPos) > 1.5f) { c.lastPos = vp; c.lastMove = now; }
        bool stuck = now - c.lastMove > 20000;
        if (gone || Dist2(vp, mp) > 450.f || stuck || N<int>(N_GET_VEHICLE_PED_IS_IN, Me(), 0) == c.veh) {
            if (MyVehicle() == c.veh) { ForgetEntity(c.ped); ForgetEntity(c.veh); }   // you took it: it's yours now
            else { DeleteEntity(c.ped); DeleteEntity(c.veh); }
            g_islCars.erase(g_islCars.begin() + i);
            continue;
        }
        if (Dist2(vp, LaneTarget(c)) < 9.f) {
            int n = ISLAND_ROADS[c.road].count;
            c.k += c.dir;
            if (c.k <= 0 || c.k >= n - 1) { c.k = std::clamp(c.k, 0, n - 1); NextRoad(c); }
            DriveOn(c);
        } else if (now - c.lastTask > 6000) DriveOn(c);
        ++i;
    }
    if (g_islCars.size() < 9) SpawnIslandCar(mp);
    // people
    for (size_t i = 0; i < g_islPeds.size();) {
        IslPed &p = g_islPeds[i];
        if (!Exists(p.ped) || Dead(p.ped) || Dist2(Pos(p.ped), mp) > 320.f) { DeleteEntity(p.ped); g_islPeds.erase(g_islPeds.begin() + i); continue; }
        if (p.walker && (p.target < 0 || Dist2(Pos(p.ped), V3(ISLAND_PEDSPOTS[p.target][0], ISLAND_PEDSPOTS[p.target][1], 0.f)) < 2.5f)) WalkOn(p);
        ++i;
    }
    if (g_islPeds.size() < 22) SpawnIslandPed(mp, false, nullptr, mp);
}

static std::string FormatTime(int ms) { char b[32]; std::snprintf(b, sizeof b, "%d:%02d.%02d", ms / 60000, ms / 1000 % 60, ms / 10 % 100); return b; }
static int g_trialStart = 0;
static void StartTimeTrial() {
    if (!InAnyVehicle()) { Notify("~r~Get in a vehicle first."); return; }
    Mission m; m.name = "ISLA NEON TIME TRIAL";
    Step s0 = Goto("Go to the ~y~start line~s~ on the Coast Road.", V3(ISLAND_RING[0][0], ISLAND_RING[0][1], ISLAND_RING[0][2]), 14.f);
    s0.onDone = [] { g_trialStart = Now(); Shard("~y~GO!", "Drive around the whole island"); Sound("CHECKPOINT_PERFECT", "HUD_MINI_GAME_SOUNDSET"); };
    m.steps.push_back(s0);
    for (int i = 1; i <= NRING; ++i) {
        const float *p = ISLAND_RING[i % NRING];
        Step s = Goto("Time trial: checkpoint ~y~" + std::to_string(i) + "/" + std::to_string(NRING) + "~s~.", V3(p[0], p[1], p[2]), 14.f);
        s.onDone = [] { Sound("CHECKPOINT_NORMAL", "HUD_MINI_GAME_SOUNDSET"); };
        m.steps.push_back(s);
    }
    m.timeLimitMs = 15 * 60000;
    m.onSuccess = [] {
        int t = Now() - g_trialStart;
        bool record = !g_bestTrial || t < g_bestTrial;
        int pay = 5000 + (record ? 10000 : 0);
        if (record) g_bestTrial = t;
        AddCash(pay); GiveRP(record ? 1500 : 500, "time trial");
        Shard(record ? "~g~NEW RECORD" : "~y~TIME TRIAL COMPLETE", FormatTime(t) + "   " + Money(pay));
        Save();
    };
    StartMission(m);
}
static void StartSkydive() {
    PayWhat what("skydive (job activity)");
    if (!Pay(500)) return;
    int me = Me();
    Mission m; m.name = "VOLCANO SKYDIVE";
    Step s = Goto("Open your parachute and land on the ~y~lookout~s~.", V3(ISLAND_LOOKOUT[0], ISLAND_LOOKOUT[1], ISLAND_LOOKOUT[2]), 30.f);
    s.onStart = [me] {
        if (InAnyVehicle()) { N(N_CLEAR_PED_TASKS_IMMEDIATELY, me); Wait(100); }
        N(N_GIVE_WEAPON_TO_PED, me, Joaat("gadget_parachute"), 1, 0, 1);
        N(N_DO_SCREEN_FADE_OUT, 400); Wait(450);
        N(N_SET_ENTITY_COORDS, me, ISLAND_VOLCANO[0] - 250.f, ISLAND_VOLCANO[1] - 250.f, 1200.f, 0, 0, 0, 0);
        Wait(300); N(N_DO_SCREEN_FADE_IN, 500);
    };
    m.steps.push_back(s);
    m.timeLimitMs = 4 * 60000;
    m.onSuccess = [] { AddCash(3000); GiveRP(800, "skydive"); Shard("~g~PERFECT LANDING", Money(3000)); };
    StartMission(m);
}
static void RentJetSki() {
    PayWhat what("jet ski rental (style)");
    if (!Pay(250)) return;
    uint32_t m = Joaat("seashark");
    if (!LoadModel(m)) return;
    int v = N<int>(N_CREATE_VEHICLE, m, ISLAND_PIEREND[0] + 8.f, ISLAND_PIEREND[1] - 6.f, 0.5f, ISLAND_PIEREND[3], 0, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
    if (!v) return;
    N(N_SET_VEHICLE_COLOURS, v, 135, 135);
    ForgetEntity(v);
    N(N_SET_NEW_WAYPOINT, ISLAND_PIEREND[0], ISLAND_PIEREND[1]);
    Notify("Your ~b~jet ski~s~ is waiting at the end of the pier.");
}
static void StartParty() {
    for (int &p : g_partyPeds) DeleteEntity(p);
    g_partyPeds.clear();
    Vec3 c = V3(ISLAND_PLAZA[0], ISLAND_PLAZA[1], ISLAND_PLAZA[2]);
    SpawnIslandPed(c, true, "WORLD_HUMAN_MUSICIAN", c);
    for (int i = 0; i < 14; ++i) {
        float a = i * 0.45f, r = 4.f + (i % 3) * 2.f;
        SpawnIslandPed(c, true, i % 4 == 0 ? "WORLD_HUMAN_DRINKING" : i % 4 == 1 ? "WORLD_HUMAN_CHEERING" : "WORLD_HUMAN_PARTYING", V3(c.x + std::cos(a) * r, c.y + std::sin(a) * r, c.z));
    }
    g_partyEnds = Now() + 10 * 60000;
    N(N_SET_NEW_WAYPOINT, c.x, c.y);
    Notify("The ~p~beach party~s~ has started at the central plaza of Isla Neon!");
}

// ---------------------------------------------------------------- Interaction menu
struct Item { std::string label, right, desc; std::function<void()> act; std::function<void()> hover = nullptr;   // hover: runs when the row gets selected (shop previews)
              std::function<void(int)> cycle = nullptr; };                                                            // left / right on a "< value >" row
enum MenuId { M_MAIN, M_INV, M_VEH, M_SELL, M_SITES, M_SITE, M_SITECLASS, M_REALESTATE, M_PTYPE, M_MYPROPS, M_PROP, M_BIZ, M_BIZONE,
              M_HEISTS, M_ISLAND, M_SERVICES, M_OPTIONS, M_JOBS, M_STYLE, M_ACTIVITIES, M_PROPHUB };
struct Frame { MenuId id; int param; int sel; };
static std::vector<Frame> g_stack;
static volatile bool g_wantMenu = false, g_wantSell = false;

static long long g_hoverKey = -1;                              // which row's hover ran last
static std::function<bool(const Frame &)> g_menuBackHook;      // true: the back press was handled (shops revert previews, leave)
static const char *g_menuBanner = nullptr;                     // a ShopUI_Title_* texture dictionary drawn as the header
static void Open(MenuId id, int param = 0) { g_stack.push_back({id, param, 0}); g_hoverKey = -1; Sound("SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET"); }
static void CloseMenu() { g_menuOpen = false; g_stack.clear(); g_hoverKey = -1; }

static void BuyCar(const CatCar *c) {
    PayWhat what("vehicle");
    if (c->model == "kosatka") {
        int i = FindProp("Kosatka");
        if (Owns("Kosatka")) { Notify("You already own the Kosatka."); return; }
        BuyProperty(i);
        return;
    }
    std::string where = AirOrSea(c->cls) ? (c->cls == 14 ? "@sea" : "@air") : FreeGarage();
    if (where == "!") { Notify("~r~Your garages are full~s~ (" + std::to_string(GarageCapacity()) + "). Buy a property or sell a vehicle."); return; }
    if (!Pay(c->price)) return;
    GVeh g; g.prop = where; g.model = c->model; g.plate = "GORI";
    g_garage.push_back(g);
    Save();
    GiveRP(100, "new vehicle");
    Notify("~g~" + c->name + "~s~ delivered to: ~b~" + GarageLabel(where) + "~s~.");
}

static std::string BizStatus(int pi) {
    BizState *b = BizOf(PROPS[pi].name);
    if (!b) return "";
    int type = PROPS[pi].type;
    char buf[64];
    if (ProductionOf(type)) { std::snprintf(buf, sizeof buf, "Stock %d%% | Sup %d%%", (int)b->stock, (int)b->supplies); return buf; }
    if (IsCargo(type)) { std::snprintf(buf, sizeof buf, "%d/%d", (int)b->stock, CargoMaxP(pi)); return buf; }
    int perDay, cap; SafeIncome(type, perDay, cap);
    if (perDay) return "Safe " + Money(b->safe);
    return "";
}

#include "Casino.inc"
#include "Internet.inc"
static float DcSpeed(int pi) {   // production speed: equipment +60%, staff +40%
    float k = 1.f;
    for (const CustCat &c : CUSTOM) if (CatFor(pi, c)) {
        if (!std::strcmp(c.name, "Equipment") && CustChoice(pi, c) > 0) k += 0.6f;
        if (!std::strcmp(c.name, "Staff") && CustChoice(pi, c) > 0) k += 0.4f;
    }
    return k;
}
static int g_secSeat = 0;   // 0 standing, 1 sitting down, 2 computer open
// ---------------------------------------------------------------- the Mile High Club plaza: a finished block, not a building site
// The construction site's scenario points (builders drilling, hammering, welding) are blocked over the whole block and its
// ped paths are switched on; while you are near, the plaza has its own life: people on the benches, strolling, on the
// phone, having a coffee. They are ordinary ambient peds (released when you leave, the game cleans them up).
static int g_plazaBlock = 0;
static std::vector<int> g_plazaPeds;
static void PlazaTick() {
    static int next = 0;
    if (Now() < next) return;
    next = Now() + 1500;
    Vec3 mp = Pos(Me()), c = V3(PLAZA_CENTER[0], PLAZA_CENTER[1], PLAZA_CENTER[2]);
    float d = std::hypot(mp.x - c.x, mp.y - c.y);
    if (d < 400.f && !g_plazaBlock) {
        g_plazaBlock = N<int>(N_ADD_SCENARIO_BLOCKING_AREA, PLAZA_BOX[0], PLAZA_BOX[1], PLAZA_BOX[2], PLAZA_BOX[3], PLAZA_BOX[4], PLAZA_BOX[5], 0, 1, 1, 1) + 1;
        N(N_SET_PED_PATHS_IN_AREA, PLAZA_BOX[0], PLAZA_BOX[1], PLAZA_BOX[2], PLAZA_BOX[3], PLAZA_BOX[4], PLAZA_BOX[5], 1, 0);
        // builders already standing there go away
        int peds[2 + 60 * 2] = {0}; peds[0] = 60;   // script array: size, then 8-byte slots
        int n = N<int>(N_GET_PED_NEARBY_PEDS, Me(), peds, -1);
        for (int k = 0; k < n && k < 60; ++k) {
            int pd = peds[2 + k * 2];
            if (!Exists(pd) || N<int>(N_IS_PED_A_PLAYER, pd) || N<int>(N_IS_PED_IN_ANY_VEHICLE, pd, 0)) continue;
            Vec3 q = Pos(pd);
            if (q.x < PLAZA_BOX[0] || q.x > PLAZA_BOX[3] || q.y < PLAZA_BOX[1] || q.y > PLAZA_BOX[4]) continue;
            if (NB(N<int>(N_IS_PED_USING_ANY_SCENARIO, pd))) { N(N_SET_ENTITY_AS_MISSION_ENTITY, pd, 1, 1); N(N_DELETE_PED, &pd); }
        }
        Log("plaza: scenario blocking on, ped paths on");
    }
    for (size_t k = 0; k < g_plazaPeds.size();) {
        int pd = g_plazaPeds[k];
        if (!Exists(pd) || N<int>(N_IS_ENTITY_DEAD, pd, 0)) { g_plazaPeds.erase(g_plazaPeds.begin() + k); continue; }
        ++k;
    }
    if (d > 220.f || g_inside >= 0 || g_mActive) {   // away (or busy): let the game keep or remove them
        for (int pd : g_plazaPeds) { int q = pd; N(N_SET_PED_AS_NO_LONGER_NEEDED, &q); }
        g_plazaPeds.clear();
        return;
    }
    static const char *MODELS[] = {"a_m_y_business_01", "a_f_y_business_01", "a_m_y_business_02", "a_f_y_business_02", "a_m_m_business_01", "a_f_y_business_04",
                                   "a_m_y_hipster_01", "a_f_y_hipster_02", "a_m_y_downtown_01", "a_f_y_tourist_01", "a_m_y_bevhills_01", "a_f_y_bevhills_02"};
    static const char *IDLE[] = {"WORLD_HUMAN_STAND_MOBILE", "WORLD_HUMAN_AA_COFFEE", "WORLD_HUMAN_SMOKING", "WORLD_HUMAN_TOURIST_MAP", "WORLD_HUMAN_HANG_OUT_STREET", "WORLD_HUMAN_STAND_IMPATIENT"};
    const int NB_ = (int)(sizeof PLAZA_BENCHES / sizeof *PLAZA_BENCHES), NS = (int)(sizeof PLAZA_SPOTS / sizeof *PLAZA_SPOTS);
    if ((int)g_plazaPeds.size() >= 14) return;
    // one new person per tick, somewhere you are not looking straight at from close by
    uint32_t m = Joaat(MODELS[Rand((int)(sizeof MODELS / sizeof *MODELS))]);
    if (!LoadModel(m)) return;
    int kind = Rand(10);
    float x, y, z, h;
    if (kind < 4) { int b = Rand(NB_); x = PLAZA_BENCHES[b][0]; y = PLAZA_BENCHES[b][1]; z = PLAZA_BENCHES[b][2]; h = PLAZA_BENCHES[b][3]; }
    else { int sp = Rand(NS); x = PLAZA_SPOTS[sp][0] + (float)(Rand(60) - 30) / 10.f; y = PLAZA_SPOTS[sp][1] + (float)(Rand(60) - 30) / 10.f; z = PLAZA_SPOTS[sp][2]; h = (float)Rand(360); }
    if (std::hypot(mp.x - x, mp.y - y) < 12.f) { N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m); return; }
    for (int pd : g_plazaPeds) { Vec3 q = Pos(pd); if (kind < 4 && std::hypot(q.x - x, q.y - y) < 0.8f) { N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m); return; } }
    int ped = N<int>(N_CREATE_PED, 4, m, x, y, z + 0.5f, h, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
    if (!ped) return;
    N(N_SET_PED_RANDOM_COMPONENT_VARIATION, ped, 0);
    if (kind < 4) N(N_TASK_START_SCENARIO_AT_POSITION, ped, "PROP_HUMAN_SEAT_BENCH", x, y, z + 0.5f, h, 0, 1, 1);
    else if (kind < 7) N(N_TASK_WANDER_IN_AREA, ped, PLAZA_CENTER[0], PLAZA_CENTER[1], PLAZA_CENTER[2], 90.f, 4.f, 6.f);
    else N(N_TASK_START_SCENARIO_IN_PLACE, ped, IDLE[Rand((int)(sizeof IDLE / sizeof *IDLE))], 0, 1);
    g_plazaPeds.push_back(ped);
}
#include "Gardens.inc"   // Hawick Gardens' residents
// buy the data center at its door, like walking into a Maze Bank listing
static void DcDoorTick() {
    static int pi = -2;
    if (pi == -2) pi = FindProp("Mile High Data Center");
    if (pi < 0 || Owns(PROPS[pi].name) || g_inside >= 0 || g_menuOpen || InAnyVehicle()) return;
    Vec3 e = V3(DC_ENTRANCE[0], DC_ENTRANCE[1], DC_ENTRANCE[2]), mp = Pos(Me());
    float d = Dist(mp, e);
    if (d < 30.f) Marker(e, 0, 170, 255);
    if (d > 1.6f) return;
    Help("~b~Mile High Data Center~s~~n~A server farm for hire under the Mile High Club.~n~Press ~INPUT_CONTEXT~ to buy it for ~g~" + Money(PROPS[pi].price) + "~s~.");
    if (JustPressed(51)) BuyProperty(pi);
}
static int g_secSince = 0;
static bool DeskTick(int pi, const float *at, const char *what, void (*open)(int), const Vec3 &mp, bool onFoot);
static void BizOpenSecuroServ(int pi); static void BizOpenDataNet(int pi); static bool BizAppOpen();   // BizAppsUI.inc: Rockstar's own apps
static bool SecuroServTick(int pi, const Vec3 &mp, bool onFoot) { return DeskTick(pi, MHC_COMPUTER, "the SecuroServ computer", BizOpenSecuroServ, mp, onFoot); }
static bool DataTerminalTick(int pi, const Vec3 &mp, bool onFoot) { return DeskTick(pi, DC_TERMINAL, "the DataNet terminal", BizOpenDataNet, mp, onFoot); }
// sit at a desk (GTA Online's office computer), open its site; logging off stands you up
static bool DeskTick(int pi, const float *at, const char *what, void (*open)(int), const Vec3 &mp, bool onFoot) {
    int me = Me();
    Vec3 pc = V3(at[0], at[1], at[2]);
    if (g_secSeat == 1 && Now() > g_secSince + 2600) { g_secSeat = 2; open(pi); }
    if (g_secSeat == 2 && !g_webOpen && !BizAppOpen()) {   // logged off: stand up, then run what was chosen (a mission, the garage)
        g_secSeat = 0; N(N_CLEAR_PED_TASKS, me);
        if (g_secPending) { auto f = g_secPending; g_secPending = nullptr; Wait(600); f(); }
        return true;
    }
    if (g_secSeat) return true;
    if (!onFoot || g_menuOpen || std::hypot(mp.x - pc.x, mp.y - pc.y) > 1.4f || std::fabs(mp.z - pc.z) > 2.5f) return false;   // pc is on the floor
    Help(std::string("Press ~INPUT_CONTEXT~ to use ") + what + ".");
    if (JustPressed(51)) {
        // GTA Online's office desk: sit in the chair facing the monitors
        N(N_TASK_START_SCENARIO_AT_POSITION, me, "PROP_HUMAN_SEAT_COMPUTER", pc.x, pc.y, pc.z + 0.5f, at[3], 0, 1, 1);
        g_secSeat = 1; g_secSince = Now();
    }
    return true;
}
#include "SpecialVehicles.inc"
#include "Yacht.inc"
static void NaptFeedDynasty8(std::vector<int> &slots);
#include "Web.inc"
#include "NewApartmentsRt.inc"   // the new apartments' doors and Dynasty 8 rows
#include "BizApps.inc"
#include "BizAppsUI.inc"
#include "Phone.inc"
static int g_ibtn = 0;
static std::string g_myName;
static void TextLong(const std::string &t, float x, float y, float scale, int r, int g, int b, float wrapL, float wrapR) {   // over 99 characters: several components
    N(N_SET_TEXT_FONT, 0); N(N_SET_TEXT_SCALE, scale, scale); N(N_SET_TEXT_COLOUR, r, g, b, 255); N(N_SET_TEXT_WRAP, wrapL, wrapR);
    N(N_BEGIN_TEXT_COMMAND_DISPLAY_TEXT, t.size() > 90 ? "CELL_EMAIL_BCON" : "STRING");   // CELL_EMAIL_BCON: ~a~ repeated, for text over 99 characters
    for (size_t i = 0; i < t.size(); i += 90) N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, t.substr(i, 90).c_str());
    N(N_END_TEXT_COMMAND_DISPLAY_TEXT, x, y, 0);
}
static int TextLines(const std::string &t, float x, float y, float scale, float wrapL, float wrapR) {
    N(N_SET_TEXT_FONT, 0); N(N_SET_TEXT_SCALE, scale, scale); N(N_SET_TEXT_WRAP, wrapL, wrapR);
    N(N_BEGIN_TEXT_COMMAND_GET_NUMBER_OF_LINES_FOR_STRING, t.size() > 90 ? "CELL_EMAIL_BCON" : "STRING");
    for (size_t i = 0; i < t.size(); i += 90) N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, t.substr(i, 90).c_str());
    return std::max(1, N<int>(N_END_TEXT_COMMAND_GET_NUMBER_OF_LINES_FOR_STRING, x, y));
}
static void MenuButtons() {   // Select / Back, bottom right
    if (!g_ibtn) g_ibtn = N<int>(N_REQUEST_SCALEFORM_MOVIE, "instructional_buttons");
    if (!NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, g_ibtn))) return;
    static int builtAt = 0;
    if (Now() - builtAt > 1000) {
        builtAt = Now();
        N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, g_ibtn, "CLEAR_ALL"); N(N_END_SCALEFORM_MOVIE_METHOD);
        const struct { int control; const char *label; } btns[] = {{202, "Back"}, {201, "Select"}};
        for (int i = 0; i < 2; ++i) {
            N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, g_ibtn, "SET_DATA_SLOT");
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, i);
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_PLAYER_NAME_STRING, N<const char *>(N_GET_CONTROL_INSTRUCTIONAL_BUTTONS_STRING, 2, btns[i].control, 1));
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_PLAYER_NAME_STRING, btns[i].label);
            N(N_END_SCALEFORM_MOVIE_METHOD);
        }
        N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, g_ibtn, "DRAW_INSTRUCTIONAL_BUTTONS"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, 0); N(N_END_SCALEFORM_MOVIE_METHOD);
    }
    N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, g_ibtn, 255, 255, 255, 255, 0);
}
// the activities' lobbies (Coronas.inc, after every activity)
static void OpenRaceCorona(int idx);
static void OpenSvCorona(int loc);
static void OpenDmCorona(int arena);
static void OpenAwCorona();
static void OpenHeistCorona(int h);
#include "Shops.inc"
#include "ModShop.inc"
#include "Races.inc"
#include "AcidLab.inc"
#include "Deathmatch.inc"
#include "Corona.inc"
#include "ContactMissions.inc"
#include "Survival.inc"
#include "ArenaWar.inc"
#include "Coronas.inc"

// ---- Interaction Menu helpers (Online's options)
// Quick GPS destinations: the nearest of each kind
struct QgpsKind { const char *name; std::vector<std::pair<float, float>> at; };
static const QgpsKind QGPS[] = {
    {"None", {}},
    {"Ammu-Nation", {{1692.f, 3758.f}, {252.f, -50.f}, {842.f, -1033.f}, {-330.f, 6083.f}, {-662.f, -935.f}, {-1305.f, -394.f}, {-1118.f, 2697.f},
                     {2567.f, 294.f}, {-3171.f, 1087.f}, {22.f, -1107.f}, {810.f, -2157.f}}},
    {"Barber", {{-814.f, -183.f}, {136.f, -1708.f}, {-1282.f, -1117.f}, {1931.f, 3730.f}, {1212.f, -472.f}, {-32.f, -152.f}, {-278.f, 6228.f}}},
    {"Clothes Store", {{72.f, -1399.f}, {-822.f, -1073.f}, {426.f, -806.f}, {-1193.f, -768.f}, {1696.f, 4829.f}, {4.f, 6512.f}, {1190.f, 2713.f},
                       {-3172.f, 1048.f}, {-1108.f, 2709.f}, {614.f, 2762.f}, {125.f, -223.f}, {-708.f, -152.f}, {-163.f, -302.f}, {-1450.f, -237.f}}},
    {"Tattoo Parlor", {{1322.f, -1651.f}, {-1153.f, -1425.f}, {322.f, 180.f}, {-3170.f, 1075.f}, {1864.f, 3747.f}, {-293.f, 6200.f}}},
    {"Los Santos Customs", {{-365.4f, -131.8f}, {731.8f, -1088.9f}, {-1155.5f, -2005.4f}, {1175.f, 2640.f}, {110.9f, 6626.4f}}},
    {"Benny's Original Motor Works", {{-205.f, -1305.f}}},
    {"Convenience Store", {{25.f, -1347.f}, {-48.f, -1757.f}, {1136.f, -982.f}, {1163.f, -323.f}, {374.f, 326.f}, {2557.f, 382.f}, {-3041.f, 585.f},
                           {-3243.f, 1001.f}, {547.f, 2671.f}, {1961.f, 3741.f}, {1729.f, 6414.f}, {1698.f, 4924.f}, {-707.f, -914.f}, {-1222.f, -907.f},
                           {-1487.f, -379.f}, {-2968.f, 391.f}, {1166.f, 2708.f}, {2678.f, 3281.f}}},
    {"Arena War", {{-376.f, -1862.f}}},
    {"Your Property", {}},
};
static const int NQGPS = (int)(sizeof QGPS / sizeof *QGPS);
static int g_qgps = 0;
static void QuickGps() {
    if (g_qgps == 0) { N(N_SET_WAYPOINT_OFF); Notify("Waypoint removed."); return; }
    Vec3 me = Pos(Me());
    float bx = 0, by = 0, bd = 1e12f;
    if (QGPS[g_qgps].at.empty()) {   // your properties
        for (int i = 0; i < NPROPS; ++i) if (Owns(PROPS[i].name)) { float d = Dist2(me, V3(PROPS[i].x, PROPS[i].y, 0)); if (d < bd) { bd = d; bx = PROPS[i].x; by = PROPS[i].y; } }
        if (bd > 1e11f) { Notify("You don't own a property yet."); return; }
    } else for (auto &p : QGPS[g_qgps].at) { float d = Dist2(me, V3(p.first, p.second, 0)); if (d < bd) { bd = d; bx = p.first; by = p.second; } }
    N(N_SET_NEW_WAYPOINT, bx, by);
    Sound("WAYPOINT_SET", "HUD_FRONTEND_DEFAULT_SOUNDSET");
}
// the other players in our session, for Highlight Player
static int g_hlSel = 0;
static std::vector<std::pair<unsigned, std::string>> PlayersHere() {
    std::vector<std::pair<unsigned, std::string>> out;
    if (!LO().getPlayers || !YouActive()) return out;
    std::vector<char> buf(8192);
    LO().getPlayers(buf.data(), (int)buf.size());
    std::string line;
    for (const char *c = buf.data(); ; ++c) {
        if (*c == '\n' || !*c) {
            auto f = Split(line, '|');
            if (f.size() >= 6 && f[5] == "1") out.push_back({(unsigned)std::strtoul(f[0].c_str(), nullptr, 10), f[1]});
            line.clear();
            if (!*c) break;
        } else line += *c;
    }
    return out;
}
// Passive Mode: the server refuses damage to and from a passive player; we keep the weapons down
static bool g_passive = false;
static int g_passiveAt = 0;
static bool InActivity() {
    return g_mActive || (g_race.phase != RP_NONE && g_race.phase != RP_LOBBY) || (g_aw.phase != AWP_NONE && g_aw.phase != AWP_LOBBY) ||
           (g_sv.phase != SVP_NONE && g_sv.phase != SVP_LOBBY) || (g_dm.phase != DP_NONE && g_dm.phase != DP_LOBBY);
}
#include "JobSession.inc"   // a job is played in its own session: no freemode on the map, Online's transition in and out
static void TogglePassive() {
    if (!g_passive && InActivity()) { Notify("~r~Passive Mode is not available during a job."); return; }
    if (Now() - g_passiveAt < 5000) { Notify("Wait a moment before changing Passive Mode again."); return; }
    g_passive = !g_passive; g_passiveAt = Now();
    if (g_passive) N(N_SET_CURRENT_PED_WEAPON, Me(), Joaat("WEAPON_UNARMED"), 1);
    Notify(g_passive ? "Passive Mode ~g~enabled~s~." : "Passive Mode ~r~disabled~s~.");
}
static void PassiveTick() {
    if (g_passive && (!YouActive() || InActivity())) { g_passive = false; Notify("Passive Mode ~r~disabled~s~."); }
    if (!g_passive) return;
    for (int c : {24, 25, 37, 45, 47, 58, 140, 141, 142, 143, 257, 263, 264, 68, 69, 70, 91, 92}) N(N_DISABLE_CONTROL_ACTION, 0, c, 1);
    N(N_DISABLE_PLAYER_FIRING, Player(), 1);
}
// Simeon's showroom (Premium Deluxe Motorsport) is locked in story mode outside its missions: open, like Online's
static void DoorsTick() {
    static int at = 0;
    if (!YouActive() || Now() - at < 2000) return;
    at = Now();
    if (Dist(Pos(Me()), V3(-45.f, -1100.f, 26.f)) > 150.f) return;
    static const struct { const char *model; float x, y, z; } doors[] = {
        {"v_ilev_csr_door_l", -37.33f, -1108.87f, 26.72f}, {"v_ilev_csr_door_r", -39.13f, -1108.21f, 26.72f},
        {"v_ilev_csr_door_l", -60.55f, -1094.75f, 26.89f}, {"v_ilev_csr_door_r", -59.89f, -1092.95f, 26.88f}};
    for (auto &d : doors) N(N_SET_STATE_OF_CLOSEST_DOOR_OF_TYPE, Joaat(d.model), d.x, d.y, d.z, 0, 0.f, 0);
}
// rank, passive and "in an activity" (a private session) to LocalOnline, once a second
static void PlayerInfoTick() {
    static int at = 0;
    if (!LO().setPlayerInfo || Now() - at < 1000) return;
    at = Now();
    LO().setPlayerInfo(LevelOf(g_rp), (g_passive ? 1 : 0) | (InActivity() ? 2 : 0));
}
// Appearance: walk style, mood, accessories
struct NamedSet { const char *name, *set; };
static const NamedSet WALKS[] = {{"Normal", ""}, {"Gangster", "move_m@gangster@generic"}, {"Confident", "move_m@confident"}, {"Tough", "move_m@tough_guy@"},
                                 {"Posh", "move_m@posh@"}, {"Sassy", "move_f@sassy"}, {"Hurried", "move_m@hurry@a"}, {"Swagger", "move_m@swagger"},
                                 {"Drunk", "move_m@drunk@moderatedrunk"}, {"Injured", "move_m@injured"}};
static const int NWALKS = (int)(sizeof WALKS / sizeof *WALKS);
static const NamedSet MOODS[] = {{"Normal", ""}, {"Happy", "mood_happy_1"}, {"Angry", "mood_angry_1"}, {"Injured", "mood_injured_1"}, {"Stressed", "mood_stressed_1"},
                                 {"Smug", "mood_smug_1"}, {"Sulk", "mood_sulk_1"}, {"Aiming", "mood_aiming_1"}, {"Sleeping", "mood_sleeping_1"}};
static const int NMOODS = (int)(sizeof MOODS / sizeof *MOODS);
static int g_walk = 0, g_mood = 0;
static void ApplyWalk() {
    int me = Me();
    if (!*WALKS[g_walk].set) { N(N_RESET_PED_MOVEMENT_CLIPSET, me, 0.25f); return; }
    N(N_REQUEST_CLIP_SET, WALKS[g_walk].set);
    for (int i = 0; i < 40 && !NB(N<int>(N_HAS_CLIP_SET_LOADED, WALKS[g_walk].set)); ++i) Wait(25);
    N(N_SET_PED_MOVEMENT_CLIPSET, me, WALKS[g_walk].set, 0.25f);
}
static void ApplyMood() {
    if (!*MOODS[g_mood].set) N(N_CLEAR_FACIAL_IDLE_ANIM_OVERRIDE, Me());
    else N(N_SET_FACIAL_IDLE_ANIM_OVERRIDE, Me(), MOODS[g_mood].set, 0);
}
static int g_accSaved[3][2] = {{-1, 0}, {-1, 0}, {-1, 0}};   // hat (prop 0), glasses (prop 1), mask (component 1): what was taken off
static bool AccessoryOn(int k) {
    int me = Me();
    if (k == 2) return N<int>(N_GET_PED_DRAWABLE_VARIATION, me, 1) > 0;
    return N<int>(N_GET_PED_PROP_INDEX, me, k, 0) >= 0;
}
static void ToggleAccessory(int k) {
    int me = Me();
    if (AccessoryOn(k)) {   // off, remembering it
        if (k == 2) { g_accSaved[2][0] = N<int>(N_GET_PED_DRAWABLE_VARIATION, me, 1); g_accSaved[2][1] = N<int>(N_GET_PED_TEXTURE_VARIATION, me, 1); N(N_SET_PED_COMPONENT_VARIATION, me, 1, 0, 0, 0); }
        else { g_accSaved[k][0] = N<int>(N_GET_PED_PROP_INDEX, me, k, 0); g_accSaved[k][1] = N<int>(N_GET_PED_PROP_TEXTURE_INDEX, me, k); N(N_CLEAR_PED_PROP, me, k, 0); }
        return;
    }
    if (g_accSaved[k][0] < 0) { Notify("Nothing to put back on: change your outfit in a clothes store."); return; }
    if (k == 2) N(N_SET_PED_COMPONENT_VARIATION, me, 1, g_accSaved[2][0], g_accSaved[2][1], 0);
    else N(N_SET_PED_PROP_INDEX, me, k, g_accSaved[k][0], g_accSaved[k][1], 1, 0);
}

static void HbOpen();
static void HbFrame();
static bool HbUsing();
#include "AptActs.inc"   // GTA Online's apartment activities in our own interiors
#include "HeistBoard.inc"
#include "Buzzer.inc"   // GTA Online's apartment buzzer: ring another player's home, the owner lets you in   // GTA Online's heist planning board (HEIST_MP) on the apartment wall

static void BuildMenu(const Frame &f, std::string &title, std::string &sub, std::vector<Item> &items) {
    items.clear();
    switch (f.id) {
    case M_MAIN: {
        title = "Interaction Menu"; sub = "INTERACTION MENU";
        if (g_mActive) items.push_back({"Quit Job", "", "Abandon the current mission.", [] { CloseMenu(); FailMission("You quit the job."); }});
        {   // Quick GPS: left / right through the destinations, select to set the waypoint (Online's PIM_TQGPS)
            items.push_back({"Quick GPS", "< " + std::string(QGPS[g_qgps].name) + " >", "Select to place your waypoint at a set location.",
                             [] { QuickGps(); }, nullptr, [](int d) { g_qgps = (g_qgps + d + NQGPS) % NQGPS; }});
        }
        if (g_inside >= 0 && TYPES[PROPS[g_inside].type].business) {
            int pi = g_inside;
            items.push_back({PROPS[pi].name, ">", "Manage this business.", [pi] { Open(M_BIZONE, pi); }});
        }
        items.push_back({"Health and Ammo", ">", "Snacks, body armor and ammo.", [] { Open(M_INV); }});
        items.push_back({"Appearance", ">", "Walk style, mood and accessories.", [] { Open(M_STYLE); }});
        items.push_back({"Manage Vehicles", ">", "Request, return or sell your personal vehicles.", [] { Open(M_VEH); }});
        items.push_back({"Services", ">", "Lester, Ammu-Nation, your special vehicles and the Acid Lab.", [] { Open(M_SERVICES); }});
        {
            std::string busy = g_race.phase == RP_LOBBY || g_aw.phase == AWP_LOBBY || g_sv.phase == SVP_LOBBY || g_dm.phase == DP_LOBBY ? "Lobby" :
                               g_mActive || g_aw.phase != AWP_NONE || g_sv.phase != SVP_NONE ? "Playing" : "";
            items.push_back({"Activities", busy.empty() ? ">" : busy, "Contact missions, heists, races, Arena War, Survival, Deathmatch and the other players' jobs.", [] { Open(M_ACTIVITIES); }});
        }
        items.push_back({"Properties", ">", "Your properties and businesses, Real Estate, the vehicle websites and the Internet.", [] { Open(M_PROPHUB); }});
        {   // Highlight Player: the chosen player's blip flashes (Online's PIM_TITLE12)
            auto ps = PlayersHere();
            if (!ps.empty()) {
                g_hlSel = (g_hlSel % (int)ps.size() + (int)ps.size()) % (int)ps.size();
                unsigned id = ps[g_hlSel].first;
                items.push_back({"Highlight Player", "< " + ps[g_hlSel].second + " >", "Highlight a player on the radar.",
                                 [id] { if (LO().highlightPlayer) LO().highlightPlayer(id, 10000); Notify("Player highlighted on the radar."); }, nullptr,
                                 [](int d) { g_hlSel += d; }});
            }
        }
        items.push_back({"Preferences", ">", "Online map and other settings.", [] { Open(M_OPTIONS); }});
        items.push_back({g_passive ? "Disable Passive Mode" : "Enable Passive Mode", "",
                         g_passive ? "Turn passive mode off: you can attack and be attacked by other players again." :
                                     "In passive mode other players can't hurt you, and you can't use weapons against them.",
                         [] { TogglePassive(); }});
        items.push_back({"Kill Yourself", "", "Respawn at your home or the nearest hospital.", [] { CloseMenu(); N(N_SET_ENTITY_HEALTH, Me(), 0, 0, 0); }});
        break;
    }
    case M_STYLE: {
        title = "Appearance"; sub = "APPEARANCE";
        items.push_back({"Walk Style", "< " + std::string(WALKS[g_walk].name) + " >", "How your character walks.", [] { ApplyWalk(); },
                         nullptr, [](int d) { g_walk = (g_walk + d + NWALKS) % NWALKS; ApplyWalk(); }});
        items.push_back({"Mood", "< " + std::string(MOODS[g_mood].name) + " >", "Your character's expression.", [] { ApplyMood(); },
                         nullptr, [](int d) { g_mood = (g_mood + d + NMOODS) % NMOODS; ApplyMood(); }});
        items.push_back({"Hat", AccessoryOn(0) ? "On" : "Off", "Take your hat or helmet off, or put it back on.", [] { ToggleAccessory(0); }});
        items.push_back({"Glasses", AccessoryOn(1) ? "On" : "Off", "Take your glasses off, or put them back on.", [] { ToggleAccessory(1); }});
        items.push_back({"Mask", AccessoryOn(2) ? "On" : "Off", "Take your mask off, or put it back on.", [] { ToggleAccessory(2); }});
        break;
    }
    case M_ACTIVITIES: {
        title = "Activities"; sub = "ACTIVITIES";
        items.push_back({"Contact Missions", ">", "Gerald, Simeon, Lamar, Martin, Ron and Trevor's jobs, by rank.", [] { Open((MenuId)CMM_LIST); }});
        items.push_back({"Heists", ">", "Preps and finales with Lester.", [] { Open(M_HEISTS); }});
        items.push_back({"Races", g_race.phase == RP_LOBBY ? "Lobby >" : ">", "Races: solo or with the other players, paid by position.", [] { Open((MenuId)RM_LIST); }});
        items.push_back({"Arena War", g_aw.phase == AWP_LOBBY ? "Lobby >" : g_aw.phase != AWP_NONE ? "Playing >" : ">", "Events in the Maze Bank Arena, alone against bots or online.", [] { Open((MenuId)AW_MENU); }});
        items.push_back({"Survival", g_sv.phase == SVP_LOBBY ? "Lobby >" : g_sv.phase != SVP_NONE ? "Playing >" : ">", "Ten waves of enemies, alone or with the other players.", [] { Open((MenuId)SV_LIST); }});
        if (CoopAvailable()) items.push_back({"Deathmatch", g_dm.phase == DP_LOBBY ? "Lobby >" : ">", "Fight the other players in an arena, paid by position.", [] { Open((MenuId)DM_LIST); }});
        if (CoopAvailable()) items.push_back({"Jobs", g_coopJobs.empty() ? ">" : std::to_string(g_coopJobs.size()) + " >", "Join another player's job and get paid when it passes.", [] { Open(M_JOBS); }});
        break;
    }
    case M_PROPHUB: {
        title = "Properties"; sub = "PROPERTIES";
        items.push_back({"My Properties", ">", "Fast travel, enter, spawn location.", [] { Open(M_MYPROPS); }});
        items.push_back({"Businesses", ">", "Supplies, product, cargo and safes.", [] { Open(M_BIZ); }});
        items.push_back({"Real Estate", ">", "Dynasty 8 and Maze Bank Foreclosures: homes, offices and businesses.", [] { Open(M_REALESTATE); }});
        items.push_back({"Vehicle Websites", ">", "Every Online vehicle: Legendary, SSASA, Warstock, Elitas, Docktease.", [] { Open(M_SITES); }});
        items.push_back({"Internet", "", "The browser, like your phone's.", [] { CloseMenu(); if (!g_realWebDefault || !RealWebOpen("WWW_EYEFIND_INFO")) OpenInternet(); }});
        items.push_back({"Isla Neon", ">", "Your island from goricito.cc, in the ocean west of Los Santos.", [] { Open(M_ISLAND); }});
        break;
    }
    case M_INV: {
        title = "Health and Ammo"; sub = "HEALTH AND AMMO";
        static const char *names[3] = {"P's & Q's", "EgoChaser", "Meteorite"};
        static const int heal[3] = {15, 30, 50}, price[3] = {1, 5, 10};
        for (int i = 0; i < 3; ++i) {
            items.push_back({std::string("Eat ") + names[i], std::to_string(g_snacks[i]), "+" + std::to_string(heal[i]) + "% health.", [i] {
                if (g_snacks[i] <= 0) { Notify(std::string("You have no ") + names[i] + " left."); return; }
                int me = Me(), maxh = N<int>(N_GET_ENTITY_MAX_HEALTH, me), h = N<int>(N_GET_ENTITY_HEALTH, me);
                int nh = h + (maxh - 100) * heal[i] / 100; if (nh > maxh) nh = maxh;
                N(N_SET_ENTITY_HEALTH, me, nh, 0, 0);
                g_snacks[i]--; Save();
            }});
            items.push_back({std::string("Buy ") + names[i], Money(price[i]), "Adds one to your inventory.", [i] { if (Pay(price[i])) { g_snacks[i]++; Save(); } }});
        }
        items.push_back({"Super Heavy Armor", Money(2500), "Full body armor.", [] { if (Pay(2500)) { N(N_SET_PED_ARMOUR, Me(), 100); Notify("Armor equipped."); } }});
        break;
    }
    case M_VEH:
        title = "Vehicles"; sub = "PERSONAL VEHICLES (" + std::to_string(g_garage.size()) + ")";
        if (Exists(g_personal)) items.push_back({"Return Personal Vehicle", "", "Stores it (with all its mods) back in its garage.", [] { StorePersonal(true); }});
        if (g_garage.empty()) items.push_back({"(no vehicles)", "", "Buy vehicles on the vehicle websites.", [] {}});
        for (int i = 0; i < (int)g_garage.size(); ++i) {
            bool out = i == g_personalIdx && Exists(g_personal);
            std::string where = GarageLabel(g_garage[i].prop);
            items.push_back({"Request " + CarName(g_garage[i].model), out ? "OUT" : where.substr(0, 14), "Your mechanic delivers it (stored at: " + where + ").", [i] { CloseMenu(); RequestVehicle(i); }});
        }
        if (!g_garage.empty()) items.push_back({"Sell Vehicles", ">", "Sell back for half the price.", [] { Open(M_SELL); }});
        break;
    case M_SELL:
        title = "Sell Vehicles"; sub = "SELL (50%)";
        for (int i = 0; i < (int)g_garage.size(); ++i) {
            const CatCar *d = FindCar(g_garage[i].model);
            int value = (d ? d->price : 20000) / 2;
            items.push_back({"Sell " + CarName(g_garage[i].model), Money(value), "Gone for good.", [i, value] {
                if (i == g_personalIdx) StorePersonal(false);
                ClearGarageCars(0);
                if (g_garProp >= 0) g_garProp = -1;
                g_garage.erase(g_garage.begin() + i);
                if (g_personalIdx == i) g_personalIdx = -1; else if (g_personalIdx > i) g_personalIdx--;
                AddCash(value); Save();
                Notify("Sold for ~g~" + Money(value));
                g_stack.back().sel = 0;
            }});
        }
        break;
    case M_SITES:
        title = "Internet"; sub = "VEHICLE WEBSITES";
        for (int s = 0; s < SITE_COUNT; ++s) {
            int n = 0; for (auto &c : g_cat) if (SiteOf(c) == s) n++;
            items.push_back({SITE_NAMES[s], std::to_string(n), "", [s] { Open(M_SITE, s); }});
        }
        break;
    case M_SITE: {
        title = SITE_NAMES[f.param]; sub = "CATEGORIES";
        int counts[23] = {0};
        for (auto &c : g_cat) if (SiteOf(c) == f.param && c.cls >= 0 && c.cls < 23) counts[c.cls]++;
        for (int k = 0; k < 23; ++k) if (counts[k]) { int p = f.param * 64 + k; items.push_back({CLASS_NAMES[k], std::to_string(counts[k]), "", [p] { Open(M_SITECLASS, p); }}); }
        break;
    }
    case M_SITECLASS: {
        int site = f.param / 64, cls = f.param % 64;
        title = SITE_NAMES[site]; sub = CLASS_NAMES[cls];
        for (auto &c : g_cat) {
            if (SiteOf(c) != site || c.cls != cls) continue;
            const CatCar *pc = &c;
            items.push_back({c.name, Money(c.price), c.weapon ? "Weaponized. Delivered to your garage." : "Delivered to your garage.", [pc] { BuyCar(pc); }});
        }
        break;
    }
    case M_REALESTATE: {
        title = "Real Estate"; sub = "DYNASTY 8 / MAZE BANK FORECLOSURES";
        static const int order[] = {T_APT, T_HOUSE, T_MANSION, T_PENTHOUSE, T_OFFICE, T_CLUB, T_METH, T_WEED, T_COKE, T_CASH, T_DOCS, T_BUNKER, T_HANGAR,
                                    T_FACILITY, T_NIGHTCLUB, T_ARCADE, T_AUTOSHOP, T_AGENCY, T_VEHWARE, T_KOSATKA,
                                    T_SALVAGE, T_BAIL, T_GARMENT, T_CARWASH, T_WEEDSHOP, T_HELITOURS, T_ARENAWS};
        for (int t : order) {
            int n = 0, owned = 0;
            for (int i = 0; i < NPROPS; ++i) if (PROPS[i].type == t) { n++; if (Owns(PROPS[i].name)) owned++; }
            items.push_back({TYPES[t].label, std::to_string(owned) + "/" + std::to_string(n), TYPES[t].residence ? "Dynasty 8 Real Estate." : "Maze Bank Foreclosures.", [t] { Open(M_PTYPE, t); }});
        }
        break;
    }
    case M_PTYPE:
        title = TYPES[f.param].label; sub = TYPES[f.param].residence ? "DYNASTY 8" : "MAZE BANK FORECLOSURES";
        for (int i = 0; i < NPROPS; ++i) {
            if (PROPS[i].type != f.param) continue;
            bool owned = Owns(PROPS[i].name);
            if (owned) items.push_back({PROPS[i].name, "OWNED", "Fast travel there.", [i] { CloseMenu(); GoToProperty(i); }});
            else items.push_back({PROPS[i].name, Money(PROPS[i].price), "Buy it. Select again after buying to travel there.", [i] { BuyProperty(i); }});
        }
        break;
    case M_MYPROPS:
        title = "My Properties"; sub = "OWNED";
        if (g_props.empty()) items.push_back({"(you own nothing yet)", "", "Buy on Real Estate.", [] {}});
        for (auto &o : g_props) {
            int i = FindProp(o);
            if (i < 0) continue;
            items.push_back({PROPS[i].name, g_spawnProp == o ? "SPAWN" : "", TYPES[PROPS[i].type].label, [i] { Open(M_PROP, i); }});
        }
        break;
    case M_PROP: {
        int i = f.param;
        title = PROPS[i].name; sub = TYPES[PROPS[i].type].label;
        items.push_back({"Fast Travel", "", "Go to the entrance.", [i] { CloseMenu(); GoToProperty(i); }});
        if (PropInterior(i)) items.push_back({"Go Inside", "", "Straight into the interior.", [i] { CloseMenu(); EnterProperty(i); }});
        if (GarageSlots(i) > 0) items.push_back({"Go to the Garage", std::to_string(CountIn(PROPS[i].name)) + "/" + std::to_string(GarageSlots(i)), "Your stored vehicles. Pick one and drive out.", [i] { CloseMenu(); EnterGarage(i, 0); }});
        if (PROPS[i].type != T_KOSATKA && PROPS[i].type != T_VILLA)
            items.push_back({"Set Waypoint", "", "Mark the entrance on the map.", [i] { Vec3 e = PropExt(i); N(N_SET_NEW_WAYPOINT, e.x, e.y); Notify("Waypoint set."); }});
        if (TYPES[PROPS[i].type].residence)
            items.push_back({"Set as Spawn Location", g_spawnProp == PROPS[i].name ? "SET" : "", "Respawn here after being wasted.", [i] { g_spawnProp = PROPS[i].name; Save(); RefreshPropertyBlips(); Notify("Spawn location set."); }});
        if (TYPES[PROPS[i].type].business) items.push_back({"Manage Business", ">", "", [i] { Open(M_BIZONE, i); }});
        if (PROPS[i].type != T_VILLA) {
            int value = PROPS[i].price / 2;
            items.push_back({"Sell Property", Money(value), "Trade it back to the bank.", [i, value] {
                std::string n = PROPS[i].name;
                g_props.erase(std::remove(g_props.begin(), g_props.end(), n), g_props.end());
                g_biz.erase(std::remove_if(g_biz.begin(), g_biz.end(), [&](const BizState &b) { return b.prop == n; }), g_biz.end());
                for (auto &g : g_garage) if (g.prop == n) g.prop = "";
                if (g_spawnProp == n) g_spawnProp.clear();
                AddCash(value); Save(); RefreshPropertyBlips();
                Notify("Sold for ~g~" + Money(value));
                g_stack.pop_back(); if (!g_stack.empty()) g_stack.back().sel = 0;
            }});
        }
        break;
    }
    case M_BIZ:
        title = "Businesses"; sub = "OWNED BUSINESSES";
        for (auto &o : g_props) {
            int i = FindProp(o);
            if (i < 0 || !TYPES[PROPS[i].type].business) continue;
            items.push_back({PROPS[i].name, BizStatus(i), TYPES[PROPS[i].type].label, [i] { Open(M_BIZONE, i); }});
        }
        if (items.empty()) items.push_back({"(no businesses)", "", "Buy one on Real Estate (Maze Bank Foreclosures).", [] {}});
        break;
    case M_BIZONE: {
        int pi = f.param;
        BizState *b = BizOf(PROPS[pi].name);
        if (!b) { BizState nb; nb.prop = PROPS[pi].name; g_biz.push_back(nb); b = &g_biz.back(); }
        int type = PROPS[pi].type;
        title = PROPS[pi].name; sub = TYPES[type].label;
        bool here = g_inside == pi;
        if (const BizInfo *p = ProductionOf(type)) {
            int value = (int)(p->fullValue * b->stock / 100.f);
            items.push_back({std::string(p->product) + " stock", std::to_string((int)b->stock) + "%", "Worth " + Money(value) + " right now.", [] {}});
            items.push_back({"Supplies", std::to_string((int)b->supplies) + "%", "100% supplies makes half a full stock.", [] {}});
            items.push_back({"Buy Supplies", Money(SUPPLY_COST), "Fills the supplies right away.", [pi] {
                BizState *s = BizOf(PROPS[pi].name);
                if (s && s->supplies >= 99.f) { Notify("Supplies are already full."); return; }
                PayWhat what("business supplies (property)");
                if (s && Pay(SUPPLY_COST)) { s->supplies = 100.f; Save(); Notify("Supplies delivered."); }
            }});
            items.push_back({"Steal Supplies", "", "Free, but a rival gang guards them (+40%).", [pi] { CloseMenu(); StealSupplies(pi); }});
            items.push_back({"Sell Product", Money(value), "Deliver it to the buyers.", [pi] { CloseMenu(); SellProduct(pi); }});
        } else if (IsCargo(type)) {
            int n = (int)b->stock;
            items.push_back({type == T_VEHWARE ? "Vehicles in stock" : "Crates in stock", std::to_string(n) + "/" + std::to_string(CargoMaxP(pi)), "", [] {}});
            items.push_back({type == T_VEHWARE ? "Source Vehicle" : "Source Cargo", "", "Steal more stock.", [pi] { CloseMenu(); SourceCargo(pi); }});
            int value = CargoTotal(pi, n);
            items.push_back({type == T_VEHWARE ? "Export Vehicle" : "Sell Cargo", Money(value), "Deliver to the buyer.", [pi] { CloseMenu(); SellCargo(pi); }});
        }
        int perDay, cap; SafeIncome(type, perDay, cap);
        if (perDay) {
            items.push_back({"Safe", Money(b->safe), "Earns " + Money(perDay) + " every in-game day (48 min). Max " + Money(cap) + ".", [] {}});
            if (!PropInterior(pi)) here = Dist(Pos(Me()), PropExt(pi)) < 15.f;   // run from the front door (no interior)
            items.push_back({"Collect Safe", "", here ? "Take the money." : PropInterior(pi) ? "You must be inside the property." : "Go to its front door.", [pi, here] {
                BizState *s = BizOf(PROPS[pi].name);
                if (!here) { Notify(PropInterior(pi) ? "~r~Go inside the property to open the safe." : "~r~Go to the business's front door to collect."); return; }
                if (!s || s->safe <= 0) { Notify("The safe is empty."); return; }
                AddCash(s->safe); Notify("Collected ~g~" + Money(s->safe)); s->safe = 0; Save();
            }});
        }
        if (type == T_AUTOSHOP) items.push_back({"Client Job", "", "Deliver a customer's car.", [pi] { CloseMenu(); ClientJob(pi); }});
        if (type == T_AGENCY) items.push_back({"Security Contract", "", "Rescue a VIP client.", [pi] { CloseMenu(); ClientJob(pi); }});
        if (type == T_DATACENTER) items.push_back({"Hacking Contract", "", "Break into a corporate server for a client.", [pi] { CloseMenu(); HackContract(pi); }});
        if (type == T_SALVAGE) items.push_back({"Salvage Yard Robbery", "", "Steal a high-end car from a gang garage and salvage it.", [pi] { CloseMenu(); SalvageRobbery(pi); }});
        if (type == T_BAIL) items.push_back({"Bounty Target", "", "Bring a bail jumper in alive.", [pi] { CloseMenu(); BountyTarget(pi); }});
        if (type == T_GARMENT) items.push_back({"Sabotage Job", "", "The hacker's job: destroy a contractor's servers.", [pi] { CloseMenu(); SabotageJob(pi); }});
        break;
    }
    case M_HEISTS:
        title = "Heists"; sub = "LESTER";
        for (int h = 0; h < NHEISTS; ++h) {
            std::string why;
            bool ok = HeistUnlocked(h, why);
            int st = g_heistStage[h];
            std::string right = !ok ? "LOCKED" : st == HEISTS[h].stages - 1 ? "FINALE" : std::string("PREP ") + std::to_string(st + 1) + "/" + std::to_string(HEISTS[h].stages - 1);
            std::string desc = std::string(HEISTS[h].desc) + " Next: " + HEISTS[h].stageNames[st] + ". Payout " + Money(HEISTS[h].payout) +
                               (st == 0 ? ", setup " + Money(HEISTS[h].fee) : "") + "." + (ok ? "" : " " + why);
            items.push_back({HEISTS[h].name, right, desc, [h] { CloseMenu(); OpenHeistCorona(h); }});
        }
        break;
    case M_ISLAND: {
        title = "Isla Neon"; sub = "GORICITO.CC";
        int vi = FindProp("Isla Neon Villa");
        items.push_back({"Travel to Isla Neon", "", "Lands you on the main road of the island.", [] { CloseMenu(); IslandArrive(ISLAND_SPAWN[0], ISLAND_SPAWN[1], ISLAND_SPAWN[2], ISLAND_SPAWN[3]); }});
        items.push_back({"Go to My Villa", "", "Your Franklin-style house under the \"My house\" pin.", [vi] { CloseMenu(); GoToProperty(vi); }});
        items.push_back({"Volcano Lookout", "", "", [] { CloseMenu(); IslandArrive(ISLAND_LOOKOUT[0], ISLAND_LOOKOUT[1], ISLAND_LOOKOUT[2], 0.f); }});
        items.push_back({"Time Trial: Coast Road", g_bestTrial ? FormatTime(g_bestTrial) : "", "Race around the island against the clock. Needs a vehicle.", [] { CloseMenu(); StartTimeTrial(); }});
        items.push_back({"Skydive over the Volcano", Money(500), "Jump from 1,200 m and land on the lookout.", [] { CloseMenu(); StartSkydive(); }});
        items.push_back({"Rent a Jet Ski", Money(250), "Waiting at the end of the pier.", [] { CloseMenu(); RentJetSki(); }});
        items.push_back({"Beach Party", "", "A DJ and a crowd at the central plaza for 10 minutes.", [] { CloseMenu(); StartParty(); }});
        items.push_back({"Set Waypoint", "", "", [] { N(N_SET_NEW_WAYPOINT, ISLAND_CX, ISLAND_CY); Notify("Waypoint set."); }});
        items.push_back({"Island Life", g_optIsland ? "ON" : "OFF", "Traffic, people and parked vehicles on the island.", [] { g_optIsland = !g_optIsland; Save(); }});
        break;
    }
    case M_SERVICES:
        title = "Services"; sub = "CONTACTS";
        SpvServiceItems(items);
        AcidServiceItems(items);
        items.push_back({"Lester: Remove Wanted Level", "$500/star", "Lester makes the cops lose you.", [] {
            int stars = N<int>(N_GET_PLAYER_WANTED_LEVEL, Player());
            if (stars == 0) { Notify("Lester: \"You're not wanted. Stop wasting my time.\""); return; }
            if (g_mActive) { Notify("Lester: \"Not during a job. Lose them yourself.\""); return; }
            PayWhat what("Lester service (contact)");
            if (Pay(500 * stars)) { N(N_CLEAR_PLAYER_WANTED_LEVEL, Player()); Notify("Lester: \"Done. The cops lost you.\""); }
        }});
        items.push_back({"Ammu-Nation: Ammo Drop", Money(1000), "Refills ammo for all your weapons.", [] {
            PayWhat what("ammo drop (contact service)");
            if (!Pay(1000)) return;
            static const char *weapons[] = {"weapon_pistol", "weapon_combatpistol", "weapon_appistol", "weapon_pistol50", "weapon_heavypistol", "weapon_revolver",
                "weapon_microsmg", "weapon_smg", "weapon_assaultsmg", "weapon_combatpdw", "weapon_mg", "weapon_combatmg", "weapon_assaultrifle", "weapon_carbinerifle",
                "weapon_advancedrifle", "weapon_specialcarbine", "weapon_bullpuprifle", "weapon_pumpshotgun", "weapon_sawnoffshotgun", "weapon_assaultshotgun",
                "weapon_heavyshotgun", "weapon_sniperrifle", "weapon_heavysniper", "weapon_marksmanrifle", "weapon_grenadelauncher", "weapon_rpg", "weapon_minigun",
                "weapon_grenade", "weapon_stickybomb", "weapon_molotov", "weapon_smokegrenade"};
            int me = Me();
            for (const char *w : weapons) { uint32_t h = Joaat(w); if (NB(N<int>(N_HAS_PED_GOT_WEAPON, me, h, 0))) N(N_ADD_AMMO_TO_PED, me, h, 9999); }
            Notify("Ammo refilled.");
        }});
        items.push_back({"GPS: Nearest LS Customs", "", "Sell stolen cars there.", [] {
            static const float shops[][2] = {{-365.4f, -131.8f}, {731.8f, -1088.9f}, {-1155.5f, -2005.4f}, {1175.0f, 2640.0f}, {110.9f, 6626.4f}};
            Vec3 me = Pos(Me()); int best = 0; float bd = 1e9f;
            for (int i = 0; i < 5; ++i) { float d = Dist2(me, V3(shops[i][0], shops[i][1], 0.f)); if (d < bd) { bd = d; best = i; } }
            N(N_SET_NEW_WAYPOINT, shops[best][0], shops[best][1]);
            Notify("Waypoint set.");
        }});
        if (Owns("Kosatka")) items.push_back({"Kosatka: Travel to Cayo Perico", "", "Drops you at the Cayo Perico airstrip.", [] { CloseMenu(); TravelCayo(4494.f, -4525.f, 4.4f, 290.f, nullptr); }});
        break;
    case M_JOBS: {
        title = "Jobs"; sub = "CO-OP JOBS";
        bool member = g_coop.id && !g_coop.hosting && g_coop.host != "you" && g_coop.status <= COOP_RUNNING;
        if (member) items.push_back({"Leave Job", "", "Stop following " + g_coop.host + "'s job (no payout).", [] { CoopJoin(0); CloseMenu(); }});
        for (const CoopJob &j : g_coopJobs) {
            std::string state = j.status == COOP_LOBBY ? "starting" : "in progress";
            std::string players = std::to_string(j.members + 1) + (j.members ? " players" : " player");
            if (j.host == "you") { items.push_back({j.title, "Hosting", "Your job: " + players + ", " + state + ". Members follow your objective.", nullptr}); continue; }
            bool in = member && g_coop.id == j.id;
            unsigned id = j.id;
            items.push_back({j.title, in ? "Joined" : j.host, "Hosted by " + j.host + ": " + players + ", " + state + "." + (in ? "" : " Join and help to get a cut."),
                             [id, in] { if (!in) CoopJoin(id); CloseMenu(); }});
        }
        if (g_coopJobs.empty()) items.push_back({"No open jobs", "", "Start a mission (business, heist, client job...) and other players can join it here.", nullptr});
        break;
    }
    case M_OPTIONS:
        title = "Options"; sub = "SETTINGS";
        items.push_back({"Online Map", g_optMpMap ? "ON" : "OFF", "Loads the GTA Online map changes and interiors. Restart the game after turning it off.", [] {
            g_optMpMap = !g_optMpMap; Save();
            if (g_optMpMap) { N(N_ON_ENTER_MP); N(N_SET_INSTANCE_PRIORITY_MODE, 1); } else N(N_ON_ENTER_SP);
        }});
        items.push_back({"Isla Neon Life", g_optIsland ? "ON" : "OFF", "Traffic, people and parked vehicles on the island.", [] { g_optIsland = !g_optIsland; Save(); }});
        break;
    default:
        if (!CasinoMenu((int)f.id, title, sub, items) && !SpvMenu(f, title, sub, items) && !ShopsMenu(f, title, sub, items) && !ModShopMenu(f, title, sub, items) && !RacesMenu(f, title, sub, items) && !AcidMenu(f, title, sub, items) && !DmMenu(f, title, sub, items) && !ContactMenu(f, title, sub, items) && !SvMenu(f, title, sub, items) && !ArenaWarMenu(f, title, sub, items) && !YachtMenu(f, title, sub, items) && !AptMenu(f, title, sub, items)) GarageMenu((int)f.id, title, sub, items);
        break;
    }
}

static int g_navHeld = 0, g_navNext = 0;
// ---- Online's menu look (the shop / interaction menu library in am_pi_menu.ysc): a 0.225 wide column at the
// top-left of the safe zone; header: CommonMenu/Interaction_bgd with the player's name (or a shop's banner);
// subtitle bar with the section in blue and "n / N"; gradient_bgd rows, gradient_nav on the selected one;
// the up/down arrows when the list scrolls; the description box; Select / Back instructional buttons.
static void MenuDraw(const Frame &f, const std::string &title, const std::string &sub, const std::vector<Item> &items) {
    N(N_REQUEST_STREAMED_TEXTURE_DICT, "CommonMenu", 0);
    bool cm = NB(N<int>(N_HAS_STREAMED_TEXTURE_DICT_LOADED, "CommonMenu"));
    float sz = N<float>(N_GET_SAFE_ZONE_SIZE); if (sz < 0.85f || sz > 1.f) sz = 1.f;
    const float m = (1.f - sz) * 0.5f, w = 0.225f, left = m + 0.016f, x = left + w / 2, rowH = 0.034722f, ts = 0.35f;
    float y = m + 0.018f;   // top edge of the next block
    // header
    const float hh = 0.0898f;
    if (g_menuBanner && NB(N<int>(N_HAS_STREAMED_TEXTURE_DICT_LOADED, g_menuBanner))) {   // a shop's own banner
        N(N_DRAW_SPRITE, g_menuBanner, g_menuBanner, x, y + hh / 2, w, hh, 0.f, 255, 255, 255, 255, 0, 0);
    } else {
        if (g_menuBanner) N(N_REQUEST_STREAMED_TEXTURE_DICT, g_menuBanner, 0);
        if (cm) N(N_DRAW_SPRITE, "CommonMenu", "interaction_bgd", x, y + hh / 2, w, hh, 0.f, 255, 255, 255, 255, 0, 0);
        else Rect(x, y + hh / 2, w, hh, 25, 85, 160, 255);
        bool interaction = !g_stack.empty() && g_stack.front().id == M_MAIN;   // the Interaction Menu: the player's name, like Online's
        if (interaction && g_myName.empty() && LO().getPlayerName) { char b[64] = {0}; LO().getPlayerName(b, sizeof b); g_myName = b; }
        std::string head = interaction && !g_myName.empty() ? g_myName : title;
        float hs = head.size() > 20 ? 0.6f : 0.75f;
        Text(head, left + 0.012f, y + hh / 2 - hs * 0.037f, hs, 0, 255, 255, 255, 255, 1);
    }
    y += hh;
    // subtitle: section in blue, "n / N"
    Rect(x, y + rowH / 2, w, rowH, 0, 0, 0, 255);
    std::string s = sub;
    for (char &c : s) c = (char)toupper((unsigned char)c);
    Text(s, left + 0.005f, y + 0.0045f, ts, 0, 93, 182, 229, 255, 1);
    Text(std::to_string(f.sel + 1) + " / " + std::to_string(items.size()), x, y + 0.0045f, ts, 0, 93, 182, 229, 255, 2, left + w - 0.005f);
    y += rowH;
    // rows
    const int visible = g_menuBanner ? 10 : 7;
    int first = f.sel - visible + 1; if (first < 0) first = 0;
    int shown = std::min(visible, (int)items.size() - first);
    if (cm) N(N_DRAW_SPRITE, "CommonMenu", "gradient_bgd", x, y + rowH * shown / 2, w, rowH * shown, 0.f, 255, 255, 255, 255, 0, 0);
    else Rect(x, y + rowH * shown / 2, w, rowH * shown, 0, 0, 0, 160);
    for (int i = first; i < first + shown; ++i) {
        const Item &it = items[i];
        bool sel = i == f.sel, off = !it.act && !it.cycle;
        if (sel) {
            if (cm) N(N_DRAW_SPRITE, "CommonMenu", "gradient_nav", x, y + rowH / 2, w, rowH, 0.f, 255, 255, 255, 255, 0, 0);
            else Rect(x, y + rowH / 2, w, rowH, 245, 245, 245, 255);
        }
        int c = sel ? 0 : off ? 155 : 245;
        std::string label = it.label.size() > 38 ? it.label.substr(0, 37) + "." : it.label;
        Text(label, left + 0.005f, y + 0.0045f, ts, 0, c, c, c, 255, 1);
        std::string r = it.right;
        if (r == ">") r.clear();   // Online's menus show no arrow for a submenu
        else if (r.size() > 2 && r.compare(r.size() - 2, 2, " >") == 0 && r[0] != '<') r = r.substr(0, r.size() - 2);
        bool list = r.size() > 4 && r[0] == '<' && r.back() == '>';
        if (list && sel && cm) {   // the left / right arrows around the value
            std::string v = r.substr(2, r.size() - 4);
            float vw = 0.f;
            N(N_BEGIN_TEXT_COMMAND_GET_SCREEN_WIDTH_OF_DISPLAY_TEXT, "STRING"); N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, v.c_str());
            N(N_SET_TEXT_FONT, 0); N(N_SET_TEXT_SCALE, ts, ts);
            vw = N<float>(N_END_TEXT_COMMAND_GET_SCREEN_WIDTH_OF_DISPLAY_TEXT, 1);
            float rx = left + w - 0.016f;
            Text(v, x, y + 0.0045f, ts, 0, 0, 0, 0, 255, 2, rx);
            N(N_DRAW_SPRITE, "CommonMenu", "arrowright", rx + 0.0075f, y + rowH / 2, 0.0115f, 0.0204f, 0.f, 0, 0, 0, 255, 0, 0);
            N(N_DRAW_SPRITE, "CommonMenu", "arrowleft", rx - vw - 0.0075f, y + rowH / 2, 0.0115f, 0.0204f, 0.f, 0, 0, 0, 255, 0, 0);
        } else if (!r.empty()) {
            if (list) r = r.substr(2, r.size() - 4);
            Text(r, x, y + 0.0045f, ts, 0, c, c, c, 255, 2, left + w - 0.005f);
        }
        y += rowH;
    }
    // up / down arrows when the list scrolls
    if ((int)items.size() > visible) {
        Rect(x, y + rowH / 2, w, rowH, 0, 0, 0, 204);
        if (cm) N(N_DRAW_SPRITE, "CommonMenu", "shop_arrows_upanddown", x, y + rowH / 2, 0.0156f, 0.0347f, 0.f, 255, 255, 255, 255, 0, 0);
        y += rowH;
    }
    // description
    const std::string &d = items[f.sel].desc;
    if (!d.empty()) {
        y += 0.0028f;
        Rect(x, y + 0.0014f, w, 0.0028f, 0, 0, 0, 255);
        y += 0.0028f;
        int lines = TextLines(d, left + 0.005f, y + 0.005f, ts, left + 0.005f, left + w - 0.005f);
        float dh = 0.011f + 0.0262f * lines;
        if (cm) N(N_DRAW_SPRITE, "CommonMenu", "gradient_bgd", x, y + dh / 2, w, dh, 0.f, 255, 255, 255, 255, 0, 0);
        else Rect(x, y + dh / 2, w, dh, 0, 0, 0, 160);
        TextLong(d, left + 0.005f, y + 0.004f, ts, 255, 255, 255, left + 0.005f, left + w - 0.005f);
    }
    MenuButtons();
}
static void MenuTick() {
    if (!g_menuOpen || g_stack.empty()) return;
    // Block weapon/phone/attack controls while browsing
    static const int block[] = {24, 25, 37, 27, 140, 141, 142, 257, 263, 264, 14, 15, 16, 17, 85, 80, 19, 0, 199, 200, 51};
    for (int c : block) N(N_DISABLE_CONTROL_ACTION, 0, c, 1);
    N(N_HIDE_HELP_TEXT_THIS_FRAME);
    N(N_SHOW_HUD_COMPONENT_THIS_FRAME, 3);

    Frame &f = g_stack.back();
    std::string title, sub; std::vector<Item> items;
    BuildMenu(f, title, sub, items);
    if (g_stack.empty() || &g_stack.back() != &f) return;
    if (items.empty()) items.push_back({"(nothing here)", "", "", [] {}});
    if (f.sel >= (int)items.size()) f.sel = (int)items.size() - 1;
    {
        long long key = ((long long)g_stack.size() << 48) ^ ((long long)f.id << 32) ^ ((long long)(f.param & 0xffff) << 16) ^ (long long)f.sel;
        if (key != g_hoverKey) { g_hoverKey = key; if (items[f.sel].hover) items[f.sel].hover(); }
    }

    // Input (keyboard arrows/Enter/Backspace or pad), with auto-repeat
    int now = Now();
    bool up = NB(N<int>(N_IS_CONTROL_PRESSED, 2, 188)), down = NB(N<int>(N_IS_CONTROL_PRESSED, 2, 187));
    bool left = NB(N<int>(N_IS_CONTROL_PRESSED, 2, 189)), right = NB(N<int>(N_IS_CONTROL_PRESSED, 2, 190));
    if (up || down) {
        if (!g_navHeld || now >= g_navNext) {
            f.sel = (f.sel + (down ? 1 : -1) + (int)items.size()) % (int)items.size();
            Sound("NAV_UP_DOWN", "HUD_FRONTEND_DEFAULT_SOUNDSET");
            g_navNext = now + (g_navHeld ? 70 : 350);
            g_navHeld = 1;
        }
    } else if ((left || right) && items[f.sel].cycle) {   // "< value >" rows: left / right
        if (!g_navHeld || now >= g_navNext) {
            items[f.sel].cycle(right ? 1 : -1);
            Sound("NAV_LEFT_RIGHT", "HUD_FRONTEND_DEFAULT_SOUNDSET");
            g_navNext = now + (g_navHeld ? 120 : 350);
            g_navHeld = 1;
        }
    } else g_navHeld = 0;
    if (NB(N<int>(N_IS_CONTROL_JUST_PRESSED, 2, 201))) {
        Sound("SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET");
        std::function<void()> act = items[f.sel].act;
        if (act) act();
        return;
    }
    if (NB(N<int>(N_IS_CONTROL_JUST_PRESSED, 2, 202)) || NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 0, 199)) || NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 0, 200))) {
        Sound("BACK", "HUD_FRONTEND_DEFAULT_SOUNDSET");
        if (g_menuBackHook && g_menuBackHook(f)) return;
        g_stack.pop_back();
        g_hoverKey = -1;
        if (g_stack.empty()) g_menuOpen = false;
        return;
    }

    MenuDraw(f, title, sub, items);
}

// ---------------------------------------------------------------- Living world + events
static void LivingWorld() {
    N(N_SET_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME, 1.5f);
    N(N_SET_RANDOM_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME, 1.5f);
    N(N_SET_PARKED_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME, 1.3f);
    N(N_SET_PED_DENSITY_MULTIPLIER_THIS_FRAME, 1.4f);
    N(N_SET_SCENARIO_PED_DENSITY_MULTIPLIER_THIS_FRAME, 1.4f, 1.4f);
    N(N_SET_WANTED_LEVEL_MULTIPLIER, 1.3f);
    N(N_SET_DISPATCH_COPS_FOR_PLAYER, Player(), 1);
}

static int g_truck = 0, g_truckDriver = 0, g_truckBlip = 0, g_truckEnds = 0;
static int g_crate = 0, g_crateBlip = 0, g_crateEnds = 0, g_nextEvent = 0;

static void EndTruck() {
    RemoveBlip(g_truckBlip);
    if (Exists(g_truck)) N(N_SET_ENTITY_AS_NO_LONGER_NEEDED, &g_truck);
    if (Exists(g_truckDriver)) N(N_SET_ENTITY_AS_NO_LONGER_NEEDED, &g_truckDriver);
    g_truck = g_truckDriver = 0;
}
static void EndCrate() {
    RemoveBlip(g_crateBlip);
    if (Exists(g_crate)) { N(N_SET_ENTITY_AS_MISSION_ENTITY, g_crate, 1, 1); N(N_DELETE_OBJECT, &g_crate); }
    g_crate = 0;
}
static void StartTruck() {
    int me = Me(); Vec3 p = Pos(me); Vec3 fwd = N<Vec3>(N_GET_ENTITY_FORWARD_VECTOR, me);
    float side = Rand(2) ? 120.f : -120.f;
    Vec3 out{}; float h = 0.f;
    if (!NB(N<int>(N_GET_CLOSEST_VEHICLE_NODE_WITH_HEADING, p.x + fwd.x * 250.f - fwd.y * side, p.y + fwd.y * 250.f + fwd.x * side, p.z, &out, &h, 1, 3.0f, 0.0f))) return;
    uint32_t vm = Joaat("stockade"), pm = Joaat("s_m_m_security_01");
    if (!LoadModel(vm) || !LoadModel(pm)) return;
    g_truck = N<int>(N_CREATE_VEHICLE, vm, out.x, out.y, out.z, h, 0, 0, 0);
    if (!g_truck) return;
    N(N_SET_ENTITY_AS_MISSION_ENTITY, g_truck, 1, 1);
    g_truckDriver = N<int>(N_CREATE_PED_INSIDE_VEHICLE, g_truck, 26, pm, -1, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, vm); N(N_SET_MODEL_AS_NO_LONGER_NEEDED, pm);
    if (g_truckDriver) N(N_TASK_VEHICLE_DRIVE_WANDER, g_truckDriver, g_truck, 15.0f, 786603);
    g_truckBlip = AddBlip(g_truck, 67, 2, "Armored Truck");
    g_truckEnds = Now() + 600000;
    Subtitle("An ~g~Armored Truck~s~ is on the move. Blow its back doors open for the cash.", 7000);
    Notify("~g~Armored Truck~s~ spotted. Check your map.");
}
static void StartCrate() {
    int me = Me(); Vec3 p = Pos(me);
    float a = (float)Rand(6283) / 1000.f;
    Vec3 out{};
    if (!NB(N<int>(N_GET_SAFE_COORD_FOR_PED, p.x + std::cos(a) * 400.f, p.y + std::sin(a) * 400.f, p.z, 1, &out, 16))) return;
    uint32_t cm = Joaat("prop_mil_crate_01");
    if (!LoadModel(cm)) return;
    g_crate = N<int>(N_CREATE_OBJECT, cm, out.x, out.y, out.z, 0, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, cm);
    if (!g_crate) return;
    N(N_SET_ENTITY_AS_MISSION_ENTITY, g_crate, 1, 1);
    g_crateBlip = AddBlip(g_crate, 306, 2, "Crate Drop");
    g_crateEnds = Now() + 600000;
    Subtitle("A ~g~Crate Drop~s~ has landed. Get to it first!", 7000);
}
static void Events() {
    int me = Me(); Vec3 mp = Pos(me);
    if (g_truck) {
        bool gone = !Exists(g_truck);
        bool robbed = !gone && (Dead(g_truck) || NB(N<int>(N_IS_VEHICLE_DOOR_DAMAGED, g_truck, 2)) || NB(N<int>(N_IS_VEHICLE_DOOR_DAMAGED, g_truck, 3)));
        if (robbed && Dist(Pos(g_truck), mp) < 60.f) {
            int cash = 5000 + Rand(10000);
            AddCash(cash);
            if (N<int>(N_GET_PLAYER_WANTED_LEVEL, Player()) < 2) { N(N_SET_PLAYER_WANTED_LEVEL, Player(), 2, 0); N(N_SET_PLAYER_WANTED_LEVEL_NOW, Player(), 0); }
            Subtitle("~g~Armored Truck robbed: +" + Money(cash), 5000);
            GiveRP(250, "armored truck");
            EndTruck();
        } else if (gone || Now() > g_truckEnds) EndTruck();
    }
    if (g_crate) {
        if (!Exists(g_crate) || Now() > g_crateEnds) EndCrate();
        else if (Dist(Pos(g_crate), mp) < 2.5f) {
            int cash = 2000 + Rand(4000);
            AddCash(cash);
            static const char *loot[] = {"weapon_assaultrifle", "weapon_carbinerifle", "weapon_smg", "weapon_pumpshotgun", "weapon_sniperrifle", "weapon_rpg"};
            N(N_GIVE_WEAPON_TO_PED, me, Joaat(loot[Rand(6)]), 250, 0, 1);
            Subtitle("~g~Crate Drop collected: +" + Money(cash) + " and a weapon", 5000);
            GiveRP(200, "crate drop");
            EndCrate();
        }
    }
    if (!g_truck && !g_crate && !g_mActive && !InActivity() && !CoronaActive() && Now() > g_nextEvent && !Dead(me) && !IslandNear(mp, 800.f)) {
        g_nextEvent = Now() + 600000 + Rand(480000);
        if (Rand(2)) StartTruck(); else StartCrate();
    }
}

// Sell stolen cars at LS Customs (press E / D-pad right inside), once every 48 minutes
static int g_lastSold = -100000000;
static void SellCars() {
    static const float shops[][3] = {{-365.4f, -131.8f, 37.9f}, {731.8f, -1088.9f, 22.2f}, {-1155.5f, -2005.4f, 13.2f}, {1175.0f, 2640.0f, 37.8f}, {110.9f, 6626.4f, 31.8f}};
    int me = Me();
    if (g_menuOpen || !InAnyVehicle()) return;
    int v = MyVehicle();
    if (v == g_personal || v == g_mVeh) return;
    for (int k = 0; k < 10; ++k) if (v == g_garCar[k]) return;
    Vec3 mp = Pos(me); bool atShop = false;
    for (auto &s : shops) if (Dist(mp, V3(s[0], s[1], s[2])) < 15.f) atShop = true;
    if (!atShop) return;
    Help("Press ~INPUT_CONTEXT~ to sell this vehicle to LS Customs.");
    if (!JustPressed(51) && !g_wantSell) return;
    g_wantSell = false;
    if (Now() - g_lastSold < 48 * 60000) { Notify("LS Customs won't buy another car yet. Come back later."); return; }
    int cls = N<int>(N_GET_VEHICLE_CLASS, v);
    int value = 1000 + (cls == 7 ? 8000 : cls == 6 ? 6000 : cls == 5 ? 5000 : 2000) + Rand(2000);
    if (N<float>(N_GET_VEHICLE_BODY_HEALTH, v) < 900.f) value = value * 7 / 10;
    g_lastSold = Now();
    N(N_TASK_LEAVE_VEHICLE, me, v, 0);
    Wait(1500);
    N(N_SET_ENTITY_AS_MISSION_ENTITY, v, 1, 1);
    N(N_DELETE_VEHICLE, &v);
    AddCash(value);
    Subtitle("Vehicle sold for ~g~" + Money(value), 4000);
    GiveRP(100, "vehicle sold");
}

// RP for kills and escaping the cops; respawn at your home
static std::set<int> g_counted;
static int g_lastScan = 0, g_maxWanted = 0;
static bool g_wasDead = false;
static void Progress() {
    int me = Me();
    if (Now() - g_lastScan > 500) {
        g_lastScan = Now();
        static int peds[1024];
        int n = shv_worldGetAllPeds(peds, 1024);
        for (int i = 0; i < n; ++i) {
            int p = peds[i];
            if (p == me || !Dead(p) || g_counted.count(p)) continue;
            if (N<int>(N_GET_PED_SOURCE_OF_DEATH, p) != me) continue;
            g_counted.insert(p);
            int type = N<int>(N_GET_PED_TYPE, p);
            GiveRP(type == 6 || type == 27 ? 25 : 10, nullptr);
        }
        if (g_counted.size() > 600) g_counted.clear();
    }
    int wanted = N<int>(N_GET_PLAYER_WANTED_LEVEL, Player());
    if (wanted > g_maxWanted) g_maxWanted = wanted;
    static int greyedAt = -100000;   // escaping means the stars went grey first; GodEye clearing them does not pay
    if (wanted > 0 && NB(N<int>(N_ARE_PLAYER_STARS_GREYED_OUT, Player()))) greyedAt = Now();
    bool dead = Dead(me);
    if (wanted == 0 && g_maxWanted > 0 && !dead) { if (!g_mActive && Now() - greyedAt < 3000) GiveRP(100 * g_maxWanted, "lost the cops"); g_maxWanted = 0; }
    if (dead) { g_wasDead = true; g_maxWanted = 0; g_inside = -1; }
    else if (g_wasDead) {
        g_wasDead = false;
        int i = FindProp(g_spawnProp);
        if (i >= 0 && Owns(g_spawnProp)) {
            Wait(1500);
            if (PropInterior(i) && PROPS[i].type != T_VILLA) EnterProperty(i); else GoToProperty(i);
        }
    }
}

#include "Traffic.inc"
#include "Knoway.inc"

// ---------------------------------------------------------------- Isla Neon on the map (radar + pause map): area blips
static std::vector<int> g_mapBlips;
static int g_mapColours[7] = {5, 25, 40, 39, 4, 62, 4};    // sand, grass, rock, city ground, road, building, runway (HUD colours)
// ---------------------------------------------------------------- Mile High Club: the finished tower replaces the construction site
static void MhcInit() {
    int n = 0;
    for (const MhcHide &h : MHC_HIDE) {
        uint32_t m = h.name ? Joaat(h.name) : h.hash;
        N(N_CREATE_MODEL_HIDE, h.x, h.y, h.z, 2.5f, m, 1);
        n++;
    }
    Log("mile high club: %d construction models hidden", n);
}
static void BuildIslandMap() {
    for (int &b : g_mapBlips) RemoveBlip(b);
    g_mapBlips.clear();
    for (const MapRect &r : ISLAND_MAP) {
        int b = N<int>(N_ADD_BLIP_FOR_AREA, r.cx, r.cy, 0.f, r.w, r.h);
        if (!b) continue;
        N(N_SET_BLIP_ROTATION, b, (int)r.rot);
        N(N_SET_BLIP_COLOUR, b, g_mapColours[r.kind]);
        N(N_SET_BLIP_ALPHA, b, 255);
        N(N_SET_BLIP_DISPLAY, b, 8);
        N(N_SET_BLIP_PRIORITY, b, r.kind >= 4 ? (r.kind == 5 ? 3 : 2) : 1);
        N(N_SET_BLIP_AS_SHORT_RANGE, b, 1);         // no off-radar markers on the radar edge
        N(N_SET_BLIP_HIDDEN_ON_LEGEND, b, 1);
        g_mapBlips.push_back(b);
    }
    Log("island map: %d area blips", (int)g_mapBlips.size());
}
// far from the island the radar draws these big area blips badly: there they only show on the pause map
static void IslandMapTick() {
    static int next = 0, mode = -1;
    if (Now() < next || g_mapBlips.empty()) return;
    next = Now() + 1500;
    int want = IslandNear(Pos(Me()), 900.f) ? 8 : 3;
    if (want == mode) return;
    mode = want;
    for (int b : g_mapBlips) N(N_SET_BLIP_DISPLAY, b, want);
}

// ---------------------------------------------------------------- Test commands (GoriOnline.cmd next to the .asi, one command per line)
static int g_lastCmd = 0;
static void ApplyOnlineMap(bool force);
static std::vector<int> *g_testObjs = nullptr;
static void CommandTick() {
    if (Now() - g_lastCmd < 1000) return;
    g_lastCmd = Now();
    std::string path = g_dir + "GoriOnline.cmd";
    FILE *f = std::fopen(path.c_str(), "r");
    if (!f) return;
    std::vector<std::string> lines; char buf[512];
    while (std::fgets(buf, sizeof buf, f)) { std::string l(buf); while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back(); if (!l.empty()) lines.push_back(l); }
    std::fclose(f);
    std::remove(path.c_str());
    for (auto &l : lines) {
        Log("cmd: %s", l.c_str());
        auto a = Split(l, ' ');
        std::string rest = l.size() > a[0].size() ? l.substr(a[0].size() + 1) : "";
        auto F = [&](size_t i) { return i < a.size() ? (float)std::atof(a[i].c_str()) : 0.f; };
        if (SfDebugCommand(a)) {}
        else if (a[0] == "tp") Teleport(F(1), F(2), F(3), F(4));
        else if (a[0] == "island") IslandArrive(ISLAND_SPAWN[0], ISLAND_SPAWN[1], ISLAND_SPAWN[2], ISLAND_SPAWN[3]);
        else if (a[0] == "enter") { int i = FindProp(rest); if (i >= 0) EnterProperty(i); }
        else if (a[0] == "webtest") {   // webtest <WWW_SITE> [seconds]: the game's own web browser movie
            std::string site = a.size() > 1 ? a[1] : "WWW_EYEFIND_INFO";
            int secs = a.size() > 2 ? std::atoi(a[2].c_str()) : 12;
            int mv = N<int>(N_REQUEST_SCALEFORM_MOVIE, "web_browser");
            int t0 = Now();
            while (!NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, mv)) && Now() - t0 < 6000) Wait(0);
            Log("webtest: movie %d loaded %d", mv, N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, mv));
            N(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, "GO_TO_WEBPAGE");
            N(N_BEGIN_TEXT_COMMAND_SCALEFORM_STRING, "STRING");
            N(N_ADD_TEXT_COMPONENT_SUBSTRING_WEBSITE, site.c_str());
            N(N_END_TEXT_COMMAND_SCALEFORM_STRING);
            N(N_END_SCALEFORM_MOVIE_METHOD);
            // appinternet.ysc: wait for the site's movie, INITIALISE_WEBSITE, IS_SITE_DYNAMIC
            t0 = Now();
            while (!NB(N<int>(N_HAS_SCALEFORM_CONTAINER_MOVIE_LOADED_INTO_PARENT, mv)) && Now() - t0 < 8000) { N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, mv, 255, 255, 255, 255, 0); Wait(0); }
            N(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, "INITIALISE_WEBSITE");
            N(N_END_SCALEFORM_MOVIE_METHOD);
            N(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, "IS_SITE_DYNAMIC");
            int rv = N<int>(N_END_SCALEFORM_MOVIE_METHOD_RETURN_VALUE);
            t0 = Now();
            while (!NB(N<int>(N_IS_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_READY, rv)) && Now() - t0 < 3000) { N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, mv, 255, 255, 255, 255, 0); Wait(0); }
            Log("webtest %s: container loaded %d, dynamic %d, website %d page %d", site.c_str(), N<int>(N_HAS_SCALEFORM_CONTAINER_MOVIE_LOADED_INTO_PARENT, mv),
                N<int>(N_GET_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_INT, rv), N<int>(N_GET_CURRENT_WEBSITE_ID), N<int>(N_GET_CURRENT_WEBPAGE_ID));
            int until = Now() + secs * 1000, logged = 0;
            while (Now() < until) {
                N(N_DISABLE_ALL_CONTROL_ACTIONS, 0);
                N(N_HIDE_HUD_AND_RADAR_THIS_FRAME);
                N(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, "SET_MOUSE_INPUT");
                N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 239));
                N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 240));
                N(N_END_SCALEFORM_MOVIE_METHOD);
                N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, mv, 255, 255, 255, 255, 0);
                if (NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 2, 237)) || NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 2, 201))) {   // click
                    N(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, "SET_INPUT_EVENT");
                    N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, 16.f);
                    N(N_END_SCALEFORM_MOVIE_METHOD);
                    N(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, "GET_CURRENT_SELECTION");
                    int sel = N<int>(N_END_SCALEFORM_MOVIE_METHOD_RETURN_VALUE);
                    for (int w = 0; w < 30 && !NB(N<int>(N_IS_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_READY, sel)); ++w) Wait(0);
                    Log("webtest click: selection %d website %d page %d", N<int>(N_GET_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_INT, sel), N<int>(N_GET_CURRENT_WEBSITE_ID), N<int>(N_GET_CURRENT_WEBPAGE_ID));
                }
                if (Now() - logged > 3000) { logged = Now(); Log("webtest: website %d page %d", N<int>(N_GET_CURRENT_WEBSITE_ID), N<int>(N_GET_CURRENT_WEBPAGE_ID)); }
                Wait(0);
            }
            N(N_SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED, &mv);
        }
        else if (a[0] == "rank") { RankBarShow(g_rp, g_rp, 5000); Log("rank: rp %d level %d (%d..%d), movie loaded %d", g_rp, LevelOf(g_rp), RpFor(LevelOf(g_rp)), RpFor(LevelOf(g_rp) + 1), N<int>(N_HAS_SCALEFORM_SCRIPT_HUD_MOVIE_LOADED, 19)); }
        else if (a[0] == "giverp") { GiveRP(std::atoi(rest.c_str()), "test"); }
        else if (a[0] == "delprop") {   // undo a test purchase: ownership, its garage cars, its business state
            int n0 = (int)g_props.size(), c0 = (int)g_garage.size();
            g_props.erase(std::remove(g_props.begin(), g_props.end(), rest), g_props.end());
            g_garage.erase(std::remove_if(g_garage.begin(), g_garage.end(), [&](const GVeh &g) { return g.prop == rest; }), g_garage.end());
            g_biz.erase(std::remove_if(g_biz.begin(), g_biz.end(), [&](const BizState &b) { return b.prop == rest; }), g_biz.end());
            if (g_spawnProp == rest) g_spawnProp.clear();
            g_personalIdx = -1;
            Save(); RefreshPropertyBlips();
            Log("delprop %s: %d property, %d vehicles removed", rest.c_str(), n0 - (int)g_props.size(), c0 - (int)g_garage.size());
        }
        else if (a[0] == "delspv") {   // delspv <MOC|Avenger|Terrorbyte|Kosatka>: undo a test purchase of a special vehicle's configuration
            for (int k = 0; k < SPV_COUNT; ++k) if (rest == SPV[k].key) { SpvForget(k); g_cust.erase(SpvKey(k)); }
            if (rest == "Ballistic") { if (g_bal.on) BallisticOff(false); g_cust.erase(BAL_OWNED); }
            Save(); Log("delspv %s", rest.c_str());
        }
        else if (a[0] == "quitjob") { if (g_mActive) FailMission("Quit (test)"); }
        else if (a[0] == "testjob") {   // 3 walk-to points east of the player: a co-op job worth $5,000 to each member
            Vec3 p = Pos(Me());
            Mission m; m.name = "CO-OP TEST"; m.coopPay = 5000;
            for (int i = 1; i <= 3; ++i) m.steps.push_back(Goto("Go to ~y~point " + std::to_string(i) + "~s~.", V3(p.x + 12.f * i, p.y, p.z), 3.f));
            m.onSuccess = [] { Shard("~g~CO-OP TEST PASSED", ""); };
            StartMission(m);
        }
        else if (a[0] == "jobs") {   // log the co-op job list and our job state
            Log("jobs: api %d you %d hosting %d | %d open", LO().getJobs ? 1 : 0, (int)YouActive(), (int)g_coopHosting, (int)g_coopJobs.size());
            for (const CoopJob &j : g_coopJobs) Log("  job %u \"%s\" host %s members %d status %d", j.id, j.title.c_str(), j.host.c_str(), j.members, j.status);
            Log("  state: id %u host %s status %d step %d obj %.0f %.0f %.0f payout %d text \"%s\" members %s blip %d",
                g_coop.id, g_coop.host.c_str(), g_coop.status, g_coop.step, g_coop.obj.x, g_coop.obj.y, g_coop.obj.z, g_coop.payout, g_coop.text.c_str(), g_coop.members.c_str(), g_coopBlip);
        }
        else if (a[0] == "label") { for (size_t k = 1; k < a.size(); ++k) Log("label %s = \"%s\"", a[k].c_str(), Label(a[k].c_str()).c_str()); }
        else if (a[0] == "avtest") {   // the Avenger 350 m up with us at the controls, then the autopilot
            int v = g_spvVeh[SPV_AVENGER];
            if (!Exists(v)) { SpvRequest(SPV_AVENGER); v = g_spvVeh[SPV_AVENGER]; }
            if (Exists(v)) {
                Vec3 p = Pos(Me());
                N(N_SET_ENTITY_COORDS, v, p.x, p.y, p.z + 350.f, 0, 0, 0, 0);
                N(N_SET_ENTITY_HEADING, v, 90.f);
                N(N_SET_PED_INTO_VEHICLE, Me(), v, -1);
                N(N_SET_VEHICLE_ENGINE_ON, v, 1, 1, 0);
                N(N_CONTROL_LANDING_GEAR, v, 3);
                N(N_SET_VEHICLE_FORWARD_SPEED, v, 50.f);
                Wait(1500);
                AvengerAutopilot();
            }
            Log("avtest: avenger %d flying %d pilot %d in %d", v, (int)AvengerFlying(), g_avPilot, g_spvIn);
        }
        else if (a[0] == "avstat") {
            int v = g_spvVeh[SPV_AVENGER];
            Vec3 vp = Exists(v) ? Pos(v) : V3(0, 0, 0);
            Log("avstat: avenger %d at %.0f %.0f %.0f speed %.1f agl %.0f pilot %d alive %d | me in spv %d, vehicle %d, at %.0f %.0f %.0f", v, vp.x, vp.y, vp.z,
                Exists(v) ? N<float>(N_GET_ENTITY_SPEED, v) : 0.f, Exists(v) ? N<float>(N_GET_ENTITY_HEIGHT_ABOVE_GROUND, v) : 0.f, g_avPilot,
                Exists(g_avPilot) ? (int)!Dead(g_avPilot) : 0, g_spvIn, MyVehicle(), Pos(Me()).x, Pos(Me()).y, Pos(Me()).z);
        }
        else if (a[0] == "avctl") AvengerTakeControls();
        else if (a[0] == "ammu") { g_menuOpen = true; g_stack.clear(); Open((MenuId)SH_AMMU); }   // the Ammu-Nation menu anywhere (test)
        else if (a[0] == "lsc") { if (InAnyVehicle()) MsEnter(std::max(0, std::min(NMS_SHOPS - 1, a.size() > 1 ? std::atoi(a[1].c_str()) : 2))); }   // lsc [shop]: a mod shop on the current vehicle (test)
        else if (a[0] == "shops") {
            Log("shops: near LSC %d, near Ammu-Nation %d, in vehicle %d (personal %d), lsc %d, armour %d", NearShop(LSC_SPOTS, 6, 9.f), NearShop(AMMU_SPOTS, 11, 3.f),
                MyVehicle(), (int)(MyVehicle() == g_personal), g_lscAt, N<int>(N_GET_PED_ARMOUR, Me()));
        }
        else if (a[0] == "races") {   // log the installed races
            g_raceListAt = -100000; RefreshRaceList();
            for (auto &d : g_raceList) Log("race %s: \"%s\" %d cps, %d grid, %d vehicles, laps %d", d.file.c_str(), d.name.c_str(), (int)d.cps.size(), (int)d.grid.size(), (int)d.vehicles.size(), (int)d.laps);
            Log("races: %d installed; current phase %d cp %d/%d", (int)g_raceList.size(), g_race.phase, g_race.cp, g_race.total);
        }
        else if (a[0] == "hbtune" && a.size() > 4) { g_hbRot = F(1); g_hbAhead = F(2); g_hbUp = F(3); g_hbW = F(4); g_hbMode = (int)F(5); g_hbRotX = F(6); g_hbCorner = (int)F(7); Log("hbtune %.0f %.2f %.2f %.2f", g_hbRot, g_hbAhead, g_hbUp, g_hbW); }   // the heist board's placement (test)
        else if (a[0] == "racepad" && a.size() > 1) {   // racepad <n>: the race car 8 m before stunt pad n, at it, 25 m/s (test)
            int k = std::atoi(a[1].c_str());
            if (g_race.phase == RP_RACING && k >= 0 && k < (int)g_race.pads.size() && Exists(g_race.veh)) {
                int pad = g_race.pads[k].first;
                Vec3 p = Pos(pad); float h = N<float>(N_GET_ENTITY_HEADING, pad);
                Vec3 s = V3(p.x + std::sin(h * 0.0174533f) * 8.f, p.y - std::cos(h * 0.0174533f) * 8.f, p.z + 1.f);
                N(N_SET_ENTITY_COORDS, g_race.veh, s.x, s.y, s.z, 0, 0, 0, 0);
                N(N_SET_ENTITY_HEADING, g_race.veh, h);
                N(N_SET_VEHICLE_FORWARD_SPEED, g_race.veh, 25.f);
                Log("racepad %d of %d: %.1f %.1f %.1f h %.0f", k, (int)g_race.pads.size(), p.x, p.y, p.z, h);
            }
        }
        else if (a[0] == "racesolo") RaceStartSolo(a.size() > 1 ? a[1] : "", a.size() > 2 ? std::atoi(a[2].c_str()) : 1, a.size() > 3 ? a[3] : "");   // racesolo <file.xml> [laps] [vehicle]
        else if (a[0] == "racehost") RaceHostOnline(a.size() > 1 ? a[1] : "", a.size() > 2 ? std::atoi(a[2].c_str()) : 1, a.size() > 3 ? a[3] : "");
        else if (a[0] == "racestart") RaceHostStart();
        else if (a[0] == "acid") {   // acid [key value]: the Acid Lab / First Dose state (fd own upg sup stock10)
            if (a.size() > 2) { AcidSet(a[1].c_str(), std::atoi(a[2].c_str())); Save(); }
            Log("acid: fd %d own %d upg %d sup %d stock %d/%d truck %d inFreak %d inLab %d", AcidGet("fd"), AcidGet("own"), AcidGet("upg"), AcidGet("sup"), AcidUnits(), ACID_MAX,
                g_acidTruck, (int)g_inFreak, (int)g_inLab);
        }
        else if (a[0] == "acidclear") { for (auto it = g_cust.begin(); it != g_cust.end();) { if (!it->first.compare(0, 6, "@acid|")) it = g_cust.erase(it); else ++it; } Save(); Log("acid: state cleared"); }
        else if (a[0] == "freak") FreakEnter();
        else if (a[0] == "freakout") FreakLeave();
        else if (a[0] == "acidlab") AcidLabEnter();
        else if (a[0] == "acidout") AcidLabLeave();
        else if (a[0] == "acidtruck") AcidTruckRequest();
        else if (a[0] == "acidret") AcidTruckReturn();
        else if (a[0] == "sprite" && a.size() > 2) {   // sprite <dict> <texture>: its resolution, drawn for 5 s
            N(N_REQUEST_STREAMED_TEXTURE_DICT, a[1].c_str(), 0);
            for (int i = 0; i < 80 && !NB(N<int>(N_HAS_STREAMED_TEXTURE_DICT_LOADED, a[1].c_str())); ++i) Wait(50);
            Vec3 r = N<Vec3>(N_GET_TEXTURE_RESOLUTION, a[1].c_str(), a[2].c_str());
            Log("sprite %s/%s: loaded %d resolution %.0f x %.0f", a[1].c_str(), a[2].c_str(), (int)NB(N<int>(N_HAS_STREAMED_TEXTURE_DICT_LOADED, a[1].c_str())), r.x, r.y);
            for (int t0 = Now(); Now() - t0 < 5000;) { N(N_DRAW_SPRITE, a[1].c_str(), a[2].c_str(), 0.5f, 0.5f, 0.15f, 0.26f, 0.f, 255, 255, 255, 255, 0, 0); Wait(0); }
        }
        else if (a[0] == "dose") StartFirstDose(std::max(0, std::min(5, std::atoi(rest.c_str()))));
        else if (a[0] == "cm" && a.size() > 2) { int c = std::atoi(a[1].c_str()) % NCONTACTS; StartContactMission(c, std::min(std::atoi(a[2].c_str()), CONTACTS[c].count - 1)); }   // cm <contact> <mission>
        else if (a[0] == "sv" && a.size() > 1) SvHost(std::max(0, std::min(NSV_LOCS - 1, std::atoi(a[1].c_str()))), a.size() > 2 && a[2] == "solo");   // sv <place> [solo]
        else if (a[0] == "svstart") SvHostStart();
        else if (a[0] == "arena") {   // arena [theme map light vehicle bots minutes]: Arena War solo (test)
            for (int k = 0; k < 8 && k + 1 < (int)a.size(); ++k) g_awSel[k] = std::atoi(a[k + 1].c_str());   // ... minutes-index mode traps
            AwStart(false);
        }
        else if (a[0] == "arenaquit") { if (g_aw.phase != AWP_NONE) AwFinish(); }
        else if (a[0] == "awfloors" && a.size() > 1 && g_aw.phase == AWP_NONE) {   // awfloors <theme>: load each map, log the floor heights at the centre and 25 m out
            int th = std::max(0, std::min(2, std::atoi(a[1].c_str()))), me = Me();
            Vec3 back = Pos(me); float bh = N<float>(N_GET_ENTITY_HEADING, me);
            N(N_FREEZE_ENTITY_POSITION, me, 1);
            for (int m = 0; m < AW_THEMES[th].maps; ++m) {
                int id = AwLoadArena(th, m, 0);
                N(N_SET_ENTITY_COORDS, me, AW_CENTRE.x, AW_CENTRE.y, 150.f, 0, 0, 0, 0);   // as AwBegin does
                for (int i = 0; i < 40; ++i) { N(N_FORCE_ROOM_FOR_ENTITY, me, id, AW_ROOM); N(N_FORCE_ROOM_FOR_GAME_VIEWPORT, id, AW_ROOM); N(N_REQUEST_COLLISION_AT_COORD, AW_CENTRE.x, AW_CENTRE.y, 120.f); Wait(50); }
                float fz = AwFindFloor(); g_aw.floorZ = fz;
                float h = 0.f; Vec3 sp = AwSpawnPoint(h);
                Log("awfloors %s map %d: floor %.1f, a spawn at %.1f %.1f %.1f", AW_THEMES[th].name, m + 1, fz, sp.x, sp.y, sp.z);
                g_aw = AwRun();
            }
            N(N_CLEAR_ROOM_FOR_ENTITY, me);
            N(N_FREEZE_ENTITY_POSITION, me, 0);
            Teleport(back.x, back.y, back.z, bh);
        }
        else if (a[0] == "awroom" && a.size() > 2 && g_aw.phase == AWP_NONE) {   // awroom <map> <z> [force 0/1]: stand at the centre at that height, log the room the game puts you in
            static Vec3 s_back = V3(0, 0, 0); static float s_bh = 0.f;
            int me = Me();
            if (s_back.x == 0.f) { s_back = Pos(me); s_bh = N<float>(N_GET_ENTITY_HEADING, me); }
            int id = AwLoadArena(0, std::atoi(a[1].c_str()), 0);
            N(N_CLEAR_ROOM_FOR_ENTITY, me); N(N_CLEAR_ROOM_FOR_GAME_VIEWPORT);
            N(N_FREEZE_ENTITY_POSITION, me, 1);
            N(N_SET_ENTITY_COORDS, me, AW_CENTRE.x + 20.f, AW_CENTRE.y, F(2), 0, 0, 0, 0);
            bool force = a.size() > 3 && a[3] == "1";
            for (int i = 0; i < 60; ++i) { if (force) { N(N_FORCE_ROOM_FOR_ENTITY, me, id, AW_ROOM); N(N_FORCE_ROOM_FOR_GAME_VIEWPORT, id, AW_ROOM); } Wait(50); }
            Log("awroom z %.1f: interior %d room 0x%08X (arena %d)", F(2), N<int>(N_GET_INTERIOR_FROM_ENTITY, me), (unsigned)N<int>(N_GET_ROOM_KEY_FROM_ENTITY, me), id);
        }
        else if (a[0] == "awview" && a.size() > 2 && g_aw.phase == AWP_NONE) {   // awview <theme> <map>: stand 3 m over the floor at the centre (then awroomend x y z h)
            int me = Me(), th = std::max(0, std::min(2, std::atoi(a[1].c_str())));
            int id = AwLoadArena(th, std::atoi(a[2].c_str()) % AW_THEMES[th].maps, 2);
            N(N_FREEZE_ENTITY_POSITION, me, 1);
            N(N_SET_ENTITY_COORDS, me, AW_CENTRE.x - 30.f, AW_CENTRE.y, 120.f, 0, 0, 0, 0);
            for (int i = 0; i < 40; ++i) { N(N_FORCE_ROOM_FOR_ENTITY, me, id, AW_ROOM); N(N_FORCE_ROOM_FOR_GAME_VIEWPORT, id, AW_ROOM); Wait(50); }
            float gz = GroundAt(AW_CENTRE.x - 30.f, AW_CENTRE.y, 140.f, 100.f);
            N(N_SET_ENTITY_COORDS, me, AW_CENTRE.x - 30.f, AW_CENTRE.y, gz + 3.f, 0, 0, 0, 0);
            N(N_SET_ENTITY_HEADING, me, 270.f);
            N(N_SET_GAMEPLAY_CAM_RELATIVE_HEADING, 0.f); N(N_SET_GAMEPLAY_CAM_RELATIVE_PITCH, -10.f, 1.f);
            for (int i = 0; i < 40; ++i) { N(N_FORCE_ROOM_FOR_ENTITY, me, id, AW_ROOM); N(N_FORCE_ROOM_FOR_GAME_VIEWPORT, id, AW_ROOM); Wait(50); }
            Log("awview %s map %s: floor %.1f", AW_THEMES[th].name, a[2].c_str(), gz);
        }
        else if (a[0] == "awprobe" && a.size() > 2) {   // awprobe x y: the ground found from several start heights
            std::string line;
            for (float top : {200.f, 170.f, 160.f, 150.f, 145.f, 140.f, 130.f, 120.f}) { char b2[32]; std::snprintf(b2, sizeof b2, " %.0f:%.1f", top, GroundAt(F(1), F(2), top, -1.f)); line += b2; }
            Log("awprobe %.1f %.1f:%s", F(1), F(2), line.c_str());
        }
        else if (a[0] == "awroomend") { int me = Me(); N(N_CLEAR_ROOM_FOR_ENTITY, me); N(N_CLEAR_ROOM_FOR_GAME_VIEWPORT); N(N_FREEZE_ENTITY_POSITION, me, 0); Teleport(F(1), F(2), F(3), F(4)); }
        else if (a[0] == "awtraps") {   // where the traps are, and how far from you
            for (auto &t : g_aw.trapList) Log("  trap %d at %.1f %.1f %.1f, %.1f m, %s", t.kind, t.p.x, t.p.y, t.p.z, Dist(Pos(Me()), t.p), Exists(t.obj) ? "up" : "down");
            if (g_aw.mode == AWM_FLAGWAR) Log("  flag carrier %d at %.1f %.1f %.1f, score %d - %d, bases %.1f %.1f / bots %.1f %.1f", g_aw.flagCarrier, g_aw.flag.x, g_aw.flag.y, g_aw.flag.z, g_aw.score, g_aw.botScore,
                                              g_aw.myBase.x, g_aw.myBase.y, g_aw.botBase.x, g_aw.botBase.y);
        }
        else if (a[0] == "awgoto" && a.size() > 1 && Exists(g_aw.myVeh)) {   // awgoto trap <i> | flag | base: drop your car there (testing)
            Vec3 p = Pos(g_aw.myVeh);
            if (a[1] == "trap" && a.size() > 2 && std::atoi(a[2].c_str()) < (int)g_aw.trapList.size()) p = g_aw.trapList[std::atoi(a[2].c_str())].p;
            else if (a[1] == "flag") p = g_aw.flag;
            else if (a[1] == "base") p = g_aw.myBase;
            N(N_SET_ENTITY_COORDS, g_aw.myVeh, p.x, p.y, p.z + 1.f, 0, 0, 0, 0);
            Log("awgoto %s: %.1f %.1f %.1f", a[1].c_str(), p.x, p.y, p.z);
        }
        else if (a[0] == "awlaunch" && Exists(g_aw.myVeh)) {   // awlaunch [vz]: throw your car up and log its height for 1.5 s (testing the flippers)
            int v = g_aw.myVeh; float vz = a.size() > 1 ? F(1) : 13.f;
            Vec3 vel = N<Vec3>(N_GET_ENTITY_VELOCITY, v);
            if (a.size() > 2 && a[2] == "1") N(N_APPLY_FORCE_TO_ENTITY_CENTER_OF_MASS, v, 1, 0.f, 0.f, vz, 0, 0, 1, 1);   // an impulse relative to the mass
            else N(N_SET_ENTITY_VELOCITY, v, vel.x, vel.y, vz);
            std::string line;
            for (int i = 0; i < 15; ++i) { Wait(100); char b2[24]; std::snprintf(b2, sizeof b2, " %.1f/%.1f", Pos(v).z, N<Vec3>(N_GET_ENTITY_VELOCITY, v).z); line += b2; }
            Log("awlaunch %.0f: z/vz%s", vz, line.c_str());
        }
        else if (a[0] == "arenaabort") { if (g_aw.phase != AWP_NONE) AwCleanup(); }   // out of the arena with no results or rewards
        else if (a[0] == "arenacp") {   // Games Masters: drop your car on the current checkpoint
            if (!g_aw.cps.empty() && Exists(g_aw.myVeh)) { Vec3 c = g_aw.cps[(size_t)g_aw.score % g_aw.cps.size()]; N(N_SET_ENTITY_COORDS, g_aw.myVeh, c.x, c.y, c.z + 0.5f, 0, 0, 0, 0); Log("arenacp: %.1f %.1f %.1f", c.x, c.y, c.z); }
        }
        else if (a[0] == "arenastat") {   // the event, then each bot: distance to you, speed, its car's health
            Log("arena: mode %d phase %d kills %d deaths %d score %d bomb %d bots %d interior %d room 0x%08X car health %d", g_aw.mode, g_aw.phase, g_aw.kills, g_aw.deaths, g_aw.score, g_aw.bombHolder, (int)g_aw.botList.size(), g_aw.interior,
                (unsigned)N<int>(N_GET_ROOM_KEY_FROM_ENTITY, Me()), Exists(g_aw.myVeh) ? N<int>(N_GET_ENTITY_HEALTH, g_aw.myVeh) : -1);
            for (size_t i = 0; i < g_aw.botList.size(); ++i) {
                const AwBot &b = g_aw.botList[i];
                if (!Exists(b.veh)) { Log("  bot %d: respawning", (int)i); continue; }
                Log("  bot %d: %.1f m, %.1f m/s, health %d", (int)i, Dist(Pos(b.veh), Pos(Me())), N<float>(N_GET_ENTITY_SPEED, b.veh), N<int>(N_GET_ENTITY_HEALTH, b.veh));
            }
        }
        else if (a[0] == "svquit") { if (g_sv.phase == SVP_LOBBY) { if (LO().jobUpdate) LO().jobUpdate(COOP_CLOSED, 0, 0, 0, 0, ""); g_sv = SvRun(); } else if (g_sv.phase != SVP_NONE) { if (g_sv.host) SvHostEnd(); SvFinish(); } }
        else if (a[0] == "svstat") Log("survival: phase %d wave %d cleared %d kills %d enemies %d/%d alive %d", g_sv.phase, g_sv.wave, g_sv.cleared, g_sv.kills, g_sv.spawned, g_sv.total, (int)g_sv.peds.size());
        else if (a[0] == "lastdose") StartLastDose(std::max(0, std::min(4, std::atoi(rest.c_str()))));
        else if (a[0] == "creator") CreatorStart(rest);                    // creator <track file.xml>
        else if (a[0] == "crcp") { g_cr.cps.push_back(Pos(InAnyVehicle() ? MyVehicle() : Me())); CreatorMarks(); }
        else if (a[0] == "crgrid") CreatorSetGrid();
        else if (a[0] == "crsave") CreatorSave();
        else if (a[0] == "crexit") CreatorStop();
        else if (a[0] == "racecp") { if (g_race.phase == RP_RACING) { Vec3 c = RaceCp(g_race.cp); N(N_SET_ENTITY_COORDS, InAnyVehicle() ? MyVehicle() : Me(), c.x, c.y, c.z, 0, 0, 0, 0); } }   // jump to the next checkpoint (test)
        else if (a[0] == "racestat") {
            int count = 1, pos = MyRacePosition(&count);
            Log("racestat: phase %d %s host %d cp %d/%d finish %d pos %d/%d veh %d props %d coop kind %d status %d startsIn %lld", g_race.phase, g_race.def.name.c_str(), (int)g_race.host,
                g_race.cp, g_race.total, g_race.finishMs, pos, count, g_race.veh, (int)g_race.props.size(), g_coop.kind, g_coop.status, g_coop.startsIn);
        }
        else if (a[0] == "jobjoin") { CoopJoin((unsigned)std::strtoul(rest.c_str(), nullptr, 10)); }
        else if (a[0] == "intscan" && a.size() > 1) {   // intscan <interior id> [half-size]: floor heights inside that interior, on a 3 m grid
            const InteriorDef *d = FindInterior(a[1].c_str());
            if (d) {
                float half = a.size() > 2 ? F(2) : 24.f;
                int me = Me();
                Vec3 back = Pos(me); float bh = N<float>(N_GET_ENTITY_HEADING, me);
                int id = LoadInterior(*d);
                N(N_SET_ENTITY_COORDS, me, d->x, d->y, d->z + 1.f, 0, 0, 0, 0);
                N(N_FREEZE_ENTITY_POSITION, me, 1);
                for (int i = 0; id && i < 100 && !NB(N<int>(N_IS_INTERIOR_READY, id)); ++i) Wait(50);
                N(N_REQUEST_COLLISION_AT_COORD, d->x, d->y, d->z);
                Wait(2000);
                int found = 0;
                for (float dx = -half; dx <= half; dx += 3.f)
                    for (float dy = -half; dy <= half; dy += 3.f) {
                        float x = d->x + dx, y = d->y + dy;
                        for (float top = d->z + 12.f; top > d->z - 10.f;) {
                            float gz = GroundAt(x, y, top, -1000.f);
                            if (gz < d->z - 10.f) break;
                            if (N<int>(N_GET_INTERIOR_AT_COORDS, x, y, gz + 1.f) == id) { Log("intscan %s: %.1f %.1f %.2f", d->id, x, y, gz); ++found; }
                            top = gz - 1.0f;
                        }
                    }
                Log("intscan %s: interior %d ready %d, %d floor points", d->id, id, id ? (int)NB(N<int>(N_IS_INTERIOR_READY, id)) : 0, found);
                N(N_FREEZE_ENTITY_POSITION, me, 0);
                Teleport(back.x, back.y, back.z, bh);
            }
        }
        else if (a[0] == "probez" && a.size() > 2) {   // probez x y: ground and water height there (designing races)
            float x = F(1), y = F(2), wz = 0.f;
            N(N_REQUEST_COLLISION_AT_COORD, x, y, 0.f);
            float gz = GroundAt(x, y, 1000.f, -1000.f);
            bool water = NB(N<int>(N_GET_WATER_HEIGHT, x, y, 100.f, &wz));
            Log("probez %.1f %.1f: ground %.2f water %s %.2f", x, y, gz, water ? "yes" : "no", wz);
        }
        else if (a[0] == "holescan" && a.size() > 7) {   // holescan u0 v0 u1 v1 step ztop zmin: points of the MHC block (tower frame) with no ground, or ground below zmin
            float u0 = F(1), v0 = F(2), u1 = F(3), v1 = F(4), st = std::max(0.2f, F(5)), zt = F(6), zmin = F(7);
            float r = MHC_GARAGE[0][3] * 3.14159265f / 180.f, c = std::cos(r), s = std::sin(r);
            int n = 0, holes = 0;
            for (float v = v0; v <= v1; v += st) for (float u = u0; u <= u1; u += st) {
                float x = MHC_GARAGE[0][0] + u * c - v * s, y = MHC_GARAGE[0][1] + u * s + v * c;
                if (++n % 300 == 0) { N(N_REQUEST_COLLISION_AT_COORD, x, y, zt); Wait(0); }
                float gz = GroundAt(x, y, zt, -1000.f);
                if (gz < zmin && holes++ < 600) Log("hole %.1f %.1f %.2f", u, v, gz);
            }
            Log("holescan done: %d points, %d holes", n, holes);
        }
        else if (a[0] == "intsets" && a.size() > 4) {   // intsets x y z set1;set2;!set3: entity sets on the interior there (exploring)
            int id = N<int>(N_GET_INTERIOR_AT_COORDS, F(1), F(2), F(3));
            if (id) {
                N(N_PIN_INTERIOR_IN_MEMORY, id);
                int on = 0;
                for (auto &st : Split(a[4], ';')) {
                    if (st.empty()) continue;
                    if (st[0] == '!') { N(N_DEACTIVATE_INTERIOR_ENTITY_SET, id, st.c_str() + 1); continue; }
                    N(N_ACTIVATE_INTERIOR_ENTITY_SET, id, st.c_str());
                    on += NB(N<int>(N_IS_INTERIOR_ENTITY_SET_ACTIVE, id, st.c_str())) ? 1 : 0;
                }
                N(N_REFRESH_INTERIOR, id);
                Log("intsets %d: %d of the sets active", id, on);
            }
        }
        else if (a[0] == "forceroom" && a.size() > 2) {   // forceroom <interior> <room key hex>: put the player and the camera in that room (exploring)
            int id = std::atoi(a[1].c_str()); uint32_t key = (uint32_t)std::strtoul(a[2].c_str(), nullptr, 16);
            N(N_FORCE_ROOM_FOR_ENTITY, Me(), id, key);
            N(N_FORCE_ROOM_FOR_GAME_VIEWPORT, id, key);
            Log("forceroom %d 0x%08X: player interior %d room 0x%08X", id, key, N<int>(N_GET_INTERIOR_FROM_ENTITY, Me()), (unsigned)N<int>(N_GET_ROOM_KEY_FROM_ENTITY, Me()));
        }
        else if (a[0] == "intinfo") {   // intinfo [x y z]: the interior there (default: the player), and the player's room
            Vec3 p = a.size() > 3 ? V3(F(1), F(2), F(3)) : Pos(Me());
            int id = N<int>(N_GET_INTERIOR_AT_COORDS, p.x, p.y, p.z), mine = N<int>(N_GET_INTERIOR_FROM_ENTITY, Me());
            Log("intinfo at %.1f %.1f %.1f: interior %d valid %d ready %d | player interior %d room 0x%08X", p.x, p.y, p.z, id,
                id ? (int)NB(N<int>(N_IS_VALID_INTERIOR, id)) : 0, id ? (int)NB(N<int>(N_IS_INTERIOR_READY, id)) : 0, mine, (unsigned)N<int>(N_GET_ROOM_KEY_FROM_ENTITY, Me()));
        }
        else if (a[0] == "intinfo2" && a.size() > 4) {   // intinfo2 x y z archetype: the interior of that type there, pinned; where it is and its state
            int id = N<int>(N_GET_INTERIOR_AT_COORDS_WITH_TYPE, F(1), F(2), F(3), a[4].c_str());
            if (id) N(N_PIN_INTERIOR_IN_MEMORY, id);
            Wait(3000);
            Vec3 at{}; uint32_t nh = 0;
            if (id) N(N_GET_INTERIOR_LOCATION_AND_NAMEHASH, id, &at, &nh);
            Log("intinfo2 %s: interior %d valid %d ready %d disabled %d at %.2f %.2f %.2f name 0x%08X (want 0x%08X)", a[4].c_str(), id,
                id ? (int)NB(N<int>(N_IS_VALID_INTERIOR, id)) : 0, id ? (int)NB(N<int>(N_IS_INTERIOR_READY, id)) : 0, id ? (int)NB(N<int>(N_IS_INTERIOR_DISABLED, id)) : 0,
                at.x, at.y, at.z, nh, Joaat(a[4].c_str()));
        }
        else if (a[0] == "app" && a.size() > 1) {   // app securo|datanet: open the business computer (test)
            if (a[1] == "securo") BizOpenSecuroServ(FindProp("Mile High Club Office"));
            else BizOpenDataNet(FindProp("Mile High Data Center"));
        }
        else if (a[0] == "dims") {   // dims <model>...: bounding box
            for (size_t k = 1; k < a.size(); ++k) {
                Vec3 mn{}, mx{};
                N(N_GET_MODEL_DIMENSIONS, Joaat(a[k].c_str()), &mn, &mx);
                Log("dims %s: %.2f %.2f %.2f .. %.2f %.2f %.2f", a[k].c_str(), mn.x, mn.y, mn.z, mx.x, mx.y, mx.z);
            }
        }
        else if (a[0] == "txd") {   // txd <dict>...: can a texture dictionary stream
            for (size_t k = 1; k < a.size(); ++k) {
                N(N_REQUEST_STREAMED_TEXTURE_DICT, a[k].c_str(), 0);
                int t0 = Now(); while (!NB(N<int>(N_HAS_STREAMED_TEXTURE_DICT_LOADED, a[k].c_str())) && Now() - t0 < 4000) Wait(0);
                Log("txd %s: loaded %d", a[k].c_str(), (int)NB(N<int>(N_HAS_STREAMED_TEXTURE_DICT_LOADED, a[k].c_str())));
            }
        }
        else if (a[0] == "mdl") {   // mdl <model>...: is it registered (an archetype), can it load
            for (size_t k = 1; k < a.size(); ++k) {
                uint32_t h = Joaat(a[k].c_str());
                bool cd = NB(N<int>(N_IS_MODEL_IN_CDIMAGE, h)), valid = NB(N<int>(N_IS_MODEL_VALID, h));
                if (cd) { N(N_REQUEST_MODEL, h); int t0 = Now(); while (!NB(N<int>(N_HAS_MODEL_LOADED, h)) && Now() - t0 < 4000) Wait(0); }
                Log("mdl %s: cdimage %d valid %d loaded %d", a[k].c_str(), cd, valid, (int)NB(N<int>(N_HAS_MODEL_LOADED, h)));
            }
        }
        else if (a[0] == "iplreq") { N(N_REQUEST_IPL, rest.c_str()); Wait(1000); Log("ipl %s active=%d", rest.c_str(), N<int>(N_IS_IPL_ACTIVE, rest.c_str())); }
        else if (a[0] == "tpx") { N(N_SET_ENTITY_COORDS, Me(), F(1), F(2), F(3), 0, 0, 0, 0); N(N_SET_ENTITY_HEADING, Me(), F(4)); }   // exact, no ground snap (interiors)
        else if (a[0] == "spvreq") { for (int k = 0; k < SPV_COUNT; ++k) if (rest == SPV[k].key) SpvRequest(k); }
        else if (a[0] == "spvout") SpvExit();
        else if (a[0] == "scr") Log("scr %s: %d threads", rest.c_str(), N<int>(N_GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH, Joaat(rest.c_str())));
        else if (a[0] == "turret") TurretStart(rest == "Terrorbyte" ? SPV_TERRORBYTE : SPV_AVENGER);
        else if (a[0] == "drone") DroneStart();
        else if (a[0] == "remoteoff") { TurretStop(); DroneStop(); }
        else if (a[0] == "probe" && a.size() > 2) {   // probe <model> <bone|weapon>...: bone indices / weapon validity
            uint32_t m = Joaat(a[1].c_str());
            int v = 0;
            if (LoadModel(m)) { Vec3 p = Pos(Me()); v = N<int>(N_CREATE_VEHICLE, m, p.x + 15.f, p.y, p.z + 1.f, 0.f, 0, 0, 0); }
            for (size_t i = 2; i < a.size(); ++i) {
                if (a[i].rfind("VEHICLE_WEAPON_", 0) == 0 || a[i].rfind("WEAPON_", 0) == 0) Log("probe weapon %s: valid %d", a[i].c_str(), N<int>(N_IS_WEAPON_VALID, Joaat(a[i].c_str())));
                else if (v) { int b = N<int>(N_GET_ENTITY_BONE_INDEX_BY_NAME, v, a[i].c_str()); Vec3 w = b >= 0 ? N<Vec3>(N_GET_WORLD_POSITION_OF_ENTITY_BONE, v, b) : Vec3{}; Vec3 o = b >= 0 ? N<Vec3>(N_GET_OFFSET_FROM_ENTITY_GIVEN_WORLD_COORDS, v, w.x, w.y, w.z) : Vec3{}; Log("probe bone %s: %d at offset %.2f %.2f %.2f", a[i].c_str(), b, o.x, o.y, o.z); }
            }
            if (v) { N(N_SET_ENTITY_AS_MISSION_ENTITY, v, 1, 1); N(N_DELETE_VEHICLE, &v); }
        }
        else if (a[0] == "seats") {   // seats <model>: spawn it nearby, log its seats (turret seats) and weapons
            uint32_t m = Joaat(rest.c_str());
            if (LoadModel(m)) {
                Vec3 p = Pos(Me());
                int v = N<int>(N_CREATE_VEHICLE, m, p.x + 15.f, p.y, p.z + 1.f, 0.f, 0, 0, 0);
                int n = N<int>(N_GET_VEHICLE_MODEL_NUMBER_OF_SEATS, m);
                std::string seats;
                for (int i = -1; i < n - 1; ++i) seats += std::to_string(i) + (NB(N<int>(N_IS_TURRET_SEAT, v, i)) ? "T " : " ");
                Log("seats %s: %d seats: %s (vehicle %d)", rest.c_str(), n, seats.c_str(), v);
                N(N_SET_ENTITY_AS_MISSION_ENTITY, v, 1, 1); N(N_DELETE_VEHICLE, &v);
            }
        }
        else if (a[0] == "inthead") {   // inthead x y z: the interior there, its position and heading
            int id = N<int>(N_GET_INTERIOR_AT_COORDS, F(1), F(2), F(3));
            Vec3 pos{}; uint32_t name = 0;
            if (id) N(N_GET_INTERIOR_LOCATION_AND_NAMEHASH, id, &pos, &name);
            Log("inthead: interior %d at %.3f %.3f %.3f heading %.3f name %08X", id, pos.x, pos.y, pos.z, id ? N<float>(N_GET_INTERIOR_HEADING, id) : 0.f, name);
        }
        else if (a[0] == "objat" && a.size() > 5) {   // objat <model> x y z heading: a test object (objclear removes them)
            static std::vector<int> objs;
            uint32_t m = Joaat(a[1].c_str());
            if (LoadModel(m)) { int o = N<int>(N_CREATE_OBJECT_NO_OFFSET, m, F(2), F(3), F(4), 0, 0, 0, 0); N(N_SET_ENTITY_HEADING, o, F(5)); N(N_FREEZE_ENTITY_POSITION, o, 1); objs.push_back(o); Log("objat %s -> %d", a[1].c_str(), o); }
            else Log("objat %s: model not found", a[1].c_str());
            g_testObjs = &objs;
        }
        else if (a[0] == "objclear") { if (g_testObjs) { for (int &o : *g_testObjs) N(N_DELETE_OBJECT, &o); g_testObjs->clear(); } }
        else if (a[0] == "yachtcfg" && a.size() > 1) { g_cust[YACHT_CFG] = (int)F(1); if (a.size() > 2) g_cust[YACHT_SPOT] = (int)F(2); else if (!g_cust.count(YACHT_SPOT)) g_cust[YACHT_SPOT] = YachtNearestSpot(); YachtUnload(); Save(); }   // test (delyacht undoes it)
        else if (a[0] == "delyacht") { YachtUnload(); g_cust.erase(YACHT_CFG); g_cust.erase(YACHT_SPOT); g_yachtName.clear(); g_yBlipSpot = -1; RemoveBlip(g_yBlip); Save(); Log("delyacht"); }
        else if (a[0] == "yachtgo") YachtTravel();
        else if (a[0] == "balreq") { g_balNextAt = 0; BallisticRequest(); }
        else if (a[0] == "balon") BallisticOn();
        else if (a[0] == "baloff") BallisticOff(false);
        else if (a[0] == "spvcfg" && a.size() > 2) { for (int k = 0; k < SPV_COUNT; ++k) if (a[1] == SPV[k].key) g_cust[SpvKey(k)] = (int)F(2); Log("spvcfg %s %d", a[1].c_str(), (int)F(2)); }   // test layouts (undo with delspv)
        else if (a[0] == "spvin") { for (int k = 0; k < SPV_COUNT; ++k) if (rest == SPV[k].key) SpvEnter(k); }   // spvin <key>: walk into it as configured
        else if (a[0] == "venture" && a.size() > 1) {   // venture salvage|bounty|sabotage: that business job from the first such property (test)
            int t = a[1] == "salvage" ? T_SALVAGE : a[1] == "bounty" ? T_BAIL : T_GARMENT, pi = -1;
            for (int i = 0; i < NPROPS && pi < 0; ++i) if (PROPS[i].type == t) pi = i;
            if (pi >= 0) { if (t == T_SALVAGE) SalvageRobbery(pi); else if (t == T_BAIL) BountyTarget(pi); else SabotageJob(pi); }
        }
        else if (a[0] == "buyprop") { int i = FindProp(rest); if (i >= 0 && !Owns(PROPS[i].name)) { g_props.push_back(PROPS[i].name); Save(); RefreshPropertyBlips(); } Log("buyprop %s -> %d", rest.c_str(), i); }
        else if (a[0] == "props") { for (int i = 0; i < NPROPS; ++i) if (PROPS[i].ob >= 0) { const OBuilding &b = *BldAt(PROPS[i].ob); Log("prop %d %s type %d ob %d (id %d) walk %.2f %.2f %.2f cars %d int %s", i, PROPS[i].name, PROPS[i].type, PROPS[i].ob, b.id, b.e[0].mx, b.e[0].my, b.e[0].mz, GarageSlots(i), PROPS[i].interior); } }
        else if (a[0] == "door") {   // stand in the exit area at the front door of the current GTA Online interior
            Vec3 pa, pb; float h;
            if (g_inside >= 0 && OnlinePoint(g_inside, 1, pa, h) && OnlinePoint(g_inside, 2, pb, h)) {
                Vec3 m = V3((pa.x + pb.x) * 0.5f, (pa.y + pb.y) * 0.5f, std::min(pa.z, pb.z) + 0.2f);
                N(N_SET_ENTITY_COORDS, Me(), m.x, m.y, m.z, 0, 0, 0, 0);
                Wait(500);
                Log("door: area %.2f %.2f %.2f - %.2f %.2f %.2f, in area %d", pa.x, pa.y, pa.z, pb.x, pb.y, pb.z, InOnlineExitArea(g_inside, Me()) ? 1 : 0);
            } else Log("door: not inside a GTA Online interior");
        }
        else if (a[0] == "leave") { if (g_inside >= 0) ExitProperty(); }
        else if (a[0] == "aptact" && a.size() > 1) AptActCommand(a[1]);   // aptact <T|C|B|W|S|D|P>: the apartment activity (test)
        else if (a[0] == "outfitclear" && a.size() > 1) { int i = std::atoi(a[1].c_str()) - 1; if (i >= 0 && i < 6) { g_outfit[i].clear(); Save(); } }   // a wardrobe slot (1-6) back to empty (tests)
        else if (a[0] == "seatpose" && a.size() > 2) { g_seatFwd = (float)std::atof(a[1].c_str()); g_seatUp = (float)std::atof(a[2].c_str()); if (a.size() > 3) g_seatScen = a[3]; }
        else if (a[0] == "bedpose" && a.size() > 3) { g_bedAlong = (float)std::atof(a[1].c_str()); g_bedUp = (float)std::atof(a[2].c_str()); g_bedTurn = (float)std::atof(a[3].c_str()); }
        else if (a[0] == "garexit") { if (g_garProp >= 0) LeaveGarageOnFoot(false); }
        else if (a[0] == "meta") {   // meta <map> <from> [to]: GTA Online interior template points (metadata)
            int map = std::atoi(a[1].c_str()), from = std::atoi(a[2].c_str()), to = a.size() > 3 ? std::atoi(a[3].c_str()) : from;
            for (int id = from; id <= to; ++id) {
                struct { float x, p0, y, p1, z, p2; } pos = {}, rot = {};
                int ok = N<int>(N_GET_BASE_ELEMENT_LOCATION_FROM_METADATA_BLOCK, &pos, &rot, id, map);
                Log("meta map %d point %d: ok %d pos %.4f %.4f %.4f rot %.2f %.2f %.2f", map, id, ok, pos.x, pos.y, pos.z, rot.x, rot.y, rot.z);
            }
        }
        else if (a[0] == "testint") {   // testint [from] [count]: every Online interior: loaded, standing on a floor
            int from = a.size() > 1 ? std::atoi(a[1].c_str()) : 0, cnt = a.size() > 2 ? std::atoi(a[2].c_str()) : 999, done = 0;
            for (int i = from; i < NPROPS && done < cnt; ++i) {
                if (PROPS[i].ob < 0) continue;
                const InteriorDef *d = PropInterior(i);
                if (!d) continue;
                ++done;
                InteriorDef cd = *d;
                TeleportInside(cd);
                Wait(1200);
                Vec3 p = Pos(Me());
                int in = N<int>(N_GET_INTERIOR_FROM_ENTITY, Me());
                float gz = GroundAt(p.x, p.y, p.z + 1.f, -1000.f);
                Log("testint %d %s: interior %d, at %.2f %.2f %.2f (target z %.2f, ground %.2f) %s", i, PROPS[i].name, in, p.x, p.y, p.z, d->z, gz,
                    in && std::fabs(p.z - 1.f - gz) < 1.5f && std::fabs(p.z - d->z) < 4.f ? "OK" : "CHECK");
                N(N_DO_SCREEN_FADE_IN, 200);
            }
        }
        else if (a[0] == "testents") {   // testents [from] [count]: every Online entrance marker is on walkable ground
            int from = a.size() > 1 ? std::atoi(a[1].c_str()) : 0, cnt = a.size() > 2 ? std::atoi(a[2].c_str()) : 999;
            for (int bi = from; bi < NO_BUILDINGS && bi < from + cnt; ++bi) {
                const OBuilding &b = O_BUILDINGS[bi];
                for (int k = 0; k < b.ne; ++k) {
                    const OEntrance &e = b.e[k];
                    N(N_SET_ENTITY_COORDS, Me(), e.mx, e.my, e.mz + 1.0f, 0, 0, 0, 0);
                    N(N_REQUEST_COLLISION_AT_COORD, e.mx, e.my, e.mz);
                    for (int w = 0; w < 30 && !NB(N<int>(N_HAS_COLLISION_LOADED_AROUND_ENTITY, Me())); ++w) Wait(100);
                    Wait(400);
                    float gz = GroundAt(e.mx, e.my, e.mz + 1.5f, -1000.f);
                    Log("testents building %d entrance %d kind %d: marker %.2f %.2f %.2f ground %.2f %s", b.id, k, e.kind, e.mx, e.my, e.mz, gz, std::fabs(gz - e.mz) < 0.6f ? "OK" : "CHECK");
                }
            }
        }
        else if (a[0] == "entrance") {   // entrance [garage] <property>: stand on GTA Online's marker
            bool gar = rest.rfind("garage ", 0) == 0;
            int i = FindProp(gar ? rest.substr(7) : rest);
            const OEntrance *e = EntranceOf(BuildingOf(i), gar ? 2 : 1);
            if (e) TeleportExact(e->mx, e->my, e->mz, e->mh);
            Log("entrance %s -> %d", rest.c_str(), i);
        }
        else if (a[0] == "goto") { int i = FindProp(rest); if (i >= 0) GoToProperty(i); }
        else if (a[0] == "garage") { int i = FindProp(rest); if (i >= 0) EnterGarage(i, 0); }
        else if (a[0] == "mpmap") ApplyOnlineMap(true);
        else if (a[0] == "time") N(N_SET_CLOCK_TIME, (int)F(1), 0, 0);
        else if (a[0] == "camh") N(N_SET_GAMEPLAY_CAM_RELATIVE_HEADING, F(1));
        else if (a[0] == "pitch") N(N_SET_GAMEPLAY_CAM_RELATIVE_PITCH, F(1), 1.0f);
        else if (a[0] == "heading") N(N_SET_ENTITY_HEADING, Me(), F(1));
        else if (a[0] == "car") { uint32_t m = Joaat(rest.c_str()); if (LoadModel(m)) { Vec3 p = Pos(Me()); int v = N<int>(N_CREATE_VEHICLE, m, p.x, p.y, p.z, N<float>(N_GET_ENTITY_HEADING, Me()), 0, 0, 0); N(N_SET_PED_INTO_VEHICLE, Me(), v, -1); } }
        else if (a[0] == "park") { int i = FindProp(rest); if (i >= 0 && InAnyVehicle()) EnterGarage(i, MyVehicle()); }
        else if (a[0] == "vmod" && InAnyVehicle()) { ApplyVehicle(MyVehicle(), rest); Log("vmod -> %s", CaptureVehicle(MyVehicle()).c_str()); }
        else if (a[0] == "cap" && InAnyVehicle()) Log("cap %s %s", ModelNameOf(MyVehicle()).c_str(), CaptureVehicle(MyVehicle()).c_str());
        else if (a[0] == "garinfo") { Log("garage prop %d, %d saved vehicles", g_garProp, (int)g_garage.size()); for (int k = 0; k < 10; ++k) if (g_garCar[k]) Log(" slot %d: %s %s", k, g_garage[g_garIdx[k]].model.c_str(), CaptureVehicle(g_garCar[k]).c_str()); }
        else if (a[0] == "driveout") { int k = (int)F(1); if (g_garProp >= 0 && g_garCar[k]) { N(N_SET_PED_INTO_VEHICLE, Me(), g_garCar[k], -1); Wait(300); DriveOut(k); Wait(1500); Vec3 p = Pos(Me()); Log("drove out at %.1f %.1f %.1f, in vehicle %d: %s", p.x, p.y, p.z, InAnyVehicle() ? 1 : 0, InAnyVehicle() ? CaptureVehicle(MyVehicle()).c_str() : "-"); } }
        else if (a[0] == "inside") { const InteriorDef *d = FindInterior(rest.c_str()); if (d) { TeleportInside(*d); Vec3 p = Pos(Me()); int id = N<int>(N_GET_INTERIOR_AT_COORDS, p.x, p.y, p.z); Log("inside %s: at %.2f %.2f %.2f interior %d", d->id, p.x, p.y, p.z, id); } }
        else if (a[0] == "casino") { const InteriorDef *d = FindInterior("casino"); if (d) TeleportInside(*d); }
        else if (a[0] == "chips") { g_chips = (int)F(1); Save(); }
        else if (a[0] == "wheelreset") { g_wheelNext = 0; Save(); }
        else if (a[0] == "water") { float h = 0.f; int r = N<int>(N_GET_WATER_HEIGHT, F(1), F(2), F(3), &h); Log("water at %.1f %.1f: %d height %.2f", F(1), F(2), r, h); }
        else if (a[0] == "fx" && a.size() > 2) {   // fx <asset> <effect>: a looped particle effect 2 m ahead (finding effects); fx off
            static int fxh = 0;
            if (fxh) { N(N_STOP_PARTICLE_FX_LOOPED, fxh, 0); fxh = 0; }
            N(N_REQUEST_NAMED_PTFX_ASSET, a[1].c_str());
            for (int k = 0; k < 100 && !NB(N<int>(N_HAS_NAMED_PTFX_ASSET_LOADED, a[1].c_str())); ++k) Wait(20);
            Vec3 p = Ahead(Pos(Me()), N<float>(N_GET_ENTITY_HEADING, Me()), 2.f);
            N(N_USE_PARTICLE_FX_ASSET, a[1].c_str());
            fxh = N<int>(N_START_PARTICLE_FX_LOOPED_AT_COORD, a[2].c_str(), p.x, p.y, p.z + 1.f, 0.f, 0.f, 0.f, 1.f, 0, 0, 0, 1);
            Log("fx %s/%s: loaded %d handle %d", a[1].c_str(), a[2].c_str(), NB(N<int>(N_HAS_NAMED_PTFX_ASSET_LOADED, a[1].c_str())) ? 1 : 0, fxh);
        }
        else if (a[0] == "ray" && a.size() > 6) {   // ray x1 y1 z1 x2 y2 z2: the first thing in the way (placing doors)
            int r = N<int>(N_START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE, F(1), F(2), F(3), F(4), F(5), F(6), 1 | 16, Me(), 7);
            int hit = 0; Vec3 end{}, nrm{}; int ent = 0;
            if (r) N<int>(N_GET_SHAPE_TEST_RESULT, r, &hit, &end, &nrm, &ent);
            Log("ray: hit %d at %.2f %.2f %.2f normal %.2f %.2f %.2f ground %.2f", hit, end.x, end.y, end.z, nrm.x, nrm.y, nrm.z, GroundAt(end.x, end.y, end.z + 1.f, 0.f));
        }
        else if (a[0] == "hgrid" && a.size() > 6) {   // hgrid x0 y0 x1 y1 step ztop: the map's height and surface material on a grid (site surveys) -> hgrid.txt
            float x0 = F(1), y0 = F(2), x1 = F(3), y1 = F(4), st = std::max(0.5f, F(5)), zt = F(6);
            FILE *hf = std::fopen((g_dir + "hgrid.txt").c_str(), "w");
            int n = 0;
            for (float y = y0; y <= y1 && hf; y += st)
                for (float x = x0; x <= x1; x += st) {
                    int r = N<int>(N_START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE, x, y, zt, x, y, zt - 120.f, 1, 0, 7);
                    int hit = 0, ent = 0; uint32_t mat = 0; Vec3 end{}, nrm{};
                    if (r) N<int>(N_GET_SHAPE_TEST_RESULT_INCLUDING_MATERIAL, r, &hit, &end, &nrm, &mat, &ent);
                    std::fprintf(hf, "%.1f %.1f %.2f %08X %.2f\n", x, y, hit ? end.z : -999.f, mat, nrm.z);
                    if (++n % 400 == 0) Wait(0);
                }
            if (hf) std::fclose(hf);
            Log("hgrid: %d points", n);
        }
        else if (a[0] == "naptphoto" && a.size() > 1) {   // naptphoto <id>: a camera on the street looking at a new apartment's building (Dynasty 8 pictures)
            for (int k = 0; k < (int)g_naptProp.size(); ++k) if (a[1] == NEW_APTS[k].id) {
                const NewAptPlace *p = NaptPlaceOf(k);
                if (!g_naptResolved[k]) { Teleport(p->fx, p->fy - p->hy - 8.f, p->z, 0.f); Wait(3000); NaptResolve(k); }
                const OEntrance &e = g_naptBld[k].e[0];
                float dx = e.mx - p->fx, dy = e.my - p->fy, len = std::max(1.f, std::hypot(dx, dy));
                bool tower = !std::strncmp(p->kind, "loft", 4) || !std::strncmp(p->kind, "ocean", 5) || p->kind[0] == 'e' || p->kind[0] == 'h';
                // towers: from across the street, aimed half way up to the apartment's floor
                float back = tower ? 70.f : 16.f + std::max(p->hx, p->hy), up = tower ? 6.f : 4.f, aim = tower ? std::max(15.f, (p->z - e.mz) * 0.55f) : 2.5f;
                Vec3 c = V3(e.mx + dx / len * back, e.my + dy / len * back, e.mz + up);
                Vec3 at = V3(p->fx, p->fy, e.mz + aim);
                static int cam = 0;
                if (!cam || !NB(N<int>(N_DOES_CAM_EXIST, cam))) cam = N<int>(N_CREATE_CAM, "DEFAULT_SCRIPTED_CAMERA", 1);
                N(N_SET_CAM_COORD, cam, c.x, c.y, c.z);
                N(N_POINT_CAM_AT_COORD, cam, at.x, at.y, at.z);
                N(N_SET_CAM_FOV, cam, tower ? 55.f : 50.f);
                N(N_SET_CAM_ACTIVE, cam, 1);
                N(N_RENDER_SCRIPT_CAMS, 1, 0, 0, 1, 0, 0);
                N(N_SET_FOCUS_POS_AND_VEL, c.x, c.y, c.z, 0.f, 0.f, 0.f);
                Log("naptphoto %s: camera %.1f %.1f %.1f", a[1].c_str(), c.x, c.y, c.z);
                break;
            }
        }
        else if (a[0] == "napt" && a.size() > 1) {   // napt <id> [in]: go to a new apartment's door (in: inside, at GTA Online's entry spot)
            for (int k = 0; k < (int)g_naptProp.size(); ++k) if (a[1] == NEW_APTS[k].id) {
                if (a.size() > 2 && a[2] == "in") { EnterProperty(g_naptProp[k]); break; }
                const NewAptPlace *p = NaptPlaceOf(k);
                if (!g_naptResolved[k]) { Teleport(p->fx, p->fy - p->hy - 8.f, p->z, 0.f); Wait(3000); NaptResolve(k); }
                const OEntrance &e = g_naptBld[k].e[0];
                TeleportExact(e.mx, e.my, e.mz + 1.f, e.mh);
                break;
            }
        }
        else if (a[0] == "knoway") {   // knoway [go | wp x y]: the robotaxi fleet; go = stand next to the nearest one, wp = set a waypoint (testing)
            if (a.size() > 3 && a[1] == "wp") N(N_SET_NEW_WAYPOINT, (float)F(2), (float)F(3));
            Vec3 me = Pos(Me()); int best = -1; float bd = 1e9f;
            for (int i = 0; i < (int)g_kw.size(); ++i) {
                float d = Dist(Pos(g_kw[i].veh), me);
                Log("knoway %d: %.0f m, speed %.1f, hired %d", i, d, N<float>(N_GET_ENTITY_SPEED, g_kw[i].veh), g_kw[i].hired ? 1 : 0);
                if (d < bd) { bd = d; best = i; }
            }
            Log("knoway: %d cars, ride %d, available %d", (int)g_kw.size(), g_kwRide, KnowayAvailable() ? 1 : 0);
            if (a.size() > 1 && a[1] == "go" && best >= 0) {
                int v = g_kw[best].veh;
                N(N_TASK_VEHICLE_TEMP_ACTION, g_kw[best].driver, v, 6, 8000);
                Vec3 p = Pos(v); float h = N<float>(N_GET_ENTITY_HEADING, v) * 3.14159265f / 180.f;
                TeleportExact(p.x + std::cos(h) * 3.f, p.y + std::sin(h) * 3.f, p.z, 0.f);
            }
        }
        else if (a[0] == "traffic") Log("traffic: %d swaps, %d models streamed, %d seen", g_tSwapped, (int)g_tActive.size(), (int)g_tSeen.size());
        else if (a[0] == "realweb") { RealWebOpen(a.size() > 1 ? a[1] : "WWW_EYEFIND_INFO"); }
        else if (a[0] == "webclose") RWClose();
        else if (a[0] == "webin") { if (g_rw.open) { RWInput(16); RWCall("GET_CURRENT_SELECTION"); g_rw.selRv = N<int>(N_END_SCALEFORM_MOVIE_METHOD_RETURN_VALUE); } }
        else if (a[0] == "webmouse") { if (g_rw.open) { RWCall("SET_MOUSE_INPUT"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, (float)F(1)); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, (float)F(2)); RWEnd(); g_rw.lastMx = (float)F(1); g_rw.lastMy = (float)F(2); g_rw.mouseHoldUntil = Now() + 4000; } }
        else if (a[0] == "webbuy") {   // webbuy <model> [colour 0-7]: what ORDER on that vehicle's page does
            for (int i = 0; i < NWEB_VEHICLES; ++i)
                if (rest.rfind(WEB_VEHICLES[i].model, 0) == 0 && (rest.size() == std::strlen(WEB_VEHICLES[i].model) || rest[std::strlen(WEB_VEHICLES[i].model)] == ' ')) {
                    if (!g_rw.open) RealWebOpen(WEB_SITE_NAMES.at(WEB_VEHICLES[i].site));
                    RWBuyVehicle(WEB_VEHICLES[i], a.size() > 2 ? std::atoi(a[2].c_str()) : -1);
                    break;
                }
        }
        else if (a[0] == "phonescan") {   // phonescan [from to]: globals holding "CELL_" labels (find the phone's app table)
            int from = a.size() > 2 ? std::atoi(a[1].c_str()) : 8000, to = a.size() > 2 ? std::atoi(a[2].c_str()) : 12000, n = 0;
            for (int g = from; g < to && n < 80; ++g) {
                uint64_t *p = GlobalPtr(g);
                if (!p) break;
                char buf[17] = {}; std::memcpy(buf, p, 16);
                if (!std::strncmp(buf, "CELL_", 5)) { Log("phonescan: Global_%d = '%s'", g, buf); ++n; }
            }
            Log("phonescan: %d hits in %d..%d", n, from, to);
        }
        else if (a[0] == "globals") {   // globals <from> <count>: raw values (find arrays in this build)
            int from = (int)F(1), cnt = a.size() > 2 ? (int)F(2) : 40;
            std::string line;
            for (int g = from; g < from + cnt; ++g) {
                uint64_t *p = GlobalPtr(g);
                if (!p) break;
                line += std::to_string(g) + "=" + std::to_string((long long)*p) + " ";
                if ((g - from) % 10 == 9) { Log("globals: %s", line.c_str()); line.clear(); }
            }
            if (!line.empty()) Log("globals: %s", line.c_str());
        }
        else if (a[0] == "phonepic" && a.size() > 2) {   // phonepic <contact> <label>: that contact's picture label (Lester's silhouette test)
            if (uint64_t *e = GlobalPtr(PH_CHARS + std::atoi(a[1].c_str()) * PH_CHAR_STRIDE)) {
                char pic[16] = {}; std::strncpy(pic, a[2].c_str(), 15); std::memcpy(e + 7, pic, 16);
                std::string txd = Label(pic);
                if (!txd.empty()) N(N_REQUEST_STREAMED_TEXTURE_DICT, txd.c_str(), 0);
                Log("phonepic %s -> %s (%s, loaded %d)", a[1].c_str(), pic, txd.c_str(), txd.empty() ? 0 : (int)NB(N<int>(N_HAS_STREAMED_TEXTURE_DICT_LOADED, txd.c_str())));
            }
        }
        else if (a[0] == "phonechars") {   // the contact charsheet (Global_2350 in 3889): index, name, picture, known by
            uint64_t *owner = GlobalPtr(21650);
            Log("phonechars: phone owner Global_21650 = %lld", owner ? (long long)*owner : -1LL);
            for (int i = 0; i < 222; ++i) {
                uint64_t *e = GlobalPtr(2350 + i * 29);
                if (!e) break;
                char lab[17] = {}, txd[17] = {}; std::memcpy(lab, e + 3, 16); std::memcpy(txd, e + 7, 16);
                if (!lab[0]) continue;
                Log("phonechars: %3d %-10s %-28s %-16s known %lld %lld %lld %lld (n=%lld)", i, lab, Label(lab).c_str(), txd,
                    (long long)e[20], (long long)e[21], (long long)e[22], (long long)e[23], (long long)e[19]);
            }
        }
        else if (a[0] == "webcat") {   // webcat: per website, how many Online vehicles this build sells / lacks
            std::map<int, std::pair<int, int>> n;
            std::string lacking;
            for (int i = 0; i < NWEB_VEHICLES; ++i) {
                const WebVehicle &v = WEB_VEHICLES[i];
                bool ok = WebVehiclePrice(v) >= 0;
                (ok ? n[v.site].first : n[v.site].second)++;
                if (!ok && lacking.find(v.model) == std::string::npos) lacking += std::string(" ") + v.model;
            }
            for (auto &e : n) Log("webcat: site %d: %d sold, %d hidden", e.first, e.second.first, e.second.second);
            Log("webcat: hidden:%s", lacking.c_str());
            for (int i = 0; i < NWEB_VEHICLES; ++i) {   // vehicles known only by hash: their in-game names
                const WebVehicle &v = WEB_VEHICLES[i];
                if (std::strncmp(v.model, "0x", 2)) continue;
                const CatCar *c = FindCar(v.model);
                Log("webcat: site %d page %d %s = %s (%s, class %d, price %d)", v.site, v.page, v.model, c ? c->name.c_str() : "?",
                    N<const char *>(N_GET_DISPLAY_NAME_FROM_VEHICLE_MODEL, ModelHash(v.model)), c ? c->cls : -1, c ? c->price : -1);
            }
        }
        else if (a[0] == "delveh") {   // delveh <model>: undo a test purchase (the newest one of that model)
            for (int i = (int)g_garage.size() - 1; i >= 0; --i)
                if (g_garage[i].model == rest) { g_garage.erase(g_garage.begin() + i); g_personalIdx = -1; Save(); Log("delveh %s: removed", rest.c_str()); break; }
        }
        else if (a[0] == "webslot") {   // webslot <slot> <int> [str]: SET_DATA_SLOT + UPDATE_TEXT (tests)
            if (g_rw.open) {
                RWCall("SET_DATA_SLOT"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, (int)F(1)); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, (int)F(2)); RWEnd();
                RWCall("UPDATE_TEXT"); RWEnd();
                Log("webslot %d %d", (int)F(1), (int)F(2));
            }
        }
        else if (a[0] == "webscroll") { if (g_rw.open) g_rw.scroll = (float)F(1); }
        else if (a[0] == "webgo") { if (g_rw.open) RWGo(a[1]); }
        else if (a[0] == "internet") { OpenInternet(); if (a.size() > 2) WebGo((int)F(1), (int)F(2), a.size() > 3 ? (int)F(3) : -1); }
        else if (a[0] == "mapcol" && a.size() > 2) { g_mapColours[(int)F(1)] = (int)F(2); BuildIslandMap(); }
        else if (a[0] == "where") { Vec3 p = Pos(Me()); Log("at %.2f %.2f %.2f heading %.1f ground %.2f interior %d", p.x, p.y, p.z, N<float>(N_GET_ENTITY_HEADING, Me()), GroundAt(p.x, p.y, p.z + 3.f, -999.f), N<int>(N_GET_INTERIOR_FROM_ENTITY, Me())); }
        else if (a[0] == "rmipl") { N(N_REMOVE_IPL, rest.c_str()); Wait(500); Log("ipl %s active=%d", rest.c_str(), N<int>(N_IS_IPL_ACTIVE, rest.c_str())); }
        else if (a[0] == "ipl") { N(N_REQUEST_IPL, rest.c_str()); Wait(500); Log("ipl %s active=%d", rest.c_str(), N<int>(N_IS_IPL_ACTIVE, rest.c_str())); }
        else if (a[0] == "iplq") Log("ipl %s active=%d", rest.c_str(), N<int>(N_IS_IPL_ACTIVE, rest.c_str()));
        else if (a[0] == "obj") {   // obj <archetype> [dx dy dz]: spawn one of our archetypes near the player
            uint32_t m = Joaat(a[1].c_str());
            Log("obj %s cdimage=%d valid=%d", a[1].c_str(), N<int>(N_IS_MODEL_IN_CDIMAGE, m), N<int>(N_IS_MODEL_VALID, m));
            if (LoadModel(m)) { Vec3 p = Pos(Me()); int o = N<int>(N_CREATE_OBJECT_NO_OFFSET, m, p.x + F(2), p.y + F(3), p.z + F(4), 0, 0, 0, 0); N(N_FREEZE_ENTITY_POSITION, o, 1); Log("obj created %d", o); }
            else Log("obj model did not load");
        }
        else if (a[0] == "cam") {   // cam x y z pitch heading [fov]: scripted camera for screenshots
            static int cam = 0;
            if (!cam || !NB(N<int>(N_DOES_CAM_EXIST, cam))) cam = N<int>(N_CREATE_CAM, "DEFAULT_SCRIPTED_CAMERA", 1);   // a death destroys script cameras
            N(N_SET_CAM_COORD, cam, F(1), F(2), F(3));
            N(N_SET_CAM_ROT, cam, F(4), 0.f, F(5), 2);
            N(N_SET_CAM_FOV, cam, a.size() > 6 ? F(6) : 60.f);
            N(N_SET_CAM_ACTIVE, cam, 1);
            N(N_RENDER_SCRIPT_CAMS, 1, 0, 0, 1, 0, 0);
            N(N_SET_FOCUS_POS_AND_VEL, F(1), F(2), F(3), 0.f, 0.f, 0.f);
        }
        else if (a[0] == "camoff") { N(N_RENDER_SCRIPT_CAMS, 0, 0, 0, 1, 0, 0); N(N_CLEAR_FOCUS); }
        else if (a[0] == "mhchide") MhcInit();
        else if (a[0] == "freeze") N(N_FREEZE_ENTITY_POSITION, Me(), (int)F(1));
        else if (a[0] == "god") { N(N_SET_ENTITY_INVINCIBLE, Me(), 1); }
        else if (a[0] == "wanted0") N(N_CLEAR_PLAYER_WANTED_LEVEL, Player());
    }
}

// ---------------------------------------------------------------- Main loop
static int g_backSince = -1;
static void OnKey(DWORD key, WORD, BYTE, BOOL, BOOL, BOOL wasDownBefore, BOOL isUpNow) {
    if (isUpNow || wasDownBefore) return;
    if (key == 'M') g_wantMenu = true;
}

static void ApplyOnlineMap(bool force) {
    if (!g_optMpMap && !force) return;
    Log("ON_ENTER_MP...");
    N(N_ON_ENTER_MP);
    Log("instance priority...");
    N(N_SET_INSTANCE_PRIORITY_MODE, 1);
    for (auto &ipl : Split(WORLD_IPLS, ';')) if (!ipl.empty()) { Log("ipl %s", ipl.c_str()); N(N_REQUEST_IPL, ipl.c_str()); }
    Log("online map loaded");
}

// Starting state of a new profile (story: what the first versions gave; YOU: a new Online player).
static void EnsureDefaults() {
    if (!g_starter) { g_starter = true; AddCash(50000); Save(); Notify("~g~Gori Online~s~: here's your ~g~$50,000~s~ starter bonus."); }
    if (!Owns("Isla Neon Villa")) { g_props.push_back("Isla Neon Villa"); Save(); }
}
// GTA Local Online switched between the story characters and YOU: put the other profile in place.
static void SwitchProfile(bool you) {
    Log("profile switch -> %s", you ? "YOU (Local Online)" : "story");
    if (!g_profileYou) { SyncPersonal(); Save(); }   // story -> YOU: the .ini gets the latest state
    if (g_mActive) { CoopReport(COOP_CLOSED, (int)g_mStep, Pos(Me()), ""); CleanupMission(); }
    ClearGarageCars(0);
    g_garProp = -1;
    if (Exists(g_personal)) { N(N_SET_ENTITY_AS_NO_LONGER_NEEDED, &g_personal); }   // left where it is, no longer ours
    RemoveBlip(g_personalBlip);
    g_personal = 0; g_personalIdx = -1;
    g_inside = -1;
    g_webOpen = false;
    if (g_menuOpen) CloseMenu();
    bool mpMap = g_optMpMap, island = g_optIsland;
    ResetState();
    g_profileYou = you;
    Load();
    g_optMpMap = mpMap; g_optIsland = island;
    if (!you) TrackCharacter();
    AssignGarages();
    EnsureDefaults();
    g_lastLevel = LevelOf(g_rp);
    Save();
    RefreshPropertyBlips();
    Notify(you ? "~g~Gori Online~s~: Local Online profile loaded (rank " + std::to_string(g_lastLevel) + ", " + std::to_string((int)g_props.size()) + " properties)."
               : "~g~Gori Online~s~: Story Mode profile loaded.");
}
static void ProfileTick() {
    bool you = YouActive();
    if (you != g_profileYou) SwitchProfile(you);
}

static void ScriptMain() {
    BuildProps();
    FcExtendProps();
    Load();
    g_lastLevel = LevelOf(g_rp);
    g_rand = (unsigned)GetTickCount();
    while (!NB(N<int>(N_IS_PLAYER_PLAYING, Player()))) Wait(500);
    Log("Gori Online v3 started (%d properties, %d vehicles owned)", NPROPS, (int)g_garage.size());
    TrackCharacter();
    Log("character %d", g_char);
    EnsureDefaults();
    // the Online map (ON_ENTER_MP) froze the game when called right at story-mode load, so it waits until the world has settled
    int mpMapAt = Now() + 25000;
    BuildCatalog();
    WebApplyOnlinePrices();
    TrafficInit();
    BuildIslandMap();
    MhcInit();
    N(N_REQUEST_IPL, "islaneon");
    Log("island map DLC: %s (islaneon_villa %s, islaneon_ter_3_3_land %s)", NB(N<int>(N_IS_MODEL_IN_CDIMAGE, Joaat("islaneon_villa"))) ? "LOADED" : "NOT LOADED",
        NB(N<int>(N_IS_MODEL_VALID, Joaat("islaneon_villa"))) ? "valid" : "invalid", NB(N<int>(N_IS_MODEL_IN_CDIMAGE, Joaat("islaneon_ter_3_3_land"))) ? "ok" : "missing");
    AssignGarages();
    Save();
    RefreshPropertyBlips();
    N(N_SET_CREATE_RANDOM_COPS, 1);
    g_nextEvent = Now() + 120000;
    ContactsInit();
    Notify("~g~Gori Online~s~ loaded. Press ~b~M~s~ (or hold ~b~Back~s~ on a controller) for the interaction menu.");
    for (;;) {
        if (mpMapAt && Now() > mpMapAt && !NB(N<int>(N_GET_IS_LOADING_SCREEN_ACTIVE))) { mpMapAt = 0; ApplyOnlineMap(false); }
        ProfileTick();
        if (!g_profileYou) TrackCharacter();
        JobSessionTick();
        LivingWorld();
        if (YouActive()) N(N_DISABLE_CONTROL_ACTION, 0, 244, 1);   // INPUT_INTERACTION_MENU: story mode's own menu (Quick GPS / Director Mode) stays shut
        PlayerInfoTick();
        PassiveTick();
        if (Freemode()) DoorsTick();
        CoronaTick();
        if (Freemode()) ContactInviteTick();
        if (CoronaActive()) { g_wantMenu = false; g_backSince = 0x3fffffff; if (g_menuOpen) CloseMenu(); }   // the lobby has the screen
        // Controller: hold Back/Select like Online's interaction menu
        bool pad = !NB(N<int>(N_IS_USING_KEYBOARD_AND_MOUSE, 2));
        if (pad && !CoronaActive() && NB(N<int>(N_IS_CONTROL_PRESSED, 0, 0))) {
            if (g_backSince < 0) g_backSince = Now();
            else if (Now() - g_backSince > 500 && !g_menuOpen) { g_menuOpen = true; g_stack.clear(); Open(M_MAIN); g_backSince = 0x3fffffff; }
        } else g_backSince = -1;
        if (g_wantMenu) {
            g_wantMenu = false;
            if (g_menuOpen) CloseMenu(); else { g_menuOpen = true; g_stack.clear(); Open(M_MAIN); }
        }
        MenuTick();
        RankTick();
        if (!g_menuOpen) DrawRank(false);
        DrawShard();
        JobCelebrationTick();
        MissionTick();
        CoopTick();
        RaceTick();
        AcidTick();
        DmTick();
        SvTick();
        ArenaWarTick();
        // freemode only (a job is played in its own session, JobSession.inc); production and the phone go on
        bool fm = Freemode();
        if (fm) ArenaWarWorldTick();
        if (fm || g_inside >= 0) PropertiesTick();   // inside a property (a heist from the planning board) it still runs
        BuzzTick();
        if (fm) { DcDoorTick(); PlazaTick(); GardensTick(); GardensValetTick(); }
        NaptHideTick();
        BusinessTick();
        if (fm) { IslandLifeTick(); GarageTick(); }
        PersonalSyncTick();
        CommandTick();
        CayoTick();
        if (fm) { CasinoTick(); IslandMapTick(); }
        PhoneTick();
        if (fm) { SpecialVehiclesTick(); ShopsTick(); ModShopTick(); }
        InternetTick();
        SfDebugTick();
        BizAppTick();
        TrafficTick();
        if (fm) { KnowayTick(); Events(); SellCars(); }
        Progress();
        Wait(0);
    }
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        char path[MAX_PATH]; GetModuleFileNameA(mod, path, MAX_PATH);
        std::string p(path); size_t s = p.find_last_of("\\/");
        g_dir = s == std::string::npos ? std::string() : p.substr(0, s + 1);
        g_iniPath = g_dir + "GoriOnline.ini";
        if (!BindScriptHook()) return TRUE;
        shv_scriptRegister(mod, ScriptMain);
        shv_keyboardRegister(OnKey);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (shv_scriptUnregister) shv_scriptUnregister(mod);
        if (shv_keyboardUnregister) shv_keyboardUnregister(OnKey);
    }
    return TRUE;
}
