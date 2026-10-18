#include "game/hud.h"
#include <cstdio>
#include <deque>
#include <map>
#include "engine/gta.h"
#include "engine/shv.h"

namespace hud {

namespace {

constexpr int kShowMs = 6000;
constexpr int kDeltaMs = 4500;
constexpr int kMultiplayerInfo = 20;   // INPUT_MULTIPLAYER_INFO (Z / d-pad down)
constexpr gta::Color kCashGreen{114, 204, 114, 255}, kLoss{224, 50, 50, 255}, kLabel{200, 200, 200, 255};

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

// ---- the player list on Z / d-pad down: Online's own card (mp_mm_card_freemode). The slot layout (as used by the
// FiveM-Playerlist project with this movie): SET_DATA_SLOT(slot, right text (the rank), name, HUD colour, right icon
// (65 = the freemode rank badge), icon overlay text, job points text, crew label, job points display type,
// headshot txd, headshot txd, friend type); SET_TITLE(left, right, icon "2"); DISPLAY_VIEW.
int g_card = 0, g_cardShownAt = 0, g_cardBuiltAt = 0;
std::map<int, int> g_shots;   // ped -> headshot handle

std::string Headshot(int ped) {
    if (!ped || !N<int>(N_DOES_ENTITY_EXIST, ped)) return "";
    auto it = g_shots.find(ped);
    if (it == g_shots.end()) { int h = N<int>(N_REGISTER_PEDHEADSHOT, ped); if (h) g_shots[ped] = h; return ""; }
    if (!NB(N<int>(N_IS_PEDHEADSHOT_VALID, it->second)) || !NB(N<int>(N_IS_PEDHEADSHOT_READY, it->second))) return "";
    const char *t = N<const char *>(N_GET_PEDHEADSHOT_TXD_STRING, it->second);
    return t ? t : "";
}

void ScStr(const std::string &s) { N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_PLAYER_NAME_STRING, s.c_str()); }
void ScInt(int v) { N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, v); }

void PlayerCard(const std::vector<PlayerRow> &players) {
    if (!g_card) g_card = N<int>(N_REQUEST_SCALEFORM_MOVIE, "mp_mm_card_freemode");
    if (!NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, g_card))) return;
    int now = gta::GameTimeMs();
    if (!g_cardShownAt) { g_cardShownAt = now; g_cardBuiltAt = 0; }
    if (now - g_cardBuiltAt > 500) {   // rebuilt twice a second: names, ranks and headshots as they load
        g_cardBuiltAt = now;
        N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, g_card, "SET_DATA_SLOT_EMPTY"); ScInt(0); ScInt(0); N(N_END_SCALEFORM_MOVIE_METHOD);
        N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, g_card, "SET_TITLE");
        ScStr("GTA Local Online (" + std::to_string(players.size()) + ")"); ScStr(""); ScStr("2");
        N(N_END_SCALEFORM_MOVIE_METHOD);
        int slot = 0;
        for (const auto &p : players) {
            if (slot >= 16) break;
            std::string shot = Headshot(p.ped);
            N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, g_card, "SET_DATA_SLOT");
            ScInt(slot++);
            ScStr(std::to_string(p.rank > 0 ? p.rank : 1));   // inside the rank badge
            ScStr(p.name);
            ScInt(p.self ? 116 : p.passive ? 6 : 111);          // HUD_COLOUR_FREEMODE for you, grey when passive
            ScInt(65);                                          // the freemode rank badge
            ScStr("");                                          // icon overlay text
            ScStr(p.passive ? "PASSIVE" : p.wanted ? std::string(p.wanted, '*') : "");   // job points text
            ScStr("");                                          // crew label
            ScInt(0);                                           // job points: number only
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_TEXTURE_NAME_STRING, shot.c_str());
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_TEXTURE_NAME_STRING, shot.c_str());
            ScStr(" ");                                         // friend type
            N(N_END_SCALEFORM_MOVIE_METHOD);
        }
        N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, g_card, "DISPLAY_VIEW"); N(N_END_SCALEFORM_MOVIE_METHOD);
    }
    N(N_SET_SCRIPT_GFX_DRAW_ORDER, 7);
    N(N_DRAW_SCALEFORM_MOVIE, g_card, 0.122f, 0.3f, 0.28f, 0.6f, 255, 255, 255, 255, 0);   // where freemode.ysc draws it
    N(N_SET_SCRIPT_GFX_DRAW_ORDER, 4);
    for (auto it = g_shots.begin(); it != g_shots.end();) {   // headshots of peds that are gone
        if (!N<int>(N_DOES_ENTITY_EXIST, it->first)) { N(N_UNREGISTER_PEDHEADSHOT, it->second); it = g_shots.erase(it); } else ++it;
    }
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

    if (info) PlayerCard(players);
    else g_cardShownAt = 0;
    (void)status;
}

}  // namespace hud
