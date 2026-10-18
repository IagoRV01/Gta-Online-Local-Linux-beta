// ---------------------------------------------------------------- The game's own web browser (GTA Online's internet)
// Driven like GTA Online's appinternet.ysc: the "web_browser" scaleform movie renders the real
// websites (eyefind, Legendary Motorsport, Maze Bank...); the script only feeds them data
// (SET_PRICES for the vehicle sites, ...) and handles the purchases.
#include "web_vehicles.h"

struct RealWeb {
    bool open = false;
    bool kbd = false;                  // the on-screen keyboard is up (Docktease: the yacht's name)
    std::string newName;               // the name typed for the yacht
    int movie = 0;
    int site = -1, page = -1;          // GET_CURRENT_WEBSITE_ID / GET_CURRENT_WEBPAGE_ID
    int initSite = -1;                 // the site we fed data to
    float lastMx = -1.f, lastMy = -1.f;
    float scroll = 0.f;                // scroll velocity, like appinternet's fLocal_737
    int selRv = 0;                     // pending GET_CURRENT_SELECTION
    int asFlag[4] = {0, 0, 0, 0};      // GET_GLOBAL_ACTIONSCRIPT_FLAG(0..3), logged on change
    int openedAt = 0;
    bool containerLoaded = false;      // the site's own movie is in the browser
    bool needFeed = false;             // INITIALISE_WEBSITE sent, data not yet
    int mouseHoldUntil = 0;            // tests: keep an injected cursor position
    std::set<int> priced;              // vehicle sites already given their prices (re-render once)
    const WebVehicle *viewing = nullptr; // the vehicle page on screen
    int colour = -1;                   // colour swatch picked on it (selection 8..15)
    const WebVehicle *ordered = nullptr; // ORDER pressed: buy it when the purchase page shows
    int orderColour = -1;
    int initAt = 0;                    // INITIALISE_WEBSITE time: the site id settles a few frames later
    int expect = -1;                   // the site id we navigated to (-1: followed a link)
    std::vector<int> d8Slots;          // Dynasty 8 listing slot -> PROPS index (appinternet's Global_1572029)
    int d8Sel = -1;                    // the property picked in the listing (epctLocal_630)
    int d8Fail = 0;                    // reason shown on the purchase-error page (LAR_* label code)
    // Maze Bank (appinternet iLocal_306/307/308): op 1 withdraw / 2 deposit, stage 1 confirm / 2 done
    int mbOp = 0, mbStage = 0, mbErr = 0;
    long long mbAmount = 0;
    int mbLedgerRev = -1;              // the transaction log page shows this ledger answer
    int newsPending = 0;               // eyefind news story n waiting for the NWS_PRO text block
    std::vector<int> exOffices;        // Dynasty 8 Executive: office list index -> PROPS index
    int exFail = 0;
    std::string resTitle, resDesc;     // dynamic sites: the purchase result page's heading / text (slots 1, 2)
    int refeedAt = 0;                  // dynamic sites: feed the map page again once it has drawn
    int lastBought = -1;               // PROPS index of the last purchase (the success page's SET WAYPOINT)
};
static const std::map<int, const char *> WEB_SITE_NAMES = {{10, "WWW_LEGENDARYMOTORSPORT_NET"}, {16, "WWW_SOUTHERNSANANDREASSUPERAUTOS_COM"},
    {11, "WWW_ELITASTRAVEL_COM"}, {13, "WWW_DOCKTEASE_COM"}, {12, "WWW_WARSTOCK_D_CACHE_D_AND_D_CARRY_COM"}, {15, "WWW_PANDMCYCLES_COM"},
    {26, "WWW_BENNYSORIGINALMOTORWORKS_COM"}, {29, "WWW_ARENAWAR_TV"}, {18, "WWW_DYNASTY8REALESTATE_COM"},
    {27, "WWW_DYNASTY8EXECUTIVEREALTY_COM"}, {28, "FORECLOSURES_MAZE_D_BANK_COM"}};
// GO_TO_WEBPAGE name -> GET_CURRENT_WEBSITE_ID (measured in game)
static int RWSiteIdOf(const std::string &name) {
    static const std::map<std::string, int> ids = {{"WWW_EYEFIND_INFO", 2}, {"WWW_LEGENDARYMOTORSPORT_NET", 10}, {"WWW_SOUTHERNSANANDREASSUPERAUTOS_COM", 16},
        {"WWW_ELITASTRAVEL_COM", 11}, {"WWW_DOCKTEASE_COM", 13}, {"WWW_WARSTOCK_D_CACHE_D_AND_D_CARRY_COM", 12}, {"WWW_MAZE_D_BANK_COM", 21},
        {"WWW_DYNASTY8REALESTATE_COM", 18}, {"FORECLOSURES_MAZE_D_BANK_COM", 28}, {"WWW_DYNASTY8EXECUTIVEREALTY_COM", 27},
        {"WWW_BENNYSORIGINALMOTORWORKS_COM", 26}, {"WWW_PANDMCYCLES_COM", 15}, {"WWW_ARENAWAR_TV", 29}};
    std::string base = name.substr(0, name.find("_S_"));
    auto it = ids.find(base);
    return it == ids.end() ? -1 : it->second;
}
// the website's colour swatches, in order (red, pink, yellow, orange, green, blue, black, grey) -> GTA paint ids
static const int WEB_SWATCH_PAINT[8] = {27, 135, 89, 38, 55, 64, 0, 4};
static RealWeb g_rw;
static bool RealWebIsOpen() { return g_rw.open; }

static void RWCall(const char *m) { N(N_BEGIN_SCALEFORM_MOVIE_METHOD, g_rw.movie, m); }
static void RWEnd() { N(N_END_SCALEFORM_MOVIE_METHOD); }
static void RWInput(int ev) { RWCall("SET_INPUT_EVENT"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, (float)ev); RWEnd(); }
static int RWSiteIdOf(const std::string &name);
static void RWGo(const std::string &site) {
    g_rw.expect = RWSiteIdOf(site);
    RWCall("GO_TO_WEBPAGE");
    N(N_BEGIN_TEXT_COMMAND_SCALEFORM_STRING, "STRING");
    N(N_ADD_TEXT_COMPONENT_SUBSTRING_WEBSITE, site.c_str());
    N(N_END_TEXT_COMMAND_SCALEFORM_STRING);
    RWEnd();
    g_rw.initSite = -1;
    g_rw.containerLoaded = false;
}
static void RWClose() {
    if (!g_rw.open) return;
    N(N_SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED, &g_rw.movie);
    g_rw = RealWeb{};
    Log("web: closed");
}
static bool RealWebOpen(const std::string &site) {
    if (g_rw.open) RWClose();
    int mv = N<int>(N_REQUEST_SCALEFORM_MOVIE, "web_browser");
    int t0 = Now();
    while (!NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, mv)) && Now() - t0 < 5000) Wait(0);
    if (!NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, mv))) { Log("web: web_browser movie did not load"); return false; }
    g_rw.open = true;
    g_rw.movie = mv;
    g_rw.openedAt = Now();
    if (g_menuOpen) CloseMenu();
    // GTA Online's browser: skin 1 (0 Michael, 2 Franklin, 3 Trevor) - the sites show their Online
    // pages only with it (Dynasty 8 listings, Benny's, Maze Bank accounts...)
    RWCall("SET_BROWSER_SKIN"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, 1); RWEnd();
    RWCall("SET_MULTIPLAYER"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, 1); RWEnd();
    RWGo(site);
    Log("web: opened %s", site.c_str());
    return true;
}

