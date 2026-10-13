// ---------------------------------------------------------------- Los Santos Drug Wars: Dax, the Freakshop and the Acid Lab
// GTA Online's Drug Wars business, recreated: the Freakshop (Dax's hideout, xm3_dlc_int_03_xm3 under the
// warehouse by the LS River), "The First Dose" (6 missions for Dax; the 3rd gives you the Acid Lab like in
// Online), and the Acid Lab itself: a Brickade 6x6 you request and walk into (xm3_dlc_int_01_xm3), with GTA
// Online's entity sets for its supplies (set_supplies_01-05), product (set_product_01-05) and equipment
// upgrade (set_equipment_upgrade). Production: up to 160 units; supplies bought ($60,000) or stolen; sold
// with the Brickade itself.
// Saved in g_cust: "@acid|fd" First Dose missions done (0-6), "@acid|own", "@acid|upg", "@acid|sup" supplies %,
// "@acid|stock10" units x10.
enum { AM_DAX = 240, AM_LAB };
static const Vec3 FREAK_DOOR = V3(622.99f, -405.01f, 24.64f);   // the loading-dock door under the freeway
static const Vec3 DAX_SPOT = V3(572.6f, -412.6f, -69.0f);
static const int ACID_MAX = 160, ACID_SUPPLY_COST = 60000, ACID_UPGRADE_COST = 250000, ACID_UNIT = 1485;
static int AcidGet(const char *k, int def = 0) { auto it = g_cust.find(std::string("@acid|") + k); return it == g_cust.end() ? def : it->second; }
static void AcidSet(const char *k, int v) { g_cust[std::string("@acid|") + k] = v; }
static int g_acidTruck = 0, g_acidBlip = 0, g_freakBlip = 0, g_dax = 0, g_acidMinute = 0;
static bool g_inFreak = false, g_inLab = false;
static Vec3 g_labBack{}; static float g_labBackH = 0.f;

static int AcidUnits() { return AcidGet("stock10") / 10; }
static int AcidUnitValue() { return AcidGet("upg") ? ACID_UNIT * 3 / 2 : ACID_UNIT; }

// ---- the interiors
static std::string AcidLabSets() {
    std::string s;
    int sup = AcidGet("sup"), units = AcidUnits();
    int sl = sup <= 0 ? 0 : std::min(5, 1 + sup / 20), pl = units <= 0 ? 0 : std::min(5, 1 + units * 5 / (ACID_MAX + 1));
    char b[32];
    for (int i = 1; i <= sl; ++i) { std::snprintf(b, sizeof b, "set_supplies_%02d", i); s += std::string(s.empty() ? "" : ";") + b; }
    for (int i = 1; i <= pl; ++i) { std::snprintf(b, sizeof b, "set_product_%02d", i); s += std::string(s.empty() ? "" : ";") + b; }
    if (AcidGet("upg")) s += std::string(s.empty() ? "" : ";") + "set_equipment_upgrade";
    return s;
}
static void AcidLabRefreshSets() {   // inside: show the current supplies / product
    int id = N<int>(N_GET_INTERIOR_AT_COORDS, 485.f, -2625.f, -49.f);
    if (!id) return;
    char b[32];
    for (int i = 1; i <= 5; ++i) {
        std::snprintf(b, sizeof b, "set_supplies_%02d", i); N(N_DEACTIVATE_INTERIOR_ENTITY_SET, id, b);
        std::snprintf(b, sizeof b, "set_product_%02d", i); N(N_DEACTIVATE_INTERIOR_ENTITY_SET, id, b);
    }
    N(N_DEACTIVATE_INTERIOR_ENTITY_SET, id, "set_equipment_upgrade");
    std::string sets = AcidLabSets(), cur;
    for (char c : sets + ";") { if (c == ';') { if (!cur.empty()) N(N_ACTIVATE_INTERIOR_ENTITY_SET, id, cur.c_str()); cur.clear(); } else cur += c; }
    N(N_REFRESH_INTERIOR, id);
}
static void FreakEnter() {
    if (const InteriorDef *d = FindInterior("freakshop")) { TeleportInside(*d); g_inFreak = true; }
}
static void FreakLeave() {
    if (Exists(g_dax)) DeleteEntity(g_dax);
    g_inFreak = false;
    Teleport(FREAK_DOOR.x, FREAK_DOOR.y, FREAK_DOOR.z, 90.f);
}
static void AcidLabEnter() {
    const InteriorDef *d = FindInterior("acidlab");
    if (!d) return;
    g_labBack = Pos(Me()); g_labBackH = N<float>(N_GET_ENTITY_HEADING, Me());
    std::string sets = AcidLabSets();
    InteriorDef dd = *d;
    dd.sets = sets.c_str();
    TeleportInside(dd);
    AcidLabRefreshSets();
    g_inLab = true;
}
static void AcidLabLeave() {
    g_inLab = false;
    Vec3 p = g_labBack; float h = g_labBackH;
    if (Exists(g_acidTruck)) { p = N<Vec3>(N_GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS, g_acidTruck, 0.f, -7.5f, 0.f); h = N<float>(N_GET_ENTITY_HEADING, g_acidTruck) + 180.f; }
    Teleport(p.x, p.y, p.z, h);
}

