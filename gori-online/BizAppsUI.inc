// ---------------------------------------------------------------- The business computers, GTA Online's own apps
//  - the office's SecuroServ computer: Rockstar's SECUROSERV movie, fed like appsecuroserv.ysc does: SET_PLAYER_DATA,
//    the 22 Special Cargo warehouses (ADD_WAREHOUSE / ADD_WAREHOUSE_SHIPMENTS), the vehicle warehouse; the movie runs
//    its own screens (log in, branch, stats, warehouse map) and the script answers the buttons it reports
//    (3007 buy a warehouse, 3008-3010 buy a shipment, 3011 sell, 3012/3013 the overlay's confirm/cancel, 4001 steal a car);
//  - the data center's DataNet terminal: the Bunker's computer (Disruption Logistics) rebranded for the data center,
//    driven like appbunkerbusiness.ysc: SET_STATS, SET_RESUPPLIES, SET_UPGRADES, SET_RESEARCH, SET_BUTTON_STATES and
//    SHOW_SCREEN when a navigation button is chosen. "Set Up" is the Hacking Contract and "Restart" the crypto wallet.
// Included from GoriOnline.cpp after BizApps.inc.

static const char *DATANET_MOVIE = "GORI_DATANET_SYSTEMS";   // our copy of DISRUPTION_LOGISTICS (IslandBuilder tools_datanet_movie.py)
static const char *DN_IMG = "dn_";                            // its images: dn_generic.ytd (dn_resupply, dn_unlock_1...)

struct BizApp {
    int kind = 0;                 // 1 SecuroServ, 2 DataNet
    int mv = 0, pi = -1;
    int rvSel = 0, rvAux = 0, rvScr = 0;
    int screen = -1, pollAt = 0, openedAt = 0;
    int pending = 0, pendArg = 0;
    float lmx = -1.f, lmy = -1.f;
    std::function<void()> after;  // a mission to start once the computer is closed
} g_app;
static bool BizAppOpen() { return g_app.kind != 0; }
static void DcResearchTickForce(int pi);

// ---- scaleform helpers
static void AppCall(const char *m) { N(N_BEGIN_SCALEFORM_MOVIE_METHOD, g_app.mv, m); }
static void AppEnd() { N(N_END_SCALEFORM_MOVIE_METHOD); }
static void PInt(int v) { N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, v); }
static void PFloat(float v) { N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, v); }
static void PBool(bool v) { N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, v ? 1 : 0); }
static void PStr(const std::string &v) { N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_TEXTURE_NAME_STRING, v.c_str()); }
static void PLabel(const char *label) { N(N_BEGIN_TEXT_COMMAND_SCALEFORM_STRING, label); N(N_END_TEXT_COMMAND_SCALEFORM_STRING); }
static void PText(const std::string &t) {   // our own text, through the game's text system like a label
    N(N_BEGIN_TEXT_COMMAND_SCALEFORM_STRING, "STRING"); N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, t.c_str()); N(N_END_TEXT_COMMAND_SCALEFORM_STRING);
}
static void AppInput(int id) { AppCall("SET_INPUT_EVENT"); PInt(id); AppEnd(); }
static void AppSound(const char *name, const char *set) { N(N_PLAY_SOUND_FRONTEND, -1, name, set, 1); }
static std::string PlayerTag() { const char *n = N<const char *>(N_GET_PLAYER_NAME, Player()); return n && *n ? n : "Gori"; }
static int CustInt(const std::string &key, int def = 0) { auto it = g_cust.find(key); return it == g_cust.end() ? def : it->second; }
static void CustAdd(const std::string &key, int v) { g_cust[key] = CustInt(key) + v; }

static bool AppLoad(const char *movie) {
    g_app.mv = N<int>(N_REQUEST_SCALEFORM_MOVIE, movie);
    int t0 = Now();
    while (!NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, g_app.mv)) && Now() - t0 < 8000) Wait(0);
    if (!NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, g_app.mv))) { Log("app: movie %s did not load", movie); N(N_SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED, &g_app.mv); g_app = BizApp(); return false; }
    // like the game's scripts: the mouse only moves the app's cursor once it moves (the log in screen puts it on its button)
    g_app.lmx = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 239); g_app.lmy = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 240);
    return true;
}
static void AppClose(std::function<void()> then = nullptr) {
    if (g_app.mv) N(N_SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED, &g_app.mv);
    Log("app %d closed", g_app.kind);
    g_app = BizApp();
    if (then) g_secPending = then;   // DeskTick stands the player up, then runs it
}

