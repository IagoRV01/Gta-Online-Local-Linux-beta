// ---------------------------------------------------------------- Los Santos Customs and Ammu-Nation for YOU
// The story shops (carmod_shop / gunclub_shop, launched by shop_controller) are written for Michael, Franklin
// and Trevor: with GTA Local Online's freemode character they do not work, so while YOU plays they are stopped
// and Gori runs GTA Online's versions: the Vehicle Workshop menu at every LS Customs (Online prices, saved on
// the personal vehicle) and gunclub_shop's multiplayer catalogue at every Ammu-Nation counter (ammunation.h).
// Both pay like Online shops: cash first, then the bank.
#include "ammunation.h"
enum { SH_AMMU = 190, SH_AMMUCAT, SH_ARMOR };
struct ShopSpot { const char *name; float x, y, z; };
// LS Customs: in front of each garage door (the car stays there while it is modified)
static const ShopSpot LSC_SPOTS[] = {
    {"Los Santos Customs", -365.4f, -131.8f, 37.9f},        // Burton
    {"Los Santos Customs", 731.8f, -1088.9f, 22.2f},        // La Mesa
    {"Los Santos Customs", -1155.5f, -2005.4f, 13.2f},      // LSIA
    {"Los Santos Customs", 1175.0f, 2640.0f, 37.8f},        // Route 68, Harmony
    {"Beeker's Garage", 110.9f, 6626.4f, 31.8f},            // Paleto Bay
    {"Benny's Original Motor Works", -205.0f, -1310.0f, 31.0f},
};
// Ammu-Nation: the counters (carmod_shop.c's shop table, shops 28-38)
static const ShopSpot AMMU_SPOTS[] = {
    {"Ammu-Nation", 23.6862f, -1106.4612f, 29.9159f}, {"Ammu-Nation", 1693.5724f, 3761.6008f, 34.8242f},
    {"Ammu-Nation", 252.8583f, -51.6284f, 70.06f}, {"Ammu-Nation", 841.0564f, -1034.7616f, 28.3137f},
    {"Ammu-Nation", -330.2908f, 6085.548f, 31.5737f}, {"Ammu-Nation", -660.9294f, -934.1031f, 21.9481f},
    {"Ammu-Nation", -1304.9764f, -395.8181f, 36.8147f}, {"Ammu-Nation", -1117.6122f, 2700.264f, 18.673f},
    {"Ammu-Nation", -3172.5115f, 1089.4125f, 20.9576f}, {"Ammu-Nation", 2566.5918f, 293.1332f, 108.8538f},
    {"Ammu-Nation", 808.8609f, -2158.5078f, 29.7379f},
};
static const char *AMMU_CATS[] = {"", "Submachine Guns", "Shotguns", "Assault Rifles", "Sniper Rifles", "Machine Guns", "Heavy Weapons",
                                  "Melee", "Pistols", "Throwables", "Jerry Can"};
static const int AMMU_CAT_ORDER[] = {7, 8, 1, 2, 3, 5, 4, 6, 9, 10};
static const struct { const char *name; int price, armour; } ARMORS[] = {{"Super Light Armor", 500, 20}, {"Light Armor", 1000, 40},
    {"Standard Armor", 1500, 60}, {"Heavy Armor", 2000, 80}, {"Super Heavy Armor", 2500, 100}};
static int g_shopNext = 0;

static int NearShop(const ShopSpot *spots, int n, float range) {
    Vec3 p = Pos(Me());
    for (int i = 0; i < n; ++i)
        if (std::hypot(p.x - spots[i].x, p.y - spots[i].y) < range && std::fabs(p.z - spots[i].z) < 4.f) return i;
    return -1;
}
// rounds bought at a time: two magazines (thrown weapons: one)
static int AmmoPack(uint32_t w, int cat) {
    if (cat == 9 || cat == 10) return 1;
    int clip = N<int>(N_GET_MAX_AMMO_IN_CLIP, Me(), w, 1);
    return std::max(1, clip) * 2;
}