// Prices of a vehicle website (appinternet func_8808): every page that sells a vehicle this game build
// has gets its GTA Online price (web_vehicles.h, else the catalogue's); the others are hidden (-1), like
// the special purchases ("@yacht", "@moc", "@ballistic") until they have their own purchase code.
static int WebVehiclePrice(const WebVehicle &v) {
    if (!std::strcmp(v.model, "@moc")) return 1225000;   // the MOC's listing: "buy from" (the cheapest cab)
    if (!std::strcmp(v.model, "@ballistic")) return 500000;
    if (!std::strcmp(v.model, "@yacht")) return 6000000;   // "from": The Orion   // the Ballistic Equipment (GTA wiki; Online takes it from the catalog server)
    if (v.model[0] == '@') return -1;
    uint32_t h = ModelHash(v.model);
    if (!NB(N<int>(N_IS_MODEL_IN_CDIMAGE, h))) return -1;
    auto it = g_catByHash.find(h);
    if (it == g_catByHash.end()) return -1;
    if (v.price > 0) return v.price;
    return g_cat[it->second].price > 0 ? g_cat[it->second].price : -1;
}
// The catalogue (Gori Online's own pages, selling, insurance) uses the same GTA Online prices
static void WebApplyOnlinePrices() {
    int n = 0;
    for (int i = 0; i < NWEB_VEHICLES; ++i) {
        const WebVehicle &v = WEB_VEHICLES[i];
        if (v.model[0] == '@' || v.price <= 0) continue;
        auto it = g_catByHash.find(ModelHash(v.model));
        if (it == g_catByHash.end() || g_cat[it->second].price == v.price) continue;
        g_cat[it->second].price = v.price;
        ++n;
    }
    std::sort(g_cat.begin(), g_cat.end(), [](const CatCar &a, const CatCar &b) { return a.price != b.price ? a.price < b.price : a.name < b.name; });
    g_catByHash.clear();
    for (int i = 0; i < (int)g_cat.size(); ++i) g_catByHash[g_cat[i].hash] = i;
    Log("catalogue: %d GTA Online website prices applied", n);
}
static bool WsConfigPage(const WebVehicle &v);
static const WebVehicle *WebVehicleAt(int site, int page) {
    for (int i = 0; i < NWEB_VEHICLES; ++i)
        if (WEB_VEHICLES[i].site == site && WEB_VEHICLES[i].page == page) return WsConfigPage(WEB_VEHICLES[i]) ? nullptr : &WEB_VEHICLES[i];
    return nullptr;
}
static void RWFeedVehicles(int site) {
    int shown = 0, hidden = 0;
    for (int i = 0; i < NWEB_VEHICLES; ++i) {
        const WebVehicle &v = WEB_VEHICLES[i];
        if (v.site != site) continue;
        int price = WebVehiclePrice(v);
        RWCall("SET_PRICES");
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, v.page);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, price);
        RWEnd();
        if (price >= 0) ++shown; else ++hidden;
    }
    Log("web: site %d priced %d vehicles (%d hidden)", site, shown, hidden);
}
static void RWLabel(const char *lbl) { N(N_BEGIN_TEXT_COMMAND_SCALEFORM_STRING, lbl); N(N_END_TEXT_COMMAND_SCALEFORM_STRING); }
static void RWSlotInt(int slot, int v) { RWCall("SET_DATA_SLOT"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, slot); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, v); RWEnd(); }
static void RWSlotLabel(int slot, const char *lbl) { RWCall("SET_DATA_SLOT"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, slot); RWLabel(lbl); RWEnd(); }
static void RWFeedNews(int n) {
    static const int images[7] = {0, 0, 3, 12, 1, 2, 13};   // func_9122(0, n)
    char hl[32], st[32];
    std::snprintf(hl, sizeof hl, "NWS_PRO_HL%d", n);
    std::snprintf(st, sizeof st, "NWS_PRO_S%d", n);
    RWSlotLabel(4, hl);
    RWSlotInt(5, images[n]);
    RWSlotLabel(6, st);
}
// eyefind.info home page, as appinternet func_9117 fills it: weather, zone, day, news story, a bleet,
// and (GTA Online) the sponsored blocks pointing at the Online sites
static void RWFeedEyefind() {
    uint32_t w = (uint32_t)N<int>(N_GET_NEXT_WEATHER_TYPE_HASH_NAME);
    int wv = 0;
    if (w == Joaat("CLEARING") || w == Joaat("NEUTRAL") || w == Joaat("CLEAR") || w == Joaat("EXTRASUNNY")) wv = 1;
    else if (w == Joaat("OVERCAST") || w == Joaat("SMOG") || w == Joaat("CLOUDY")) wv = 3;
    else if (w == Joaat("CLOUDS")) wv = 2;
    else if (w == Joaat("RAIN")) wv = 4;
    else if (w == Joaat("THUNDER")) wv = 6;
    else if (w == Joaat("SNOW") || w == Joaat("XMAS")) wv = 7;
    RWSlotInt(0, wv);
    Vec3 p = Pos(Me());
    RWSlotLabel(2, N<const char *>(N_GET_NAME_OF_ZONE, p.x, p.y, p.z));
    static const char *days[] = {"BS_PH_SU", "BS_PH_M", "BS_PH_TU", "BS_PH_W", "BS_PH_TH", "BS_PH_F", "BS_PH_SA"};
    RWSlotLabel(3, days[N<int>(N_GET_CLOCK_DAY_OF_WEEK) % 7]);
    // the news story lives in the NWS_PRO text block (appinternet func_9136 loads it in slot 4)
    int n = 1 + (int)(((g_rand = g_rand * 1103515245u + 12345u) >> 8) % 6u);
    N(N_REQUEST_ADDITIONAL_TEXT, "NWS_PRO", 4);
    if (NB(N<int>(N_HAS_THIS_ADDITIONAL_TEXT_LOADED, "NWS_PRO", 4))) RWFeedNews(n);
    else g_rw.newsPending = n;
    int b = 2 * (int)(((g_rand = g_rand * 1103515245u + 12345u) >> 8) % 40u);   // BLE_<even> author, BLE_<odd> text
    char b1[16], b2[16];
    std::snprintf(b1, sizeof b1, "BLE_%d", b);
    std::snprintf(b2, sizeof b2, "BLE_%d", b + 1);
    RWSlotLabel(7, b1);
    RWSlotLabel(8, b2);
    // GTA Online: three sponsored blocks + the featured sites (website ids)
    RWCall("SET_DATA_SLOT");
    N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, 9);
    for (int site : {10, 12, 18}) { N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, site); RWLabel("EYEDISC_SA"); }
    for (int site : {10, 16, 11, 13, 12, 18, 21, 28, 15, 26}) N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, site);
    RWEnd();
    RWCall("UPDATE_TEXT"); RWEnd();
    Log("web: eyefind fed (weather %d, news %d, bleet %d)", wv, n, b);
}

// Dynasty 8's property listing, as appinternet func_9107 / func_9108 fill it: every apartment,
// house and garage GTA Online sells there (property ids 1-85) that you don't own yet, one
// SET_DATA_SLOT each: slot, name label, blip x/y, price, picture (DYN_MP_<id>), description label,
// size class, area, kind label, on-sale flags, stilt/custom flag.
static void RWFeedDynasty8() {
    g_rw.d8Slots.clear();
    RWCall("SET_DATA_SLOT_EMPTY"); RWEnd();
    for (int i = 0; i < NO_PROPS && i < NPROPS; ++i) {
        const ODyn &d = O_DYN[i];
        if (d.pid < 1 || d.pid > 85 || !*d.label || Owns(PROPS[i].name)) continue;
        int slot = (int)g_rw.d8Slots.size();
        g_rw.d8Slots.push_back(i);
        RWCall("SET_DATA_SLOT");
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, slot);
        RWLabel(d.label);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, PROPS[i].x);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, PROPS[i].y);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, PROPS[i].price);
        char pic[16];
        std::snprintf(pic, sizeof pic, "DYN_MP_%d", d.pid);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_PLAYER_NAME_STRING, pic);
        RWLabel(d.desc);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, d.size);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, d.area);
        bool special = (d.pid >= 73 && d.pid <= 85);
        RWLabel(d.pid >= 61 && d.pid <= 65 ? "DYN_UPDATEDINT" : d.pid >= 73 && d.pid <= 82 ? "DYN_STILTAPT" : d.pid >= 83 ? "DYN_CUSTOMAPT" : "");
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, 0);   // on sale
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, 0);    // trade-in bits
        RWLabel("");                                     // rebate label
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, 0);    // sale price
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, special ? 1 : 0);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, 0);
        RWEnd();
    }
    NaptFeedDynasty8(g_rw.d8Slots);   // the new apartments (NewApartmentsRt.inc)
    RWCall("UPDATE_TEXT"); RWEnd();
    Log("web: Dynasty 8 listing: %d properties", (int)g_rw.d8Slots.size());
}
// Buying the property picked on the listing ("Buy property" -> page 4, like func_7885), then the
// site's own success / error page (error page 23 says why, as appinternet fills it)
static void RWBuyProperty(int i) {
    const PropDef &pd = PROPS[i];
    bool had = Owns(pd.name);
    g_payFromBank = true;
    BuyProperty(i);
    g_payFromBank = false;
    bool ok = !had && Owns(pd.name);
    g_rw.d8Fail = ok ? 0 : had ? 6 : 2;
    Log("web: Dynasty 8 purchase %s ($%d) -> %s", pd.name, pd.price, ok ? "OK" : "FAILED");
    RWGo(std::string("WWW_DYNASTY8REALESTATE_COM") + (ok ? "_S_PURCHASE_D_SUCCESS" : "_S_PURCHASE_D_ERROR"));
}
static void RWDynasty8Error() {
    RWCall("SET_DATA_SLOT_EMPTY"); RWEnd();
    RWCall("SET_DATA_SLOT"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, 0); RWLabel("VHSITPURFH"); RWEnd();
    RWCall("SET_DATA_SLOT"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, 1); RWLabel(g_rw.d8Fail == 2 ? "LAR_ICASH" : "LAR_SAVTRB"); RWEnd();
    RWCall("UPDATE_TEXT"); RWEnd();
}