// ================================================================ SecuroServ
static const int SHIP_COST[4] = {0, 6000, 12000, 18000};   // 1, 2 or 3 crates
static const char *SS_SET = "GTAO_Exec_SecuroServ_Computer_Sounds";
static std::vector<int> WarehouseProps() { std::vector<int> v; for (int i = 0; i < NPROPS; ++i) if (PROPS[i].type == T_WAREHOUSE) v.push_back(i); return v; }
static std::vector<int> OwnedWarehouses() { std::vector<int> v; for (int i : WarehouseProps()) if (Owns(PROPS[i].name)) v.push_back(i); return v; }
static std::string SsKey(const char *what) { return std::string("SecuroServ|") + what; }

static void SecAddWarehouse(int k, int i) {
    const PropDef &p = PROPS[i];
    bool own = Owns(p.name);
    BizState *b = BizOf(p.name);
    int n = b ? (int)b->stock : 0, cap = CargoMaxP(i);
    std::string in = p.interior; int size = in == "whs_s" ? 0 : in == "whs_m" ? 1 : 2;
    auto owned = OwnedWarehouses(); int order = -1;
    for (size_t q = 0; q < owned.size(); ++q) if (owned[q] == i) order = (int)q;
    char lab[32], des[32], txd[16];
    std::snprintf(lab, sizeof lab, "MP_WHOUSE_%d", k); std::snprintf(des, sizeof des, "MP_WHOUSE_%dDES", k); std::snprintf(txd, sizeof txd, "DYN_MPWH_%d", k + 1);
    const char *zone = (k == 5 || k == 7) ? "WH_LISA_1" : N<const char *>(N_GET_NAME_OF_ZONE, p.x, p.y, p.z);
    AppCall("ADD_WAREHOUSE");
    PInt(k); PFloat(p.x); PFloat(p.y); PInt(p.price);
    PLabel(lab); PLabel(zone ? zone : ""); PLabel(des); PStr(txd);
    PBool(own); PInt(size); PInt(cap); PInt(n); PInt(own ? CargoTotal(i, n) : 0);
    PInt(-1); PInt(0); PInt(order); PInt(-1);
    AppEnd();
    AppCall("ADD_WAREHOUSE_SHIPMENTS");
    PInt(k); PInt(1); PInt(SHIP_COST[1]); PText("Special Cargo"); PInt(2); PInt(SHIP_COST[2]); PText(""); PInt(3); PInt(SHIP_COST[3]);
    PBool(false); PInt(-1); PInt(-1); PInt(-1);
    AppEnd();
}
static void SecFeed() {
    Vec3 me = Pos(Me());
    int vw = OwnedOfType(T_VEHWARE);
    AppCall("SET_PLAYER_DATA");
    PStr(PlayerTag()); PBool(true); PFloat((float)CustInt(SsKey("Earned")));
    int col = CustInt(SsKey("Collections")), sal = CustInt(SsKey("Sales"));
    PInt(col); PFloat(col ? 100.f : 0.f); PInt(sal); PFloat(sal ? 100.f : 0.f);
    PStr("Gori Executive");
    PInt(CustInt(SsKey("Stolen"))); PFloat(100.f); PInt(CustInt(SsKey("Exported"))); PFloat(100.f); PInt(CustInt(SsKey("VehEarned")));
    PBool(false); PFloat(me.x); PFloat(me.y); PBool(true); PBool(vw >= 0);
    AppEnd();
    auto all = WarehouseProps();
    for (size_t k = 0; k < all.size(); ++k) SecAddWarehouse((int)k, all[k]);
    if (vw >= 0) {   // the vehicle warehouse (Import / Export)
        const PropDef &p = PROPS[vw]; BizState *b = BizOf(p.name);
        int n = b ? (int)b->stock : 0;
        AppCall("ADD_VEHICLE_WAREHOUSE");
        PInt(0); PFloat(p.x); PFloat(p.y); PInt(p.price); PInt(-1); PInt(0); PInt(-1); PInt(0); PInt(-1);
        PText(p.name); PLabel(N<const char *>(N_GET_NAME_OF_ZONE, p.x, p.y, p.z)); PText("Your vehicle warehouse.");
        PStr(""); PBool(true); PInt(CargoMaxP(vw)); PInt(n); PInt(CargoTotal(vw, n)); PFloat(0.f); PInt(0);
        AppEnd();
    }
}
static void SecOverlay(const char *title, const std::string &msg, bool twoButtons, bool literal = false) {
    AppCall("SHOW_OVERLAY");
    PLabel(title);
    if (literal) PText(msg); else PLabel(msg.c_str());
    PLabel("WHOUSE_CONF"); PLabel(twoButtons ? "WHOUSE_CANC" : "");
    PBool(twoButtons); PBool(false);
    AppEnd();
}
static int SecWarehouseByOrder(int k) { auto all = WarehouseProps(); return k >= 0 && k < (int)all.size() ? all[k] : -1; }
enum { SSP_NONE, SSP_BUY, SSP_SHIP, SSP_SELL, SSP_STEAL };
static void SecSelect(int sel, int wh) {
    Log("securoserv: selection %d warehouse %d (screen %d)", sel, wh, g_app.screen);
    int i = SecWarehouseByOrder(wh);
    switch (sel) {
    case 1002: AppClose(); return;                                         // QUIT
    case 3007:                                                             // BUY_WAREHOUSE
        if (i < 0) return;
        if (OwnedWarehouses().size() >= 5) { SecOverlay("WHOUSE_PURFA", "You can own up to five Warehouses.", false, true); g_app.pending = SSP_NONE; return; }
        SecOverlay("WHOUSE_PURCH", "WHOUSE_PURCHQ", true); g_app.pending = SSP_BUY; g_app.pendArg = wh; return;
    case 3008: case 3009: case 3010: {                                     // SMALL / MEDIUM / LARGE_SHIPMENT
        int n = sel - 3007; BizState *b = i >= 0 ? BizOf(PROPS[i].name) : nullptr;
        if (!b) return;
        if (b->stock + n > CargoMaxP(i)) { SecOverlay("WHOUSE_PURFA", "There is not enough space in this Warehouse.", false, true); g_app.pending = SSP_NONE; return; }
        SecOverlay("WHOUSE_SHI", "Buy " + std::to_string(n) + (n == 1 ? " crate" : " crates") + " of Special Cargo for " + Money(SHIP_COST[n]) +
                   "? You must collect it and bring it to " + PROPS[i].name + ".", true, true);
        g_app.pending = SSP_SHIP; g_app.pendArg = wh * 10 + n; return;
    }
    case 3011: {                                                           // SELL_CONTRABAND
        BizState *b = i >= 0 ? BizOf(PROPS[i].name) : nullptr;
        if (!b || b->stock < 1.f) { SecOverlay("MP_WH_SELL", "MP_WH_SELL_FE", false); g_app.pending = SSP_NONE; return; }
        SecOverlay("MP_WH_SELL", "MP_WH_SELL_C", true); g_app.pending = SSP_SELL; g_app.pendArg = wh; return;
    }
    case 4001: case 5006: {                                                // STEAL_VEHICLE (Import / Export)
        if (OwnedOfType(T_VEHWARE) < 0) return;
        AppCall("SHOW_OVERLAY"); PLabel("SEC_VEH_STEAL"); PLabel("SEC_VEH_STEALQ"); PLabel("WHOUSE_CONF"); PLabel("WHOUSE_CANC"); PBool(true); PBool(false); AppEnd();
        g_app.pending = SSP_STEAL; return;
    }
    case 5010:                                                             // buy a vehicle warehouse
        SecOverlay("IEWHOUSE_PURCH", "Vehicle Warehouses are sold on Maze Bank Foreclosures.", false, true); g_app.pending = SSP_NONE; return;
    case 3012: case 4003: case 5007: {                                     // the overlay's Confirm
        int what = g_app.pending; g_app.pending = SSP_NONE;
        if (what == SSP_BUY) {
            int w = SecWarehouseByOrder(g_app.pendArg);
            if (w < 0) return;
            g_payFromBank = true; BuyProperty(w); g_payFromBank = false;
            if (Owns(PROPS[w].name)) {
                AppSound("Popup_Confirm_Success", SS_SET);
                SecAddWarehouse(g_app.pendArg, w);
                SecOverlay("WHOUSE_PURCH", "WHOUSE_PURCHD", false);
            } else SecOverlay("WHOUSE_PURFA", "WHSE_PURCHF_NM", false);
            return;
        }
        if (what == SSP_SHIP) {
            int w = SecWarehouseByOrder(g_app.pendArg / 10), n = g_app.pendArg % 10;
            PayWhat pw("special cargo"); g_payFromBank = true; bool ok = w >= 0 && Pay(SHIP_COST[n]); g_payFromBank = false;
            if (!ok) { SecOverlay("WHOUSE_PURFA", "WHSE_PURCHF_NM", false); return; }
            CustAdd(SsKey("Collections"), 1); Save();
            AppClose([w, n] { SourceCargo(w, n); });
            return;
        }
        if (what == SSP_SELL) {
            int w = SecWarehouseByOrder(g_app.pendArg);
            if (BizState *b = w >= 0 ? BizOf(PROPS[w].name) : nullptr) { CustAdd(SsKey("Sales"), 1); CustAdd(SsKey("Earned"), CargoTotal(w, (int)b->stock)); Save(); }
            AppClose([w] { SellCargo(w); });
            return;
        }
        if (what == SSP_STEAL) { int vw = OwnedOfType(T_VEHWARE); CustAdd(SsKey("Stolen"), 1); Save(); AppClose([vw] { SourceCargo(vw); }); return; }
        AppCall("HIDE_OVERLAY"); AppEnd();
        return;
    }
    case 3013: case 4004: case 5008:                                       // the overlay's Cancel
        g_app.pending = SSP_NONE; AppCall("HIDE_OVERLAY"); AppEnd(); AppSound("Popup_Cancel", SS_SET); return;
    default: return;
    }
}
static void BizOpenSecuroServ(int pi) {
    if (g_menuOpen) CloseMenu();
    g_app = BizApp(); g_app.kind = 1; g_app.pi = pi;
    if (!AppLoad("SECUROSERV")) return;
    g_app.kind = 1; g_app.pi = pi; g_app.openedAt = Now();
    SecFeed();
    AppSound("Login", SS_SET);
    Log("securoserv: opened (%d warehouses owned)", (int)OwnedWarehouses().size());
}

