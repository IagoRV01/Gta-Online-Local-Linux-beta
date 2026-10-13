#include "ui/startup.h"
#include <cmath>
#include "common/log.h"
#include "engine/gta.h"
#include "engine/shv.h"
#include "game/devcmd.h"

namespace startup {

using namespace gta;

namespace {

const Vec3 kOrbitCentre{-75.f, -818.f, 160.f};   // Maze Bank Tower, downtown
const float kOrbitRadius = 950.f, kOrbitHeight = 380.f;

const char *kPortraits[] = {"CHAR_MICHAEL", "CHAR_FRANKLIN", "CHAR_TREVOR"};

struct Card { float x; const char *title; Color band; };
const float kCardY = 0.58f, kCardW = 0.30f, kCardH = 0.46f;
const Card kCards[2] = {{0.335f, "STORY MODE", {46, 125, 50, 255}}, {0.665f, "LOCAL ONLINE", {41, 128, 214, 255}}};

void Sprite(const char *dict, float x, float y, float h, int alpha) {
    if (!NB(N<int>(N_HAS_STREAMED_TEXTURE_DICT_LOADED, dict))) return;
    float w = h * 9.f / 16.f;   // square texture on a 16:9 screen
    N(N_DRAW_SPRITE, dict, dict, x, y, w, h, 0.f, 255, 255, 255, alpha, 0, 0);
}

void DrawCard(int i, bool selected, const Info &info, float pulse) {
    const Card &c = kCards[i];
    if (selected) Rect(c.x, kCardY, kCardW + 0.008f, kCardH + 0.014f, {255, 255, 255, (uint8_t)(170 + 60 * pulse)});
    Rect(c.x, kCardY, kCardW, kCardH, {8, 8, 12, (uint8_t)(selected ? 235 : 190)});
    float top = kCardY - kCardH / 2;
    Rect(c.x, top + 0.045f, kCardW, 0.09f, selected ? c.band : Color{c.band.r, c.band.g, c.band.b, 150});
    Text(c.title, c.x, top + 0.012f, 1.05f, kWhite, 0, 7);
    float left = c.x - kCardW / 2 + 0.014f, wrap = c.x + kCardW / 2 - 0.014f;
    if (i == 0) {
        for (int k = 0; k < 3; ++k) Sprite(kPortraits[k], c.x + (k - 1) * 0.075f, top + 0.18f, 0.115f, selected ? 255 : 160);
        Text("Michael, Franklin and Trevor.", left, top + 0.27f, 0.42f, kWhite, 1, 4, wrap);
        Text("Continue your story save exactly where you left it.", left, top + 0.31f, 0.36f, kGrey, 1, 0, wrap);
    } else {
        Sprite("CHAR_MULTIPLAYER", c.x, top + 0.18f, 0.115f, selected ? 255 : 160);
        if (info.characterName.empty()) {
            Text("Create your character.", left, top + 0.27f, 0.42f, kWhite, 1, 4, wrap);
            Text("A fourth protagonist, saved on your own server.", left, top + 0.31f, 0.36f, kGrey, 1, 0, wrap);
        } else {
            Text("Continue as " + info.characterName, left, top + 0.27f, 0.42f, kWhite, 1, 4, wrap);
            Text("Your freemode character. Michael, Franklin and Trevor stay untouched.", left, top + 0.31f, 0.36f, kGrey, 1, 0, wrap);
        }
        if (!info.serverText.empty()) Text(info.serverText, left, top + 0.39f, 0.3f, kAccent, 1, 0, wrap);
    }
}

}  // namespace

Choice Run(const Info &info) {
    N(N_DO_SCREEN_FADE_OUT, 0);
    int me = PlayerPed();
    N(N_SET_PLAYER_CONTROL, PlayerId(), 0, 0);
    Freeze(me, true);
    for (const char *d : kPortraits) N(N_REQUEST_STREAMED_TEXTURE_DICT, d, 0);
    N(N_REQUEST_STREAMED_TEXTURE_DICT, "CHAR_MULTIPLAYER", 0);

    unsigned long long t0 = GetTickCount64();
    auto camAt = [&](float &angle) {
        angle = (float)(GetTickCount64() - t0) / 1000.f * 0.012f + 3.6f;   // ~0.7 deg/s
        Vec3 eye{kOrbitCentre.x + kOrbitRadius * std::cos(angle), kOrbitCentre.y + kOrbitRadius * std::sin(angle), kOrbitHeight};
        return eye;
    };
    float angle = 0;
    Vec3 eye = camAt(angle);
    int cam = CamCreate(eye, kOrbitCentre, 50.f);
    N(N_SET_FOCUS_POS_AND_VEL, kOrbitCentre.x, kOrbitCentre.y, kOrbitCentre.z, 0.f, 0.f, 0.f);
    LOGI("startup", "-", "start screen shown (character: %s)", info.characterName.empty() ? "none" : info.characterName.c_str());

    int sel = 1;   // Local Online preselected
    bool faded = false, done = false;
    Choice choice = Choice::LocalOnline;
    while (!done) {
        if (!faded && GetTickCount64() - t0 > 1800) { FadeIn(1200); faded = true; }   // give the city time to stream
        eye = camAt(angle);
        CamMove(cam, eye, kOrbitCentre, 50.f);

        DisableAllControls();
        N(N_HIDE_HUD_AND_RADAR_THIS_FRAME);
        N(N_THEFEED_HIDE_THIS_FRAME);
        N(N_SET_MOUSE_CURSOR_THIS_FRAME);

        // input: pad/keyboard left-right, mouse hover + click
        if (JustPressed(kLeft)) sel = 0;
        if (JustPressed(kRight)) sel = 1;
        float mx = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 0, 239), my = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 0, 240);
        int hover = -1;
        for (int i = 0; i < 2; ++i)
            if (std::fabs(mx - kCards[i].x) < kCardW / 2 && std::fabs(my - kCardY) < kCardH / 2) hover = i;
        static float lastMx = -1, lastMy = -1;
        if (hover >= 0 && (mx != lastMx || my != lastMy)) sel = hover;   // only follow the mouse when it moves
        lastMx = mx; lastMy = my;
        if (JustPressed(kAccept) || (hover >= 0 && JustPressed(237))) { choice = sel == 0 ? Choice::Story : Choice::LocalOnline; done = true; }
        std::string line;
        if (devcmd::NextFor("ui", line)) {
            if (line.find("story") != std::string::npos) { choice = Choice::Story; done = true; }
            else if (line.find("online") != std::string::npos) { choice = Choice::LocalOnline; done = true; }
        }

