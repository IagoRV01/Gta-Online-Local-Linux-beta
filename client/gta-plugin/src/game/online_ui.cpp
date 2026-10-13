#include "game/online_ui.h"
#include <windows.h>
#include <ctime>
#include <map>
#include "common/log.h"
#include "engine/gta.h"
#include "engine/shv.h"
#include "game/hud.h"

namespace online_ui {

namespace {

void ScaleformString(const std::string &s) {
    N(N_BEGIN_TEXT_COMMAND_SCALEFORM_STRING, "STRING");
    N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, s.c_str());
    N(N_END_TEXT_COMMAND_SCALEFORM_STRING);
}

const char *kDays[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};

// The pause menu header is filled by the game from the story character's stats every time the menu
// opens; GTA Online writes its own (maintransition.ysc: SET_HEADER_TITLE on the frontend header).
void PauseHeader(const std::string &name, int64_t wallet, int64_t bank) {
    if (!NB(N<int>(N_IS_PAUSE_MENU_ACTIVE))) return;
    if (NB(N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD_ON_FRONTEND_HEADER, "SET_HEADER_TITLE"))) {
        ScaleformString("Grand Theft Auto Online");
        N(N_END_SCALEFORM_MOVIE_METHOD);
    }
    char when[64];
    std::snprintf(when, sizeof when, "%s %02d:%02d", kDays[N<int>(N_GET_CLOCK_DAY_OF_WEEK) % 7], N<int>(N_GET_CLOCK_HOURS), N<int>(N_GET_CLOCK_MINUTES));
    if (NB(N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD_ON_FRONTEND_HEADER, "SET_HEADING_DETAILS"))) {
        ScaleformString(name);
        ScaleformString(when);
        ScaleformString("BANK " + hud::Money(bank) + "  CASH " + hud::Money(wallet));
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, 0);
        N(N_END_SCALEFORM_MOVIE_METHOD);
    }
}

// Story-mode blips that GTA Online doesn't have (sprites from a dump of the map on 2026-10-10):
// Strangers and Freaks, story businesses for sale/owned, base jumps, story races, car wash.
const int kStorySprites[] = {66, 94, 100, 197, 205, 314, 315, 316, 374, 375, 388, 389};   // + yoga, therapist
// The story characters' safehouses (Michael, Franklin x2, Trevor x2): their house blip (sprite 40,
// white) and Michael's private tennis court go; Gori Online's own houses are elsewhere.
const float kSafehouses[][2] = {{-813.6f, 179.5f}, {7.1f, 536.6f}, {-14.4f, -1438.5f}, {1974.5f, 3816.6f}, {-1151.0f, -1518.0f}};
bool NearSafehouse(int b, float r) {
    NVec3 c = N<NVec3>(N_GET_BLIP_INFO_ID_COORD, b);
    for (const auto &h : kSafehouses)
        if ((c.x - h[0]) * (c.x - h[0]) + (c.y - h[1]) * (c.y - h[1]) < r * r) return true;
    return false;
}
std::map<int, int> g_hidden;   // blip -> its display before we hid it
int g_lastScan = 0;

bool IsStoryCar(int b) {
    // the story characters' personal vehicles: a coordinate blip (the story remembers where they
    // were parked) or a blip on Michael's / Franklin's / Trevor's own car
    if (N<int>(N_GET_BLIP_INFO_ID_TYPE, b) == 4) return true;
    int ent = N<int>(N_GET_BLIP_INFO_ID_ENTITY_INDEX, b);
    if (!ent || !N<int>(N_DOES_ENTITY_EXIST, ent)) return false;
    if (N<int>(N_GET_BLIP_COLOUR, b) != 0) return false;   // ours (Local Online / Gori Online personal vehicles) are coloured
    uint32_t m = gta::ModelOf(ent);
    for (const char *story : {"tailgater", "buffalo2", "bagger", "bodhi2"})
        if (m == gta::Joaat(story)) return true;
    return false;
}

void Hide(int b) {
    int d = N<int>(N_GET_BLIP_INFO_ID_DISPLAY, b);
    if (d == 0) return;
    if (!g_hidden.count(b)) g_hidden[b] = d;
    N(N_SET_BLIP_DISPLAY, b, 0);
}