// ================================================================ DataNet (the data center)
static const char *DL_SET = "DLC_GR_Disruption_Logistics_Sounds";
struct DcUnlock { const char *title, *desc; };
static const DcUnlock DC_UNLOCKS[] = {
    {"Zero-Day Exploit Kit", "Hacking Contracts pay 15% more."},
    {"Botnet Farm", "Encrypted data is produced 10% faster."},
    {"Police Scanner Jammer", "Hacking Contracts draw one wanted star less."},
    {"Encrypted Couriers", "Data sales pay 10% more."},
    {"Mining Rigs", "The crypto wallet fills 25% faster."},
    {"Ghost Protocol", "Hacks finish in 20 seconds instead of 30."},
};
static const int DC_NUNLOCK = (int)(sizeof DC_UNLOCKS / sizeof *DC_UNLOCKS);
static std::string DcKey(int pi, const char *what) { return std::string(PROPS[pi].name) + "|DataNet " + what; }
static int DcUnlocked(int pi) { return std::min(DC_NUNLOCK, CustInt(DcKey(pi, "Unlocked"))); }
static int DcStaff(int pi) { return CustInt(DcKey(pi, "Staff")); }   // 0 production, 1 research, 2 both
static float DcMfgShare(int pi) { int s = DcStaff(pi); return (s == 1 ? 0.f : s == 2 ? 0.5f : 1.f) * (DcUnlocked(pi) >= 2 ? 1.1f : 1.f); }
static float DcHackBonus(int pi) { return DcUnlocked(pi) >= 1 ? 1.15f : 1.f; }
static int DcHackWanted(int pi) { return DcUnlocked(pi) >= 3 ? 2 : 3; }
static float DcSellBonus(int pi) { return DcUnlocked(pi) >= 4 ? 1.1f : 1.f; }
static int DcHackSeconds(int pi) { return DcUnlocked(pi) >= 6 ? 20 : 30; }
static float DcResearch(int pi) { return CustInt(DcKey(pi, "Research")) / 10.f; }   // 0..100 of the current project