// ---- LS Customs
static void LscStart(int shop) {
    int v = MyVehicle(), me = Me();
    if (!Exists(v) || N<int>(N_GET_PED_IN_VEHICLE_SEAT, v, -1, 0) != me) return;
    g_lscAt = shop;
    g_wsVeh = v;
    g_wsIdx = v == g_personal ? g_personalIdx : -1;
    // the value Online's mod prices are based on: the catalogue price of the model
    g_wsValue = 0;
    uint32_t model = (uint32_t)N<int>(N_GET_ENTITY_MODEL, v);
    auto it = g_catByHash.find(model);
    if (it != g_catByHash.end()) g_wsValue = g_cat[it->second].price;
    N(N_SET_VEHICLE_FORWARD_SPEED, v, 0.f);
    N(N_FREEZE_ENTITY_POSITION, v, 1);
    N(N_SET_VEHICLE_ENGINE_ON, v, 0, 1, 1);
    Sound("SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET");
    g_menuOpen = true; g_stack.clear(); Open((MenuId)SM_TUNE);
    Log("lsc: %s, vehicle %d (garage %d, value %d)", LSC_SPOTS[shop].name, v, g_wsIdx, g_wsValue);
}
static void LscFinish() {   // from SpvWorkshopFinish
    int v = g_wsVeh;
    if (Exists(v)) {
        N(N_FREEZE_ENTITY_POSITION, v, 0);
        N(N_SET_VEHICLE_ENGINE_ON, v, 1, 1, 0);
        if (g_wsIdx >= 0) Notify("Your ~b~personal vehicle~s~'s modifications are saved.");
    }
    g_lscAt = -1;
}
static int RepairPrice(int v) {
    float body = N<float>(N_GET_VEHICLE_BODY_HEALTH, v), eng = N<float>(N_GET_VEHICLE_ENGINE_HEALTH, v);
    float dmg = std::max(0.f, 1000.f - body) + std::max(0.f, 1000.f - eng);
    if (dmg < 1.f && !NB(N<int>(N_ARE_ALL_VEHICLE_WINDOWS_INTACT, v))) dmg = 50.f;
    return dmg < 1.f ? 0 : std::max(100, (int)(dmg * 2.f / 50.f) * 50);
}
static void LscItems(std::vector<Item> &items) {   // added on top of the Vehicle Workshop menu at LS Customs
    int v = g_wsVeh;
    if (!Exists(v)) return;
    int price = RepairPrice(v);
    if (price > 0)
        items.insert(items.begin(), {"Repair", Money(price), "Fix the body, the engine and the windows.", [price] {
            int v2 = g_wsVeh; if (!Exists(v2)) return;
            PayWhat what("vehicle repair");
            if (!Pay(price)) return;
            N(N_SET_VEHICLE_FIXED, v2); N(N_SET_VEHICLE_DEFORMATION_FIXED, v2); N(N_SET_VEHICLE_DIRT_LEVEL, v2, 0.f);
            Sound("PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET");
        }});
    if (g_wsIdx < 0)
        items.insert(items.begin(), {"Not your personal vehicle", "", "Modifications on other vehicles are not saved. Buy vehicles on the vehicle websites.", nullptr});
}

// ---- Ammu-Nation
static void AmmuBuy(int i) {
    const AmmuWeapon &a = AMMU_WEAPONS[i];
    uint32_t w = Joaat(a.weapon);
    int me = Me();
    bool owned = NB(N<int>(N_HAS_PED_GOT_WEAPON, me, w, 0));
    if (owned && a.cat == 7) return;   // melee: nothing more to buy
    int price = owned ? a.rounds : a.price;   // ammunation.h: "rounds" is the ammo price of the weapon
    if (owned && price <= 0) return;
    PayWhat what(owned ? "ammo" : "weapon");
    if (price > 0 && !Pay(price)) return;
    int pack = AmmoPack(w, a.cat);
    if (owned) N(N_ADD_AMMO_TO_PED, me, w, pack);
    else N(N_GIVE_WEAPON_TO_PED, me, w, a.cat == 7 ? 1 : pack, 0, 1);
    Sound("PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET");
}
static bool ShopsMenu(const Frame &f, std::string &title, std::string &sub, std::vector<Item> &items) {
    int me = Me();
    switch ((int)f.id) {
    case SH_AMMU: {
        title = "Ammu-Nation"; sub = "WEAPONS";
        for (int c : AMMU_CAT_ORDER) items.push_back({AMMU_CATS[c], ">", "", [c] { Open((MenuId)SH_AMMUCAT, c); }});
        items.push_back({"Armor", ">", "Body armor.", [] { Open((MenuId)SH_ARMOR); }});
        items.push_back({"Parachute", Money(500), "A parachute: jump from high enough and open it.", [] {
            PayWhat what("parachute");
            if (Pay(500)) { N(N_GIVE_WEAPON_TO_PED, Me(), Joaat("GADGET_PARACHUTE"), 1, 0, 0); Sound("PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET"); }
        }});
        items.push_back({"Weapon Workshop", ">", "Upgrades, tints and attachments for the weapons you own.", [] { Open((MenuId)SM_WWS); }});
        return true;
    }
    case SH_AMMUCAT: {
        int c = f.param;
        title = AMMU_CATS[c]; sub = "AMMU-NATION";
        std::vector<int> list;
        for (int i = 0; i < NAMMU_WEAPONS; ++i)
            if (AMMU_WEAPONS[i].cat == c && NB(N<int>(N_IS_WEAPON_VALID, Joaat(AMMU_WEAPONS[i].weapon)))) list.push_back(i);
        std::sort(list.begin(), list.end(), [](int a, int b) { return AMMU_WEAPONS[a].price < AMMU_WEAPONS[b].price; });
        for (int i : list) {
            const AmmuWeapon &a = AMMU_WEAPONS[i];
            uint32_t w = Joaat(a.weapon);
            bool owned = NB(N<int>(N_HAS_PED_GOT_WEAPON, me, w, 0));
            std::string right = !owned ? Money(a.price) : c == 7 ? "Owned" : a.rounds > 0 ? "Ammo " + Money(a.rounds) : "Owned";
            std::string desc = !owned ? "Buy it." : c == 7 ? "You own it." : "Buy " + std::to_string(AmmoPack(w, c)) + (c == 9 || c == 10 ? "" : " rounds") + ".";
            items.push_back({WeaponName(a.weapon), right, desc, [i] { AmmuBuy(i); }});
        }
        if (items.empty()) items.push_back({"Nothing for sale", "", "", nullptr});
        return true;
    }
    case SH_ARMOR: {
        title = "Armor"; sub = "AMMU-NATION";
        int cur = N<int>(N_GET_PED_ARMOUR, me);
        for (auto &a : ARMORS) {
            int price = a.price, value = a.armour;
            items.push_back({a.name, cur >= value ? "Equipped" : Money(price), std::to_string(value) + "% armor.", [price, value] {
                if (N<int>(N_GET_PED_ARMOUR, Me()) >= value) return;
                PayWhat what("armor");
                if (Pay(price)) { N(N_SET_PED_ARMOUR, Me(), value); Sound("PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET"); }
            }});
        }
        return true;
    }
    }
    return false;
}