// ---- Maze Bank (www.maze-bank.com), as appinternet func_9000 and its pages do it ----
// pages: 1 home, 2 transaction log, 3 menu, 4 withdraw, 5 deposit, 6 error, 8 confirm / done
static long long MbWallet() { return YouActive() && LO().getWallet ? LO().getWallet() : 0; }
static long long MbBank() { return YouActive() && LO().getBank ? LO().getBank() : 0; }
// func_9005: the six amounts (deposits are capped by the cash you carry; -1 = button hidden)
static long long MbAmount(int i, bool deposit) {
    static const long long amt[6] = {50, 10000, 500, 100000, 2500, 1000000};
    static const long long lo[6] = {0, 2500, 50, 10000, 500, 100000};
    if (!deposit) return amt[i];
    long long w = MbWallet();
    if (w <= lo[i]) return -1;
    return i == 5 || w < amt[i] ? w : amt[i];
}
static void RWStr(const std::string &s) { N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_PLAYER_NAME_STRING, s.c_str()); }
static void RWSlotLbl(int slot, const char *lbl) { RWCall("SET_DATA_SLOT"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, slot); RWLabel(lbl); RWEnd(); }
static void RWSlotMoney(int slot, const char *fmt, long long v) {
    RWCall("SET_DATA_SLOT");
    N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, slot);
    N(N_BEGIN_TEXT_COMMAND_SCALEFORM_STRING, fmt);
    N(N_ADD_TEXT_COMPONENT_FORMATTED_INTEGER, (int)v, 1);
    N(N_END_TEXT_COMMAND_SCALEFORM_STRING);
    RWEnd();
}
static std::string MbName() { const char *n = N<const char *>(N_GET_PLAYER_NAME, N<int>(N_PLAYER_ID)); return n && *n ? n : "Gori"; }
static void MbHeader(const char *title) {   // balance, account holder, page title
    RWCall("SET_DATA_SLOT_EMPTY"); RWEnd();
    RWCall("SET_DATA_SLOT"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, 0); RWStr("$" + std::to_string(MbBank())); RWEnd();   // the site formats it
    RWCall("SET_DATA_SLOT"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, 1); RWStr(MbName()); RWEnd();
    RWSlotLbl(2, title);
}
// a ledger reason -> the log's line (GTA Online's MONEY_SPENT_* / MONEY_EARN_* categories)
static const char *MbCategory(const std::string &why, long long delta) {
    std::string r = why;
    for (char &c : r) c = (char)std::tolower((unsigned char)c);
    auto has = [&](const char *k) { return r.find(k) != std::string::npos; };
    if (delta < 0) {
        if (has("property") || has("apartment") || has("garage") || has("utilit") || has("office") || has("clubhouse") || has("estate")) return "MONEY_SPENT_PROPERTY_UTIL";
        if (has("insurance") || has("vehicle") || has("car") || has("repair") || has("mechanic") || has("impound") || has("destroyed") || has("mod")) return "MONEY_SPENT_VEH_MAINTENANCE";
        if (has("hospital") || has("medical") || has("health")) return "MONEY_SPENT_HEALTHCARE";
        if (has("lester") || has("contact") || has("service") || has("call")) return "MONEY_SPENT_CONTACT_SERVICE";
        if (has("entry") || has("job") || has("race")) return "MONEY_SPENT_JOB_ACTIVITY";
        if (has("clothes") || has("tattoo") || has("hair") || has("ammo") || has("weapon") || has("drink") || has("casino") || has("chips") || has("style")) return "MONEY_SPENT_STYLE_ENT";
        return "MPATM_PLCHLDR_PRCH";
    }
    if (has("refund")) return "MONEY_EARN_REFUND";
    if (has("sell") || has("sold")) return "MONEY_EARN_SELLING_VEH";
    if (has("heist")) return "MONEY_EARN_HEIST_JOB";
    if (has("bet") || has("casino") || has("wheel")) return "MONEY_EARN_BETTING";
    if (has("pick")) return "MONEY_EARN_PICKED_UP";
    return "MONEY_EARN_JOBS";
}
static void RWFeedMazeBank(int page) {
    switch (page) {
    case 1:
        RWCall("SET_DATA_SLOT"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, 1); RWStr(MbName()); RWEnd();
        break;
    case 2: {   // transaction log (func_9009): newest first, in = 1 / out = 0, amount, what
        MbHeader("MPATM_LOG");
        RWCall("SET_DATA_SLOT"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, 3); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, (int)MbBank()); RWLabel("MPATM_ACBA"); RWEnd();
        std::string text;
        if (LO().getLedger) {
            int n = LO().getLedger(nullptr, 0);
            if (n > 0) { text.resize((size_t)n + 1); LO().getLedger(&text[0], n + 1); text.resize((size_t)n); }
            g_rw.mbLedgerRev = LO().ledgerRevision ? LO().ledgerRevision() : 0;
        }
        int slot = 4;
        size_t pos = 0;
        while (pos < text.size() && slot < 20) {
            size_t e = text.find('\n', pos);
            std::string line = text.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
            pos = e == std::string::npos ? text.size() : e + 1;
            size_t t1 = line.find('\t'), t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
            if (t2 == std::string::npos) continue;
            long long delta = std::atoll(line.substr(0, t1).c_str());
            int account = std::atoi(line.substr(t1 + 1, t2 - t1 - 1).c_str());
            std::string why = line.substr(t2 + 1);
            std::string lw = why;
            for (char &c : lw) c = (char)std::tolower((unsigned char)c);
            bool deposit = lw.find("deposit") != std::string::npos, withdraw = lw.find("withdraw") != std::string::npos;
            if ((deposit || withdraw) && account == 0) continue;   // the cash half of a bank move
            const char *lbl = deposit ? "MPATM_PLCHLDR_CAD" : withdraw ? "MPATM_PLCHLDR_WDR" : MbCategory(why, delta);
            RWCall("SET_DATA_SLOT");
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, slot++);
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, delta > 0 ? 1 : 0);
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, (int)(delta < 0 ? -delta : delta));
            RWLabel(lbl);
            RWEnd();
        }
        break;
    }
    case 3:   // menu (func_9008)
        g_rw.mbOp = g_rw.mbStage = 0;
        MbHeader("MPATM_SER");
        RWSlotLbl(3, "MPATM_DIDM");
        if (MbBank() >= 50) RWSlotLbl(4, "MPATM_WITM");
        else { RWCall("DISABLE_BUTTON"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, 4); RWLabel("MPATM_WITM"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, 0); RWEnd(); }
        RWSlotLbl(5, "MPATM_LOG");
        break;
    case 4: case 5: {   // withdraw (func_9006) / deposit (func_9003) amounts
        bool dep = page == 5;
        MbHeader(dep ? "MPATM_DITMT" : "MPATM_WITMT");
        for (int i = 0; i <= 5; ++i) {
            long long a = MbAmount(i, dep);
            if (a > 0 && (dep || MbBank() >= a)) RWSlotMoney(i + 3, "ESDOLLA", a);
        }
        RWSlotLbl(9, "MPATM_BACK");
        break;
    }
    case 6:   // error (func_9002)
        RWCall("SET_DATA_SLOT_EMPTY"); RWEnd();
        RWSlotLbl(0, g_rw.mbErr == 1 ? "MPATM_NODO2" : g_rw.mbErr == 2 ? "MPATM_NODO" : "MPATM_ERR");
        break;
    case 8:   // confirm (func_9001)
        RWCall("SET_DATA_SLOT_EMPTY"); RWEnd();
        if (g_rw.mbStage == 1) {
            RWSlotMoney(0, g_rw.mbOp == 1 ? "MPATC_CONFW" : "MPATM_CONF", g_rw.mbAmount);
            RWSlotLbl(1, "MO_YES");
            RWSlotLbl(2, "MO_NO");
        } else if (g_rw.mbStage == 2) {
            RWSlotLbl(0, "MPATM_TRANCOM");
            RWSlotLbl(1, "MPATM_MENU");
        }
        break;
    default: break;
    }
    RWCall("UPDATE_TEXT"); RWEnd();
}
static void MbGo(const char *page) { RWGo(std::string("WWW_MAZE_D_BANK_COM_S_") + page); }
static void MbError(int why) { g_rw.mbErr = why; MbGo("ERROR"); }
// a click on a Maze Bank page (appinternet func_9231 / func_9230 / func_9229 / func_9215)
static void RWMazeBankClick(int page, int sel) {
    if (page == 3) {
        if (sel == 3) { if (MbAmount(0, true) > 0) MbGo("DEPOSIT"); else MbError(2); }
        else if (sel == 4) { if (MbBank() >= 50) MbGo("WITHDRAW"); else MbError(1); }
        else if (sel == 5) MbGo("TRANSACTIONS");
    } else if ((page == 4 || page == 5) && sel >= 3 && sel <= 8) {
        bool dep = page == 5;
        long long a = MbAmount(sel - 3, dep);
        if (a <= 0 || (!dep && MbBank() < a)) { MbError(dep ? 2 : 1); return; }
        g_rw.mbOp = dep ? 2 : 1; g_rw.mbStage = 1; g_rw.mbAmount = a;
        MbGo("MESSAGE");
    } else if ((page == 4 || page == 5) && sel == 9) MbGo("MAINMENU");
    else if (page == 8) {
        if (g_rw.mbStage == 1 && sel == 0) {
            bool dep = g_rw.mbOp == 2;
            bool ok = LO().changeAccount && YouActive() && LO().changeAccount(2, dep ? g_rw.mbAmount : -g_rw.mbAmount, dep ? "Maze Bank deposit" : "Maze Bank withdrawal");
            Log("web: Maze Bank %s %lld -> %s", dep ? "deposit" : "withdrawal", g_rw.mbAmount, ok ? "OK" : "FAILED");
            if (!ok) { MbError(3); return; }
            g_rw.mbStage = 2;
            if (LO().requestLedger) LO().requestLedger();
            RWFeedMazeBank(8);
        } else if (sel == 1 || g_rw.mbStage == 2) MbGo("MAINMENU");
    }
}