// research (staff on research or both use supplies), once a minute; the crypto wallet's Mining Rigs bonus
static void DcResearchTick() {
    static int next = 0;
    if (Now() < next) return;
    next = Now() + 60000;
    for (auto &b : g_biz) {
        int pi = FindProp(b.prop);
        if (pi < 0 || PROPS[pi].type != T_DATACENTER) continue;
        int s = DcStaff(pi);
        if (s != 0 && b.supplies > 0.f && DcUnlocked(pi) < DC_NUNLOCK) {
            float r = DcResearch(pi) + (s == 1 ? 1.6f : 0.8f) * DcSpeed(pi);
            b.supplies = std::max(0.f, b.supplies - (s == 1 ? 0.6f : 0.3f));
            if (r >= 100.f) {
                int u = DcUnlocked(pi);
                g_cust[DcKey(pi, "Unlocked")] = u + 1; r = 0.f;
                Notify(std::string("~b~DataNet~s~ research complete: ~g~") + DC_UNLOCKS[u].title + "~s~. " + DC_UNLOCKS[u].desc);
            }
            g_cust[DcKey(pi, "Research")] = (int)(r * 10.f);
        }
        if (DcUnlocked(pi) >= 5) { int perDay, cap; SafeIncome(T_DATACENTER, perDay, cap); b.safe = std::min(cap, b.safe + perDay / 4 / 1440 + 1); }
    }
}

