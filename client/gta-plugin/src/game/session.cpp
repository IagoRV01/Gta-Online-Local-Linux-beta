#include "game/session.h"
#include <shlobj.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>
#include "common/character.h"
#include "common/log.h"
#include "engine/gta.h"
#include "engine/shv.h"
#include "game/appearance.h"
#include "game/devcmd.h"
#include "game/profile_store.h"
#include "game/sync.h"
#include "game/net_client.h"
#include "game/remote.h"
#include "game/combat.h"
#include "game/economy.h"
#include "game/garage.h"
#include "game/bank.h"
#include "game/hud.h"
#include "game/online_ui.h"
#include "game/world_sync.h"
#include "common/json.h"
#include "ui/creator.h"
#include "ui/menu.h"

namespace session {

using character::Character;

namespace {

enum class Mode { Story, You };

struct StoryChar {
    int ped = 0;
    uint32_t model = 0;
    gta::Vec3 pos;
    float heading = 0;
    int wanted = 0;
    character::Variation comps[character::kNumComponents];
    character::PropSlot props[character::kNumProps];
};

Config g_cfg;
Mode g_mode = Mode::Story;
StoryChar g_story;
Character g_char;
bool g_haveChar = false;
int g_you = 0;                 // the freemode ped while in YOU mode
int g_lastSave = 0, g_lastHide = 0, g_enteredAt = 0;
int g_modDataDirtyAt = 0;   // another mod changed its per-character data: save soon
bool g_wasDead = false;
bool g_savesBackedUp = false;
volatile bool g_menuRequested = false;
bool g_appearanceSent = false;   // our look has been sent to the server in this connection
uint32_t g_appearanceLogin = 0;  // the login it was sent in (a quick server restart is never seen offline here)

const char *Who() { return g_haveChar ? g_char.name.c_str() : "-"; }
void StopStoryRespawn();
void KeepStoryRespawnStopped();
void RestoreStoryRespawn();
void RestoreAfterDeath();

// ---------------------------------------------------------------- story save backup
// Copies Documents\Rockstar Games\GTA V\Profiles\<id>\SGTA5* once per game session, before the
// first switch to YOU, so the story progress can always be restored by hand.
void BackupStorySaves() {
    if (g_savesBackedUp) return;
    g_savesBackedUp = true;
    char docs[MAX_PATH];
    if (SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, docs) != S_OK) { LOGW("backup", "-", "Documents folder not found; story saves NOT backed up"); return; }
    std::string root = std::string(docs) + "\\Rockstar Games\\GTA V\\Profiles\\";
    char stamp[32];
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::strftime(stamp, sizeof stamp, "saves-%Y%m%d-%H%M%S\\", &tm);
    std::string dest = g_cfg.backupDir + stamp;
    CreateDirectoryA(g_cfg.backupDir.c_str(), nullptr);
    CreateDirectoryA(dest.c_str(), nullptr);
    int copied = 0;
    WIN32_FIND_DATAA pd;
    HANDLE hp = FindFirstFileA((root + "*").c_str(), &pd);
    if (hp == INVALID_HANDLE_VALUE) { LOGW("backup", "-", "no profiles under %s", root.c_str()); return; }
    do {
        if (!(pd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || pd.cFileName[0] == '.') continue;
        std::string src = root + pd.cFileName + "\\", dst = dest + pd.cFileName + "\\";
        CreateDirectoryA(dst.c_str(), nullptr);
        WIN32_FIND_DATAA fd;
        HANDLE hf = FindFirstFileA((src + "SGTA5*").c_str(), &fd);
        if (hf == INVALID_HANDLE_VALUE) continue;
        do {
            if (CopyFileA((src + fd.cFileName).c_str(), (dst + fd.cFileName).c_str(), FALSE)) ++copied;
        } while (FindNextFileA(hf, &fd));
        FindClose(hf);
    } while (FindNextFileA(hp, &pd));
    FindClose(hp);
    LOGI("backup", "-", "story saves backed up: %d file(s) -> %s", copied, dest.c_str());
}

// ---------------------------------------------------------------- peds
int CreateYouPed(bool female, const gta::Vec3 &p, float heading) {
    uint32_t model = gta::Joaat(female ? "mp_f_freemode_01" : "mp_m_freemode_01");
    if (!gta::LoadModel(model)) { LOGE("session", Who(), "freemode model 0x%08X failed to load", model); return 0; }
    int ped = N<int>(N_CREATE_PED, female ? 5 : 4, model, p.x, p.y, p.z, heading, 0, 1);
    gta::ReleaseModel(model);
    if (!ped) { LOGE("session", Who(), "CREATE_PED failed"); return 0; }
    gta::MakeMission(ped);
    N(N_SET_PED_DEFAULT_COMPONENT_VARIATION, ped);
    return ped;
}

void DeletePed(int &ped) {
    if (gta::Exists(ped)) { gta::MakeMission(ped); N(N_DELETE_PED, &ped); }
    ped = 0;
}

void HideStoryPed(int ped) {
    gta::SetVisible(ped, false);
    gta::SetCollision(ped, false);
    gta::Freeze(ped, true);
    gta::SetInvincible(ped, true);
    N(N_SET_BLOCKING_OF_NON_TEMPORARY_EVENTS, ped, 1);
    N(N_SET_PED_CAN_RAGDOLL, ped, 0);
}

void ShowStoryPed(int ped) {
    gta::SetVisible(ped, true);
    gta::SetCollision(ped, true);
    gta::Freeze(ped, false);
    gta::SetInvincible(ped, false);
    N(N_SET_BLOCKING_OF_NON_TEMPORARY_EVENTS, ped, 0);
    N(N_SET_PED_CAN_RAGDOLL, ped, 1);
}

// Makes `ped` the player. GTA's death restart rebuilds the player from the ped that was the player
// BEFORE the last CHANGE_PLAYER_PED (observed 2026-10-07: dying as Michael after leaving YOU came
// back as a freemode ped, and the first death as YOU came back as Michael). So we switch through
// an invisible clone of the target first: the "previous" ped then has the target's model and look.
void SwitchPlayerTo(int ped) {
    int clone = N<int>(N_CLONE_PED, ped, 0, 1, 1);
    if (clone) {
        gta::SetVisible(clone, false);
        gta::SetCollision(clone, false);
        gta::Freeze(clone, true);
        N(N_CHANGE_PLAYER_PED, gta::PlayerId(), clone, 0, 0);
    }
    N(N_CHANGE_PLAYER_PED, gta::PlayerId(), ped, 0, 0);
    if (clone) DeletePed(clone);
}

// Moves control to a new freemode ped of the other gender (creator). Returns the new ped.
int SwapGender(bool female) {
    int old = gta::PlayerPed();
    gta::Vec3 p = gta::Pos(old);
    float h = gta::Heading(old);
    int ped = CreateYouPed(female, {p.x, p.y, p.z - 1.f}, h);
    if (!ped) return old;
    SwitchPlayerTo(ped);
    if (old != g_story.ped) DeletePed(old);
    g_you = ped;
    LOGI("session", Who(), "gender swapped -> %s (ped %d)", female ? "female" : "male", ped);
    return ped;
}

void SaveNow(const char *why) {
    if (g_mode != Mode::You || !gta::Exists(g_you)) return;
    if (!gta::Dead(g_you)) appearance::CaptureState(g_you, g_char);
    g_char.playSeconds += (gta::GameTimeMs() - g_lastSave) / 1000;
    sync::Save(g_char, why);
    LOGI("session", Who(), "saved (%s) server rev %u%s pos %.1f %.1f %.1f", why, g_char.revision, sync::Online() ? "" : " [offline]", g_char.pos.x, g_char.pos.y, g_char.pos.z);
    g_modDataDirtyAt = 0;
    g_lastSave = gta::GameTimeMs();
}

// ---------------------------------------------------------------- enter / exit
std::string CanEnter() {
    int me = gta::PlayerPed();
    if (!gta::PlayerPlaying() || gta::Dead(me)) return "You must be alive.";
    if (NB(N<int>(N_GET_MISSION_FLAG))) return "Not available during a mission.";
    uint32_t m = gta::ModelOf(me);
    if (m == gta::Joaat("mp_m_freemode_01") || m == gta::Joaat("mp_f_freemode_01"))
        return "You are already a freemode ped (another mod changed the model). Load story mode first.";
    if (NB(N<int>(N_IS_PLAYER_SWITCH_IN_PROGRESS)) || NB(N<int>(N_IS_CUTSCENE_ACTIVE))) return "Wait for the switch/cutscene to end.";
    return "";
}

void Enter() {
    if (g_mode == Mode::You) return;
    std::string why = CanEnter();
    if (!why.empty()) { gta::Notify("~r~Local Online:~s~ " + why); LOGW("session", "-", "enter refused: %s", why.c_str()); return; }
    BackupStorySaves();

    // ---- the profile: server first (Phase 2), local cache as fallback
    gta::Notify("~b~Local Online~s~  Connecting to " + g_cfg.server.host + "...");
    std::string status;
    sync::Begin b = sync::Start(g_char, status);
    LOGI("session", "-", "profile source: %s", status.c_str());
    if (b == sync::Begin::Failed) { gta::Notify("~r~Local Online:~s~ " + status); return; }
    bool isNew = b == sync::Begin::NeedsCreator;
    if (isNew) {
        g_char = character::MakeDefault(false);
        g_char.uuid = character::NewUuid();
    }
    g_haveChar = !isNew;
    if (!sync::Online()) gta::Notify("~o~Server offline~s~ - playing from the local save; it syncs when the server is back.");

    gta::FadeOut(600);
    // ---- remember the story character exactly as it is
    int sp = gta::PlayerPed();
    if (gta::InVehicle(sp)) {   // step out (warp) so the hidden story ped stands next to its vehicle
        N(N_TASK_LEAVE_ANY_VEHICLE, sp, 0, 16);
        for (int i = 0; i < 40 && gta::InVehicle(sp); ++i) gta::Wait(50);
    }
    g_story = StoryChar{};
    g_story.ped = sp;
    g_story.model = gta::ModelOf(sp);
    g_story.pos = gta::Pos(sp);
    g_story.heading = gta::Heading(sp);
    g_story.wanted = N<int>(N_GET_PLAYER_WANTED_LEVEL, gta::PlayerId());
    for (int i = 0; i < character::kNumComponents; ++i)
        g_story.comps[i] = {N<int>(N_GET_PED_DRAWABLE_VARIATION, sp, i), N<int>(N_GET_PED_TEXTURE_VARIATION, sp, i), N<int>(N_GET_PED_PALETTE_VARIATION, sp, i)};
    for (int a = 0; a < character::kNumProps; ++a)
        g_story.props[a] = {N<int>(N_GET_PED_PROP_INDEX, sp, a, 0), N<int>(N_GET_PED_PROP_TEXTURE_INDEX, sp, a)};
    gta::MakeMission(sp);   // keeps the game from cleaning it up while it is not the player
    LOGI("session", Who(), "story character 0x%08X kept at %.1f %.1f %.1f (wanted %d)", g_story.model, g_story.pos.x, g_story.pos.y, g_story.pos.z, g_story.wanted);

    // ---- the freemode ped
    const auto &p = g_char.pos;
    int ped = CreateYouPed(g_char.female, {p.x, p.y, p.z + 1.f}, p.heading);
    if (!ped) { gta::FadeIn(500); gta::Notify("~r~Local Online:~s~ could not create the character."); return; }
    N(N_CLEAR_PLAYER_WANTED_LEVEL, gta::PlayerId());
    SwitchPlayerTo(ped);
    g_you = ped;
    HideStoryPed(sp);
    g_mode = Mode::You;
    g_enteredAt = g_lastSave = g_lastHide = gta::GameTimeMs();
    hud::Reset();
    hud::ShowMoney(8000);
    g_wasDead = false;

    appearance::ApplyLook(ped, g_char);
    appearance::ApplyState(ped, g_char);
    gta::Teleport({p.x, p.y, p.z}, p.heading);
    N(N_SET_GAMEPLAY_CAM_RELATIVE_HEADING, 0.f);
    gta::Wait(300);
    gta::FadeIn(800);

    if (isNew) {
        bool ok = creator::Run(g_char, true, SwapGender);
        if (!ok) {   // backed out of the first creation: undo everything
            LOGI("session", "-", "creation cancelled, back to story");
            g_haveChar = false;
            gta::FadeOut(400);
            // fallthrough to exit below
            g_mode = Mode::You;
            ShowStoryPed(g_story.ped);
            SwitchPlayerTo(g_story.ped);
            DeletePed(g_you);
            g_mode = Mode::Story;
            gta::FadeIn(500);
            return;
        }
        g_haveChar = true;
        appearance::ApplyState(gta::PlayerPed(), g_char);
        g_you = gta::PlayerPed();
        SaveNow("created");
    }
    StopStoryRespawn();
    world_sync::Begin();
    LOGI("session", Who(), "entered YOU mode (ped %d, %s, rev %u)", g_you, g_char.female ? "female" : "male", g_char.revision);
    gta::Notify("~b~Local Online~s~  Welcome, ~y~" + g_char.name + "~s~.  ~y~F10~s~ menu.");
}

void Exit() {
    if (g_mode != Mode::You) return;
    if (gta::Exists(g_you) && !gta::Dead(g_you)) appearance::CaptureState(g_you, g_char);
    if (sync::Online()) garage::StoreAll();
    remote::Clear();
    combat::Reset();
    world_sync::End();
    g_appearanceSent = false;
    sync::Finish(g_char);   // waits for the server's ack, then disconnects (offline: cache only)
    LOGI("session", Who(), "saved (exit) server rev %u", g_char.revision);
    gta::FadeOut(600);
    int you = g_you;
    if (gta::Exists(g_story.ped) && !gta::Dead(g_story.ped)) {
        ShowStoryPed(g_story.ped);
        SwitchPlayerTo(g_story.ped);
        LOGI("session", Who(), "back to the story character (kept ped %d)", g_story.ped);
    } else {
        // Fallback: the kept ped vanished. Rebuild the story character from what we captured.
        LOGW("session", Who(), "story ped %d is gone; rebuilding model 0x%08X with the captured outfit", g_story.ped, g_story.model);
        if (gta::LoadModel(g_story.model)) {
            N(N_SET_PLAYER_MODEL, gta::PlayerId(), g_story.model);
            gta::ReleaseModel(g_story.model);
            int me = gta::PlayerPed();
            for (int i = 0; i < character::kNumComponents; ++i) N(N_SET_PED_COMPONENT_VARIATION, me, i, g_story.comps[i].drawable, g_story.comps[i].texture, g_story.comps[i].palette);
            for (int a = 0; a < character::kNumProps; ++a)
                if (g_story.props[a].drawable >= 0) N(N_SET_PED_PROP_INDEX, me, a, g_story.props[a].drawable, g_story.props[a].texture, 1, 0);
            gta::Teleport(g_story.pos, g_story.heading);
        } else LOGE("session", Who(), "story model failed to load");
        you = (gta::PlayerPed() == you) ? 0 : you;   // SET_PLAYER_MODEL replaced the ped already
    }
    if (you && you != gta::PlayerPed()) DeletePed(you);
    g_you = 0;
    if (g_story.wanted > 0) {
        N(N_SET_PLAYER_WANTED_LEVEL, gta::PlayerId(), g_story.wanted, 0);
        N(N_SET_PLAYER_WANTED_LEVEL_NOW, gta::PlayerId(), 0);
    }
    N(N_SET_GAMEPLAY_CAM_RELATIVE_HEADING, 0.f);
    RestoreAfterDeath();
    online_ui::Leave();
    g_mode = Mode::Story;
    RestoreStoryRespawn();
    gta::Wait(250);
    gta::FadeIn(800);
    gta::Notify("~b~Local Online~s~  Back to Story Mode.");
}

void EditCharacter() {
    if (g_mode != Mode::You) return;
    Character edited = g_char;
    if (creator::Run(edited, false, SwapGender)) {
        g_char = edited;
        g_you = gta::PlayerPed();
        appearance::ApplyState(g_you, g_char);
        SaveNow("appearance");
        g_appearanceSent = false;   // other players get the new look next frame
        gta::Notify("Appearance saved.");
    }
    g_you = gta::PlayerPed();
}

// ---------------------------------------------------------------- death / respawn
// While YOU is the player the story respawn (game restart + respawn_controller) must not run: it
// rebuilds the player as a story character and bills the story character for the hospital.
// We pause it every frame and resurrect the freemode ped ourselves at the nearest hospital.
struct Spot { float x, y, z, h; };
const Spot kHospitals[] = {
    {298.8f, -584.6f, 43.3f, 70.f},      // Pillbox Hill Medical
    {296.6f, -1446.5f, 29.9f, 320.f},    // Central Los Santos Medical
    {-449.6f, -340.8f, 34.5f, 80.f},     // Mount Zonah
    {1151.2f, -1529.6f, 35.4f, 330.f},   // St Fiacre
    {1839.4f, 3672.9f, 34.3f, 210.f},    // Sandy Shores Medical
    {-247.5f, 6331.2f, 32.4f, 225.f},    // Paleto Bay Care Center
};

// Real time, not GET_GAME_TIMER: the game clock runs in slow motion during the WASTED effect, and
// the story restart kicks in ~8 s (real) after death, so we must resurrect well before that.
unsigned long long g_diedAt = 0;
constexpr unsigned kRespawnAfterMs = 3000;

bool g_tryStateOfPlay = false;            // diagnostics (try.* commands)
unsigned long long g_finishRestartUntil = 0;   // let the game complete its paused death restart
unsigned long long g_tryNoPauseUntil = 0;
void SuppressStoryRestart() {
    if (g_tryStateOfPlay) N(N_DISPLAY_HUD_WHEN_NOT_IN_STATE_OF_PLAY_THIS_FRAME);
    // After our own respawn the game still has its death restart pending (paused below): until it
    // runs, the game treats the player as WASTED - no minimap, no pause menu (seen 2026-10-10). Let
    // it finish for a few seconds once YOU is alive again; if it rebuilds the ped, the freemode
    // branch in YouFrame keeps YOU's look and weapons.
    if (GetTickCount64() < g_tryNoPauseUntil || GetTickCount64() < g_finishRestartUntil) { N(N_PAUSE_DEATH_ARREST_RESTART, 0); N(N_IGNORE_NEXT_RESTART, 0); return; }
    N(N_PAUSE_DEATH_ARREST_RESTART, 1);
    N(N_IGNORE_NEXT_RESTART, 1);
    N(N_SET_FADE_OUT_AFTER_DEATH, 0);
}

// The story script that runs the WASTED -> hospital restart (and rebuilds the player as the story
// character). It re-arms the code restart every frame, so while YOU is active it is stopped and our
// own respawn runs instead; leaving YOU starts it again.
const char *kRespawnScript = "respawn_controller";
constexpr int kRespawnScriptStack = 1424;   // DEFAULT_STACK_SIZE
bool g_respawnStopped = false;

int ScriptThreads(const char *name) { return N<int>(N_GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH, gta::Joaat(name)); }

void StopStoryRespawn() {
    if (ScriptThreads(kRespawnScript) > 0) {
        N(N_TERMINATE_ALL_SCRIPTS_WITH_THIS_NAME, kRespawnScript);
        g_respawnStopped = true;
        LOGI("session", Who(), "%s stopped while YOU is active", kRespawnScript);
    }
}

// main.ysc (func_1898) relaunches respawn_controller as soon as the player is injured and it isn't
// running: on every death as YOU. Left alone it plays the story hospital restart, hides the HUD and
// blocks the pause menu frame after frame (seen 2026-10-10 01:21). Stop it the moment it appears and
// undo what its first frames may have done.
void KeepStoryRespawnStopped() {
    if (ScriptThreads(kRespawnScript) == 0) return;
    N(N_TERMINATE_ALL_SCRIPTS_WITH_THIS_NAME, kRespawnScript);
    g_respawnStopped = true;
    int me = gta::PlayerPed();
    N(N_SET_GAME_PAUSED, 0);
    if (!gta::Dead(me)) {
        N(N_SET_PLAYER_CONTROL, gta::PlayerId(), 1, 0);
        gta::Freeze(me, false);
        gta::SetInvincible(me, false);
        RestoreAfterDeath();
    }
    static unsigned long long lastLog = 0;
    if (GetTickCount64() - lastLog > 5000) { lastLog = GetTickCount64(); LOGI("session", Who(), "%s relaunched by the story (death): stopped again", kRespawnScript); }
}

bool StartScript(const char *name, int stack) {
    if (ScriptThreads(name) > 0) return true;
    if (!NB(N<int>(N_DOES_SCRIPT_EXIST, name))) { LOGE("session", Who(), "script %s does not exist", name); return false; }
    N(N_REQUEST_SCRIPT, name);
    for (int i = 0; i < 100 && !NB(N<int>(N_HAS_SCRIPT_LOADED, name)); ++i) { gta::Wait(20); N(N_REQUEST_SCRIPT, name); }
    int id = N<int>(N_START_NEW_SCRIPT, name, stack);
    N(N_SET_SCRIPT_AS_NO_LONGER_NEEDED, name);
    LOGI("session", Who(), "started %s (thread %d, stack %d)", name, id, stack);
    return id != 0;
}

void RestoreStoryRespawn() {
    if (!g_respawnStopped) return;
    if (StartScript(kRespawnScript, kRespawnScriptStack)) g_respawnStopped = false;
    else LOGE("session", Who(), "could not restart %s: story deaths will not respawn until GTA restarts", kRespawnScript);
}

// Story cash of the hidden story character (SP0/1/2_TOTAL_CASH), so a story restart that bills the
// hospital to it can be undone. -1 when the kept ped is not a story protagonist.
int StoryCharIndex() {
    uint32_t m = g_story.model;
    return m == gta::Joaat("player_zero") ? 0 : m == gta::Joaat("player_one") ? 1 : m == gta::Joaat("player_two") ? 2 : -1;
}
uint32_t CashStat(int idx) { char b[24]; std::snprintf(b, sizeof b, "SP%d_TOTAL_CASH", idx); return gta::Joaat(b); }
int g_cashAtDeath = -1;

// respawn_controller (story) hides the HUD, slows time and plays the WASTED effects when the player
// dies, and restores all of it only at the end of its own fade-out. We resurrect YOU before that, so
// we undo it ourselves (seen 2026-10-09: no minimap/pause/weapon wheel/radio after a YOU death).
unsigned long long g_hudRestoreUntil = 0;
void RestoreAfterDeath() {
    N(N_DISPLAY_HUD, 1);
    N(N_DISPLAY_RADAR, 1);
    N(N_SET_TIME_SCALE, 1.f);
    N(N_ANIMPOSTFX_STOP_ALL);
    N(N_SET_CAM_DEATH_FAIL_EFFECT_STATE, 0);
    N(N_STOP_AUDIO_SCENE, "DEATH_SCENE");
    N(N_SET_FADE_IN_AFTER_DEATH_ARREST, 1);
}

void RespawnYou() {
    int me = gta::PlayerPed();
    gta::Vec3 p = gta::Pos(me);
    const Spot *best = &kHospitals[0];
    float bestD = 1e30f;
    for (const auto &h : kHospitals) {
        float d = (h.x - p.x) * (h.x - p.x) + (h.y - p.y) * (h.y - p.y);
        if (d < bestD) { bestD = d; best = &h; }
    }
    gta::FadeOut(800);
    N(N_NETWORK_RESURRECT_LOCAL_PLAYER, best->x, best->y, best->z, best->h, 0, 0, 0, 0, 0);
    gta::Wait(100);
    me = gta::PlayerPed();
    if (me != g_you) LOGW("session", Who(), "resurrect changed the player ped (%d -> %d)", g_you, me);
    g_you = me;
    N(N_CLEAR_PED_TASKS_IMMEDIATELY, me);
    N(N_CLEAR_PED_BLOOD_DAMAGE, me);
    N(N_ANIMPOSTFX_STOP_ALL);
    appearance::ApplyLook(me, g_char);
    Character fresh = g_char;
    fresh.health = 200;
    fresh.armour = 0;
    appearance::ApplyState(me, fresh);   // weapons as of the last save
    gta::Teleport({best->x, best->y, best->z}, best->h);
    gta::Wait(300);
    gta::FadeIn(800);
    RestoreAfterDeath();
    g_hudRestoreUntil = GetTickCount64() + 8000;   // the story script may still be mid-way: keep restoring briefly
    g_finishRestartUntil = GetTickCount64() + 1500 + 8000;
    LOGI("session", Who(), "respawned at hospital %.0f %.0f (ped %d)", best->x, best->y, me);
    SaveNow("respawn");
}

// ---------------------------------------------------------------- multiplayer (Phase 4)
uint32_t g_stateSeq = 0;
unsigned long long g_lastStateSent = 0;

void SendAppearance() {
    net::SendAppearance(json::Write(character::ToJson(character::AppearanceOnly(g_char)), 0));
    g_appearanceSent = true;
    g_appearanceLogin = net::Logins();
}

void SendOwnState(int me) {
    unsigned long long now = GetTickCount64();
    if (now - g_lastStateSent < 50) return;   // 20 Hz
    g_lastStateSent = now;
    proto::PlayerStateMsg s;
    s.seq = ++g_stateSeq;
    s.clientTimeMs = (uint32_t)now;
    gta::Vec3 p = gta::Pos(me), v = gta::Velocity(me);
    s.pos = {p.x, p.y, p.z};
    s.vel = {v.x, v.y, v.z};
    s.heading = gta::Heading(me);
    if (NB(N<int>(N_IS_PED_JUMPING, me)) || NB(N<int>(N_IS_PED_FALLING, me))) s.move = proto::Move::Air;
    else if (NB(N<int>(N_IS_PED_SPRINTING, me))) s.move = proto::Move::Sprint;
    else if (NB(N<int>(N_IS_PED_RUNNING, me))) s.move = proto::Move::Run;
    else if (NB(N<int>(N_IS_PED_WALKING, me))) s.move = proto::Move::Walk;
    else s.move = proto::Move::Still;
    uint16_t f = 0;
    if (gta::InVehicle(me)) f |= proto::kFlagInVehicle;
    if (NB(N<int>(N_IS_PLAYER_FREE_AIMING, gta::PlayerId()))) f |= proto::kFlagAiming;
    if (NB(N<int>(N_IS_PED_SHOOTING, me))) f |= proto::kFlagShooting;
    if (gta::Dead(me)) f |= proto::kFlagDead;
    if (NB(N<int>(N_IS_PED_RAGDOLL, me))) f |= proto::kFlagRagdoll;
    if (NB(N<int>(N_IS_PED_JUMPING, me))) f |= proto::kFlagJumping;
    s.flags = f;
    if (f & (proto::kFlagAiming | proto::kFlagShooting)) s.aim = combat::AimPoint();
    int hp = gta::Health(me) - 100;
    s.health = (uint8_t)(hp < 0 ? 0 : hp > 100 ? 100 : hp);
    int ar = N<int>(N_GET_PED_ARMOUR, me);
    s.armour = (uint8_t)(ar < 0 ? 0 : ar > 100 ? 100 : ar);
    s.weapon = N<uint32_t>(N_GET_SELECTED_PED_WEAPON, me);
    int wl = N<int>(N_GET_PLAYER_WANTED_LEVEL, gta::PlayerId());
    s.wanted = (uint8_t)(wl < 0 ? 0 : wl > 5 ? 5 : wl);
    if (f & proto::kFlagInVehicle) {
        int veh = N<int>(N_GET_VEHICLE_PED_IS_IN, me, 0);
        proto::VehicleBlock &vb = s.veh;
        vb.model = gta::ModelOf(veh);
        for (int seat = -1; seat < 16; ++seat)
            if (N<int>(N_GET_PED_IN_VEHICLE_SEAT, veh, seat, 0) == me) { vb.seat = (int8_t)seat; break; }
        int driver = N<int>(N_GET_PED_IN_VEHICLE_SEAT, veh, -1, 0);
        vb.driverNetId = driver == me ? net::SelfNetId() : remote::NetIdOfPed(driver);
        gta::Vec3 vp = gta::Pos(veh), vv = gta::Velocity(veh);
        NVec3 av = N<NVec3>(N_GET_ENTITY_ROTATION_VELOCITY, veh);
        vb.pos = {vp.x, vp.y, vp.z};
        vb.vel = {vv.x, vv.y, vv.z};
        vb.angVel = {av.x, av.y, av.z};
        N(N_GET_ENTITY_QUATERNION, veh, &vb.rot.x, &vb.rot.y, &vb.rot.z, &vb.rot.w);
        int c1 = 0, c2 = 0;
        N(N_GET_VEHICLE_COLOURS, veh, &c1, &c2);
        vb.color1 = (uint8_t)c1;
        vb.color2 = (uint8_t)c2;
        int lightsOn = 0, high = 0;
        N(N_GET_VEHICLE_LIGHTS_STATE, veh, &lightsOn, &high);
        uint8_t vf = 0;
        if (NB(N<int>(N_GET_IS_VEHICLE_ENGINE_RUNNING, veh))) vf |= proto::kVehEngine;
        if (lightsOn) vf |= proto::kVehLights;
        if (high) vf |= proto::kVehHighBeams;
        if (NB(N<int>(N_IS_VEHICLE_SIREN_ON, veh))) vf |= proto::kVehSiren;
        if (NB(N<int>(N_IS_HORN_ACTIVE, veh))) vf |= proto::kVehHorn;
        vb.vflags = vf;
    }
    net::SendState(s);
}

void MultiplayerFrame(int me) {
    if (!sync::Online()) {
        if (g_appearanceSent) { remote::Clear(); g_appearanceSent = false; }
        return;
    }
    if (g_appearanceSent && g_appearanceLogin != net::Logins()) { remote::Clear(); g_appearanceSent = false; }   // a new server session
    if (!g_appearanceSent) SendAppearance();
    SendOwnState(me);
    combat::Frame(me);
    world_sync::Frame();
    garage::Frame();
    remote::Frame();
}

// ---------------------------------------------------------------- per-frame
void YouFrame() {
    SuppressStoryRestart();
    KeepStoryRespawnStopped();
    online_ui::PauseKey();
    if (g_hudRestoreUntil && GetTickCount64() < g_hudRestoreUntil && !gta::Dead(gta::PlayerPed())) RestoreAfterDeath();
    else if (g_hudRestoreUntil && GetTickCount64() >= g_hudRestoreUntil) g_hudRestoreUntil = 0;
    int me = gta::PlayerPed();
    // the story selector must not run while YOU is the player (Phase 3 replaces this with the 4th slot)
    for (int c : {gta::kCharWheel, gta::kSelMichael, gta::kSelFranklin, gta::kSelTrevor, gta::kSelMultiplayer}) gta::DisableControl(c);

    if (me != g_you) {
        uint32_t m = gta::ModelOf(me);
        bool freemode = m == gta::Joaat("mp_m_freemode_01") || m == gta::Joaat("mp_f_freemode_01");
        LOGW("session", Who(), "player ped changed outside the plugin (ped %d -> %d, model 0x%08X)", g_you, me, m);
        if (freemode) {   // the game's own restart rebuilt the player with YOU's model: same look and weapons
            g_you = me;
            appearance::ApplyLook(me, g_char);
            appearance::GiveWeapons(me, g_char.weapons);
            RestoreAfterDeath();
        }
        else if (g_diedAt && GetTickCount64() - g_diedAt < 30000) {
            // The story restart that follows the first death of a session rebuilt the player as the
            // story character. Undo it: back into YOU, drop the rebuilt ped, keep the original
            // hidden story ped, refund a hospital bill charged to the story character.
            LOGW("session", Who(), "story restart after death rebuilt ped %d (model 0x%08X): undoing it", me, m);
            gta::Vec3 at = gta::Pos(me);
            float h = gta::Heading(me);
            if (!gta::Exists(g_you) || gta::Dead(g_you)) {   // the restart deleted our ped: build YOU again where it put us
                gta::FadeOut(300);
                int ped = CreateYouPed(g_char.female, {at.x, at.y, at.z - 1.f}, h);
                if (!ped) { LOGE("session", Who(), "could not rebuild the YOU ped"); return; }
                g_you = ped;
                appearance::ApplyState(ped, g_char);
            }
            SwitchPlayerTo(g_you);
            if (me != g_story.ped) { int rebuilt = me; DeletePed(rebuilt); }
            if (gta::Exists(g_story.ped)) HideStoryPed(g_story.ped);
            if (g_cashAtDeath >= 0) {
                int now = 0;
                N(N_STAT_GET_INT, CashStat(StoryCharIndex()), &now, -1);
                if (now != g_cashAtDeath) {
                    N(N_STAT_SET_INT, CashStat(StoryCharIndex()), g_cashAtDeath, 1);
                    LOGI("session", Who(), "story cash restored %d -> %d", now, g_cashAtDeath);
                }
            }
            appearance::ApplyLook(g_you, g_char);
            RestoreAfterDeath();
            g_hudRestoreUntil = GetTickCount64() + 8000;
            if (NB(N<int>(N_IS_SCREEN_FADED_OUT)) || NB(N<int>(N_IS_SCREEN_FADING_OUT))) gta::FadeIn(500);
            return;
        }
        else {   // the game put a story character back anyway: leave YOU cleanly
            DeletePed(g_you);
            if (gta::Exists(g_story.ped) && g_story.ped != me) {
                // the kept story ped is now a hidden duplicate of the character the game rebuilt
                LOGW("session", Who(), "removing the hidden duplicate story ped %d", g_story.ped);
                DeletePed(g_story.ped);
            } else if (g_story.ped == me) ShowStoryPed(me);
            g_story = StoryChar{};
            online_ui::Leave();
            g_mode = Mode::Story;
            world_sync::End();
            RestoreStoryRespawn();
            gta::Notify("~b~Local Online~s~  The game returned you to Story Mode.");
            return;
        }
    }

    bool dead = gta::Dead(me);
    if (dead && !g_wasDead) {
        g_char.deaths++;
        g_diedAt = GetTickCount64();
        if (sync::Online()) combat::OnLocalDeath();
        g_cashAtDeath = -1;
        if (StoryCharIndex() >= 0) N(N_STAT_GET_INT, CashStat(StoryCharIndex()), &g_cashAtDeath, -1);
        LOGI("session", Who(), "died (deaths %d)", g_char.deaths);
    }
    g_wasDead = dead;
    if (dead) {
        if (GetTickCount64() - g_diedAt >= kRespawnAfterMs) { RespawnYou(); g_wasDead = false; }
        return;
    }

    int now = gta::GameTimeMs();
    if (now - g_lastHide > 1000) {
        g_lastHide = now;
        if (gta::Exists(g_story.ped)) HideStoryPed(g_story.ped);
    }
    if (!dead && now - g_lastSave > g_cfg.autosaveSeconds * 1000) SaveNow("auto");
    else if (!dead && g_modDataDirtyAt && now - g_modDataDirtyAt > 2000) SaveNow("mod data");
    MultiplayerFrame(me);
    std::vector<hud::PlayerRow> rows{{g_cfg.server.playerName.empty() ? g_char.name : g_cfg.server.playerName, true, N<int>(N_GET_PLAYER_WANTED_LEVEL, gta::PlayerId()),
                                      remote::MyRank(), (remote::MyFlags() & proto::kInfoPassive) != 0, me}};
    for (const auto &r : remote::Rows()) rows.push_back({r.name, false, r.wanted, r.rank, r.passive, r.ped});
    hud::Frame(g_char.money, g_char.bank, rows, sync::StatusLine());
    online_ui::Frame(g_cfg.server.playerName.empty() ? g_char.name : g_cfg.server.playerName, g_char.money, g_char.bank);
    bank::Frame(me);
}

// ---------------------------------------------------------------- story-mode guard
// After a death as YOU, GTA's restart can later rebuild a dying STORY character with the freemode
// model (seen 2026-10-07). In story mode we keep a rolling snapshot of the story character and, if
// a death comes back as a freemode ped, put the story character back with its outfit and weapons.
struct StorySnapshot {
    bool valid = false;
    uint32_t model = 0;
    character::Variation comps[character::kNumComponents];
    character::PropSlot props[character::kNumProps];
    std::vector<character::Weapon> weapons;
};
StorySnapshot g_snap;
int g_lastSnap = 0;
unsigned long long g_storyDiedAt = 0;
bool g_storyWasDead = false;

bool IsStoryModel(uint32_t m) { return m == gta::Joaat("player_zero") || m == gta::Joaat("player_one") || m == gta::Joaat("player_two"); }
bool IsFreemodeModel(uint32_t m) { return m == gta::Joaat("mp_m_freemode_01") || m == gta::Joaat("mp_f_freemode_01"); }

void StoryFrame() {
    int me = gta::PlayerPed();
    uint32_t m = gta::ModelOf(me);
    bool dead = gta::Dead(me);
    if (dead && !g_storyWasDead) g_storyDiedAt = GetTickCount64();
    g_storyWasDead = dead;
    int now = gta::GameTimeMs();
    if (!dead && IsStoryModel(m) && now - g_lastSnap > 3000) {
        g_lastSnap = now;
        g_snap.valid = true;
        g_snap.model = m;
        for (int i = 0; i < character::kNumComponents; ++i)
            g_snap.comps[i] = {N<int>(N_GET_PED_DRAWABLE_VARIATION, me, i), N<int>(N_GET_PED_TEXTURE_VARIATION, me, i), N<int>(N_GET_PED_PALETTE_VARIATION, me, i)};
        for (int a = 0; a < character::kNumProps; ++a)
            g_snap.props[a] = {N<int>(N_GET_PED_PROP_INDEX, me, a, 0), N<int>(N_GET_PED_PROP_TEXTURE_INDEX, me, a)};
        g_snap.weapons = appearance::CaptureWeapons(me);
    }
    if (!dead && IsFreemodeModel(m) && g_snap.valid && g_storyDiedAt && GetTickCount64() - g_storyDiedAt < 60000) {
        g_storyDiedAt = 0;
        LOGW("session", Who(), "story respawn came back as a freemode ped (ped %d): restoring story model 0x%08X", me, g_snap.model);
        gta::Vec3 p = gta::Pos(me);
        float h = gta::Heading(me);
        if (!gta::LoadModel(g_snap.model)) { LOGE("session", Who(), "story model failed to load"); return; }
        N(N_SET_PLAYER_MODEL, gta::PlayerId(), g_snap.model);
        gta::ReleaseModel(g_snap.model);
        me = gta::PlayerPed();
        for (int i = 0; i < character::kNumComponents; ++i) N(N_SET_PED_COMPONENT_VARIATION, me, i, g_snap.comps[i].drawable, g_snap.comps[i].texture, g_snap.comps[i].palette);
        for (int a = 0; a < character::kNumProps; ++a) {
            if (g_snap.props[a].drawable >= 0) N(N_SET_PED_PROP_INDEX, me, a, g_snap.props[a].drawable, g_snap.props[a].texture, 1, 0);
            else N(N_CLEAR_PED_PROP, me, a, 0);
        }
        appearance::GiveWeapons(me, g_snap.weapons);
        N(N_SET_ENTITY_COORDS_NO_OFFSET, me, p.x, p.y, p.z, 0, 0, 0);
        gta::SetHeading(me, h);
        LOGI("session", Who(), "story character restored (ped %d, %d weapons)", me, (int)g_snap.weapons.size());
    }
}

void MainMenu() {
    ui::Menu m;
    m.title = "LOCAL ONLINE";
    bool close = false;
    auto build = [&] {
        m.items.clear();
        if (g_mode == Mode::Story) {
            m.subtitle = "Story Mode";
            m.items.push_back({profile_store::Exists() || g_haveChar ? "Play as YOU" : "Create character (YOU)", nullptr, nullptr,
                               [&] { close = true; devcmd::PushFront("enter"); }, "Leaves Michael/Franklin/Trevor exactly where they are."});
        } else {
            m.subtitle = g_char.name + "  -  " + sync::StatusLine();
            m.items.push_back({"Save now", nullptr, nullptr, [&] { SaveNow("menu"); gta::Notify("Saved."); }, ""});
            m.items.push_back({"Edit appearance", nullptr, nullptr, [&] { close = true; devcmd::PushFront("creator"); }, ""});
            m.items.push_back({"My vehicles", nullptr, nullptr, [&] { close = true; devcmd::PushFront("garage.menu"); },
                               sync::Online() ? "Your personal vehicles, kept on the server." : "Needs the server (offline now)."});
            m.items.push_back({"Maze Bank", nullptr, nullptr, [&] { close = true; devcmd::PushFront("bank.menu"); },
                               "Online banking: deposit your cash. Withdrawals need an ATM."});
            m.items.push_back({"Return to Story Mode", nullptr, nullptr, [&] { close = true; devcmd::PushFront("exit"); }, "Saves and switches back to the story character."});
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

}  // namespace

// ---------------------------------------------------------------- public
void Init(const Config &cfg) {
    g_cfg = cfg;
    profile_store::Init(cfg.cacheDir);
    sync::Init(cfg.server);
    economy::Init(cfg.cacheDir, {[] { return (int64_t)g_char.money; }, [] { return (int64_t)g_char.bank; },
                                 [](int64_t v) { g_char.money = v; }, [](int64_t v) { g_char.bank = v; }});
    economy::SetListener([](int64_t dw, int64_t db) { if (g_mode == Mode::You) hud::OnMoneyChange(dw, db); });
    remote::Init();
    std::string err;
    if (profile_store::Exists()) {
        if (profile_store::Load(g_char, err)) { g_haveChar = true; LOGI("session", Who(), "profile found (rev %u, %s)", g_char.revision, g_char.updatedAt.c_str()); }
        else LOGE("session", "-", "profile present but unreadable: %s", err.c_str());
    } else LOGI("session", "-", "no profile yet: the first 'Play as YOU' opens the creator");
}

bool InYou() { return g_mode == Mode::You; }

void EnterYou() {
    // From the start screen the game may still be finishing a load switch/cutscene (seen after the
    // ScriptHookV "online" alert): wait for it instead of refusing, with the screen kept black.
    unsigned long long until = GetTickCount64() + 45000;
    bool logged = false;
    while (GetTickCount64() < until && !CanEnter().empty()) {
        if (!logged) { LOGI("session", "-", "waiting before entering YOU: %s", CanEnter().c_str()); logged = true; }
        if (!NB(N<int>(N_IS_SCREEN_FADED_OUT))) N(N_DO_SCREEN_FADE_OUT, 0);
        gta::Text("Loading Local Online...", 0.5f, 0.47f, 0.6f, gta::kWhite, 0, 4);
        gta::Wait(0);
    }
    Enter();
    if (g_mode != Mode::You && NB(N<int>(N_IS_SCREEN_FADED_OUT))) gta::FadeIn(500);   // refused: never leave a black screen
}

std::string DisplayName() { return g_cfg.server.playerName.empty() ? g_char.name : g_cfg.server.playerName; }
std::string CharacterName() { return g_haveChar ? g_char.name : ""; }

long long Money() { return g_haveChar ? g_char.money + g_char.bank : 0; }
long long Wallet() { return g_haveChar ? g_char.money : 0; }
long long Bank() { return g_haveChar ? g_char.bank : 0; }

bool AddMoney(long long delta, const char *reason) {
    if (g_mode != Mode::You || !g_haveChar) return false;
    std::string why = reason ? reason : "unspecified";
    // server-owned balances (Phase 8). Like GTA Online, earnings go to the bank; spending takes the
    // wallet first, then the bank.
    if (!(delta > 0 ? economy::Change(proto::kBank, delta, why) : economy::Spend(-delta, why))) return false;
    SaveNow("money");
    return true;
}

int GetData(const char *key, char *buf, int cap) {
    if (g_mode != Mode::You || !g_haveChar || !key) return -1;
    auto it = g_char.modData.find(key);
    if (it == g_char.modData.end()) return -1;
    int n = (int)it->second.size();
    if (buf && cap > 0) {
        int c = n < cap - 1 ? n : cap - 1;
        memcpy(buf, it->second.data(), (size_t)c);
        buf[c] = 0;
    }
    return n;
}

bool SetData(const char *key, const char *value) {
    if (g_mode != Mode::You || !g_haveChar || !key || !value || !character::ValidModKey(key)) return false;
    size_t n = strlen(value);
    if (n > character::kMaxModDataValue) { LOGW("session", Who(), "mod data '%s' too large (%d bytes)", key, (int)n); return false; }
    auto it = g_char.modData.find(key);
    if (it == g_char.modData.end() && g_char.modData.size() >= character::kMaxModDataKeys) return false;
    if (it != g_char.modData.end() && it->second == value) return true;
    g_char.modData[key] = value;
    if (!g_modDataDirtyAt) g_modDataDirtyAt = gta::GameTimeMs();
    return true;
}

// The clothes the player is wearing now become YOU's (Gori Online's apartment wardrobe): every component but the
// face and the hair (those belong to the creator and the barber), and the props; saved and sent to the others.
bool AdoptOutfit() {
    if (g_mode != Mode::You || !g_haveChar || !gta::Exists(g_you) || gta::Dead(g_you)) return false;
    int me = g_you;
    for (int i = 0; i < character::kNumComponents; ++i) {
        if (i == 0 || i == 2) continue;
        auto &v = g_char.components[i];
        v.drawable = N<int>(N_GET_PED_DRAWABLE_VARIATION, me, i);
        v.texture = N<int>(N_GET_PED_TEXTURE_VARIATION, me, i);
        v.palette = N<int>(N_GET_PED_PALETTE_VARIATION, me, i);
    }
    for (int a = 0; a < character::kNumProps; ++a) {
        auto &p = g_char.props[a];
        p.drawable = N<int>(N_GET_PED_PROP_INDEX, me, a, 0);
        p.texture = p.drawable < 0 ? 0 : N<int>(N_GET_PED_PROP_TEXTURE_INDEX, me, a);
    }
    SaveNow("outfit");
    g_appearanceSent = false;   // other players get the new look next frame
    return true;
}

bool Spend(long long amount, const char *reason, bool preferBank) {
    if (g_mode != Mode::You || !g_haveChar || amount < 0) return false;
    if (!economy::Spend(amount, reason ? reason : "purchase", preferBank)) return false;
    SaveNow("money");
    return true;
}

bool ChangeAccount(int account, long long delta, const char *reason) {
    if (g_mode != Mode::You || !g_haveChar || account < proto::kWallet || account > proto::kDeposit) return false;
    if (!economy::Change((uint8_t)account, delta, reason ? reason : "unspecified")) return false;
    SaveNow("money");
    return true;
}

void OnKey(DWORD key) {
    if ((int)key == g_cfg.menuKey) g_menuRequested = true;
}

bool HandleCommand(const std::string &line) {
    std::istringstream in(line);
    std::string v;
    in >> v;
    if (v == "enter") Enter();
    else if (v == "exit") Exit();
    else if (v == "creator") EditCharacter();
    else if (v == "save") SaveNow("command");
    else if (v == "hudstate") {   // diagnostics for "no minimap / pause / weapon wheel / radio"
        LOGI("status", Who(), "hud hidden %d radar hidden %d pause active %d control on %d | enabled: pause %d wheel %d radio %d charwheel %d | respawn_controller %d",
             N<int>(N_IS_HUD_HIDDEN), N<int>(N_IS_RADAR_HIDDEN), N<int>(N_IS_PAUSE_MENU_ACTIVE), N<int>(N_IS_PLAYER_CONTROL_ON, gta::PlayerId()),
             N<int>(N_IS_CONTROL_ENABLED, 0, 199), N<int>(N_IS_CONTROL_ENABLED, 0, 37), N<int>(N_IS_CONTROL_ENABLED, 0, 85), N<int>(N_IS_CONTROL_ENABLED, 0, 19),
             ScriptThreads(kRespawnScript));
        int me = gta::PlayerPed();
        LOGI("status", Who(), "player dead %d ped dead %d visible %d alpha %d | switch %d (state %d) cutscene %d loadscene %d faded out %d fading in %d | minimap rendering %d, frontend %d, death restart paused, time since death %d ms | script cam rendering %d",
             N<int>(N_IS_PLAYER_DEAD, gta::PlayerId()), N<int>(N_IS_ENTITY_DEAD, me, 0), N<int>(N_IS_ENTITY_VISIBLE, me), N<int>(N_GET_ENTITY_ALPHA, me),
             N<int>(N_IS_PLAYER_SWITCH_IN_PROGRESS), N<int>(N_GET_PLAYER_SWITCH_STATE), N<int>(N_IS_CUTSCENE_ACTIVE), N<int>(N_IS_NEW_LOAD_SCENE_ACTIVE),
             N<int>(N_IS_SCREEN_FADED_OUT), N<int>(N_IS_SCREEN_FADING_IN), N<int>(N_IS_MINIMAP_RENDERING), N<int>(N_IS_FRONTEND_FADING),
             N<int>(N_GET_TIME_SINCE_LAST_DEATH), N<int>(N_IS_GAMEPLAY_CAM_RENDERING) ? 0 : 1);
    }
    else if (v == "try.stateofplay") { g_tryStateOfPlay = !g_tryStateOfPlay; LOGI("status", Who(), "force HUD out of state of play: %d", g_tryStateOfPlay); }
    else if (v == "try.nopause") { g_tryNoPauseUntil = GetTickCount64() + 10000; LOGI("status", Who(), "death/arrest restart not paused for 10 s"); }
    else if (v == "try.playing") LOGI("status", Who(), "player playing %d, control %d, being arrested %d, wasted text %d", N<int>(N_IS_PLAYER_PLAYING, gta::PlayerId()),
                                    N<int>(N_IS_PLAYER_CONTROL_ON, gta::PlayerId()), N<int>(N_IS_PLAYER_BEING_ARRESTED, gta::PlayerId(), 0), N<int>(N_IS_PED_INJURED, gta::PlayerPed()));
    else if (v == "hudfix") { RestoreAfterDeath(); LOGI("status", Who(), "HUD restored"); }
    else if (v == "netstatus") LOGI("status", Who(), "server: %s, char rev %u, remote players %d, wallet %lld, bank %lld (%d pending)", sync::StatusLine().c_str(), g_char.revision, remote::Count(), (long long)g_char.money, (long long)g_char.bank, economy::PendingCount());
    else if (online_ui::HandleCommand(v, in)) {}
    else if (v.rfind("bank.", 0) == 0) { if (g_mode != Mode::You || !bank::HandleCommand(v, in)) LOGW("devcmd", Who(), "bank command needs YOU / unknown"); }
    else if (v == "garage.menu") { if (g_mode == Mode::You && sync::Online()) garage::Menu(); }
    else if (v.rfind("garage.", 0) == 0) { if (!garage::HandleCommand(v, in)) LOGW("devcmd", Who(), "unknown garage command"); }
    else if (v == "money") {   // money <delta> [reason]: test helper for the economy round trip
        long long d = 0;
        std::string why = "test";
        in >> d;
        std::getline(in, why);
        if (g_mode == Mode::You && d) LOGI("devcmd", Who(), "money %+lld -> %s", d, AddMoney(d, why.empty() ? "test" : why.c_str()) ? "ok" : "refused locally");
    }
    else if (v == "shootat") {   // shootat <name prefix> [shots]: our ped fires at a remote player (combat test)
        std::string who;
        int shots = 5;
        in >> who >> shots;
        int target = remote::PedOf(who);
        if (!target) LOGW("devcmd", Who(), "shootat: no remote player '%s'", who.c_str());
        else {
            int me = gta::PlayerPed();
            uint32_t pistol = gta::Joaat("WEAPON_PISTOL");
            N(N_GIVE_WEAPON_TO_PED, me, pistol, 120, 0, 1);
            N(N_SET_CURRENT_PED_WEAPON, me, pistol, 1);
            for (int i = 0; i < shots; ++i) {
                gta::Vec3 t = gta::Pos(target);
                N(N_SET_PED_SHOOTS_AT_COORD, me, t.x, t.y, t.z + 0.3f, 1);
                gta::Wait(450);
            }
        }
    }
    else if (v == "spectate") {   // spectate [name prefix] | spectate off
        std::string who;
        std::getline(in, who);
        while (!who.empty() && who[0] == ' ') who.erase(0, 1);
        if (who == "off") remote::StopSpectate();
        else if (!remote::Spectate(who)) LOGW("devcmd", Who(), "spectate: no remote player '%s'", who.c_str());
    }
    else if (v == "cache.delete") {   // Phase 2 test: forget the local copy (only outside YOU)
        if (g_mode == Mode::You) LOGW("devcmd", Who(), "cache.delete refused while YOU is active");
        else { profile_store::Delete(); g_haveChar = false; LOGI("devcmd", "-", "local profile cache deleted"); }
    }
    else if (v == "menu") g_menuRequested = true;
    else if (v == "where" || v == "status") {
        int me = gta::PlayerPed();
        gta::Vec3 p = gta::Pos(me);
        LOGI("status", Who(), "where: %.2f %.2f %.2f h %.1f mode %s ped %d model 0x%08X hp %d story_ped %d exists %d",
             p.x, p.y, p.z, gta::Heading(me), g_mode == Mode::You ? "YOU" : "STORY", me, gta::ModelOf(me), gta::Health(me),
             g_story.ped, gta::Exists(g_story.ped) ? 1 : 0);
    } else if (v == "kill") N(N_SET_ENTITY_HEALTH, gta::PlayerPed(), 0, 0, 0);
    else if (v == "script") {   // script count|stop|start <name> [stack]   (test helper)
        std::string op, name;
        int stack = kRespawnScriptStack;
        in >> op >> name >> stack;
        if (op == "stop") N(N_TERMINATE_ALL_SCRIPTS_WITH_THIS_NAME, name.c_str());
        else if (op == "start") StartScript(name.c_str(), stack);
        LOGI("devcmd", Who(), "script %s: %d thread(s)", name.c_str(), ScriptThreads(name.c_str()));
    }
    else if (v == "tp") {
        float x, y, z;
        if (in >> x >> y >> z) gta::Teleport({x, y, z}, gta::Heading(gta::PlayerPed()));
    } else if (v == "veh") {      // veh <model>: spawn a vehicle and seat the player (driving test helper)
        std::string model;
        if (in >> model) {
            uint32_t m = gta::Joaat(model.c_str());
            int me = gta::PlayerPed();
            if (gta::LoadModel(m)) {
                gta::Vec3 p = gta::Pos(me);
                int veh = N<int>(N_CREATE_VEHICLE, m, p.x, p.y, p.z, gta::Heading(me), 0, 0, 0);
                gta::ReleaseModel(m);
                if (veh) { N(N_SET_PED_INTO_VEHICLE, me, veh, -1); N(N_SET_VEHICLE_AS_NO_LONGER_NEEDED, &veh); }
                LOGI("devcmd", Who(), "veh %s -> %d", model.c_str(), veh);
            } else LOGW("devcmd", Who(), "veh: model %s not available", model.c_str());
        }
    } else if (v == "outfit") {   // outfit <component> <drawable> <texture>  (test helper)
        int c, d, t;
        if (in >> c >> d >> t && c >= 0 && c < character::kNumComponents && g_mode == Mode::You) {
            g_char.components[c] = {d, t, 0};
            appearance::ApplyLook(gta::PlayerPed(), g_char);
        }
    } else return false;
    return true;
}

void Tick() {
    if (g_menuRequested) { g_menuRequested = false; MainMenu(); }
    std::string line;
    while (devcmd::Next(line))
        if (!HandleCommand(line)) LOGW("devcmd", "-", "unknown command '%s'", line.c_str());
    sync::Tick(g_char, g_mode == Mode::You);
    if (g_mode == Mode::You) YouFrame();
    else StoryFrame();
}

}  // namespace session