// ---- Dynasty 8 Executive (www.dynasty8executiverealty.com), a "dynamic" site: its ActionScript
// (WWW_DYNASTY8EXECUTIVEREALTY_COM / dynasty8ExecutiveRealty.Office) reads slot 0 = the office you
// own and slots 3-6 = the four offices, 216 values each (appinternet func_8994 / func_8980), and
// hands the choices back in global ActionScript flags (GameGlobals OFFICE 12, GARAGE_1 14, GARAGE_2 15)
// before it goes to the PURCHASE page (10).
static const int EX_STYLE_TO_GORI[9] = {4, 5, 3, 0, 1, 2, 6, 7, 8};   // website style -> O_OFFICE index
static const int EX_STYLE_COST[9] = {300000, 415000, 500000, 950000, 685000, 760000, 1000000, 835000, 910000};   // DYNASTY8_OFFICE_DECOR_*
static const int EX_PERSONNEL = 150000, EX_FONT = 50000, EX_SIGNAGE = 50000, EX_GUNLOCKER = 520000, EX_VAULT = 335000, EX_BED = 795000;
static const int EX_GARAGE_FLOOR[3] = {1150000, 855000, 745000};   // MP_PROP_OFFG1-3: the three garage levels
static int ExCust(int pi, const char *cat, int def) {
    auto it = g_cust.find(std::string(PROPS[pi].name) + "|" + cat);
    return it == g_cust.end() ? def : it->second;
}
static void ExSetCust(int pi, const char *cat, int v) { g_cust[std::string(PROPS[pi].name) + "|" + cat] = v; }
static int ExGoriToStyle(int g) { for (int s = 0; s < 9; ++s) if (EX_STYLE_TO_GORI[s] == g) return s; return 0; }
static int ExOwnedOffice() {
    for (int i = 0; i < NO_PROPS && i < NPROPS; ++i) if (O_DYN[i].pid >= 87 && O_DYN[i].pid <= 90 && Owns(PROPS[i].name)) return i;
    return -1;
}
static int ExGarageCat(int pi, const char *name) {   // Gori Online's office garage options
    for (const CustCat &c : CUSTOM) if (!std::strcmp(c.interior, "officegarage") && !std::strcmp(c.name, name)) return CustChoice(pi, c);
    return 0;
}
static const CustCat *ExGarageCatDef(const char *name) {
    for (const CustCat &c : CUSTOM) if (!std::strcmp(c.interior, "officegarage") && !std::strcmp(c.name, name)) return &c;
    return nullptr;
}
static void RWArgI(int v) { N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, v); }
// the purchase result of a dynamic site (appinternet func_9240): MP_OFF_S_BUY_T / MP_OFF_F_BUY_T and
// MP_<kind>_S_BUY (new), _S_REN (renovated), _F_BUY_2 (not enough money)
static void RWSendResult() {
    if (g_rw.resTitle.empty()) return;
    RWSlotLabel(1, g_rw.resTitle.c_str());
    RWSlotLabel(2, g_rw.resDesc.c_str());
}
static void RWSetResult(const char *kind, bool ok, bool renovated) {
    g_rw.resTitle = ok ? "MP_OFF_S_BUY_T" : "MP_OFF_F_BUY_T";
    g_rw.resDesc = std::string("MP_") + kind + (ok ? (renovated ? "_S_REN" : "_S_BUY") : "_F_BUY_2");
    RWSendResult();
    RWCall("UPDATE_TEXT"); RWEnd();
}
static void RWFeedExecutive() {
    g_rw.exOffices.clear();
    RWCall("SET_DATA_SLOT_EMPTY"); RWEnd();
    RWSendResult();
    int own = ExOwnedOffice();
    int levels = own >= 0 ? ExCust(own, "Garage Levels", 0) : 0;
    // slot 0: the office you own and its choices (func_8994)
    RWCall("SET_DATA_SLOT");
    RWArgI(0);
    RWArgI(own >= 0 ? O_DYN[own].pid : -1);
    RWArgI(own >= 0 ? ExGoriToStyle(CustChoice(own, CUSTOM[0])) : -1);
    RWArgI(own >= 0 ? ExCust(own, "Personnel", 0) : -1);
    RWArgI(own >= 0 ? ExCust(own, "Font", 0) : -1);
    RWArgI(own >= 0 ? ExCust(own, "Colour", 0) : -1);
    N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_LITERAL_STRING, own >= 0 ? "GORI ONLINE" : "");
    N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_LITERAL_STRING, "");
    RWArgI(own >= 0 ? ExCust(own, "Gun Locker", 0) : -1);
    RWArgI(own >= 0 ? ExCust(own, "Vault", 0) : -1);
    RWArgI(own >= 0 ? ExCust(own, "Accommodation", 0) : -1);
    N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, 1);
    RWEnd();
    RWCall("APPEND_OFFICE_DATA_SLOT");
    RWArgI(0);
    for (int f = 0; f < 3; ++f) {
        if (own >= 0 && levels > f) { RWArgI(1); RWArgI(ExGarageCat(own, "Garage Decor")); RWArgI(ExGarageCat(own, "Garage Lighting")); RWArgI(ExGarageCat(own, "Garage Signage")); }
        else { RWArgI(-1); RWArgI(-1); RWArgI(-1); RWArgI(-1); }
    }
    RWArgI(-1); RWArgI(-1);   // no vehicle workshop in Gori Online's offices
    RWEnd();
    // slots 3-6: the offices (func_8980)
    const CustCat *gd = ExGarageCatDef("Garage Decor"), *gl = ExGarageCatDef("Garage Lighting"), *gn = ExGarageCatDef("Garage Signage");
    int slot = 3;
    for (int i = 0; i < NO_PROPS && i < NPROPS; ++i) {
        const ODyn &d = O_DYN[i];
        if (d.pid < 87 || d.pid > 90) continue;
        bool owned = Owns(PROPS[i].name);
        g_rw.exOffices.push_back(i);
        RWCall("SET_DATA_SLOT");
        RWArgI(slot);
        RWArgI(d.pid);
        RWLabel(d.label);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, PROPS[i].x);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, PROPS[i].y);
        char txd[24]; std::snprintf(txd, sizeof txd, "DYN_OFFICE_%d", d.pid - 86);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_PLAYER_NAME_STRING, txd);
        RWLabel(d.desc);
        RWArgI(PROPS[i].price);
        for (int s = 0; s < 9; ++s) RWArgI(s == 0 && !owned ? 0 : EX_STYLE_COST[s]);   // Executive Rich comes with the office
        RWEnd();
        RWCall("APPEND_OFFICE_DATA_SLOT");
        RWArgI(slot);
        RWArgI(owned ? EX_PERSONNEL : 0); RWArgI(owned ? EX_PERSONNEL : 0); RWArgI(owned ? EX_FONT : 0); RWArgI(owned ? EX_SIGNAGE : 0);
        RWArgI(EX_GUNLOCKER); RWArgI(EX_VAULT); RWArgI(EX_BED);
        RWEnd();
        RWCall("APPEND_OFFICE_DATA_SLOT");   // no sale prices
        RWArgI(slot);
        for (int k = 0; k < 17; ++k) RWArgI(-1);
        RWEnd();
        // garage: description, base, then per level 4 decor + 9 lighting + 9 numbering costs, 20 workshop
        RWCall("APPEND_OFFICE_DATA_SLOT");
        RWArgI(slot);
        char gdesc[24]; std::snprintf(gdesc, sizeof gdesc, "MP_GAR_%dDES", d.pid + 9);
        RWLabel(gdesc);
        RWArgI(EX_GARAGE_FLOOR[0]);
        RWEnd();
        for (int f = 0; f < 3; ++f) {
            RWCall("APPEND_OFFICE_DATA_SLOT");
            RWArgI(slot);
            for (int k = 0; k < 4; ++k) RWArgI(EX_GARAGE_FLOOR[f] + (gd && k < gd->n ? gd->opts[k].price : 0));
            for (int k = 0; k < 9; ++k) RWArgI(gl && k < gl->n ? gl->opts[k].price : -1);
            for (int k = 0; k < 9; ++k) RWArgI(gn && k < gn->n ? gn->opts[k].price : -1);
            RWEnd();
        }
        RWCall("APPEND_OFFICE_DATA_SLOT");
        RWArgI(slot);
        for (int k = 0; k < 20; ++k) RWArgI(-1);
        RWEnd();
        RWCall("APPEND_OFFICE_DATA_SLOT");   // garage sale prices + starter pack flag
        RWArgI(slot);
        for (int k = 0; k < 87; ++k) RWArgI(-1);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, 0);
        RWEnd();
        ++slot;
    }
    RWCall("UPDATE_TEXT"); RWEnd();
    for (int k : {12, 14, 15}) N(N_RESET_GLOBAL_ACTIONSCRIPT_FLAG, k);
    Log("web: Dynasty 8 Executive: %d offices (own %s)", (int)g_rw.exOffices.size(), own >= 0 ? PROPS[own].name : "none");
}
// the PURCHASE page: the office (or its garage) with the choices the site packed into the flags
static void RWExecutivePurchase() {
    int g1 = N<int>(N_GET_GLOBAL_ACTIONSCRIPT_FLAG, 14), g2 = N<int>(N_GET_GLOBAL_ACTIONSCRIPT_FLAG, 15), of = N<int>(N_GET_GLOBAL_ACTIONSCRIPT_FLAG, 12);
    bool ok = false;
    const char *kind = "OFF";
    bool renovated = false;
    if (g1 != 0) {
        kind = "GAR";   // garage: office 7 bits, then per level equipped 1, style 2, lighting 4, number 4
        int idx = g1 & 0x7f;
        int pi = idx < (int)g_rw.exOffices.size() ? g_rw.exOffices[idx] : -1;
        int eq[3] = {(g1 >> 7) & 1, (g1 >> 18) & 1, (g2 >> 7) & 1};
        int st[3] = {(g1 >> 8) & 3, (g1 >> 19) & 3, (g2 >> 8) & 3};
        int li[3] = {(g1 >> 10) & 15, (g1 >> 21) & 15, (g2 >> 10) & 15};
        int nu[3] = {(g1 >> 14) & 15, (g1 >> 25) & 15, (g2 >> 14) & 15};
        if (pi >= 0 && Owns(PROPS[pi].name)) {
            const CustCat *gd = ExGarageCatDef("Garage Decor"), *gl = ExGarageCatDef("Garage Lighting"), *gn = ExGarageCatDef("Garage Signage");
            int have = ExCust(pi, "Garage Levels", 0), want = 0;
            renovated = have > 0;
            long long cost = 0;
            for (int f = 0; f < 3; ++f) {
                if (!eq[f]) continue;
                want = f + 1;
                bool owned = have > f;
                if (!owned || st[f] != ExGarageCat(pi, "Garage Decor")) cost += EX_GARAGE_FLOOR[f] * (owned ? 0 : 1) + (gd && st[f] < gd->n ? gd->opts[st[f]].price : 0);
                if (!owned || li[f] != ExGarageCat(pi, "Garage Lighting")) cost += gl && li[f] < gl->n ? gl->opts[li[f]].price : 0;
                if (!owned || nu[f] != ExGarageCat(pi, "Garage Signage")) cost += gn && nu[f] < gn->n ? gn->opts[nu[f]].price : 0;
            }
            PayWhat what("office garage (property)");
            g_payFromBank = true;
            ok = cost <= 0 || Pay((int)cost);
            g_payFromBank = false;
            if (ok) {
                ExSetCust(pi, "Garage Levels", std::max(have, want));
                ExSetCust(pi, "Garage Decor", st[0]); ExSetCust(pi, "Garage Lighting", li[0]); ExSetCust(pi, "Garage Signage", nu[0]);
                Save();
                if (cost > 0) Shard("~g~GARAGE PURCHASED", PROPS[pi].name);
            }
            Log("web: Executive garage %s levels %d decor %d light %d number %d cost %lld -> %s", PROPS[pi].name, want, st[0], li[0], nu[0], cost, ok ? "OK" : "FAILED");
        }
    } else {   // office: index 6 bits, style 4, personnel 1, font 4, colour 4, gun locker 1, vault 1, bed 1
        int idx = of & 0x3f, style = (of >> 6) & 15, pers = (of >> 10) & 1, font = (of >> 11) & 15, col = (of >> 15) & 15;
        int gun = (of >> 19) & 1, vault = (of >> 20) & 1, bed = (of >> 21) & 1;
        int pi = idx < (int)g_rw.exOffices.size() ? g_rw.exOffices[idx] : -1;
        if (pi >= 0) {
            bool owned = Owns(PROPS[pi].name);
            renovated = owned;
            long long cost = owned ? 0 : PROPS[pi].price;
            int curStyle = owned ? ExGoriToStyle(CustChoice(pi, CUSTOM[0])) : -1;
            if (style != curStyle && style < 9) cost += style == 0 && !owned ? 0 : EX_STYLE_COST[style];
            if (owned) {
                if (pers != ExCust(pi, "Personnel", 0)) cost += EX_PERSONNEL;
                if (font != ExCust(pi, "Font", 0)) cost += EX_FONT;
            }
            if (gun && !ExCust(pi, "Gun Locker", 0)) cost += EX_GUNLOCKER;
            if (vault && !ExCust(pi, "Vault", 0)) cost += EX_VAULT;
            if (bed && !ExCust(pi, "Accommodation", 0)) cost += EX_BED;
            g_payFromBank = true;
            if (!owned) { BuyProperty(pi, (int)cost); ok = Owns(PROPS[pi].name); }
            else { PayWhat what("office renovation (property)"); ok = cost <= 0 || Pay((int)cost); }
            g_payFromBank = false;
            if (ok) {
                for (const CustCat &c : CUSTOM) if (CatFor(pi, c) && c.kind == 1) g_cust[std::string(PROPS[pi].name) + "|" + c.name] = EX_STYLE_TO_GORI[style % 9];
                ExSetCust(pi, "Personnel", pers); ExSetCust(pi, "Font", font); ExSetCust(pi, "Colour", col);
                ExSetCust(pi, "Gun Locker", gun); ExSetCust(pi, "Vault", vault); ExSetCust(pi, "Accommodation", bed);
                Save();
            }
            if (ok) g_rw.lastBought = pi;
            Log("web: Executive office %s style %d personnel %d gun %d vault %d bed %d cost %lld -> %s", PROPS[pi].name, style, pers, gun, vault, bed, cost, ok ? "OK" : "FAILED");
        }
    }
    for (int k : {12, 14, 15}) N(N_RESET_GLOBAL_ACTIONSCRIPT_FLAG, k);
    g_rw.exFail = ok ? 0 : 2;
    RWSetResult(kind, ok, renovated);
    RWGo(std::string("WWW_DYNASTY8EXECUTIVEREALTY_COM") + (ok ? "_S_PURCHASE_D_SUCCESS" : "_S_PURCHASE_D_FAILED"));
}