static int DcUpgradeCat(int k) { const char *n[] = {"Equipment", "Staff", "Security"}; for (int c = 0; c < NCUSTOM; ++c) if (!std::strcmp(CUSTOM[c].interior, "datacenter") && !std::strcmp(CUSTOM[c].name, n[k])) return c; return -1; }
static void DnFeed() {
    int pi = g_app.pi;
    BizState *b = BizOf(PROPS[pi].name);
    const BizInfo *info = ProductionOf(T_DATACENTER);
    float stock = b ? b->stock : 0.f, sup = b ? b->supplies : 0.f;
    int value = info ? (int)(info->fullValue * stock / 100.f * DcSellBonus(pi)) : 0;
    const char *zone = N<const char *>(N_GET_NAME_OF_ZONE, PROPS[pi].x, PROPS[pi].y, PROPS[pi].z);
    AppCall("SET_STATS");
    PStr(PlayerTag()); PStr("Gori Data"); PStr("dn_datacenter");
    PText(PROPS[pi].name); PLabel(zone ? zone : "");
    PInt(1); PInt((int)stock); PInt((int)DcResearch(pi)); PInt((int)sup);
    PInt(CustInt(DcKey(pi, "Earned"))); PInt(CustInt(DcKey(pi, "Sales")));
    PInt(100); PInt(100); PInt(100);
    PInt(CustInt(DcKey(pi, "Units"))); PInt(DcUnlocked(pi)); PInt(DC_NUNLOCK); PInt(DcStaff(pi));
    AppEnd();
    AppCall("SET_RESUPPLIES"); PInt(SUPPLY_COST); PInt(2); PInt(2); PInt(-1); AppEnd();
    AppCall("SET_UPGRADES");
    for (int k = 0; k < 3; ++k) { int c = DcUpgradeCat(k); bool own = c >= 0 && CustChoice(pi, CUSTOM[c]) > 0; PInt(own || c < 0 ? -1 : CUSTOM[c].opts[1].price); PInt(2); }
    PInt(-1); PInt(-1); PInt(-1);
    AppEnd();
    AppCall("SET_SELL_PRICES"); PInt((int)(value * 1.5f)); PInt(value); AppEnd();
    int u = DcUnlocked(pi);
    char tex[32]; std::snprintf(tex, sizeof tex, "%sunlock_%d", DN_IMG, std::min(u, DC_NUNLOCK - 1) + 1);
    AppCall("SET_RESEARCH");
    PInt((int)((100.f - DcResearch(pi)) * 3000.f)); PInt((int)DcResearch(pi)); PStr(tex); PInt(u >= DC_NUNLOCK ? 2 : 1);
    PText(u >= DC_NUNLOCK ? "Every exploit has been developed." : DC_UNLOCKS[u].desc);
    PText(u >= DC_NUNLOCK ? "All research complete" : DC_UNLOCKS[u].title); PInt(-1);
    AppEnd();
    for (int k = 0; k < DC_NUNLOCK; ++k) {
        char t[32]; std::snprintf(t, sizeof t, "%sunlock_%d%s", DN_IMG, k + 1, k < u ? "" : "_locked");
        AppCall("ADD_RESEARCH_UNLOCKABLE"); PInt(k < u ? 2 : 1); PStr(t); PStr(DC_UNLOCKS[k].title); PStr(DC_UNLOCKS[k].desc); PInt(k); AppEnd();
    }
    // setup = Hacking Contract, restart = crypto wallet; shut down hidden
    AppCall("SET_BUTTON_STATES");
    PInt(2); PInt(2); PInt(2); PInt(2); PInt(2); PInt(0); PInt(2); PInt(stock >= 5.f ? 2 : 1); PInt(u < DC_NUNLOCK ? 2 : 1); PInt(sup < 99.f ? 2 : 1);
    AppEnd();
}
static void DnOverlay(const std::string &msg, bool two, const char *img = "") {
    AppCall("SHOW_OVERLAY"); PText(msg); PLabel("OR_OVRLY_OK"); PLabel(two ? "OR_OVRLY_CANC" : ""); PStr(img); PText(""); AppEnd();
    AppSound(two ? "Click_Link" : "Click_Fail", DL_SET);
}
enum { DNP_NONE, DNP_HACK, DNP_WALLET, DNP_SELL_BC, DNP_SELL_LS, DNP_BUYSUP, DNP_UPGRADE, DNP_FAST };
static void DnSelect(int sel) {
    int pi = g_app.pi;
    BizState *b = BizOf(PROPS[pi].name);
    Log("datanet: selection %d (screen %d)", sel, g_app.screen);
    auto show = [](int s) { AppCall("SHOW_SCREEN"); PInt(s); AppEnd(); };
    switch (sel) {
    case 0: g_app.pending = DNP_NONE; AppCall("HIDE_OVERLAY"); AppEnd(); return;   // overlay cancel
    case 1: {                                                                       // overlay accept
        int what = g_app.pending, arg = g_app.pendArg; g_app.pending = DNP_NONE;
        AppCall("HIDE_OVERLAY"); AppEnd();
        if (what == DNP_HACK) { AppClose([pi] { HackContract(pi); }); return; }
        if (what == DNP_SELL_BC || what == DNP_SELL_LS) {
            const BizInfo *info = ProductionOf(T_DATACENTER);
            int v = b && info ? (int)(info->fullValue * b->stock / 100.f * DcSellBonus(pi) * (what == DNP_SELL_LS ? 1.5f : 1.f)) : 0;
            CustAdd(DcKey(pi, "Sales"), 1); CustAdd(DcKey(pi, "Earned"), v); CustAdd(DcKey(pi, "Units"), b ? (int)b->stock : 0); Save();
            bool toLS = what == DNP_SELL_LS;
            AppClose([pi, toLS] { g_dcSellFar = toLS; SellProduct(pi); });
            return;
        }
        if (what == DNP_WALLET) {
            if (b && b->safe > 0) { AddCash(b->safe); CustAdd(DcKey(pi, "Earned"), b->safe); b->safe = 0; Save(); AppSound("Purchase", DL_SET); }
            DnFeed(); return;
        }
        if (what == DNP_BUYSUP) {
            PayWhat pw("business supplies (property)"); g_payFromBank = true; bool ok = b && Pay(SUPPLY_COST); g_payFromBank = false;
            if (ok) { b->supplies = 100.f; Save(); DnOverlay("Hardware supplies are on their way to the data center.", false); } else DnOverlay("You do not have enough money in your Maze Bank account.", false);
            DnFeed(); return;
        }
        if (what == DNP_UPGRADE) {
            int c = DcUpgradeCat(arg); if (c < 0) return;
            PayWhat pw("business upgrade"); g_payFromBank = true; bool ok = Pay(CUSTOM[c].opts[1].price); g_payFromBank = false;
            if (ok) { g_cust[std::string(PROPS[pi].name) + "|" + CUSTOM[c].name] = 1; Save(); AppSound("Purchase", DL_SET); DnOverlay("Upgrade purchased. It will be installed the next time you walk in.", false); }
            else DnOverlay("You do not have enough money in your Maze Bank account.", false);
            DnFeed(); return;
        }
        if (what == DNP_FAST) {
            int cost = (int)((100.f - DcResearch(pi)) * 3000.f);
            PayWhat pw("research"); g_payFromBank = true; bool ok = Pay(cost); g_payFromBank = false;
            if (ok) { g_cust[DcKey(pi, "Research")] = 1000; Save(); DcResearchTickForce(pi); AppCall("ACTIVATE_FAST_TRACK"); AppEnd(); }
            else DnOverlay("You do not have enough money in your Maze Bank account.", false);
            DnFeed(); return;
        }
        return;
    }
    case 2: DnOverlay("Accept a Hacking Contract? A client pays you to break into a corporate server: get there, keep the hack running and lose the police.", true); g_app.pending = DNP_HACK; return;
    case 3: show(2); return;   // resupply
    case 4: show(3); return;   // research
    case 5: show(4); return;   // staff
    case 6: show(5); return;   // upgrades
    case 8: DnOverlay("Withdraw " + Money(b ? b->safe : 0) + " from the data center's crypto wallet?", true); g_app.pending = DNP_WALLET; return;
    case 9: case 10:
        if (!b || b->stock < 5.f) { DnOverlay("You have no data to sell.", false); return; }
        DnOverlay(sel == 10 ? "Sell your encrypted data to corporate buyers across Los Santos? They pay more, but the deliveries are further." :
                              "Sell your encrypted data to local buyers?", true);
        g_app.pending = sel == 10 ? DNP_SELL_LS : DNP_SELL_BC; return;
    case 11: DnOverlay("Pay " + Money(SUPPLY_COST) + " to have hardware supplies delivered to the data center?", true); g_app.pending = DNP_BUYSUP; return;
    case 12: AppClose([pi] { StealSupplies(pi); }); return;
    case 13: case 14: case 15: {
        int k = sel - 13, c = DcUpgradeCat(k);
        if (c < 0 || CustChoice(pi, CUSTOM[c]) > 0) return;
        DnOverlay("Buy this upgrade for " + Money(CUSTOM[c].opts[1].price) + "?", true); g_app.pending = DNP_UPGRADE; g_app.pendArg = k; return;
    }
    case 16: case 17: case 18: g_cust[DcKey(pi, "Staff")] = sel == 16 ? 0 : sel == 17 ? 1 : 2; Save(); AppSound("Click_Generic", DL_SET); DnFeed(); return;
    case 19: DnOverlay("Fast-track the current research project for " + Money((int)((100.f - DcResearch(pi)) * 3000.f)) + "?", true); g_app.pending = DNP_FAST; return;
    case 20: show(6); return;  // unlocks
    case 21: if (!b || b->stock < 5.f) { DnOverlay("You have no data to sell.", false); return; } show(7); return;
    default: return;
    }
}
static void DcResearchTickForce(int pi) {   // a fast-tracked project completes now
    int u = DcUnlocked(pi);
    if (u >= DC_NUNLOCK) return;
    g_cust[DcKey(pi, "Unlocked")] = u + 1; g_cust[DcKey(pi, "Research")] = 0; Save();
    Notify(std::string("~b~DataNet~s~ research complete: ~g~") + DC_UNLOCKS[u].title + "~s~. " + DC_UNLOCKS[u].desc);
}
static void BizOpenDataNet(int pi) {
    if (g_menuOpen) CloseMenu();
    g_app = BizApp(); g_app.kind = 2; g_app.pi = pi;
    if (!AppLoad(DATANET_MOVIE) && !AppLoad("DISRUPTION_LOGISTICS")) return;   // our copy, else Rockstar's original
    g_app.kind = 2; g_app.pi = pi; g_app.openedAt = Now();
    DnFeed();
    Log("datanet: opened");
}

