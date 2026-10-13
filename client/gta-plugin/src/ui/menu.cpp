#include "ui/menu.h"
#include <cstdio>
#include "engine/gta.h"
#include "engine/shv.h"

namespace ui {

using namespace gta;

static const float kX = 0.155f, kW = 0.24f, kTop = 0.08f, kRow = 0.034f;
static const int kVisible = 12;

bool Menu::Frame() {
    DisableAllControls();
    N(N_HIDE_HUD_AND_RADAR_THIS_FRAME);
    int n = (int)items.size();
    if (sel >= n) sel = n - 1;
    if (sel < 0) sel = 0;

    // ---- input
    bool back = false;
    if (JustPressed(kUp)) { sel = (sel + n - 1) % n; N(N_PLAY_SOUND_FRONTEND, -1, "NAV_UP_DOWN", "HUD_FRONTEND_DEFAULT_SOUNDSET", 1); }
    if (JustPressed(kDown)) { sel = (sel + 1) % n; N(N_PLAY_SOUND_FRONTEND, -1, "NAV_UP_DOWN", "HUD_FRONTEND_DEFAULT_SOUNDSET", 1); }
    int now = GameTimeMs();
    int dir = Pressed(kLeft) ? -1 : Pressed(kRight) ? 1 : 0;
    if (dir && n) {
        bool fire = false;
        if (dir != holdDir_) { holdDir_ = dir; holdStart_ = now; lastRepeat_ = now; fire = true; }
        else if (now - holdStart_ > 350 && now - lastRepeat_ > (now - holdStart_ > 1500 ? 30 : 90)) { lastRepeat_ = now; fire = true; }
        if (fire && items[sel].change) { items[sel].change(dir); N(N_PLAY_SOUND_FRONTEND, -1, "NAV_LEFT_RIGHT", "HUD_FRONTEND_DEFAULT_SOUNDSET", 1); }
    } else holdDir_ = 0;
    if (JustPressed(kAccept) && n && items[sel].select) {
        N(N_PLAY_SOUND_FRONTEND, -1, "SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET", 1);
        items[sel].select();
    }
    if (JustPressed(kCancel)) { back = true; N(N_PLAY_SOUND_FRONTEND, -1, "BACK", "HUD_FRONTEND_DEFAULT_SOUNDSET", 1); }

    // ---- draw
    n = (int)items.size();   // select() may have changed the list
    if (sel >= n) sel = n - 1;
    Rect(kX, kTop, kW, 0.075f, {20, 110, 180, 230});
    Text(title, kX, kTop - 0.03f, 0.9f, kWhite, 0, 1);
    float y = kTop + 0.0375f + kRow / 2;
    Rect(kX, y, kW, kRow, {0, 0, 0, 230});
    Text(subtitle, kX - kW / 2 + 0.006f, y - 0.013f, 0.33f, kAccent, 1, 0);
    char pos[24];
    std::snprintf(pos, sizeof pos, "%d / %d", sel + 1, n);
    Text(pos, 0, y - 0.013f, 0.33f, kAccent, 2, 0, kX + kW / 2 - 0.006f);
    int first = sel - kVisible / 2;
    if (first > n - kVisible) first = n - kVisible;
    if (first < 0) first = 0;
    for (int i = first; i < n && i < first + kVisible; ++i) {
        y += kRow;
        bool on = i == sel;
        Rect(kX, y, kW, kRow, on ? Color{240, 240, 240, 235} : Color{0, 0, 0, 170});
        Color tc = on ? Color{10, 10, 10, 255} : kWhite;
        Text(items[i].label, kX - kW / 2 + 0.006f, y - 0.013f, 0.33f, tc, 1, 0);
        if (items[i].value) {
            std::string v = items[i].value();
            if (items[i].change) v = "< " + v + " >";
            Text(v, 0, y - 0.013f, 0.33f, tc, 2, 0, kX + kW / 2 - 0.006f);
        }
    }
    if (n && !items[sel].help.empty()) {
        y += kRow + 0.004f;
        Rect(kX, y + 0.01f, kW, kRow + 0.02f, {0, 0, 0, 200});
        Text(items[sel].help, kX - kW / 2 + 0.006f, y - 0.008f, 0.3f, kWhite, 1, 0, kX + kW / 2 - 0.006f);
    }
    return !back;
}

bool Menu::Activate(const std::string &prefix, int dir) {
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].label.compare(0, prefix.size(), prefix) != 0) continue;
        sel = (int)i;
        if (dir && items[i].change) items[i].change(dir);
        else if (!dir && items[i].select) items[i].select();
        return true;
    }
    return false;
}

}  // namespace ui
