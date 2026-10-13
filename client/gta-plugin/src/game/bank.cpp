#include "game/bank.h"
#include <cstdlib>
#include "common/log.h"
#include "engine/gta.h"
#include "engine/shv.h"
#include "game/devcmd.h"
#include "game/economy.h"
#include "game/hud.h"
#include "protocol/protocol.h"
#include "ui/menu.h"

namespace bank {

namespace {

constexpr int kContext = 51;   // INPUT_CONTEXT (E / d-pad right)
const char *kAtmModels[] = {"prop_atm_01", "prop_atm_02", "prop_atm_03", "prop_fleeca_atm"};
const int64_t kAmounts[] = {100, 500, 1000, 5000, 10000, 50000, 100000};

int g_atm = 0;
int g_lastScan = 0;

int NearestAtm(int me) {
    gta::Vec3 p = gta::Pos(me);
    for (const char *m : kAtmModels) {
        int o = N<int>(N_GET_CLOSEST_OBJECT_OF_TYPE, p.x, p.y, p.z, 1.6f, gta::Joaat(m), 0, 0, 0);
        if (o && gta::Exists(o)) return o;
    }
    return 0;
}

bool AskAmount(const char *title, int64_t max, int64_t &out) {
    std::string s;
    if (!gta::Keyboard(title, "", 12, s) || s.empty()) return false;
    long long v = std::atoll(s.c_str());
    if (v <= 0) { gta::Notify("~r~Enter an amount above $0."); return false; }
    if (v > max) { gta::Notify("~r~Not enough money:~s~ the maximum is " + hud::Money(max) + "."); return false; }
    out = v;
    return true;
}

bool Deposit(int64_t v) {
    if (!economy::Change(proto::kDeposit, v, "ATM deposit")) { gta::Notify("~r~Not enough cash."); return false; }
    N(N_PLAY_SOUND_FRONTEND, -1, "PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET", 1);
    return true;
}

bool Withdraw(int64_t v) {
    if (!economy::Change(proto::kDeposit, -v, "ATM withdrawal")) { gta::Notify("~r~Insufficient funds in your Maze Bank account."); return false; }
    N(N_PLAY_SOUND_FRONTEND, -1, "PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET", 1);
    return true;
}

}  // namespace

void Menu(bool atAtm) {
    ui::Menu m;
    m.title = "MAZE BANK";
    m.subtitle = atAtm ? "ATM" : "Online banking";
    bool close = false;
    int amountIdx = 2;
    m.items.push_back({"Balance", [] { return hud::Money(economy::Total()); }, nullptr, nullptr, "Bank and cash together."});
    m.items.push_back({"Deposit all cash", nullptr, nullptr, [&] { /* filled below */ }, "Moves the cash you carry into your account."});
    m.items.push_back({"Deposit amount...", nullptr, nullptr, nullptr, ""});
    if (atAtm) {
        m.items.push_back({"Withdraw", [&] { return hud::Money(kAmounts[amountIdx]); },
                           [&](int d) { amountIdx = (amountIdx + d + 7) % 7; }, [&] { Withdraw(kAmounts[amountIdx]); }, "Left/right chooses the amount."});
        m.items.push_back({"Withdraw amount...", nullptr, nullptr, nullptr, ""});
    }
    m.items.push_back({"Close", nullptr, nullptr, [&] { close = true; }, ""});
    // items that need the balances (set once the accessors are known)
    for (auto &it : m.items) {
        if (it.label == "Deposit all cash") it.select = [] { int64_t w = economy::Wallet(); if (w > 0) Deposit(w); else gta::Notify("You are not carrying any cash."); };
        else if (it.label == "Deposit amount...") it.select = [] { int64_t v; if (AskAmount("Deposit amount", economy::Wallet(), v)) Deposit(v); };
        else if (it.label == "Withdraw amount...") it.select = [] { int64_t v; if (AskAmount("Withdraw amount", economy::Bank(), v)) Withdraw(v); };
        else if (it.label == "Balance") it.value = [] { return "Bank " + hud::Money(economy::Bank()) + "   Cash " + hud::Money(economy::Wallet()); };
    }
    if (!atAtm) m.items[0].help = "Withdrawals need an ATM.";
    int me = gta::PlayerPed();
    if (atAtm) N(N_TASK_START_SCENARIO_IN_PLACE, me, "PROP_HUMAN_ATM", 0, 1);
    while (!close) {
        hud::ShowMoney(500);
        std::string line;
        if (devcmd::NextFor("ui back", line)) {
            if (line.rfind("ui ", 0) == 0) m.Activate(line.substr(3));
            else close = true;
        }
        if (!m.Frame()) close = true;
        gta::Wait(0);
    }
    if (atAtm) N(N_CLEAR_PED_TASKS, me);
}

void Frame(int me) {
    if (gta::InVehicle(me)) { g_atm = 0; return; }
    int now = gta::GameTimeMs();
    if (now - g_lastScan > 400) { g_lastScan = now; g_atm = NearestAtm(me); }
    if (!g_atm) return;
    gta::Help("Press ~INPUT_CONTEXT~ to use the ATM.");
    if (gta::JustPressed(kContext)) Menu(true);
}

bool HandleCommand(const std::string &v, std::istringstream &in) {
    long long x = 0;
    if (v == "bank.menu") Menu(false);
    else if (v == "bank.atm") Menu(true);
    else if (v == "bank.deposit") { in >> x; LOGI("devcmd", "-", "deposit %lld -> %s", x, x > 0 && Deposit(x) ? "ok" : "refused"); }
    else if (v == "bank.withdraw") { in >> x; LOGI("devcmd", "-", "withdraw %lld -> %s", x, x > 0 && Withdraw(x) ? "ok" : "refused"); }
    else if (v == "bank.status") LOGI("devcmd", "-", "wallet %lld bank %lld, %d pending, ATM %s", (long long)economy::Wallet(), (long long)economy::Bank(), economy::PendingCount(), g_atm ? "near" : "none");
    else return false;
    return true;
}

}  // namespace bank