        // draw
        float pulse = 0.5f + 0.5f * std::sin((float)(GetTickCount64() - t0) / 300.f);
        Rect(0.5f, 0.5f, 1.f, 1.f, {0, 0, 0, 90});
        Rect(0.5f, 0.06f, 1.f, 0.12f, {0, 0, 0, 160});
        Text("grand theft auto v", 0.05f, 0.025f, 1.0f, kWhite, 1, 7);
        Text("LOCAL ONLINE", 0.052f, 0.083f, 0.5f, kAccent, 1, 4);
        Text("Choose how to play", 0.5f, 0.24f, 0.6f, kWhite, 0, 4);
        DrawCard(0, sel == 0, info, pulse);
        DrawCard(1, sel == 1, info, pulse);
        Rect(0.5f, 0.93f, 1.f, 0.05f, {0, 0, 0, 150});
        Text("LEFT / RIGHT  choose          ENTER / A  play          mouse  click a card", 0.5f, 0.915f, 0.36f, kWhite, 0, 0);
        Wait(0);
    }

    N(N_PLAY_SOUND_FRONTEND, -1, "SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET", 1);
    LOGI("startup", "-", "chose %s", choice == Choice::Story ? "STORY MODE" : "LOCAL ONLINE");
    FadeOut(500);
    CamDestroy(cam);
    N(N_CLEAR_FOCUS);
    for (const char *d : kPortraits) N(N_SET_STREAMED_TEXTURE_DICT_AS_NO_LONGER_NEEDED, d);
    N(N_SET_STREAMED_TEXTURE_DICT_AS_NO_LONGER_NEEDED, "CHAR_MULTIPLAYER");
    Freeze(me, false);
    N(N_SET_PLAYER_CONTROL, PlayerId(), 1, 0);
    if (choice == Choice::Story) { Wait(300); FadeIn(800); }
    return choice;
}

}  // namespace startup