// ---- Maze Bank Foreclosures (foreclosures.maze-bank.com), a "dynamic" site like Dynasty 8 Executive.
// foreclosures.h holds GTA Online's own rows (generated by running appinternet's feed functions);
// the website's ActionScript (FORECLOSURES_MAZE_D_BANK_COM_WRAPPER + foreclosures.*) reads owned
// properties in fixed slots, hands the choices back in the CLUBHOUSE flag (13) and goes to PURCHASE (9).
#include "foreclosures.h"
// how a choice is priced: 0 cost[c0 + v], 1 add-on (v == 1: cost[c0]), 2 level (v > 0: cost[c0 + v - 1]),
// 3 cumulative levels (cost[c0] .. cost[c0 + v - 1]), 4 free, 5 a change costs cost[c0], 6 hangar lighting
struct FcField { const char *key; int bits; int c0, n; uint8_t mode; const char *gori; };
struct FcType { int code; const char *kind; int idxBits, ownedSlot, summaryPage; const char *res; std::vector<FcField> f; };
static const std::vector<FcType> &FcTypes() {
    static const std::vector<FcType> t = {
        {0, "clubhouse", 6, 0, 8, "CLU", {{"wall", 1, 9, 2, 0, "Decor"}, {"hanging", 1, 11, 2, 0, nullptr}, {"furniture", 1, 13, 2, 0, nullptr},
            {"mural", 4, 15, 9, 0, "Mural"}, {"font", 4, 24, 1, 5, nullptr}, {"fontcolour", 4, 0, 0, 4, nullptr}, {"emblem", 4, 26, 9, 4, nullptr},   // the site sends "no crew emblem" here, not a choice
            {"hidesign", 1, 0, 0, 4, nullptr}, {"gunlocker", 1, 36, 1, 1, nullptr}, {"bikeshop", 1, 37, 1, 1, nullptr}}},
        {2, "bunker", 6, 15, 20, "BUN", {{"style", 2, 9, 3, 0, "Bunker Style"}, {"quarters", 1, 12, 1, 1, nullptr}, {"range", 2, 13, 2, 2, nullptr},
            {"gunlocker", 1, 15, 1, 1, nullptr}, {"transport", 2, 16, 2, 2, nullptr}}},
        {3, "hangar", 6, 47, 27, "HAN", {{"style", 4, 9, 9, 0, "Shell Colour"}, {"lighting", 2, 18, 0, 6, nullptr}, {"decal", 4, 42, 9, 0, "Floor Decal"},
            {"furniture", 2, 51, 3, 0, "Office"}, {"quarters", 2, 54, 2, 2, "Living Quarters"}, {"workshop", 1, 56, 1, 1, nullptr}}},
        {4, "facility", 6, 60, 34, "DBA", {{"style", 4, 9, 9, 0, nullptr}, {"graphics", 4, 18, 9, 0, "Decal"}, {"weapon", 1, 27, 1, 1, nullptr},
            {"security", 1, 28, 1, 1, nullptr}, {"lounge", 2, 29, 3, 0, "Lounge"}, {"quarters", 2, 32, 3, 2, "Personal Quarters"}, {"window", 1, 35, 1, 1, nullptr}}},
        {5, "nightclub", 6, 70, 41, "NCL", {{"style", 2, 9, 3, 0, "Club Style"}, {"lighting", 3, 12, 5, 0, nullptr}, {"dancers", 4, 18, 9, 2, nullptr},
            {"storage", 3, 27, 4, 2, nullptr}, {"garage", 3, 31, 4, 2, nullptr}, {"name", 4, 35, 9, 0, "Club Name"}, {"dryice", 1, 17, 1, 1, nullptr}}},
        {6, "arcade", 6, 90, 48, "ARC", {{"floor", 4, 9, 9, 0, "Floor"}, {"style", 2, 18, 3, 0, nullptr}, {"mural", 4, 21, 9, 0, "Mural"},
            {"quarters", 1, 30, 1, 1, nullptr}, {"garage", 1, 31, 1, 1, nullptr}, {"neon", 4, 32, 9, 0, nullptr}, {"highscores", 1, 41, 1, 1, nullptr}}},
        {7, "autoshop", 3, 100, 55, "AUT", {{"style", 4, 9, 9, 0, nullptr}, {"tint", 4, 18, 9, 0, nullptr}, {"emblem", 4, 27, 9, 0, nullptr},
            {"name", 1, 36, 1, 1, nullptr}, {"staff1", 1, 37, 1, 1, nullptr}, {"staff2", 1, 38, 1, 1, nullptr}, {"carlift", 1, 39, 1, 1, nullptr},
            {"quarters", 1, 40, 1, 1, nullptr}, {"membership", 1, 41, 1, 1, nullptr}}},
    };
    return t;
}
static const FcType *FcTypeOf(int code) { for (const FcType &t : FcTypes()) if (t.code == code) return &t; return nullptr; }
static int FcPType(int code) {
    switch (code) { case 0: return T_CLUB; case 2: return T_BUNKER; case 3: return T_HANGAR; case 4: return T_FACILITY; case 5: return T_NIGHTCLUB; case 6: return T_ARCADE; case 7: return T_AUTOSHOP; }
    return -1;
}
static std::vector<int> g_fcProp;   // FC_ROWS index -> PROPS index
static std::deque<std::string> g_fcNames;
// Online sells more hangars, arcades and auto shops than Gori Online had: those become properties with
// the type's interior at GTA Online's location (called once, before the save is loaded)
static void FcExtendProps() {
    g_fcProp.assign(NFC_ROWS, -1);
    std::vector<bool> used(PROPS.size(), false);
    for (int r = 0; r < NFC_ROWS; ++r) {
        const FcRow &row = FC_ROWS[r];
        int pt = FcPType(row.type), best = -1;
        float bd = 250.f;
        for (int i = 0; i < (int)PROPS.size(); ++i) {
            if (PROPS[i].type != pt || used[i]) continue;
            float d = std::hypot(PROPS[i].x - row.x, PROPS[i].y - row.y);
            if (d < bd) { bd = d; best = i; }
        }
        if (best < 0 && pt >= 0 && row.z != 0.f) {
            const PropDef *tpl = nullptr;
            for (const PropDef &p : PROPS) if (p.type == pt) { tpl = &p; break; }
            if (!tpl) continue;
            std::string label = Label(row.label);
            if (label.empty()) continue;
            static const char *prefix[8] = {"", "", "Bunker", "Hangar", "Facility", "Nightclub", "Arcade", "Auto Shop"};
            g_fcNames.push_back(std::string(prefix[row.type]) + ": " + label);
            PropDef p = *tpl;
            p.name = g_fcNames.back().c_str();
            p.price = FC_VALS[row.vals0 + 8];
            p.x = row.x; p.y = row.y; p.z = row.z; p.h = 0.f; p.ob = -1;
            PROPS.push_back(p);
            used.push_back(false);
            best = (int)PROPS.size() - 1;
            Log("foreclosures: added %s at %.0f %.0f %.0f", p.name, p.x, p.y, p.z);
        }
        if (best >= 0) { used[best] = true; g_fcProp[r] = best; }
    }
    NPROPS = (int)PROPS.size();
}
static int FcGet(int pi, const char *key, int def) {
    auto it = g_cust.find(std::string(PROPS[pi].name) + "|fc." + key);
    return it == g_cust.end() ? def : it->second;
}
static void FcSet(int pi, const char *key, int v) { g_cust[std::string(PROPS[pi].name) + "|fc." + key] = v; }
static int FcCost(const FcRow &row, const FcField &f, int v, int style) {
    const int *c = &FC_VALS[row.vals0];
    auto at = [&](int k) { return k >= 0 && k < row.nvals && c[k] > 0 ? c[k] : 0; };
    switch (f.mode) {
    case 0: return v < f.n ? at(f.c0 + v) : 0;
    case 1: return v == 1 ? at(f.c0) : 0;
    case 2: return v > 0 && v <= f.n ? at(f.c0 + v - 1) : 0;
    case 3: { int s = 0; for (int k = 0; k < v && k < f.n; ++k) s += at(f.c0 + k); return s; }
    case 5: return at(f.c0);
    case 6: { static const int base[9] = {0, 2, 4, 6, 8, 10, 12, 16, 20}; return at(f.c0 + base[std::min(8, std::max(0, style))] + v); }
    }
    return 0;
}
static void FcReplay(int r, int slot) {
    const FcRow &row = FC_ROWS[r];
    for (int k = row.op0; k < row.op0 + row.nops; ++k) {
        const FcOp &o = FC_OPS[k];
        switch (o.kind) {
        case 0: RWCall("SET_DATA_SLOT"); RWArgI(slot); break;
        case 1: RWCall("APPEND_OFFICE_DATA_SLOT"); RWArgI(slot); break;
        case 2: RWArgI(o.i); break;
        case 3: N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, o.f); break;
        case 4: N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, o.i ? 1 : 0); break;
        case 5: RWLabel(o.s); break;
        case 6: N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_PLAYER_NAME_STRING, o.s); break;
        case 7: N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_LITERAL_STRING, o.s); break;
        case 9: RWEnd(); break;
        }
    }
}
static void RWFeedForeclosures() {
    RWCall("SET_DATA_SLOT_EMPTY"); RWEnd();
    RWSendResult();
    for (const FcType &t : FcTypes()) {   // the owned property of each kind and what it has
        int own = -1, ownRow = -1;
        for (int r = 0; r < NFC_ROWS; ++r) if (FC_ROWS[r].type == t.code && g_fcProp[r] >= 0 && Owns(PROPS[g_fcProp[r]].name)) { own = g_fcProp[r]; ownRow = r; break; }
        RWCall("SET_DATA_SLOT");
        RWArgI(t.ownedSlot);
        RWArgI(own >= 0 ? FC_ROWS[ownRow].id : -1);
        if (t.code == 0) {   // clubhouse: wall, hanging, furniture, mural, font, colour, emblem, hide, purchased signage, signage, gun locker, bike shop
            for (const char *k : {"wall", "hanging", "furniture", "mural", "font", "fontcolour", "emblem", "hidesign"}) RWArgI(own >= 0 ? FcGet(own, k, 0) : -1);
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_LITERAL_STRING, "GORI MC");   // purchased signage, signage (the MC's name)
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_LITERAL_STRING, "GORI MC");
            RWArgI(own >= 0 ? FcGet(own, "gunlocker", 0) : -1); RWArgI(own >= 0 ? FcGet(own, "bikeshop", 0) : -1);
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, 1);
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_LITERAL_STRING, "");
        } else {
            for (const FcField &f : t.f) {
                if (t.code == 5 && !std::strcmp(f.key, "dryice")) { RWArgI(-1); }   // nightclub: G4 before dry ice
                RWArgI(own >= 0 ? FcGet(own, f.key, 0) : -1);
            }
            if (t.code == 7) { N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_LITERAL_STRING, own >= 0 ? "GORI" : ""); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_LITERAL_STRING, ""); }
        }
        RWEnd();
    }
    int shown = 0;
    for (int r = 0; r < NFC_ROWS; ++r) if (g_fcProp[r] >= 0) { FcReplay(r, FC_ROWS[r].slot); ++shown; }
    RWCall("UPDATE_TEXT"); RWEnd();
    N(N_RESET_GLOBAL_ACTIONSCRIPT_FLAG, 13);
    Log("web: Foreclosures: %d properties", shown);
}
// PURCHASE (page 9), reached from a type's summary page: decode the CLUBHOUSE flag
static void RWForeclosuresPurchase(int fromPage) {
    const FcType *t = nullptr;
    for (const FcType &x : FcTypes()) if (x.summaryPage == fromPage) t = &x;
    int flag = N<int>(N_GET_GLOBAL_ACTIONSCRIPT_FLAG, 13);
    bool ok = false, ren = false;
    if (t) {
        int idx = flag & ((1 << t->idxBits) - 1), n = 0, r = -1;
        for (int k = 0; k < NFC_ROWS; ++k) if (FC_ROWS[k].type == t->code && g_fcProp[k] >= 0) { if (n == idx) { r = k; break; } ++n; }
        if (r >= 0) {
            int pi = g_fcProp[r];
            bool owned = Owns(PROPS[pi].name);
            ren = owned;
            std::vector<int> v;
            int sh = t->idxBits;
            for (const FcField &f : t->f) { v.push_back((flag >> sh) & ((1 << f.bits) - 1)); sh += f.bits; }
            long long cost = owned ? 0 : FC_VALS[FC_ROWS[r].vals0 + 8];
            int style = v.empty() ? 0 : v[0];
            for (size_t k = 0; k < t->f.size(); ++k) {
                const FcField &f = t->f[k];
                if (owned && FcGet(pi, f.key, 0) == v[k]) continue;
                if (f.mode == 5 && (!owned || FcGet(pi, f.key, 0) == v[k])) continue;
                cost += FcCost(FC_ROWS[r], f, v[k], style);
            }
            g_payFromBank = true;
            if (!owned) { BuyProperty(pi, (int)cost); ok = Owns(PROPS[pi].name); }
            else { PayWhat what("renovation (property)"); ok = cost <= 0 || Pay((int)cost); }
            g_payFromBank = false;
            if (ok) {
                for (size_t k = 0; k < t->f.size(); ++k) {
                    const FcField &f = t->f[k];
                    FcSet(pi, f.key, v[k]);
                    if (!f.gori) continue;
                    for (const CustCat &c : CUSTOM) if (CatFor(pi, c) && !std::strcmp(c.name, f.gori)) {
                        int g = f.mode == 2 ? v[k] - 1 : v[k];
                        g_cust[std::string(PROPS[pi].name) + "|" + c.name] = std::min(c.n - 1, std::max(0, g));
                    }
                }
                Save();
            }
            if (ok) g_rw.lastBought = pi;
            Log("web: Foreclosures %s %s flag %d cost %lld -> %s", t->kind, PROPS[pi].name, flag, cost, ok ? "OK" : "FAILED");
        }
    }
    N(N_RESET_GLOBAL_ACTIONSCRIPT_FLAG, 13);
    RWSetResult(t ? t->res : "OFF", ok, ren);
    RWGo(std::string("FORECLOSURES_MAZE_D_BANK_COM") + (ok ? "_S_PURCHASE_D_SUCCESS" : "_S_PURCHASE_D_FAILED"));
}