// the clerk behind each counter (gunclub_shop spawns him; it is stopped while YOU plays)
static int g_clerk = 0, g_clerkShop = -1;
static void AmmuClerkTick() {
    Vec3 p = Pos(Me());
    int shop = -1;
    for (int i = 0; i < (int)(sizeof AMMU_SPOTS / sizeof *AMMU_SPOTS); ++i)
        if (Dist(p, V3(AMMU_SPOTS[i].x, AMMU_SPOTS[i].y, AMMU_SPOTS[i].z)) < 60.f) shop = i;
    if (shop != g_clerkShop && Exists(g_clerk)) DeleteEntity(g_clerk);
    g_clerkShop = shop;
    if (shop < 0) return;
    if (!Exists(g_clerk)) {
        static const bool country[11] = {false, true, false, false, true, false, false, true, true, true, false};
        uint32_t m = Joaat(country[shop] ? "s_m_m_ammucountry" : "s_m_y_ammucity_01");
        if (!LoadModel(m)) return;
        const ShopSpot &s = AMMU_SPOTS[shop];
        g_clerk = N<int>(N_CREATE_PED, 4, m, s.x, s.y, s.z - 0.9f, 0.f, 0, 0);
        N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
        if (!g_clerk) return;
        N(N_SET_ENTITY_AS_MISSION_ENTITY, g_clerk, 1, 1);
        N(N_SET_BLOCKING_OF_NON_TEMPORARY_EVENTS, g_clerk, 1);
        N(N_SET_ENTITY_INVINCIBLE, g_clerk, 1);
        N(N_SET_PED_DEFAULT_COMPONENT_VARIATION, g_clerk);
        N(N_FREEZE_ENTITY_POSITION, g_clerk, 1);
    }
    if (Dist(p, Pos(g_clerk)) < 6.f && !NB(N<int>(N_IS_PED_FACING_PED, g_clerk, Me(), 30.f)))
        N(N_TASK_TURN_PED_TO_FACE_ENTITY, g_clerk, Me(), 1000);
}
static void ShopsTick() {
    if (!YouActive()) { if (Exists(g_clerk)) DeleteEntity(g_clerk); return; }
    AmmuClerkTick();
    // the story shop scripts misbehave with the freemode character: Gori's shops replace them
    if (Now() > g_shopNext) {
        g_shopNext = Now() + 1000;
        for (const char *s : {"carmod_shop", "gunclub_shop"})
            if (N<int>(N_GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH, Joaat(s)) > 0) { N(N_TERMINATE_ALL_SCRIPTS_WITH_THIS_NAME, s); Log("shops: stopped %s (YOU uses Gori's shop)", s); }
    }
    if (g_menuOpen || g_lscAt >= 0 || g_mActive) return;
    int me = Me();
    if (InAnyVehicle()) {
        int v = MyVehicle();
        if (v != g_personal) return;   // other cars: SellCars offers to sell them here
        int s = NearShop(LSC_SPOTS, (int)(sizeof LSC_SPOTS / sizeof *LSC_SPOTS), 9.f);
        if (s < 0 || N<float>(N_GET_ENTITY_SPEED, v) > 4.f) return;
        Help(std::string("Press ~INPUT_CONTEXT~ to enter ") + LSC_SPOTS[s].name + ".");
        if (JustPressed(51)) LscStart(s);
        return;
    }
    int s = NearShop(AMMU_SPOTS, (int)(sizeof AMMU_SPOTS / sizeof *AMMU_SPOTS), 3.f);
    if (s < 0 || Dead(me)) return;
    Help("Press ~INPUT_CONTEXT~ to browse Ammu-Nation.");
    if (JustPressed(51)) { g_menuOpen = true; g_stack.clear(); Open((MenuId)SH_AMMU); }
}