// ================================================================ the frame: draw, forward the input, answer the buttons
static void BizAppTick() {
    DcResearchTick();
    if (!g_app.kind || !g_app.mv) return;
    N(N_DISABLE_ALL_CONTROL_ACTIONS, 0);
    N(N_HIDE_HUD_AND_RADAR_THIS_FRAME);
    N(N_SET_MOUSE_CURSOR_THIS_FRAME);
    auto just = [](int c) { return NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 2, c)); };
    bool kbm = NB(N<int>(N_IS_USING_KEYBOARD_AND_MOUSE, 2));
    // which screen is showing (log in / branch / stats close the app on Back)
    if (!g_app.rvScr && Now() >= g_app.pollAt) { AppCall("GET_CURRENT_SCREEN_ID"); g_app.rvScr = N<int>(N_END_SCALEFORM_MOVIE_METHOD_RETURN_VALUE); g_app.pollAt = Now() + 250; }
    if (g_app.rvScr && NB(N<int>(N_IS_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_READY, g_app.rvScr))) { g_app.screen = N<int>(N_GET_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_INT, g_app.rvScr); g_app.rvScr = 0; }
    // mouse / sticks
    if (kbm) {
        float mx = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 239), my = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 240);
        if (mx != g_app.lmx || my != g_app.lmy) { AppCall("SET_MOUSE_INPUT"); PFloat(mx); PFloat(my); AppEnd(); g_app.lmx = mx; g_app.lmy = my; }
        if (just(241)) AppInput(241);
        if (just(242)) AppInput(242);
    } else {
        float lx = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 218) * 127.f, ly = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 219) * 127.f;
        float rx = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 220) * 127.f, ry = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 221) * 127.f;
        AppCall("SET_ANALOG_STICK_INPUT"); PBool(true); PFloat(lx); PFloat(ly); PBool(false); AppEnd();
        AppCall("SET_ANALOG_STICK_INPUT"); PBool(false); PFloat(rx); PFloat(ry); PBool(false); AppEnd();
    }
    for (int c : {187, 188, 189, 190, 205, 206, 207, 208}) if (just(c)) AppInput(c);   // D-pad / arrow keys jump between buttons, bumpers
    // accept: the movie, then which button it was
    if (Now() - g_app.openedAt > 400 && (just(237) || just(201)) && !g_app.rvSel) {
        AppInput(just(237) ? 237 : 201);
        AppCall("GET_CURRENT_SELECTION"); g_app.rvSel = N<int>(N_END_SCALEFORM_MOVIE_METHOD_RETURN_VALUE);
        if (g_app.kind == 1) { AppCall("GET_SELECTED_WAREHOUSE_ID"); g_app.rvAux = N<int>(N_END_SCALEFORM_MOVIE_METHOD_RETURN_VALUE); }
    }
    if (g_app.rvSel && NB(N<int>(N_IS_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_READY, g_app.rvSel)) &&
        (!g_app.rvAux || NB(N<int>(N_IS_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_READY, g_app.rvAux)))) {
        int sel = N<int>(N_GET_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_INT, g_app.rvSel);
        int wh = g_app.rvAux ? N<int>(N_GET_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_INT, g_app.rvAux) : -1;
        g_app.rvSel = g_app.rvAux = 0;
        if (g_app.kind == 1) SecSelect(sel, wh); else DnSelect(sel);
        if (!g_app.kind) return;   // closed
    }
    // back: on the first screens it logs off, elsewhere the movie goes back by itself
    if (Now() - g_app.openedAt > 600 && (just(202) || just(238) || just(200))) {
        bool top = g_app.kind == 1 ? (g_app.screen == 0 || g_app.screen == 3 || g_app.screen < 0) : (g_app.screen <= 1);
        if (g_app.pending) { g_app.pending = 0; AppCall("HIDE_OVERLAY"); AppEnd(); }
        else if (top) { AppSound(g_app.kind == 1 ? "Logout" : "Click_Back", g_app.kind == 1 ? SS_SET : DL_SET); AppClose(); return; }
        else AppInput(just(238) ? 238 : 202);
    }
    N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, g_app.mv, 255, 255, 255, 255, 0);
}