// ---- Warstock's configurators: TRUCK (MOC), CHOPPER (Avenger), HACKER (Terrorbyte), SUB (Kosatka).
// appinternet sends func_8830/8824/8819/8812's data (warstock_specials.h; the first row is what you own),
// the site packs your choices into GameGlobals TRUCK (17) / CHOPPER (18) and goes to its PURCHASE page,
// where the price is the site's own total (its ProgressPanel.updateCosts).
#include "warstock_specials.h"
struct WsSite { int spv, ownedSlot, flag, buyPage; const char *okPage, *failPage; };
static const WsSite WS_SITES[] = {
    {SPV_MOC, 10, 17, 60, "TRUCK_D_PURCHASE_OK", "TRUCK_D_PURCHASE_FAIL"},
    {SPV_AVENGER, 15, 18, 88, "AVENGER_D_BUY_D_OK", "AVENGER_D_BUY_D_FAIL"},
    {SPV_TERRORBYTE, 18, 18, 108, "HACKERTRUCK_D_SOLD", "HACKERTRUCK_D_NOSALE"},
    {SPV_KOSATKA, 25, 18, 145, "SUB_D_PURCHASE_D_OK", "SUB_D_PURCHASE_D_FAIL"},
};
// the configurator pages are not vehicle pages (no ORDER button): catalogue items 323/357/402/518
static bool WsConfigPage(const WebVehicle &v) { return (v.site == 12 && (v.index == 323 || v.index == 357 || v.index == 402 || v.index == 518)) || (v.site == 13 && v.index == 245); }
static const WsRow *WsSlot(int slot) {
    for (int i = 0; i < NWS_ROWS; ++i) if (WS_ROWS[i].nvals && WS_ROWS[i].vals[0].i == slot && std::strncmp(WS_ROWS[i].feed, "yacht", 5)) return &WS_ROWS[i];
    return nullptr;
}
static const int WS_UNDEF = -0x7fffffff;
static int WsGet(int slot, int idx) {   // dataProviderUI[slot][idx]
    const WsRow *r = WsSlot(slot);
    if (!r || idx < 0 || idx + 1 >= r->nvals) return WS_UNDEF;
    return r->vals[idx + 1].i;
}
static void RWSlotText(int slot, const std::string &t) {
    RWCall("SET_DATA_SLOT"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, slot);
    N(N_BEGIN_TEXT_COMMAND_SCALEFORM_STRING, "STRING"); N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, t.c_str()); N(N_END_TEXT_COMMAND_SCALEFORM_STRING);
    RWEnd();
}
static int SpvOwnedConfig(int spv) {   // what the site shows as yours (-1: not owned)
    if (!SpvOwned(spv)) return -1;
    int c = SpvConfig(spv);
    return c < 0 ? 0 : c;   // a Kosatka bought before the configurator: the standard one
}
static void RWFeedWarstock() {
    for (int i = 0; i < NWS_ROWS; ++i) {
        const WsRow &r = WS_ROWS[i];
        if (!std::strncmp(r.feed, "yacht", 5)) continue;   // Docktease's
        int slot = r.vals[0].i;
        const WsSite *own = nullptr;
        for (const WsSite &w : WS_SITES) if (w.ownedSlot == slot) own = &w;
        int ch[8]; int n = own ? SpvChoices(own->spv, SpvOwnedConfig(own->spv), ch) : 0;
        RWCall(r.method);
        int c = 0;
        for (int k = 0; k < r.nvals; ++k) {
            const WsVal &v = r.vals[k];
            if (own && k >= 1 && v.kind == 'i' && c < n) { N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, ch[c++]); continue; }
            if (v.kind == 'b') N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, v.i);
            else if (v.kind == 'f') N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, v.f);
            else if (v.kind == 's') RWLabel(v.s);
            else N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, v.i);
        }
        RWEnd();
    }
}
// ProgressPanel.updateCostStep
static int WsStep(int normal, int sale, int cur, int purchased) {
    if (cur == purchased || (sale == -1 && normal <= 0) || (sale == WS_UNDEF && normal == WS_UNDEF)) return 0;
    return sale == -1 || sale == WS_UNDEF ? normal : sale;
}
static int MocCostIndex(int type, int slotNum) {   // TruckProgressPanel.getCostIndex over MODULE_DATA
    struct M { int type; int nloc; int loc[3]; int cost[3]; };
    static const M MD[] = {{1, 2, {2, 4}, {5, 5}}, {2, 3, {1, 2, 4}, {6, 11, 11}}, {3, 1, {1}, {7}}, {4, 2, {2, 4}, {8, 8}}, {5, 1, {6}, {9}}, {6, 1, {4}, {10}}};
    for (const M &m : MD) if (m.type == type) { for (int j = 0; j < m.nloc; ++j) if (m.loc[j] & slotNum) return m.cost[j]; return m.type; }
    return -1;
}
static long long WsTotal(int spv, const int *c, const int *p) {   // c: chosen, p: owned (-1 = not owned)
    long long t;
    auto base = [&](int s) { int sale = WsGet(s + 1, 0); return (long long)(sale != -1 && sale != WS_UNDEF ? sale : WsGet(s, 0)); };
    auto opt = [](int i, int v) { return i != 1 ? 0 : v; };
    auto optSale = [](int i, int v) { return i != 1 ? -1 : v; };
    switch (spv) {
    case SPV_MOC: {
        t = base(11);
        t += WsStep(WsGet(11, c[0] + 2), WsGet(12, c[0] + 2), c[0], p[0]);
        static const int SLOT[3] = {1, 2, 4};
        for (int m = 0; m < 3; ++m) { int ci = MocCostIndex(c[1 + m], SLOT[m]); t += WsStep(WsGet(11, ci), WsGet(12, ci), c[1 + m], p[1 + m]); }
        t += WsStep(WsGet(13, c[4]), WsGet(14, c[4]), c[4], p[4]);
        break; }
    case SPV_AVENGER: {
        t = base(16);
        t += WsStep(WsGet(16, c[0] + 5), WsGet(17, c[0] + 5), c[0], p[0]);
        for (int k = 1; k <= 3; ++k) t += WsStep(opt(c[k], WsGet(16, k + 1)), optSale(c[k], WsGet(17, k + 1)), c[k], p[k]);
        bool term = WsGet(17, 14) != WS_UNDEF;
        t += WsStep(c[4] == 1 && term ? WsGet(16, 14) : 0, c[4] == 1 && term ? WsGet(17, 14) : -1, c[4], p[4]);
        break; }
    case SPV_TERRORBYTE: {
        t = base(19);
        t += WsStep(WsGet(19, c[0] + 6), WsGet(20, c[0] + 6), c[0], p[0]);
        t += WsStep(c[1] >= 12 ? WsGet(22, c[1] - 12) : WsGet(21, c[1]), c[1] >= 12 ? WsGet(24, c[1] - 12) : WsGet(23, c[1]), c[1], p[1]);
        for (int k = 2; k <= 5; ++k) t += WsStep(opt(c[k], WsGet(19, k)), optSale(c[k], WsGet(20, k)), c[k], p[k]);
        break; }
    default: {
        t = base(26);
        t += WsStep(WsGet(28, c[0]), WsGet(29, c[0]), c[0], p[0]);
        t += WsStep(WsGet(30, c[1]), WsGet(31, c[1]), c[1], p[1]);
        for (int k = 2; k <= 4; ++k) t += WsStep(opt(c[k], WsGet(26, k)), optSale(c[k], WsGet(27, k)), c[k], p[k]);
        int heli = WsStep(opt(c[5], WsGet(26, 5)), optSale(c[5], WsGet(27, 5)), c[5], p[5]);
        int mini = WsStep(opt(c[6], WsGet(26, 6)), optSale(c[6], WsGet(27, 6)), c[6], p[6]);
        if (p[5] == 1 && p[6] == 1) {}
        else if (p[5] == 1) t += mini;
        else if (p[6] == 1) t += heli;
        else t += heli + mini;
        break; }
    }
    return t;
}
// the site's PURCHASE page: like appinternet (catalogue 323 / 357 / 402 / 518) with Gori Online's bank
static void RWWarstockPurchase(const WsSite &w) {
    int flag = N<int>(N_GET_GLOBAL_ACTIONSCRIPT_FLAG, w.flag);
    int spv = w.spv;
    bool owned = SpvOwned(spv);
    int c[8], p[8];
    SpvChoices(spv, flag, c);
    SpvChoices(spv, SpvOwnedConfig(spv), p);
    long long cost = WsTotal(spv, c, p);
    bool ok = false;
    std::string why;
    if (!SpvHasBase(spv)) why = std::string("You do not own a ") + SPV[spv].needName + ". Purchase one from foreclosures.maze-bank.com";
    else {
        PayWhat what(SPV[spv].name);
        g_payFromBank = true;
        if (spv == SPV_KOSATKA && !owned) {
            int i = FindProp("Kosatka");
            if (i >= 0) { BuyProperty(i, (int)cost); ok = Owns("Kosatka"); }
        } else ok = cost <= 0 || Pay((int)cost);
        g_payFromBank = false;
        if (!ok) why = "You do not have enough money in your Maze Bank account.";
    }
    if (ok) {
        g_cust[SpvKey(spv)] = flag;
        Save();
        GiveRP(owned ? 100 : 1000, owned ? "renovation" : "special vehicle");
    }
    N(N_RESET_GLOBAL_ACTIONSCRIPT_FLAG, w.flag);
    Log("web: Warstock %s flag %d cost %lld -> %s%s%s", SPV[spv].name, flag, cost, ok ? "OK" : "FAILED", why.empty() ? "" : ": ", why.c_str());
    if (!ok) { RWSlotLabel(1, "MP_OFF_F_BUY_T"); RWSlotText(2, why); RWCall("UPDATE_TEXT"); RWEnd(); }
    RWGo(std::string("WWW_WARSTOCK_D_CACHE_D_AND_D_CARRY_COM_S_") + (ok ? w.okPage : w.failPage));
}