// ---- the Brickade 6x6
static void AcidTruckReturn() {
    RemoveBlip(g_acidBlip);
    if (Exists(g_acidTruck)) { N(N_SET_ENTITY_AS_MISSION_ENTITY, g_acidTruck, 1, 1); N(N_DELETE_VEHICLE, &g_acidTruck); }
    g_acidTruck = 0;
}
static void AcidTruckRequest() {
    if (!AcidGet("own")) { Notify("You do not own the ~b~Acid Lab~s~ yet: work for Dax at the ~p~Freakshop~s~."); return; }
    if (Exists(g_acidTruck)) { Notify("Your ~b~Acid Lab~s~ is already nearby (see the map)."); return; }
    uint32_t m = Joaat("brickade2");
    if (!LoadModel(m)) return;
    int me = Me();
    Vec3 mp = Pos(me), fwd = N<Vec3>(N_GET_ENTITY_FORWARD_VECTOR, me);
    Vec3 pos = V3(mp.x + fwd.x * 25.f, mp.y + fwd.y * 25.f, mp.z), out{};
    float h = N<float>(N_GET_ENTITY_HEADING, me), oh = h;
    if (NB(N<int>(N_GET_CLOSEST_VEHICLE_NODE_WITH_HEADING, pos.x, pos.y, pos.z, &out, &oh, 1, 3.0f, 0.0f))) { pos = out; h = oh; }
    g_acidTruck = N<int>(N_CREATE_VEHICLE, m, pos.x, pos.y, pos.z, h, 0, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
    if (!g_acidTruck) return;
    N(N_SET_ENTITY_AS_MISSION_ENTITY, g_acidTruck, 1, 1);
    N<int>(N_SET_VEHICLE_ON_GROUND_PROPERLY, g_acidTruck, 5.0f);
    g_acidBlip = AddBlip(g_acidTruck, 840, 0, "Acid Lab");
    Notify("Your ~b~Acid Lab~s~ has been delivered nearby.");
}

// ---- production (every minute, like Online's: slower without the equipment upgrade)
static void AcidProductionTick() {
    if (!AcidGet("own")) return;
    if (!g_acidMinute) g_acidMinute = Now();
    if (Now() - g_acidMinute < 60000) return;
    g_acidMinute += 60000;
    int sup = AcidGet("sup"), st = AcidGet("stock10");
    if (sup <= 0 || st >= ACID_MAX * 10) return;
    int add = AcidGet("upg") ? 10 : 7;   // 1 unit a minute upgraded, 0.7 without
    st = std::min(ACID_MAX * 10, st + add);
    sup = std::max(0, sup - (AcidGet("upg") ? 1 : 1));   // 100 % of supplies makes ~100 units
    AcidSet("stock10", st); AcidSet("sup", sup);
    if (sup == 0) Notify("~y~Acid Lab~s~ has run out of supplies.");
    if (st >= ACID_MAX * 10) Notify("~g~Acid Lab~s~ is full of product. Time to sell.");
    if (g_inLab) AcidLabRefreshSets();
    if (st % 100 < add) Save();
}

// ---- missions
static void AcidStealSupplies() {
    const char *(&gang)[2] = GANGS[Rand(6)];
    Mission m; m.name = "ACID LAB: SUPPLIES";
    Step a = Pickup("Steal the ~g~chemicals~s~.", PickSpot(HOSTILE_SPOTS), 2, "prop_barrel_02a");
    a.snapFoot = true;
    m.steps.push_back(Enemies(a, 5 + Rand(3), gang[0], gang[1]));
    m.steps.push_back(Goto("Take the chemicals to the ~y~Freakshop~s~.", FREAK_DOOR, 5.f));
    m.timeLimitMs = 20 * 60000;
    m.onSuccess = [] { AcidSet("sup", std::min(100, AcidGet("sup") + 50)); Save(); GiveRP(500, "acid supplies"); Shard("~g~SUPPLIES DELIVERED", "Acid Lab supplies +50%"); };
    StartMission(m);
}
static void AcidSell() {
    int units = AcidUnits();
    if (units < 10) { Notify("~r~The Acid Lab needs at least 10 units to sell."); return; }
    int value = units * AcidUnitValue();
    int drops = std::min(4, 1 + units / 50);
    Mission m; m.name = "ACID LAB: SELL";
    Step get; get.type = S_STEAL; get.text = "Get in the ~b~Acid Lab~s~."; get.vehicle = "brickade2";
    get.pos = Exists(g_acidTruck) ? Pos(g_acidTruck) : FREAK_DOOR; get.heading = Exists(g_acidTruck) ? N<float>(N_GET_ENTITY_HEADING, g_acidTruck) : 90.f;
    AcidTruckReturn();   // the mission's own Brickade replaces it
    m.steps.push_back(get);
    for (int i = 0; i < drops; ++i) {
        Step d = Deliver("Deliver the acid to the ~y~buyer~s~ (" + std::to_string(i + 1) + "/" + std::to_string(drops) + ").", PickSpot(DROPS));
        if (i == 0) d.chasers = 1 + Rand(2);
        if (Rand(3) == 0) d.wanted = 2;
        m.steps.push_back(d);
    }
    m.timeLimitMs = 30 * 60000;
    m.coopPay = value / 4;
    m.onSuccess = [value] {
        AcidSet("stock10", 0); AddCash(value); GiveRP(1500, "acid sold"); Save();
        Shard("~g~ACID SOLD", "You earned " + Money(value));
    };
    StartMission(m);
}
// The First Dose: Dax's six missions (GTA Online's story, with Gori's mission steps)
struct DoseMission { const char *name, *intro; int pay, rp; };
static const DoseMission FIRST_DOSE[6] = {
    {"Welcome to the Troupe", "Dax: \"The Lost took our chemicals. Go get 'em back, new friend.\"", 25000, 2000},
    {"Designated Driver", "Dax: \"My boy Lab Rat needs a ride outta a party. Don't let the cops get him.\"", 30000, 2000},
    {"Fatal Incursion", "Dax: \"The Lost got a mobile lab, a Brickade 6x6. Steal it and it's yours.\"", 35000, 2500},
    {"Uncontrolled Substance", "Dax: \"Test batch. Drink it and... uh, defend yourself.\"", 35000, 2500},
    {"Make War Not Love", "Dax: \"Time to hit the Lost where it hurts: their clubhouse.\"", 40000, 3000},
    {"Off the Rails", "Dax: \"A train full of precursor chemicals just came off the rails. Get there first!\"", 60000, 5000},
};
static void FirstDoseDone(int n) {
    const DoseMission &d = FIRST_DOSE[n];
    if (AcidGet("fd") <= n) AcidSet("fd", n + 1);
    AddCash(d.pay); GiveRP(d.rp, "The First Dose");
    std::string sub = Money(d.pay);
    if (n == 2 && !AcidGet("own")) {
        AcidSet("own", 1); AcidSet("sup", 20);
        sub += ". The Acid Lab is yours: request it from Services.";
    }
    if (n == 5) sub += ". The First Dose complete!";
    Save();
    Shard("~g~MISSION PASSED", std::string(d.name) + "  " + sub);
}
static void StartFirstDose(int n) {
    Mission m;
    m.name = FIRST_DOSE[n].name;
    for (auto &c : m.name) c = (char)toupper((unsigned char)c);
    m.timeLimitMs = 30 * 60000;
    switch (n) {
    case 0: {   // the Lost MC's stolen chemicals
        Step a = Pickup("Take back Dax's ~g~chemicals~s~ from the ~r~Lost MC~s~.", V3(1985.f, 3053.f, 47.2f), 2, "prop_barrel_02a");   // Yellow Jack Inn
        a.snapFoot = true;
        m.steps.push_back(Enemies(a, 7, "g_m_y_lost_01", "weapon_sawnoffshotgun"));
        m.steps.push_back(Goto("Bring them to the ~y~Freakshop~s~.", FREAK_DOOR, 5.f));
        break;
    }
    case 1: {   // Lab Rat out of a Vinewood party, cops on your tail
        Step a = Goto("Pick up ~b~Lab Rat~s~ at the party in ~y~Vinewood Hills~s~.", V3(-1546.f, 106.f, 56.8f), 8.f);
        m.steps.push_back(a);
        Step e = Escape("Lose the ~r~cops~s~ with Lab Rat.");
        e.ally = "ig_labrat"; e.wanted = 3;
        m.steps.push_back(e);
        m.steps.push_back(Goto("Drop Lab Rat at the ~y~Freakshop~s~.", FREAK_DOOR, 6.f));
        break;
    }
    case 2: {   // the Brickade 6x6: the Acid Lab
        Step s = Steal("Steal the ~g~Brickade 6x6~s~ from the ~r~Lost~s~ in Stab City.", V3(60.f, 3712.f, 39.7f), 0.f, "brickade2");
        m.steps.push_back(Enemies(s, 8, "g_m_y_lost_01", "weapon_assaultrifle"));
        Step d = Deliver("Drive it to the ~y~Freakshop~s~.", FREAK_DOOR);
        d.chasers = 2;
        m.steps.push_back(d);
        break;
    }
    case 3: {   // a bad trip: clowns
        Step a = Hold("Survive the ~p~trip~s~.", V3(1395.f, 1141.f, 114.3f), 60);   // the La Fuente Blanca fields
        a.onStart = [] { N(N_ANIMPOSTFX_PLAY, "DrugsMichaelAliensFight", 0, 1); };
        a.onDone = [] { N(N_ANIMPOSTFX_STOP, "DrugsMichaelAliensFight"); };
        a.radius = 40.f;
        m.steps.push_back(Enemies(Goto("Go to the ~y~test site~s~.", V3(1395.f, 1141.f, 114.3f), 15.f), 0, "s_m_y_clown_01", "weapon_bat"));
        m.steps.push_back(Enemies(a, 12, "s_m_y_clown_01", "weapon_bat"));
        m.onFail = [] { N(N_ANIMPOSTFX_STOP, "DrugsMichaelAliensFight"); };
        break;
    }
    case 4: {   // the Lost MC clubhouse in East Vinewood
        Step a = Goto("Hit the ~r~Lost MC clubhouse~s~.", V3(981.f, -103.f, 74.8f), 8.f);
        m.steps.push_back(Enemies(a, 10, "g_m_y_lost_02", "weapon_pumpshotgun"));
        Step p = Pickup("Destroy their ~g~stash~s~ (take it).", V3(987.f, -95.f, 74.8f), 3, "prop_box_wood02a");
        p.snapFoot = true;
        m.steps.push_back(p);
        m.steps.push_back(Escape("Lose the ~r~cops~s~."));
        m.steps.back().wanted = 3;
        break;
    }
    case 5: {   // Off the Rails: the derailed chemical train (GTA Online's xm3_train_crash)
        Step a = Goto("Get to the ~y~derailed train~s~ in the Tataviam Mountains.", V3(2616.f, 1520.f, 29.3f), 25.f);
        a.onStart = [] { N(N_REQUEST_IPL, "xm3_train_crash"); };
        m.steps.push_back(Enemies(a, 10, "g_m_y_lost_01", "weapon_assaultrifle"));
        Step p = Pickup("Grab the ~g~precursor chemicals~s~.", V3(2610.f, 1510.f, 29.3f), 3, "prop_barrel_02a");
        p.snapFoot = true;
        m.steps.push_back(p);
        Step d = Goto("Bring them to the ~y~Freakshop~s~.", FREAK_DOOR, 6.f);
        d.chasers = 2;
        m.steps.push_back(d);
        m.onFail = [] { N(N_REMOVE_IPL, "xm3_train_crash"); };
        break;
    }
    }
    m.coopPay = FIRST_DOSE[n].pay / 2;
    m.onSuccess = [n] { if (n == 5) N(N_REMOVE_IPL, "xm3_train_crash"); FirstDoseDone(n); };
    Notify(FIRST_DOSE[n].intro);
    StartMission(m);
}

// The Last Dose: Dax's five follow-up missions, once The First Dose is done (GTA Online's 2023 update)
static const DoseMission LAST_DOSE[5] = {
    {"This Is an Intervention", "Dax: \"A cult grabbed Lab Rat. Some wellness retreat in the hills. Get him out.\"", 50000, 3000},
    {"Unusual Suspects", "Dax: \"The doc behind all this keeps his files in a van at the docks. Bring it to me.\"", 50000, 3000},
    {"FriedMind", "Dax: \"Bad batch, man. Real bad. Try to stay alive till it wears off.\"", 55000, 3500},
    {"Checking In", "Dax: \"His clinic. Get in, copy his research, get out.\"", 60000, 4000},
    {"BDKD", "Dax: \"Last one. His lab. Burn it all.\"", 100000, 6000},
};
static void LastDoseDone(int n) {
    const DoseMission &d = LAST_DOSE[n];
    if (AcidGet("ld") <= n) AcidSet("ld", n + 1);
    AddCash(d.pay); GiveRP(d.rp, "The Last Dose");
    Save();
    Shard("~g~MISSION PASSED", std::string(d.name) + "  " + Money(d.pay) + (n == 4 ? ". The Last Dose complete!" : ""));
}
static Step ExitTo(Step s, Vec3 out, float h) {   // reaching the step takes you back outside
    auto prev = s.onDone;
    s.onDone = [out, h, prev] { if (prev) prev(); Teleport(out.x, out.y, out.z, h); };
    return s;
}
static void StartLastDose(int n) {
    Mission m;
    m.name = LAST_DOSE[n].name;
    for (auto &c : m.name) c = (char)toupper((unsigned char)c);
    m.timeLimitMs = 30 * 60000;
    switch (n) {
    case 0: {   // the cult's retreat in the Great Chaparral
        Step a = Goto("Find ~b~Lab Rat~s~ at the ~y~retreat~s~ in the Great Chaparral.", V3(-1093.f, 2696.f, 19.f), 10.f);
        m.steps.push_back(Enemies(a, 9, "a_m_y_acult_01", "weapon_machete"));
        Step e = Escape("Get Lab Rat away from the ~r~cult~s~.", V3(-1093.f, 2696.f, 19.f), 350.f);
        e.ally = "ig_labrat"; e.chasers = 2;
        m.steps.push_back(e);
        m.steps.push_back(Goto("Bring Lab Rat to the ~y~Freakshop~s~.", FREAK_DOOR, 6.f));
        break;
    }
    case 1: {   // the doctor's van at the docks
        Step s = Steal("Steal the ~g~doctor's van~s~ at the docks.", V3(1010.f, -3170.f, 5.9f), 90.f, "pony2");
        m.steps.push_back(Enemies(s, 6, "s_m_m_chemsec_01", "weapon_carbinerifle"));
        Step d = Deliver("Take the van to the ~y~Freakshop~s~.", FREAK_DOOR);
        d.chasers = 2; d.wanted = 2;
        m.steps.push_back(d);
        break;
    }
    case 2: {   // FriedMind: another trip
        Step a = Hold("Survive the ~p~bad trip~s~.", V3(711.f, 1198.f, 348.5f), 75);   // under the Vinewood sign
        a.radius = 45.f;
        a.onStart = [] { N(N_ANIMPOSTFX_PLAY, "DrugsTrevorClownsFight", 0, 1); };
        a.onDone = [] { N(N_ANIMPOSTFX_STOP, "DrugsTrevorClownsFight"); };
        m.steps.push_back(Goto("Meet Dax's guy under the ~y~Vinewood sign~s~.", V3(711.f, 1198.f, 348.5f), 15.f));
        m.steps.push_back(Enemies(a, 14, "s_m_y_clown_01", "weapon_bat"));
        m.onFail = [] { N(N_ANIMPOSTFX_STOP, "DrugsTrevorClownsFight"); };
        break;
    }
    case 3: {   // Checking In: the clinic (xm3_dlc_int_04_xm3)
        Vec3 door = V3(-449.f, -340.f, 34.5f);   // a side door of the Mount Zonah Medical Center
        m.steps.push_back(Interior(Goto("Get into the doctor's ~y~clinic~s~.", door, 4.f), "xm3_clinic"));
        Step h = Hold("Copy the ~g~research~s~ from his computer.", V3(497.f, -2558.f, -49.f), 30);
        h.snapFoot = true; h.enemies = 6; h.enemyModel = "s_m_m_security_01"; h.enemyWeapon = "weapon_pistol";
        m.steps.push_back(h);
        m.steps.push_back(ExitTo(Goto("Get out of the ~y~clinic~s~.", V3(495.f, -2560.f, -49.f), 3.f), door, 160.f));
        Step e = Escape("Lose the ~r~cops~s~."); e.wanted = 3;
        m.steps.push_back(e);
        break;
    }
    case 4: {   // BDKD: the doctor's lab (xm3_dlc_int_02_xm3)
        Vec3 door = V3(-113.f, -2618.f, 6.f);   // a warehouse on Elysian Island
        m.steps.push_back(Enemies(Interior(Goto("Hit the doctor's ~y~lab~s~ on Elysian Island.", door, 5.f), "xm3_lab"), 8, "s_m_m_chemsec_01", "weapon_carbinerifle"));
        Step p = Pickup("Destroy his ~g~acid batches~s~ (take them).", V3(520.f, -2625.f, -49.f), 3, "prop_barrel_02a");
        p.snapFoot = true; p.enemies = 8; p.enemyModel = "s_m_m_chemsec_01"; p.enemyWeapon = "weapon_assaultrifle";
        m.steps.push_back(p);
        m.steps.push_back(ExitTo(Goto("Get out of the ~y~lab~s~.", V3(520.f, -2625.f, -49.f), 3.f), door, 0.f));
        Step d = Goto("Get back to the ~y~Freakshop~s~.", FREAK_DOOR, 6.f);
        d.chasers = 3;
        m.steps.push_back(d);
        break;
    }
    }
    m.coopPay = LAST_DOSE[n].pay / 2;
    m.onSuccess = [n] { LastDoseDone(n); };
    Notify(LAST_DOSE[n].intro);
    StartMission(m);
}

// ---- menus
static bool AcidMenu(const Frame &f, std::string &title, std::string &sub, std::vector<Item> &items) {
    switch ((int)f.id) {
    case AM_DAX: {
        title = "Dax"; sub = "THE FIRST DOSE";
        int done = AcidGet("fd");
        for (int i = 0; i < 6; ++i) {
            bool next = i == done, past = i < done;
            items.push_back({FIRST_DOSE[i].name, past ? "Replay" : next ? Money(FIRST_DOSE[i].pay) : "Locked", FIRST_DOSE[i].intro,
                             (past || next) ? std::function<void()>([i] { CloseMenu(); if (g_inFreak) FreakLeave(); StartFirstDose(i); }) : nullptr});
        }
        if (done >= 6) {   // The Last Dose
            int ld = AcidGet("ld");
            for (int i = 0; i < 5; ++i) {
                bool next = i == ld, past = i < ld;
                items.push_back({std::string("Last Dose: ") + LAST_DOSE[i].name, past ? "Replay" : next ? Money(LAST_DOSE[i].pay) : "Locked", LAST_DOSE[i].intro,
                                 (past || next) ? std::function<void()>([i] { CloseMenu(); if (g_inFreak) FreakLeave(); StartLastDose(i); }) : nullptr});
            }
        }
        if (AcidGet("own")) items.push_back({"Request Acid Lab", "", "Dax's Brickade 6x6, delivered nearby.", [] { CloseMenu(); if (g_inFreak) FreakLeave(); AcidTruckRequest(); }});
        return true;
    }
    case AM_LAB: {
        title = "Acid Lab"; sub = "BUSINESS";
        int units = AcidUnits(), sup = AcidGet("sup");
        items.push_back({"Product", std::to_string(units) + "/" + std::to_string(ACID_MAX), "Value " + Money((long long)units * AcidUnitValue()) + ".", nullptr});
        items.push_back({"Supplies", std::to_string(sup) + "%", "Production stops without supplies.", nullptr});
        items.push_back({"Buy Supplies", Money(ACID_SUPPLY_COST), "Fill the supplies.", [sup] {
            if (sup >= 100) return;
            PayWhat what("acid lab supplies");
            if (Pay(ACID_SUPPLY_COST)) { AcidSet("sup", 100); Save(); AcidLabRefreshSets(); Sound("PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET"); }
        }});
        items.push_back({"Steal Supplies", "", "A mission: +50% supplies.", [] { CloseMenu(); AcidLabLeave(); AcidStealSupplies(); }});
        items.push_back({"Sell Product", units >= 10 ? Money((long long)units * AcidUnitValue()) : "Not enough", "Deliver it with the Acid Lab.", [] { CloseMenu(); AcidLabLeave(); AcidSell(); }});
        if (!AcidGet("upg"))
            items.push_back({"Equipment Upgrade", Money(ACID_UPGRADE_COST), "Faster production, product worth 50% more.", [] {
                PayWhat what("acid lab upgrade");
                if (Pay(ACID_UPGRADE_COST)) { AcidSet("upg", 1); Save(); AcidLabRefreshSets(); Sound("PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET"); }
            }});
        else items.push_back({"Equipment Upgrade", "Owned", "", nullptr});
        items.push_back({"Leave the Acid Lab", "", "", [] { CloseMenu(); AcidLabLeave(); }});
        return true;
    }
    }
    return false;
}
static void AcidServiceItems(std::vector<Item> &items) {
    if (!AcidGet("own")) return;
    if (Exists(g_acidTruck)) items.push_back({"Return Acid Lab", "", "Send the Brickade 6x6 back.", [] { CloseMenu(); AcidTruckReturn(); }});
    else items.push_back({"Request Acid Lab", "", "Your Brickade 6x6 mobile lab.", [] { CloseMenu(); AcidTruckRequest(); }});
}

// ---- every frame
static void AcidTick() {
    if (!YouActive()) { RemoveBlip(g_freakBlip); return; }
    if (!g_freakBlip) g_freakBlip = CoordBlip(FREAK_DOOR, 847, 0, "Freakshop", true);
    AcidProductionTick();
    if (g_menuOpen || g_mActive) return;
    int me = Me();
    Vec3 p = Pos(me);
    if (g_inFreak) {
        if (!Exists(g_dax) && LoadModel(Joaat("ig_dax"))) {
            g_dax = N<int>(N_CREATE_PED, 4, Joaat("ig_dax"), DAX_SPOT.x, DAX_SPOT.y, DAX_SPOT.z, 180.f, 0, 0);
            if (g_dax) { N(N_SET_ENTITY_AS_MISSION_ENTITY, g_dax, 1, 1); N(N_SET_BLOCKING_OF_NON_TEMPORARY_EVENTS, g_dax, 1); N(N_SET_ENTITY_INVINCIBLE, g_dax, 1);
                         N(N_SET_PED_DEFAULT_COMPONENT_VARIATION, g_dax);   // his clothes (created bare otherwise)
                         N(N_TASK_START_SCENARIO_IN_PLACE, g_dax, "WORLD_HUMAN_SMOKING_POT", 0, 1); }
        }
        if (p.z > -60.f) { g_inFreak = false; if (Exists(g_dax)) DeleteEntity(g_dax); return; }   // left some other way
        if (Exists(g_dax) && Dist(p, Pos(g_dax)) < 2.5f) {
            Help("Press ~INPUT_CONTEXT~ to talk to ~b~Dax~s~. Press ~INPUT_ENTER~ to leave.");
            if (JustPressed(51)) { g_menuOpen = true; g_stack.clear(); Open((MenuId)AM_DAX); }
        } else Help("Press ~INPUT_ENTER~ to leave the Freakshop.");
        if (JustPressed(23)) FreakLeave();
        return;
    }
    if (g_inLab) {
        if (p.z > -40.f) { g_inLab = false; return; }
        Help("Press ~INPUT_CONTEXT~ to run the ~b~Acid Lab~s~. Press ~INPUT_ENTER~ to leave.");
        if (JustPressed(51)) { g_menuOpen = true; g_stack.clear(); Open((MenuId)AM_LAB); }
        if (JustPressed(23)) AcidLabLeave();
        return;
    }
    if (!InAnyVehicle() && Dist(p, FREAK_DOOR) < 2.5f) {
        Help("Press ~INPUT_CONTEXT~ to enter the ~p~Freakshop~s~.");
        if (JustPressed(51)) FreakEnter();
        return;
    }
    if (Exists(g_acidTruck) && !InAnyVehicle()) {
        Vec3 rear = N<Vec3>(N_GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS, g_acidTruck, 0.f, -6.0f, 0.f);
        if (Dist(p, rear) < 2.5f) {
            Help("Press ~INPUT_CONTEXT~ to enter the ~b~Acid Lab~s~.");
            if (JustPressed(51)) AcidLabEnter();
        }
    }
}