void HideStoryBlips() {
    int now = gta::GameTimeMs();
    if (now - g_lastScan < 1000) return;   // story scripts re-create their blips: keep checking
    g_lastScan = now;
    for (int sprite : kStorySprites)
        for (int b = N<int>(N_GET_FIRST_BLIP_INFO_ID, sprite); N<int>(N_DOES_BLIP_EXIST, b); b = N<int>(N_GET_NEXT_BLIP_INFO_ID, sprite)) Hide(b);
    for (int b = N<int>(N_GET_FIRST_BLIP_INFO_ID, 225); N<int>(N_DOES_BLIP_EXIST, b); b = N<int>(N_GET_NEXT_BLIP_INFO_ID, 225))
        if (IsStoryCar(b)) Hide(b);
    for (int b = N<int>(N_GET_FIRST_BLIP_INFO_ID, 40); N<int>(N_DOES_BLIP_EXIST, b); b = N<int>(N_GET_NEXT_BLIP_INFO_ID, 40))
        if (N<int>(N_GET_BLIP_COLOUR, b) == 0 && NearSafehouse(b, 40.f)) Hide(b);
    for (int b = N<int>(N_GET_FIRST_BLIP_INFO_ID, 122); N<int>(N_DOES_BLIP_EXIST, b); b = N<int>(N_GET_NEXT_BLIP_INFO_ID, 122))
        if (NearSafehouse(b, 70.f)) Hide(b);
}

// The game pauses as soon as its pause menu opens and no script runs while it is open, so the header
// can't be changed after the player presses ESC. While YOU plays we open the menu ourselves (same
// menu, same keys) and write GTA Online's header in the frames before the game pauses.
std::string g_name;
int64_t g_wallet = 0, g_bank = 0;
int g_pauseClosedAt = 0;
bool g_pauseWasActive = false;

}  // namespace

void PauseKey() {
    int now = gta::GameTimeMs();
    bool active = NB(N<int>(N_IS_PAUSE_MENU_ACTIVE));
    if (g_pauseWasActive && !active) g_pauseClosedAt = now;
    g_pauseWasActive = active;
    if (active) { PauseHeader(g_name, g_wallet, g_bank); return; }
    N(N_DISABLE_CONTROL_ACTION, 0, 199, 1);   // INPUT_FRONTEND_PAUSE (ESC, P, Start)
    N(N_DISABLE_CONTROL_ACTION, 0, 200, 1);   // INPUT_FRONTEND_PAUSE_ALTERNATE (ESC)
    if (now - g_pauseClosedAt < 400) return;   // the key that closed the menu
    if (NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 0, 199)) || NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 0, 200))) {
        N(N_ACTIVATE_FRONTEND_MENU, gta::Joaat("FE_MENU_VERSION_SP_PAUSE"), 0, -1);
        PauseHeader(g_name, g_wallet, g_bank);
    }
}

void Frame(const std::string &playerName, int64_t wallet, int64_t bank) {
    g_name = playerName; g_wallet = wallet; g_bank = bank;
    PauseHeader(playerName, wallet, bank);
    HideStoryBlips();
}

void Leave() {   // story mode again: every hidden blip back as it was
    int n = 0;
    for (auto &kv : g_hidden)
        if (N<int>(N_DOES_BLIP_EXIST, kv.first)) { N(N_SET_BLIP_DISPLAY, kv.first, kv.second); ++n; }
    if (!g_hidden.empty()) LOGI("online_ui", "-", "%d story blips shown again", n);
    g_hidden.clear();
}

bool HandleCommand(const std::string &v, std::istringstream &in) {
    if (v == "blips") {   // every blip on the map: sprite, place, colour, display, what it is attached to
        int n = 0;
        for (int sprite = 0; sprite < 900; ++sprite) {
            for (int b = N<int>(N_GET_FIRST_BLIP_INFO_ID, sprite); N<int>(N_DOES_BLIP_EXIST, b); b = N<int>(N_GET_NEXT_BLIP_INFO_ID, sprite)) {
                NVec3 c = N<NVec3>(N_GET_BLIP_INFO_ID_COORD, b);
                int type = N<int>(N_GET_BLIP_INFO_ID_TYPE, b), ent = N<int>(N_GET_BLIP_INFO_ID_ENTITY_INDEX, b);
                uint32_t model = ent && N<int>(N_DOES_ENTITY_EXIST, ent) ? gta::ModelOf(ent) : 0;
                LOGI("blips", "-", "blip %d sprite %d at %.1f %.1f %.1f colour %d display %d type %d alpha %d short %d entity %d model 0x%08X",
                     b, sprite, c.x, c.y, c.z, N<int>(N_GET_BLIP_COLOUR, b), N<int>(N_GET_BLIP_INFO_ID_DISPLAY, b), type, N<int>(N_GET_BLIP_ALPHA, b),
                     N<int>(N_IS_BLIP_SHORT_RANGE, b), ent, model);
                if (++n > 2000) return true;
            }
        }
        LOGI("blips", "-", "%d blips", n);
        return true;
    }
    if (v == "ui.hidden") { LOGI("online_ui", "-", "%d story blips hidden", (int)g_hidden.size()); return true; }
    if (v == "ui.pause") {   // opens the pause menu on the map (tests)
        N(N_ACTIVATE_FRONTEND_MENU, gta::Joaat("FE_MENU_VERSION_SP_PAUSE"), 0, -1);
        return true;
    }
    if (v == "ui.close") { N(N_SET_FRONTEND_ACTIVE, 0); return true; }
    (void)in;
    return false;
}

}  // namespace online_ui
