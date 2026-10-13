#include "game/hud.h"
#include <cstdio>
#include <deque>
#include "engine/gta.h"
#include "engine/shv.h"

namespace hud {

namespace {

constexpr int kShowMs = 6000;
constexpr int kDeltaMs = 4500;
constexpr int kMultiplayerInfo = 20;   // INPUT_MULTIPLAYER_INFO (Z / d-pad down)
constexpr gta::Color kCashGreen{114, 204, 114, 255}, kLoss{224, 50, 50, 255}, kLabel{200, 200, 200, 255},
                     kListHeader{40, 72, 130, 230}, kListRow{0, 0, 0, 150}, kListSelf{93, 182, 229, 200};

struct Delta { int64_t amount; bool bank; int at; };
std::deque<Delta> g_deltas;
int g_showUntil = 0;

float Margin() {
    float s = N<float>(N_GET_SAFE_ZONE_SIZE);   // 1.0 = full screen; the HUD keeps (1-s)/2 from each edge
    if (s < 0.85f || s > 1.f) s = 1.f;
    return (1.f - s) * 0.5f;
}

void MoneyLine(const char *label, int64_t v, float right, float y) {
    std::string t = Money(v);
    gta::Text(t, right, y, 0.56f, gta::kWhite, 2, 7, right);
    float lr = right - gta::TextWidth(t, 0.56f, 7) - 0.004f;
    gta::Text(label, lr, y + 0.013f, 0.3f, kLabel, 2, 0, lr);
}

}  // namespace

std::string Money(int64_t v) {
    bool neg = v < 0;
    unsigned long long u = neg ? (unsigned long long)(-v) : (unsigned long long)v;
    std::string digits = std::to_string(u), out;
    int n = (int)digits.size();
    for (int i = 0; i < n; ++i) {
        out += digits[i];
        if ((n - i - 1) % 3 == 0 && i != n - 1) out += ',';
    }
    return (neg ? "-$" : "$") + out;
}

void OnMoneyChange(int64_t dWallet, int64_t dBank) {
    int now = gta::GameTimeMs();
    if (dBank) g_deltas.push_back({dBank, true, now});
    if (dWallet) g_deltas.push_back({dWallet, false, now});
    while (g_deltas.size() > 4) g_deltas.pop_front();
    ShowMoney(kShowMs);
}

void ShowMoney(int ms) { g_showUntil = gta::GameTimeMs() + ms; }

void Reset() { g_deltas.clear(); g_showUntil = 0; }

void Frame(int64_t wallet, int64_t bank, const std::vector<PlayerRow> &players, const std::string &status) {
    // The story cash (component 3) and the MP cash components belong to the story save: never for YOU.
    N(N_HIDE_HUD_COMPONENT_THIS_FRAME, 3);
    N(N_HIDE_HUD_COMPONENT_THIS_FRAME, 4);
    N(N_HIDE_HUD_COMPONENT_THIS_FRAME, 13);
    if (N<int>(N_IS_PAUSE_MENU_ACTIVE) || N<int>(N_IS_HUD_HIDDEN)) return;

    int now = gta::GameTimeMs();
    bool info = gta::Pressed(kMultiplayerInfo);
    if (info) ShowMoney(1500);
    while (!g_deltas.empty() && now - g_deltas.front().at > kDeltaMs) g_deltas.pop_front();
    float m = Margin(), right = 1.f - m - 0.004f, y = m + 0.008f;

    if (now < g_showUntil) {
        MoneyLine("BANK", bank, right, y);
        MoneyLine("CASH", wallet, right, y + 0.036f);
        float dy = y + 0.074f;
        for (const auto &d : g_deltas) {
            std::string t = (d.amount > 0 ? "+" : "-") + Money(d.amount > 0 ? d.amount : -d.amount) + (d.bank ? "" : "  cash");
            int age = now - d.at, alpha = age > kDeltaMs - 600 ? 255 * (kDeltaMs - age) / 600 : 255;
            gta::Color c = d.amount > 0 ? kCashGreen : kLoss;
            c.a = (uint8_t)(alpha < 0 ? 0 : alpha);
            gta::Text(t, right, dy, 0.42f, c, 2, 7, right);
            dy += 0.028f;
        }
    }

    if (info) {   // player list, like Online's on Z
        float w = 0.17f, x = right - w * 0.5f, ly = y + 0.19f, rh = 0.027f;
        gta::Rect(x, ly, w, rh + 0.004f, kListHeader);
        gta::Text("GTA LOCAL ONLINE", right - w + 0.005f, ly - 0.012f, 0.32f, gta::kWhite, 1, 4);
        char cnt[24];
        std::snprintf(cnt, sizeof cnt, "%d/32", (int)players.size());
        gta::Text(cnt, right - 0.004f, ly - 0.012f, 0.32f, gta::kWhite, 2, 4, right - 0.004f);
        ly += rh + 0.004f;
        for (const auto &p : players) {
            gta::Rect(x, ly, w, rh, p.self ? kListSelf : kListRow);
            gta::Text(p.name, right - w + 0.006f, ly - 0.0115f, 0.32f, gta::kWhite, 1, 4);
            if (p.wanted) gta::Text(std::string(p.wanted, '*'), right - 0.005f, ly - 0.0115f, 0.32f, gta::kYellow, 2, 4, right - 0.005f);
            ly += rh + 0.002f;
        }
        if (!status.empty()) gta::Text(status, right - w + 0.004f, ly - 0.009f, 0.26f, kLabel, 1, 0);
    }
}

}  // namespace hud