// ---- Docktease's yacht (appinternet func_8838 / its purchase of catalogue item 245)
static std::vector<int> YachtSiteData(bool owned) {   // dataProviderUI[18] as the site sees it (strings count as one entry)
    std::vector<int> d;
    const char *feed = owned ? "yacht_owned" : "yacht";
    for (int i = 0; i < NWS_ROWS; ++i) {
        if (std::strcmp(WS_ROWS[i].feed, feed)) continue;
        for (int k = 1; k < WS_ROWS[i].nvals; ++k) d.push_back(WS_ROWS[i].vals[k].kind == 'i' || WS_ROWS[i].vals[k].kind == 'b' ? WS_ROWS[i].vals[k].i : 0);
    }
    return d;
}
static void RWFeedDocktease() {
    std::string ownName = YachtOwned() ? g_yachtName : std::string();
    bool owned = YachtOwned();
    YachtCfg c = YachtDecode(owned ? g_cust[YACHT_CFG] : 0);
    int own[5] = {c.colour, c.lighting, c.flag, c.model, c.fittings};
    const char *feed = owned ? "yacht_owned" : "yacht";
    for (int i = 0; i < NWS_ROWS; ++i) {
        const WsRow &r = WS_ROWS[i];
        if (std::strcmp(r.feed, feed)) continue;
        bool first = !std::strcmp(r.method, "SET_DATA_SLOT");
        RWCall(r.method);
        for (int k = 0; k < r.nvals; ++k) {
            const WsVal &v = r.vals[k];
            if (first && owned && k >= 1 && k <= 5) { N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, own[k - 1]); continue; }
            if (v.kind == 'n') N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_PLAYER_NAME_STRING, (k == 7 ? g_rw.newName : ownName).c_str());   // vals[6] DATA_NAME, vals[7] DATA_NEW_NAME
            else if (v.kind == 's') RWLabel(v.s);
            else if (v.kind == 'b') N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, v.i);
            else if (v.kind == 'f') N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, v.f);
            else N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, v.i);
        }
        RWEnd();
    }
}
// YACHT_D_PURCHASE: the site's getTotalPrice over what changed (a first purchase: everything chosen)
static void RWYachtPurchase() {
    int flag = N<int>(N_GET_GLOBAL_ACTIONSCRIPT_FLAG, 11);
    bool owned = YachtOwned();
    YachtCfg c = YachtDecode(flag), o = owned ? YachtDecode(g_cust[YACHT_CFG]) : YachtCfg{-1, -1, -1, -1, -1};
    std::vector<int> d = YachtSiteData(owned);
    auto at = [&](int i) { return i >= 0 && i < (int)d.size() ? d[i] : 0; };
    auto price = [&](int i, int si) { int p = at(i), s = at(si); return s >= 0 ? std::min(p, s) : p; };
    long long cost = 0;
    if (c.model != o.model) cost += price(8 + c.model, 43 + c.model);
    if (c.fittings != o.fittings) cost += price(11 + c.fittings, 46 + c.fittings);
    if (c.lighting != o.lighting) cost += price(13 + c.lighting, 48 + c.lighting);
    if (c.colour != o.colour) cost += price(25 + c.colour, 56 + c.colour);
    if (c.flag != o.flag) cost += price(41, 72);
    PayWhat what("Super Yacht");
    g_payFromBank = true;
    bool ok = cost <= 0 || Pay((int)cost);
    g_payFromBank = false;
    if (ok && !g_rw.newName.empty() && g_rw.newName != g_yachtName) {   // DATA_NAME_COST (free with a first purchase)
        std::vector<int> d2 = YachtSiteData(owned);
        int nc = d2.size() > 73 ? (d2[73] >= 0 ? std::min(d2[42], d2[73]) : d2[42]) : 0;
        if (nc > 0) { g_payFromBank = true; ok = Pay(nc); g_payFromBank = false; cost += nc; }
        if (ok) g_yachtName = g_rw.newName;
    }
    if (ok) {
        g_cust[YACHT_CFG] = flag;
        if (!owned) g_cust[YACHT_SPOT] = YachtNearestSpot();
        Save();
        if (g_yLoaded >= 0) YachtBuild(YachtSpotIdx());
        GiveRP(owned ? 100 : 1000, owned ? "yacht renovation" : "Super Yacht");
    }
    N(N_RESET_GLOBAL_ACTIONSCRIPT_FLAG, 11);
    Log("web: Docktease yacht flag %d (model %d fittings %d lights %d colour %d flag %d) cost %lld -> %s", flag, c.model, c.fittings, c.lighting, c.colour, c.flag, cost, ok ? "OK" : "FAILED");
    if (!ok) { RWSlotLabel(1, "MP_OFF_F_BUY_T"); RWSlotText(2, "You do not have enough money in your Maze Bank account."); RWCall("UPDATE_TEXT"); RWEnd(); }
    RWGo(std::string("WWW_DOCKTEASE_COM_S_") + (ok ? "YACHT_D_PURCHASE_SUCCESS" : "YACHT_D_PURCHASE_FAILED"));
}

// A site was (re)loaded: its data (INITIALISE_WEBSITE was sent when its movie arrived)
static void RWInitSite(int site) {
    switch (site) {
    case 2: RWFeedEyefind(); break;
    case 18: RWFeedDynasty8(); break;
    case 21:
        if (LO().requestLedger) LO().requestLedger();
        RWFeedMazeBank(N<int>(N_GET_CURRENT_WEBPAGE_ID));
        break;
    case 27: RWFeedExecutive(); break;
    case 28: RWFeedForeclosures(); break;
    case 10: case 11: case 12: case 13: case 15: case 16: case 26: case 29:
        RWFeedVehicles(site);
        if (site == 12) RWFeedWarstock();
        if (site == 13) RWFeedDocktease();
        if (!g_rw.priced.count(site)) {   // the list was drawn before it had prices: draw it again
            g_rw.priced.insert(site);
            auto it = WEB_SITE_NAMES.find(site);
            if (it != WEB_SITE_NAMES.end()) { RWGo(it->second); return; }
        }
        break;
    default: break;
    }
    g_rw.initSite = site;
    Log("web: site %d initialised", site);
}

// The website's "Purchase pending..." page: buy it like GTA Online (Maze Bank account, a garage with
// room), then the site's own success / error page.
static bool BallisticBuy(int price);
static void RWBuyVehicle(const WebVehicle &wv, int colour) {
    if (!std::strcmp(wv.model, "@ballistic")) {
        g_payFromBank = true;
        bool ok = BallisticBuy(WebVehiclePrice(wv));
        g_payFromBank = false;
        Log("web: purchase Ballistic Equipment -> %s", ok ? "OK" : "FAILED");
        RWGo(std::string(WEB_SITE_NAMES.at(wv.site)) + (ok ? "_S_PURCHASE_D_SUCCESS" : "_S_PURCHASE_D_ERROR"));
        return;
    }
    uint32_t h = ModelHash(wv.model);
    auto it = g_catByHash.find(h);
    int price = WebVehiclePrice(wv);
    if (it == g_catByHash.end() || price < 0) {
        Log("web: %s is not sold in this game build", wv.model);
        auto sn = WEB_SITE_NAMES.find(wv.site);
        if (sn != WEB_SITE_NAMES.end()) RWGo(std::string(sn->second) + "_S_PURCHASE_D_ERROR");
        return;
    }
    CatCar tmp = g_cat[it->second];   // the catalogue entry at the website's price
    tmp.price = price;
    const CatCar *c = &tmp;
    size_t before = g_garage.size();
    bool ownedSub = Owns("Kosatka");
    g_payFromBank = true;
    BuyCar(c);
    g_payFromBank = false;
    bool ok = g_garage.size() > before || (!ownedSub && Owns("Kosatka"));
    if (ok && g_garage.size() > before && colour >= 0 && colour < 8) {
        GVeh &g = g_garage.back();
        g.data = "c=" + std::to_string(WEB_SWATCH_PAINT[colour]) + "," + std::to_string(WEB_SWATCH_PAINT[colour]) + (g.data.empty() ? "" : ";" + g.data);
        Save();
    }
    Log("web: purchase %s (colour %d) -> %s", wv.model, colour, ok ? "OK" : "FAILED");
    auto sn = WEB_SITE_NAMES.find(wv.site);
    if (sn != WEB_SITE_NAMES.end()) RWGo(std::string(sn->second) + (ok ? "_S_PURCHASE_D_SUCCESS" : "_S_PURCHASE_D_ERROR"));
}

static void RealWebTick() {
    if (!g_rw.open) return;
    int mv = g_rw.movie;
    if (g_rw.kbd) {   // the yacht's name: appinternet's on-screen keyboard, then DATA_NEW_NAME for the site to poll
        int st = N<int>(N_UPDATE_ONSCREEN_KEYBOARD);
        if (st == 1 || st == 2 || st == 3) {
            g_rw.kbd = false;
            const char *r = st == 1 ? N<const char *>(N_GET_ONSCREEN_KEYBOARD_RESULT) : nullptr;
            if (r && *r) { g_rw.newName = r; RWFeedDocktease(); Log("web: yacht name \"%s\"", r); }
        }
        N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, mv, 255, 255, 255, 255, 0);
        return;
    }
    N(N_DISABLE_ALL_CONTROL_ACTIONS, 0);
    N(N_HIDE_HUD_AND_RADAR_THIS_FRAME);
    N(N_SET_MOUSE_CURSOR_THIS_FRAME);
    // site / page tracking (the movie navigates by itself when links are clicked)
    bool loaded = NB(N<int>(N_HAS_SCALEFORM_CONTAINER_MOVIE_LOADED_INTO_PARENT, mv));
    if (loaded && !g_rw.containerLoaded) {   // a site arrived (opened, or a link was followed): appinternet's INITIALISE_WEBSITE
        RWCall("INITIALISE_WEBSITE"); RWEnd();
        g_rw.needFeed = true;
        g_rw.initAt = Now();
        Log("web: site movie loaded, initialised");
    }
    g_rw.containerLoaded = loaded;
    if (loaded) {
        int site = N<int>(N_GET_CURRENT_WEBSITE_ID), page = N<int>(N_GET_CURRENT_WEBPAGE_ID);
        // GET_CURRENT_WEBSITE_ID keeps the previous site for a few frames after INITIALISE_WEBSITE
        bool settled = g_rw.expect >= 0 ? site == g_rw.expect || Now() - g_rw.initAt > 1500 : Now() - g_rw.initAt > 400;
        if (site != 0 && g_rw.needFeed && settled) { g_rw.needFeed = false; g_rw.expect = -1; RWInitSite(site); }
        if (site != g_rw.site || page != g_rw.page) {
            int prevSite = g_rw.site, prevPage = g_rw.page;
            g_rw.site = site; g_rw.page = page;
            if (site == 18 && prevSite == 18 && g_rw.d8Sel >= 0) {   // appinternet's site-18 page logic
                if (prevPage == 21 && page == 4) RWBuyProperty(g_rw.d8Sel);
                else if (prevPage == 21 && page == 5) { const PropDef &pd = PROPS[g_rw.d8Sel]; N(N_SET_NEW_WAYPOINT, pd.x, pd.y); Log("web: waypoint set to %s", pd.name); }
            }
            if (site == 18 && page == 23) RWDynasty8Error();
            if (site == 18 && page == 2 && prevPage != 2 && g_rw.initSite == 18) RWFeedDynasty8();
            if (site == 21 && g_rw.initSite == 21) RWFeedMazeBank(page);
            if ((site == 27 || site == 28) && page == 2) g_rw.refeedAt = Now() + 400;
            if (site == 27 && page == 10 && prevSite == 27) RWExecutivePurchase();
            if (site == 28 && page == 9 && prevSite == 28) RWForeclosuresPurchase(prevPage);
            if (((site == 27 && page == 14) || (site == 28 && page == 13)) && g_rw.lastBought >= 0) {   // WAYPOINT_D_SET
                N(N_SET_NEW_WAYPOINT, PROPS[g_rw.lastBought].x, PROPS[g_rw.lastBought].y);
                Log("web: waypoint set to %s", PROPS[g_rw.lastBought].name);
            }
            if (site == 12 && prevSite == 12) for (const WsSite &w : WS_SITES) if (page == w.buyPage && prevPage != page) RWWarstockPurchase(w);
            if (site == 13 && prevSite == 13 && page == 26 && prevPage != 26) RWYachtPurchase();
            const WebVehicle *wv = WebVehicleAt(site, page);
            Log("web: site %d page %d%s%s", site, page, wv ? " vehicle " : "", wv ? wv->model : "");
            if (wv) { g_rw.viewing = wv; g_rw.colour = -1; }
            else if (g_rw.ordered) {   // ORDER took us to "Purchase pending..."
                const WebVehicle *o = g_rw.ordered;
                g_rw.ordered = nullptr;
                RWBuyVehicle(*o, g_rw.orderColour);
            } else g_rw.viewing = nullptr;
        }
    }
    if (g_rw.newsPending && g_rw.site == 2 && NB(N<int>(N_HAS_THIS_ADDITIONAL_TEXT_LOADED, "NWS_PRO", 4))) {
        RWFeedNews(g_rw.newsPending);
        g_rw.newsPending = 0;
        RWCall("UPDATE_TEXT"); RWEnd();
    }
    if (g_rw.refeedAt && Now() >= g_rw.refeedAt) {
        g_rw.refeedAt = 0;
        if (g_rw.site == 27 && g_rw.page == 2) RWFeedExecutive();
        if (g_rw.site == 28 && g_rw.page == 2) RWFeedForeclosures();
    }
    // the transaction log arrived from the server while its page is open
    if (g_rw.site == 21 && g_rw.page == 2 && LO().ledgerRevision && LO().ledgerRevision() != g_rw.mbLedgerRev) RWFeedMazeBank(2);
    for (int k = 0; k < 4; ++k) {
        int f = N<int>(N_GET_GLOBAL_ACTIONSCRIPT_FLAG, k);
        if (f != g_rw.asFlag[k]) { Log("web: actionscript flag %d: %d -> %d (site %d page %d)", k, g_rw.asFlag[k], f, g_rw.site, g_rw.page); g_rw.asFlag[k] = f; }
    }
    // mouse
    float mx = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 239), my = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 240);
    if (Now() >= g_rw.mouseHoldUntil && (mx != g_rw.lastMx || my != g_rw.lastMy)) {
        RWCall("SET_MOUSE_INPUT"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, mx); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, my); RWEnd();
        g_rw.lastMx = mx; g_rw.lastMy = my;
    }
    // scrolling: mouse wheel / up-down, the way appinternet does it
    auto pressed = [](int c) { return NB(N<int>(N_IS_DISABLED_CONTROL_PRESSED, 2, c)); };
    auto just = [](int c) { return NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 2, c)); };
    if (pressed(242)) g_rw.scroll = g_rw.scroll <= 0.f ? 200.f : std::min(1000.f, g_rw.scroll + 200.f);         // wheel down
    else if (pressed(241)) g_rw.scroll = g_rw.scroll >= 0.f ? -200.f : std::max(-1000.f, g_rw.scroll - 200.f); // wheel up
    else if (pressed(187)) g_rw.scroll = 200.f;                                                               // frontend down
    else if (pressed(188)) g_rw.scroll = -200.f;                                                              // frontend up
    if (g_rw.scroll != 0.f) {
        g_rw.scroll *= 0.9f;
        if (g_rw.scroll < 5.f && g_rw.scroll > -5.f) g_rw.scroll = 0.f;
        RWCall("SET_ANALOG_STICK_INPUT");
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, 0.f); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, 0.f); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, g_rw.scroll);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, 1);
        RWEnd();
    }
    // controller: left stick moves the cursor, right stick scrolls
    if (!NB(N<int>(N_IS_USING_KEYBOARD_AND_MOUSE, 2))) {
        float lx = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 218) * 127.f, ly = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 219) * 127.f;
        float rx = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 220) * 127.f, ry = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 221) * 127.f;
        RWCall("SET_ANALOG_STICK_INPUT"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, 1.f); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, lx); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, ly); RWEnd();
        RWCall("SET_ANALOG_STICK_INPUT"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, 0.f); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, rx); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, ry); RWEnd();
    }
    if (just(205)) RWInput(4);   // LB
    if (just(206)) RWInput(6);   // RB
    if (just(207)) RWInput(5);   // LT
    if (just(208)) RWInput(7);   // RT
    // click / accept
    if (just(237) || just(201)) {
        RWInput(16);
        RWCall("GET_CURRENT_SELECTION");
        g_rw.selRv = N<int>(N_END_SCALEFORM_MOVIE_METHOD_RETURN_VALUE);
    }
    if (g_rw.selRv && NB(N<int>(N_IS_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_READY, g_rw.selRv))) {
        int sel = N<int>(N_GET_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_INT, g_rw.selRv);
        g_rw.selRv = 0;
        Log("web: click selection %d (site %d page %d)", sel, g_rw.site, g_rw.page);
        if (g_rw.site == 21) RWMazeBankClick(g_rw.page, sel);
        if (g_rw.site == 13 && g_rw.page == 24 && sel == 0) {   // YACHT_D_PERSONALIZE: "Enter name"
            N(N_DISPLAY_ONSCREEN_KEYBOARD, 0, "FMMC_KEY_TIP8", "", g_rw.newName.c_str(), "", "", "", 15);
            g_rw.kbd = true;
        }
        if (g_rw.site == 18 && g_rw.page == 2 && sel >= 0 && sel < (int)g_rw.d8Slots.size()) {
            g_rw.d8Sel = g_rw.d8Slots[sel];
            Log("web: Dynasty 8 picked %s", PROPS[g_rw.d8Sel].name);
        }
        if (g_rw.viewing && sel >= 8 && sel <= 15) g_rw.colour = sel - 8;                                         // colour swatch
        else if (g_rw.viewing && sel == 0) { g_rw.ordered = g_rw.viewing; g_rw.orderColour = g_rw.colour; }        // ORDER
    }
    // back / close: right click, ESC, B
    if (Now() - g_rw.openedAt > 500 && (just(238) || just(202) || just(200) || just(199))) { RWClose(); return; }
    N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, mv, 255, 255, 255, 255, 0);
}
