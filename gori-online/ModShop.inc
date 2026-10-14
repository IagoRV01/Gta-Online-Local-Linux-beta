// ---------------------------------------------------------------- Los Santos Customs and Benny's, like GTA Online
// carmod_shop.ysc's shops 39-44: you drive into the shop's entry area, the garage door opens, the car is parked in
// the bay and the menu comes up under the shop's own banner (ShopUI_Title_Carmod / _Supermod). Browsing a part
// fits it on the car straight away (the camera turns to it); buying keeps it, going back puts back what you own.
// Everything is saved on the personal vehicle; another car can be made personal with Loss/Theft Prevention
// (tracker and insurance, like Online's). Benny's adds its conversions (Benny's Original Motor Works) and the
// supermod kit's parts. Online's prices come from Rockstar's catalogue server, so these are close, not exact.
enum { MS_ROOT = 300, MS_MOD, MS_PAIR, MS_LIGHTS, MS_XENON, MS_NEON, MS_NEONCOL, MS_RESPRAY, MS_PAINTGRP, MS_PAINT,
       MS_WHEELS, MS_WTYPE, MS_RIMS, MS_TYRES, MS_SMOKE, MS_WINDOWS, MS_PLATES, MS_BENNYUP, MS_INTERIOR, MS_ENGBAY, MS_LIVERY };
struct MsShop {
    const char *name; int kind;                  // 0 LS Customs, 1 Benny's
    float ax, ay, az, bx, by, bz, w;              // the entry area (IS_ENTITY_IN_ANGLED_AREA)
    int door1, door2;                             // door system hashes of the garage doors
    float px, py, pz, ph;                         // the bay: where the car sits while it is modified
    float ex, ey, ez;                             // where it drives out to
};
static const MsShop MS_SHOPS[] = {
    {"Los Santos Customs: LSIA", 0, -1138.81f, -1991.07f, 12.17f, -1145.78f, -1983.84f, 16.16f, 13.13f, -1871080926, 0,
     -1151.20f, -1996.88f, 12.18f, 315.68f, -1140.00f, -1985.63f, 12.17f},
    {"Los Santos Customs: La Mesa", 0, 719.73f, -1083.27f, 20.44f, 719.95f, -1093.45f, 25.32f, 9.5f, 1168079979, 0,
     732.03f, -1088.14f, 21.17f, 93.88f, 719.48f, -1088.46f, 21.66f},
    {"Los Santos Customs: Burton", 0, -357.79f, -129.71f, 37.68f, -360.73f, -137.64f, 41.68f, 10.75f, 1206354175, 0,
     -348.04f, -137.09f, 38.01f, 71.42f, -362.01f, -132.40f, 37.68f},
    {"Beeker's Garage: Paleto Bay", 0, 108.74f, 6612.34f, 31.0f, 119.63f, 6623.33f, 34.96f, 10.44f, -1038180727, 1200466273,
     108.1f, 6624.2f, 31.4f, 223.8f, 116.5f, 6615.4f, 31.2f},
    {"Los Santos Customs: Route 68", 0, 1188.10f, 2649.34f, 36.02f, 1169.04f, 2649.44f, 40.85f, 10.75f, 1391004277, -459199009,
     1178.5f, 2640.2f, 37.6f, 0.f, 1178.6f, 2655.0f, 37.8f},
    {"Benny's Original Motor Works", 1, -202.16f, -1307.19f, 35.57f, -208.44f, -1307.25f, 29.58f, 8.f, -288764223, 0,
     -205.99f, -1316.04f, 29.89f, 358.76f, -197.24f, -1301.50f, 30.30f},
    // the Arena Workshop: am_mp_arena_garage's first vehicle door at the Maze Bank Arena; the bay is on the workshop floor
    {"Arena Workshop", 2, -360.08f, -1874.24f, 19.59f, -368.45f, -1876.19f, 23.26f, 7.f, 0, 0,
     200.0f, 5180.0f, -89.6f, 90.f, -364.3f, -1868.0f, 19.6f},
};
static const int NMS_SHOPS = (int)(sizeof MS_SHOPS / sizeof *MS_SHOPS);

struct MsState {
    int shop = -1, veh = 0, idx = -1, value = 100000, focus = 0, leftAt = 0, cam = 0, lastShop = -1, interior = 0;   // lastShop: wait until the car is out of its area
    std::string owned;                  // CaptureVehicle of what is bought: previews go back to it
    float camAngle = 0.f; Vec3 camPos{}, camAt{};
    bool camInit = false, hoodOpen = false;
};
static MsState g_ms;
static bool MsActive() { return g_ms.shop >= 0; }

// ---- prices (Online's base prices; a car's value scales them like Online's per-vehicle price groups)
static float MsFactor() { return std::max(0.6f, std::min(2.5f, std::sqrt((float)g_ms.value / 150000.f))); }
static int MsRound(float v) { return std::max(50, (int)(v / 50.f + 0.5f) * 50); }
static int MsModPrice(int t, int i) {
    static const int ENGINE[] = {9000, 12500, 18000, 33500}, BRAKES[] = {2000, 2700, 3500, 4500}, TRANS[] = {2950, 3250, 4000, 5000},
                     SUSP[] = {1000, 2000, 3400, 4400, 5200}, ARMOR[] = {2500, 5000, 7500, 10000, 12500};
    auto pick = [&](const int *a, int n) { return a[std::max(0, std::min(i, n - 1))]; };
    float base = t == 11 ? pick(ENGINE, 4) : t == 12 ? pick(BRAKES, 4) : t == 13 ? pick(TRANS, 4) : t == 15 ? pick(SUSP, 5) : t == 16 ? pick(ARMOR, 5)
               : t == 14 ? 500.f + 300.f * i : t == 23 || t == 24 ? 600.f + 120.f * i : t == 48 ? 2500.f + 500.f * i
               : std::min(15000.f, 300.f + 350.f * (i + 1));
    return MsRound(base * MsFactor());
}
static bool MsPersonal() { return g_ms.idx >= 0 && g_ms.veh == g_personal; }

// ---- the car: previews, purchases
static void MsRevert() {   // back to what is bought: every slot cleared first (ApplyVehicle only sets)
    int v = g_ms.veh;
    if (!Exists(v)) return;
    N(N_SET_VEHICLE_MOD_KIT, v, 0);
    for (int t = 0; t <= 49; ++t) { if (t >= 17 && t <= 22) N(N_TOGGLE_VEHICLE_MOD, v, t, 0); else N(N_REMOVE_VEHICLE_MOD, v, t); }
    for (int i = 0; i < 4; ++i) N(N_SET_VEHICLE_NEON_ENABLED, v, i, 0);
    N(N_SET_VEHICLE_XENON_LIGHT_COLOR_INDEX, v, -1);
    ApplyVehicle(v, g_ms.owned);
}
static bool MsBuy(int price, const char *what) {   // pay and keep the car as it is now
    if (!MsPersonal()) { Notify("~r~Buy ~b~Loss/Theft Prevention~r~ first:~s~ only your personal vehicles are modified."); return false; }
    PayWhat w(what);
    if (price > 0 && !Pay(price)) return false;
    g_ms.owned = CaptureVehicle(g_ms.veh);
    if (g_ms.idx >= 0 && g_ms.idx < (int)g_garage.size()) { g_garage[g_ms.idx].data = g_ms.owned; Save(); }
    Sound("PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET");
    return true;
}
// an item that fits on hover and is bought on select; `owned` marks what the car has
static Item MsItem(const std::string &label, int price, bool owned, std::function<void()> apply, const char *what, const std::string &desc = "") {
    return {label, owned ? "Owned" : Money(price), desc,
            [price, owned, apply, what] { if (owned) return; MsRevert(); apply(); if (!MsBuy(price, what)) MsRevert(); },
            [apply] { MsRevert(); apply(); }};
}

// ---- the camera: it follows the part being looked at
enum { MF_OVERVIEW, MF_FRONT, MF_REAR, MF_SIDE, MF_TOP, MF_ENGINE, MF_INSIDE };
static void MsFocus(int f) {
    g_ms.focus = f;
    int v = g_ms.veh;
    if (!Exists(v)) return;
    bool hood = f == MF_ENGINE;
    if (hood != g_ms.hoodOpen) { if (hood) N(N_SET_VEHICLE_DOOR_OPEN, v, 4, 0, 0); else N(N_SET_VEHICLE_DOOR_SHUT, v, 4, 0); g_ms.hoodOpen = hood; }
}
static int MsFocusOf(int t) {
    switch (t) {
    case 1: case 6: case 7: case 22: return t == 7 ? MF_ENGINE : MF_FRONT;
    case 2: case 4: case 0: case 25: case 26: case 37: return MF_REAR;
    case 3: case 8: case 9: case 15: case 23: case 24: case 42: return MF_SIDE;
    case 10: case 43: case 48: return MF_TOP;
    case 11: case 18: case 39: case 40: case 41: return MF_ENGINE;
    case 27: case 28: case 29: case 30: case 31: case 32: case 33: case 34: case 35: case 36: return MF_INSIDE;
    default: return MF_OVERVIEW;
    }
}
static void MsCameraTick() {
    int v = g_ms.veh;
    if (!Exists(v) || !g_ms.cam) return;
    float ox = 0.f, oy = 0.f, oz = 0.f, ax = 0.f, ay = 0.f, az = 0.3f;
    switch (g_ms.focus) {
    case MF_FRONT: ox = 1.2f; oy = 5.0f; oz = 1.0f; ay = 1.2f; break;
    case MF_REAR: ox = -1.2f; oy = -5.5f; oz = 1.2f; ay = -1.4f; break;
    case MF_SIDE: ox = 4.2f; oy = 0.6f; oz = 0.5f; ay = 0.3f; az = 0.1f; break;
    case MF_TOP: ox = 3.5f; oy = 3.5f; oz = 3.2f; break;
    case MF_ENGINE: ox = 0.6f; oy = 4.2f; oz = 2.6f; ay = 1.4f; az = 0.6f; break;
    case MF_INSIDE: ox = 0.9f; oy = -0.4f; oz = 1.0f; ax = -0.4f; ay = 0.6f; az = 0.5f; break;
    default: {   // a slow turn around the car
        g_ms.camAngle += 0.12f;
        float r = g_ms.camAngle * 3.14159265f / 180.f;
        ox = std::sin(r) * 5.0f; oy = std::cos(r) * 5.0f; oz = 1.5f;
    }
    }
    // inside the garage: a camera beyond its walls or roof sees the shell, not the interior (MLOs render through
    // portals), so a ray from the car to the wanted spot stops the camera just before the first wall it meets
    Vec3 want = N<Vec3>(N_GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS, v, ox, oy, oz);
    Vec3 from = N<Vec3>(N_GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS, v, 0.f, 0.f, 0.9f);
    int ray = N<int>(N_START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE, from.x, from.y, from.z, want.x, want.y, want.z, 1 | 16, v, 7);
    int hit = 0; Vec3 end{}, normal{}; int ent = 0;
    if (ray && N<int>(N_GET_SHAPE_TEST_RESULT, ray, &hit, &end, &normal, &ent) == 2 && hit) {
        float dx = want.x - from.x, dy = want.y - from.y, dz = want.z - from.z, len = std::sqrt(dx * dx + dy * dy + dz * dz);
        float back = std::min(0.4f, len);
        if (len > 0.01f) want = V3(end.x - dx / len * back, end.y - dy / len * back, end.z - dz / len * back);
    }
    Vec3 at = N<Vec3>(N_GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS, v, ax, ay, az);
    float k = g_ms.camInit ? 0.08f : 1.f;
    g_ms.camInit = true;
    g_ms.camPos = V3(g_ms.camPos.x + (want.x - g_ms.camPos.x) * k, g_ms.camPos.y + (want.y - g_ms.camPos.y) * k, g_ms.camPos.z + (want.z - g_ms.camPos.z) * k);
    g_ms.camAt = V3(g_ms.camAt.x + (at.x - g_ms.camAt.x) * k, g_ms.camAt.y + (at.y - g_ms.camAt.y) * k, g_ms.camAt.z + (at.z - g_ms.camAt.z) * k);
    N(N_SET_CAM_COORD, g_ms.cam, g_ms.camPos.x, g_ms.camPos.y, g_ms.camPos.z);
    N(N_POINT_CAM_AT_COORD, g_ms.cam, g_ms.camAt.x, g_ms.camAt.y, g_ms.camAt.z);
}

// ---- doors
static void MsDoors(const MsShop &s, bool open) {
    for (int h : {s.door1, s.door2}) {
        if (!h || !NB(N<int>(N_IS_DOOR_REGISTERED_WITH_SYSTEM, h))) continue;
        N(N_DOOR_SYSTEM_SET_DOOR_STATE, h, open ? 0 : 1, 0, 1);
        N(N_DOOR_SYSTEM_SET_OPEN_RATIO, h, open ? 1.0f : 0.0f, 0, 1);
    }
}

// ---- in and out
static bool MsBack(const Frame &f);
static void MsEnter(int shop) {
    int v = MyVehicle();
    if (!Exists(v) || g_mActive) return;
    const MsShop &s = MS_SHOPS[shop];
    g_ms = MsState();
    g_ms.shop = shop; g_ms.veh = v;
    g_ms.idx = v == g_personal ? g_personalIdx : -1;
    uint32_t model = (uint32_t)N<int>(N_GET_ENTITY_MODEL, v);
    auto it = g_catByHash.find(model);
    if (it != g_catByHash.end() && g_cat[it->second].price > 0) g_ms.value = g_cat[it->second].price;
    N(N_DO_SCREEN_FADE_OUT, 500); Wait(550);
    MsDoors(s, true);
    if (s.kind == 2) if (const InteriorDef *d = FindInterior("arenaws")) {   // the workshop is an interior far from its door
        g_ms.interior = LoadInterior(*d);
        N(N_REQUEST_COLLISION_AT_COORD, s.px, s.py, s.pz);
        for (int i = 0; i < 40 && g_ms.interior && !NB(N<int>(N_IS_INTERIOR_READY, g_ms.interior)); ++i) Wait(50);
    }
    N(N_SET_ENTITY_COORDS, v, s.px, s.py, s.pz + 0.5f, 0, 0, 0, 0);
    if (s.kind == 2) for (int i = 0; i < 30 && !NB(N<int>(N_HAS_COLLISION_LOADED_AROUND_ENTITY, v)); ++i) Wait(50);
    N(N_SET_ENTITY_HEADING, v, s.ph);
    N<int>(N_SET_VEHICLE_ON_GROUND_PROPERLY, v, 5.0f);
    N(N_SET_VEHICLE_FORWARD_SPEED, v, 0.f);
    N(N_FREEZE_ENTITY_POSITION, v, 1);
    N(N_SET_VEHICLE_ENGINE_ON, v, 0, 1, 1);
    N(N_SET_VEHICLE_MOD_KIT, v, 0);
    g_ms.owned = CaptureVehicle(v);
    g_ms.cam = N<int>(N_CREATE_CAM_WITH_PARAMS, "DEFAULT_SCRIPTED_CAMERA", s.px, s.py, s.pz + 2.f, 0.f, 0.f, 0.f, 50.f, 0, 2);
    N(N_SET_CAM_ACTIVE, g_ms.cam, 1);
    N(N_RENDER_SCRIPT_CAMS, 1, 0, 0, 1, 0, 0);
    MsFocus(MF_OVERVIEW); MsCameraTick();
    g_menuBackHook = MsBack;
    g_menuBanner = s.kind == 2 ? "ShopUI_Title_ArenaWar" : s.kind == 1 ? "ShopUI_Title_Supermod" : "ShopUI_Title_Carmod";
    N(N_REQUEST_STREAMED_TEXTURE_DICT, g_menuBanner, 0);
    g_menuOpen = true; g_stack.clear(); Open((MenuId)MS_ROOT);
    N(N_DO_SCREEN_FADE_IN, 500);
    Log("modshop: %s, vehicle %d (garage %d, value %d)", s.name, v, g_ms.idx, g_ms.value);
}
static void MsExit() {
    const MsShop &s = MS_SHOPS[g_ms.shop];
    int v = g_ms.veh;
    CloseMenu();
    g_menuBanner = nullptr;
    N(N_DO_SCREEN_FADE_OUT, 500); Wait(550);
    if (Exists(v)) {
        MsRevert();
        if (g_ms.hoodOpen) N(N_SET_VEHICLE_DOOR_SHUT, v, 4, 1);
        // facing away from the entry area, and a little further out than Online's cutscene end (clear of the area)
        float cx = (s.ax + s.bx) * 0.5f, cy = (s.ay + s.by) * 0.5f;
        float dx = s.ex - cx, dy = s.ey - cy, len = std::max(0.1f, std::sqrt(dx * dx + dy * dy));
        float h = std::atan2(-dx, dy) * 180.f / 3.14159265f;
        float ox = s.ex + dx / len * 4.f, oy = s.ey + dy / len * 4.f;
        N(N_FREEZE_ENTITY_POSITION, v, 0);
        N(N_SET_ENTITY_COORDS, v, ox, oy, s.ez + 0.5f, 0, 0, 0, 0);
        N(N_SET_ENTITY_HEADING, v, h);
        N<int>(N_SET_VEHICLE_ON_GROUND_PROPERLY, v, 5.0f);
        N(N_SET_VEHICLE_ENGINE_ON, v, 1, 1, 0);
        N(N_SET_VEHICLE_FORWARD_SPEED, v, 4.f);
    }
    if (g_ms.cam) { N(N_RENDER_SCRIPT_CAMS, 0, 0, 0, 1, 0, 0); N(N_DESTROY_CAM, g_ms.cam, 0); }
    N(N_SET_GAMEPLAY_CAM_RELATIVE_HEADING, 0.f);
    Wait(300);
    N(N_DO_SCREEN_FADE_IN, 500);
    if (MsPersonal()) Notify("Your ~b~personal vehicle~s~ is saved with its modifications.");
    int shop = g_ms.shop;
    g_ms = MsState();
    g_ms.leftAt = Now(); g_ms.lastShop = shop;
}
static bool MsBack(const Frame &f) {   // g_menuBackHook: put the previews back; leaving the root leaves the shop
    if (!MsActive() || f.id < MS_ROOT || f.id > MS_LIVERY) return false;
    MsRevert();
    if (f.id == MS_ROOT) { MsExit(); return true; }
    MsFocus(MF_OVERVIEW);
    return false;
}

// ---- Benny's conversions (Benny's Original Motor Works, approximate Online prices)
struct BennyUp { const char *from, *to; int price; };
static const BennyUp BENNY_UPS[] = {
    {"banshee", "banshee2", 565000}, {"sultan", "sultanrs", 795000}, {"specter", "specter2", 252000}, {"elegy2", "elegy", 904000},
    {"faction", "faction2", 335000}, {"faction2", "faction3", 695000}, {"chino", "chino2", 180000}, {"moonbeam", "moonbeam2", 370000},
    {"primo", "primo2", 400000}, {"buccaneer", "buccaneer2", 390000}, {"voodoo2", "voodoo", 420000}, {"sabregt", "sabregt2", 490000},
    {"slamvan", "slamvan3", 394000}, {"tornado", "tornado5", 375000}, {"virgo3", "virgo2", 240000}, {"minivan", "minivan2", 300000},
    {"glendale", "glendale2", 290000}, {"manana", "manana2", 305000}, {"peyote", "peyote3", 285000}, {"yosemite", "yosemite3", 260000},
};
static void MsConvert(const BennyUp &u) {
    if (!MsPersonal()) { Notify("~r~Benny only upgrades your personal vehicles."); return; }
    PayWhat w("Benny's upgrade");
    if (!Pay(u.price)) return;
    const MsShop &s = MS_SHOPS[g_ms.shop];
    int old = g_ms.veh;
    uint32_t m = Joaat(u.to);
    if (!LoadModel(m)) return;
    N(N_DO_SCREEN_FADE_OUT, 400); Wait(450);
    int a = 0, b = 0; N(N_GET_VEHICLE_COLOURS, old, &a, &b);
    N(N_SET_ENTITY_COORDS, old, s.px, s.py, s.pz - 30.f, 0, 0, 0, 0);
    int v = N<int>(N_CREATE_VEHICLE, m, s.px, s.py, s.pz + 0.5f, s.ph, 0, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
    N(N_SET_ENTITY_AS_MISSION_ENTITY, v, 1, 1);
    N(N_SET_VEHICLE_COLOURS, v, a, b);
    N<int>(N_SET_VEHICLE_ON_GROUND_PROPERLY, v, 5.0f);
    N(N_FREEZE_ENTITY_POSITION, v, 1);
    N(N_SET_PED_INTO_VEHICLE, Me(), v, -1);
    N(N_SET_VEHICLE_MOD_KIT, v, 0);
    DeleteEntity(old);
    g_ms.veh = v;
    g_garage[g_ms.idx].model = u.to;
    MakePersonal(v, g_ms.idx);
    g_ms.owned = CaptureVehicle(v);
    g_garage[g_ms.idx].data = g_ms.owned;
    auto it = g_catByHash.find(m);
    if (it != g_catByHash.end() && g_cat[it->second].price > 0) g_ms.value = g_cat[it->second].price;
    Save();
    g_stack.clear(); Open((MenuId)MS_ROOT);
    N(N_DO_SCREEN_FADE_IN, 500);
    Sound("PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET");
    Shard("~g~UPGRADE COMPLETE", std::string("Benny's Original Motor Works: ") + u.to);
}

// ---- menus
static const char *PAINT_GROUPS[] = {"Classic", "Metallic", "Pearlescent", "Matte", "Metals", "Chrome"};
struct MsColour { const char *name; int c; };
static const MsColour CLASSIC_COLS[] = {
    {"Black", 0}, {"Carbon Black", 147}, {"Graphite", 1}, {"Anthracite Black", 11}, {"Black Steel", 2}, {"Dark Steel", 3}, {"Silver", 4},
    {"Bluish Silver", 5}, {"Rolled Steel", 6}, {"Shadow Silver", 7}, {"Stone Silver", 8}, {"Midnight Silver", 9}, {"Cast Iron Silver", 10},
    {"Red", 27}, {"Torino Red", 28}, {"Formula Red", 29}, {"Lava Red", 150}, {"Blaze Red", 30}, {"Grace Red", 31}, {"Garnet Red", 32},
    {"Sunset Red", 33}, {"Cabernet Red", 34}, {"Wine Red", 143}, {"Candy Red", 35}, {"Hot Pink", 135}, {"Pfister Pink", 137}, {"Salmon Pink", 136},
    {"Sunrise Orange", 36}, {"Orange", 38}, {"Bright Orange", 138}, {"Gold", 37}, {"Bronze", 90}, {"Yellow", 88}, {"Race Yellow", 89},
    {"Dew Yellow", 91}, {"Dark Green", 49}, {"Racing Green", 50}, {"Sea Green", 51}, {"Olive Green", 52}, {"Bright Green", 53},
    {"Gasoline Green", 54}, {"Lime Green", 92}, {"Midnight Blue", 141}, {"Galaxy Blue", 61}, {"Dark Blue", 62}, {"Saxon Blue", 63},
    {"Blue", 64}, {"Mariner Blue", 65}, {"Harbor Blue", 66}, {"Diamond Blue", 67}, {"Surf Blue", 68}, {"Nautical Blue", 69},
    {"Racing Blue", 73}, {"Ultra Blue", 70}, {"Light Blue", 74}, {"Chocolate Brown", 96}, {"Bison Brown", 101}, {"Creek Brown", 95},
    {"Feltzer Brown", 94}, {"Maple Brown", 97}, {"Beechwood Brown", 103}, {"Sienna Brown", 104}, {"Saddle Brown", 98}, {"Moss Brown", 100},
    {"Woodbeech Brown", 102}, {"Straw Brown", 99}, {"Sandy Brown", 105}, {"Bleached Brown", 106}, {"Schafter Purple", 71},
    {"Spinnaker Purple", 72}, {"Midnight Purple", 142}, {"Bright Purple", 145}, {"Cream", 107}, {"Ice White", 111}, {"Frost White", 112}};
static const MsColour MATTE_COLS[] = {
    {"Black", 12}, {"Gray", 13}, {"Light Gray", 14}, {"Ice White", 131}, {"Blue", 83}, {"Dark Blue", 82}, {"Midnight Blue", 84},
    {"Midnight Purple", 149}, {"Schafter Purple", 148}, {"Red", 39}, {"Dark Red", 40}, {"Orange", 41}, {"Yellow", 42}, {"Lime Green", 55},
    {"Green", 128}, {"Forest Green", 151}, {"Foliage Green", 155}, {"Olive Drab", 152}, {"Dark Earth", 153}, {"Desert Tan", 154}};
static const MsColour METAL_COLS[] = {{"Brushed Steel", 117}, {"Brushed Black Steel", 118}, {"Brushed Aluminum", 119}, {"Pure Gold", 158}, {"Brushed Gold", 159}};
static const char *WHEEL_TYPES[] = {"Sport", "Muscle", "Lowrider", "SUV", "Offroad", "Tuner", "Bike Wheels", "High End", "Benny's Original",
                                    "Benny's Bespoke", "Open Wheel", "Street", "Track"};
static const char *XENON_COLS[] = {"White", "Blue", "Electric Blue", "Mint Green", "Lime Green", "Yellow", "Golden Shower", "Orange", "Red",
                                   "Pony Pink", "Hot Pink", "Purple", "Blacklight"};
static const int NEON_RGB[][3] = {{222, 222, 255}, {2, 21, 255}, {3, 83, 255}, {0, 255, 140}, {94, 255, 1}, {255, 255, 0}, {255, 150, 0},
                                  {255, 62, 0}, {255, 1, 1}, {255, 50, 100}, {255, 5, 190}, {35, 1, 255}, {15, 3, 255}};
static const struct { const char *name; int r, g, b; } SMOKE_COLS[] = {
    {"White", 254, 254, 254}, {"Black", 1, 1, 1}, {"Blue", 0, 150, 255}, {"Yellow", 255, 255, 50}, {"Purple", 153, 10, 153},
    {"Orange", 255, 153, 51}, {"Green", 10, 255, 10}, {"Red", 255, 10, 10}, {"Pink", 255, 102, 178}, {"Brown", 180, 130, 97}, {"Patriot", 0, 0, 0}};

static std::string MsModName(int v, int t, int i) {
    if (i < 0) return "Stock";
    static const char *ENG[] = {"EMS Upgrade, Level 1", "EMS Upgrade, Level 2", "EMS Upgrade, Level 3", "EMS Upgrade, Level 4"};
    static const char *BRK[] = {"Street Brakes", "Sport Brakes", "Race Brakes", "Super Brakes"};
    static const char *TRN[] = {"Street Transmission", "Sports Transmission", "Race Transmission", "Super Transmission"};
    static const char *SUS[] = {"Lowered Suspension", "Street Suspension", "Sport Suspension", "Competition Suspension", "Race Suspension"};
    static const char *ARM[] = {"Armor Upgrade 20%", "Armor Upgrade 40%", "Armor Upgrade 60%", "Armor Upgrade 80%", "Armor Upgrade 100%"};
    if (t == 11 && i < 4) return ENG[i];
    if (t == 12 && i < 4) return BRK[i];
    if (t == 13 && i < 4) return TRN[i];
    if (t == 15 && i < 5) return SUS[i];
    if (t == 16 && i < 5) return ARM[i];
    const char *l = N<const char *>(N_GET_MOD_TEXT_LABEL, v, t, i);
    std::string nm = l && *l ? Label(l) : "";
    if (nm.empty() || nm == "NULL") nm = std::string(MOD_SLOT_NAMES[t][0] ? MOD_SLOT_NAMES[t] : "Option") + " " + std::to_string(i + 1);
    return nm;
}
static std::string MsSlotName(int v, int t) {
    const char *slot = N<const char *>(N_GET_MOD_SLOT_NAME, v, t);
    if (slot && *slot) { std::string l = Label(slot); if (!l.empty() && l != "NULL") return l; }
    return MOD_SLOT_NAMES[t];
}
static bool MsHas(int v, int t) { return N<int>(N_GET_NUM_VEHICLE_MODS, v, t) > 0; }
static void MsCategory(std::vector<Item> &items, int v, int t, const std::string &name = "") {
    if (!MsHas(v, t)) return;
    std::string nm = name.empty() ? MsSlotName(v, t) : name;
    int f = MsFocusOf(t);
    items.push_back({nm, ">", "", [t] { Open((MenuId)MS_MOD, t); }, [f] { MsFocus(f); }});
}
static bool ModShopMenu(const Frame &f, std::string &title, std::string &sub, std::vector<Item> &items) {
    int id = (int)f.id;
    if (id < MS_ROOT || id > MS_LIVERY) return false;
    int v = g_ms.veh;
    if (!MsActive() || !Exists(v)) { CloseMenu(); return true; }
    const MsShop &s = MS_SHOPS[g_ms.shop];
    title = s.kind == 2 ? "Arena Workshop" : s.kind == 1 ? "Benny's" : "Los Santos Customs";
    N(N_SET_VEHICLE_MOD_KIT, v, 0);
    int cls = N<int>(N_GET_VEHICLE_CLASS, v);
    switch (id) {
    case MS_ROOT: {
        sub = "CATEGORIES";
        if (!MsPersonal()) {
            int price = std::max(1000, MsRound(g_ms.value * 0.05f));
            items.push_back({"Loss/Theft Prevention", Money(price), "Tracker and insurance: this vehicle becomes one of your personal vehicles and its modifications are saved.",
                             [price] {
                                 std::string where = FreeGarage();
                                 if (where == "!") { Notify("~r~Your garages are full."); return; }
                                 PayWhat w("vehicle insurance");
                                 if (!Pay(price)) return;
                                 GVeh g; g.prop = where; g.model = ModelNameOf(g_ms.veh); g.plate = PlateOf(g_ms.veh); g.data = CaptureVehicle(g_ms.veh);
                                 g_garage.push_back(g);
                                 StorePersonal(true);   // the old personal vehicle goes back to its garage, saved
                                 g_ms.idx = (int)g_garage.size() - 1;
                                 MakePersonal(g_ms.veh, g_ms.idx);
                                 Save();
                                 Sound("PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET");
                                 Notify("Tracker and insurance fitted: it is your ~b~personal vehicle~s~ now (" + GarageLabel(where) + ").");
                             }, [] { MsFocus(MF_OVERVIEW); }});
        }
        int rp = RepairPrice(v);
        if (rp > 0)
            items.push_back({"Repair", Money(rp), "Fix the body, the engine and the windows.", [rp] {
                PayWhat w("vehicle repair");
                if (!Pay(rp)) return;
                N(N_SET_VEHICLE_FIXED, g_ms.veh); N(N_SET_VEHICLE_DEFORMATION_FIXED, g_ms.veh); N(N_SET_VEHICLE_DIRT_LEVEL, g_ms.veh, 0.f);
                Sound("PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET");
            }, [] { MsFocus(MF_OVERVIEW); }});
        if (s.kind == 1) {
            std::string mdl = ModelNameOf(v);
            for (const BennyUp &u : BENNY_UPS)
                if (mdl == u.from) { items.push_back({"Upgrades", ">", "Benny's Original Motor Works conversions.", [] { Open((MenuId)MS_BENNYUP); }, [] { MsFocus(MF_OVERVIEW); }}); break; }
            if (MsHas(v, 27) || MsHas(v, 29) || MsHas(v, 32) || MsHas(v, 33))
                items.push_back({"Interior", ">", "", [] { Open((MenuId)MS_INTERIOR); }, [] { MsFocus(MF_INSIDE); }});
            if (MsHas(v, 39) || MsHas(v, 40) || MsHas(v, 41))
                items.push_back({"Engine Bay", ">", "", [] { Open((MenuId)MS_ENGBAY); }, [] { MsFocus(MF_ENGINE); }});
        }
        MsCategory(items, v, 16, "Armor");
        MsCategory(items, v, 12, "Brakes");
        if (MsHas(v, 1) || MsHas(v, 2)) items.push_back({"Bumpers", ">", "", [] { Open((MenuId)MS_PAIR, 0); }, [] { MsFocus(MF_FRONT); }});
        MsCategory(items, v, 5);
        MsCategory(items, v, 11, "Engine");
        MsCategory(items, v, 4, "Exhaust");
        if (MsHas(v, 8) || MsHas(v, 9)) items.push_back({"Fenders", ">", "", [] { Open((MenuId)MS_PAIR, 1); }, [] { MsFocus(MF_SIDE); }});
        MsCategory(items, v, 6, "Grille");
        MsCategory(items, v, 7, "Hood");
        MsCategory(items, v, 14, "Horns");
        items.push_back({"Lights", ">", "", [] { Open((MenuId)MS_LIGHTS); }, [] { MsFocus(MF_FRONT); }});
        if (MsHas(v, 48)) MsCategory(items, v, 48, "Livery");
        else if (N<int>(N_GET_VEHICLE_LIVERY_COUNT, v) > 1) items.push_back({"Livery", ">", "", [] { Open((MenuId)MS_LIVERY); }, [] { MsFocus(MF_TOP); }});
        items.push_back({"Plates", ">", "", [] { Open((MenuId)MS_PLATES); }, [] { MsFocus(MF_REAR); }});
        items.push_back({"Respray", ">", "", [] { Open((MenuId)MS_RESPRAY); }, [] { MsFocus(MF_TOP); }});
        MsCategory(items, v, 10, "Roof");
        MsCategory(items, v, 3, "Skirts");
        MsCategory(items, v, 0, "Spoiler");
        MsCategory(items, v, 15, "Suspension");
        MsCategory(items, v, 13, "Transmission");
        if (cls != 8 && cls != 13) {
            bool on = NB(N<int>(N_IS_TOGGLE_MOD_ON, v, 18));
            int price = MsRound(15000.f * MsFactor());
            items.push_back(MsItem("Turbo Tuning", price, on, [] { N(N_TOGGLE_VEHICLE_MOD, g_ms.veh, 18, 1); }, "vehicle modification", "Improved acceleration."));
            items.back().hover = [] { MsRevert(); MsFocus(MF_ENGINE); };
        }
        items.push_back({"Wheels", ">", "", [] { Open((MenuId)MS_WHEELS); }, [] { MsFocus(MF_SIDE); }});
        items.push_back({"Windows", ">", "", [] { Open((MenuId)MS_WINDOWS); }, [] { MsFocus(MF_TOP); }});
        if (s.kind == 1) for (int t : {25, 26, 37, 38, 42, 43, 45, 46}) MsCategory(items, v, t);
        if (s.kind == 2) for (int t = 25; t <= 46; ++t) MsCategory(items, v, t);   // arena vehicles put their armour, rams, spikes and weapons here
        items.push_back({"Exit", "", "Drive out of the shop.", [] { MsExit(); }, [] { MsFocus(MF_OVERVIEW); }});
        return true;
    }
    case MS_PAIR: {
        sub = f.param == 0 ? "BUMPERS" : "FENDERS";
        if (f.param == 0) { MsCategory(items, v, 1, "Front Bumper"); MsCategory(items, v, 2, "Rear Bumper"); }
        else { MsCategory(items, v, 8, "Left Fender"); MsCategory(items, v, 9, "Right Fender"); }
        return true;
    }
    case MS_INTERIOR: {
        sub = "INTERIOR";
        for (int t : {27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 44}) MsCategory(items, v, t);
        int a = 0, b = 0; N(N_GET_VEHICLE_EXTRA_COLOUR_5, v, &a); N(N_GET_VEHICLE_EXTRA_COLOUR_6, v, &b);
        items.push_back({"Trim Color", ">", "", [] { Open((MenuId)MS_PAINT, 6 * 10); }, [] { MsFocus(MF_INSIDE); }});
        items.push_back({"Dashboard Color", ">", "", [] { Open((MenuId)MS_PAINT, 7 * 10); }, [] { MsFocus(MF_INSIDE); }});
        return true;
    }
    case MS_ENGBAY: { sub = "ENGINE BAY"; for (int t : {39, 40, 41}) MsCategory(items, v, t); return true; }
    case MS_MOD: {
        int t = f.param;
        sub = MsSlotName(v, t);
        for (auto &c : sub) c = (char)toupper((unsigned char)c);
        int n = N<int>(N_GET_NUM_VEHICLE_MODS, v, t), cur = N<int>(N_GET_VEHICLE_MOD, v, t);
        bool custom = N<int>(N_GET_VEHICLE_MOD_VARIATION, v, 23) != 0;
        MsFocus(MsFocusOf(t));
        // what is owned: from the snapshot, not the previewed car
        int owned = -1;
        for (auto &e : Split(g_ms.owned, ';')) if (e.rfind("m=", 0) == 0) for (auto &kv : Split(e.substr(2), ',')) {
            auto p = Split(kv, ':'); if (p.size() == 2 && std::atoi(p[0].c_str()) == t) owned = std::atoi(p[1].c_str());
        }
        (void)cur;
        for (int i = -1; i < n; ++i) {
            int price = i < 0 ? 0 : MsModPrice(t, i);
            items.push_back(MsItem(MsModName(v, t, i), price, owned == i, [t, i, custom] {
                if (i < 0) N(N_REMOVE_VEHICLE_MOD, g_ms.veh, t); else N(N_SET_VEHICLE_MOD, g_ms.veh, t, i, custom ? 1 : 0);
                if (t == 14) N(N_START_VEHICLE_HORN, g_ms.veh, 1500, Joaat("HELDDOWN"), 0);
            }, "vehicle modification"));
        }
        return true;
    }
    case MS_LIGHTS: {
        sub = "LIGHTS";
        bool xenon = NB(N<int>(N_IS_TOGGLE_MOD_ON, v, 22));
        items.push_back(MsItem("Stock Lights", 0, !xenon, [] { N(N_TOGGLE_VEHICLE_MOD, g_ms.veh, 22, 0); }, "vehicle modification"));
        items.push_back(MsItem("Xenon Lights", MsRound(1000.f * MsFactor()), xenon, [] { N(N_TOGGLE_VEHICLE_MOD, g_ms.veh, 22, 1); N(N_SET_VEHICLE_LIGHTS, g_ms.veh, 2); },
                               "vehicle modification"));
        if (xenon) items.push_back({"Xenon Color", ">", "", [] { Open((MenuId)MS_XENON); }, [] { MsRevert(); }});
        if (cls != 8) items.push_back({"Neon Kits", ">", "Underglow.", [] { Open((MenuId)MS_NEON); }, [] { MsRevert(); }});
        return true;
    }
    case MS_XENON: {
        sub = "XENON COLOR";
        int cur = N<int>(N_GET_VEHICLE_XENON_LIGHT_COLOR_INDEX, v);
        items.push_back(MsItem("Stock", 0, cur < 0 || cur == 255, [] { N(N_SET_VEHICLE_XENON_LIGHT_COLOR_INDEX, g_ms.veh, -1); }, "vehicle modification"));
        for (int i = 0; i < 13; ++i)
            items.push_back(MsItem(XENON_COLS[i], MsRound(1500.f * MsFactor()), cur == i, [i] {
                N(N_SET_VEHICLE_XENON_LIGHT_COLOR_INDEX, g_ms.veh, i); N(N_SET_VEHICLE_LIGHTS, g_ms.veh, 2); }, "vehicle modification"));
        return true;
    }
    case MS_NEON: {
        sub = "NEON KITS";
        static const char *LAYOUT[] = {"None", "Front", "Back", "Sides", "Front and Back", "Front and Sides", "Back and Sides", "Front, Back and Sides"};
        static const int MASK[] = {0, 4, 8, 3, 12, 7, 11, 15};   // bits: 0 left, 1 right, 2 front, 3 back
        int cur = 0; for (int i = 0; i < 4; ++i) if (NB(N<int>(N_GET_VEHICLE_NEON_ENABLED, v, i))) cur |= 1 << i;
        for (int k = 0; k < 8; ++k) {
            int mask = MASK[k], sides = (mask & 1) + ((mask >> 1) & 1) + ((mask >> 2) & 1) + ((mask >> 3) & 1);
            items.push_back(MsItem(LAYOUT[k], MsRound(1000.f * sides * MsFactor()), cur == mask,
                                   [mask] { for (int i = 0; i < 4; ++i) N(N_SET_VEHICLE_NEON_ENABLED, g_ms.veh, i, (mask >> i) & 1); }, "vehicle modification"));
        }
        if (cur) items.push_back({"Neon Color", ">", "", [] { Open((MenuId)MS_NEONCOL); }, [] { MsRevert(); }});
        return true;
    }
    case MS_NEONCOL: {
        sub = "NEON COLOR";
        int r = 0, g = 0, b = 0; N(N_GET_VEHICLE_NEON_COLOUR, v, &r, &g, &b);
        for (int i = 0; i < 13; ++i) {
            int cr = NEON_RGB[i][0], cg = NEON_RGB[i][1], cb = NEON_RGB[i][2];
            items.push_back(MsItem(XENON_COLS[i], MsRound(500.f * MsFactor()), r == cr && g == cg && b == cb,
                                   [cr, cg, cb] { N(N_SET_VEHICLE_NEON_COLOUR, g_ms.veh, cr, cg, cb); }, "vehicle modification"));
        }
        return true;
    }
    case MS_RESPRAY: {
        sub = "RESPRAY";
        items.push_back({"Primary Color", ">", "", [] { Open((MenuId)MS_PAINTGRP, 0); }, [] { MsFocus(MF_TOP); }});
        items.push_back({"Secondary Color", ">", "", [] { Open((MenuId)MS_PAINTGRP, 1); }, [] { MsFocus(MF_TOP); }});
        items.push_back({"Pearlescent", ">", "", [] { Open((MenuId)MS_PAINT, 2 * 10 + 1); }, [] { MsFocus(MF_TOP); }});
        return true;
    }
    case MS_PAINTGRP: {
        int which = f.param;
        sub = which == 0 ? "PRIMARY COLOR" : "SECONDARY COLOR";
        for (int g = 0; g < 6; ++g) {
            if (g == 2) continue;   // pearlescent is its own layer
            items.push_back({PAINT_GROUPS[g], ">", "", [which, g] { Open((MenuId)MS_PAINT, which * 10 + g); }, [] { MsFocus(MF_TOP); }});
        }
        return true;
    }
    case MS_PAINT: {   // param: layer * 10 + group; layers 0 primary, 1 secondary, 2 pearlescent, 3 wheels, 6 trim, 7 dashboard
        int layer = f.param / 10, grp = f.param % 10;
        sub = layer == 2 ? "PEARLESCENT" : layer == 3 ? "WHEEL COLOR" : layer == 6 ? "TRIM COLOR" : layer == 7 ? "DASHBOARD COLOR" : PAINT_GROUPS[grp];
        for (auto &c : sub) c = (char)toupper((unsigned char)c);
        const MsColour *cols = CLASSIC_COLS; int n = (int)(sizeof CLASSIC_COLS / sizeof *CLASSIC_COLS);
        if (layer <= 1 && grp == 3) { cols = MATTE_COLS; n = (int)(sizeof MATTE_COLS / sizeof *MATTE_COLS); }
        if (layer <= 1 && grp == 4) { cols = METAL_COLS; n = (int)(sizeof METAL_COLS / sizeof *METAL_COLS); }
        static const MsColour CHROME[] = {{"Chrome", 120}};
        if (layer <= 1 && grp == 5) { cols = CHROME; n = 1; }
        static const int GRP_PRICE[] = {300, 400, 1500, 2500, 3000, 5000};
        int price = MsRound((layer == 2 ? 1500.f : layer == 3 ? 1000.f : layer >= 6 ? 800.f : (float)GRP_PRICE[grp]) * MsFactor());
        int a = 0, b = 0, pearl = 0, wheel = 0, trim = 0, dash = 0;
        N(N_GET_VEHICLE_COLOURS, v, &a, &b); N(N_GET_VEHICLE_EXTRA_COLOURS, v, &pearl, &wheel);
        N(N_GET_VEHICLE_EXTRA_COLOUR_5, v, &trim); N(N_GET_VEHICLE_EXTRA_COLOUR_6, v, &dash);
        int cur = layer == 0 ? a : layer == 1 ? b : layer == 2 ? pearl : layer == 3 ? wheel : layer == 6 ? trim : dash;
        for (int i = 0; i < n; ++i) {
            int col = cols[i].c;
            items.push_back(MsItem(cols[i].name, price, cur == col, [layer, grp, col] {
                int v2 = g_ms.veh, a2 = 0, b2 = 0, p2 = 0, w2 = 0;
                N(N_GET_VEHICLE_COLOURS, v2, &a2, &b2); N(N_GET_VEHICLE_EXTRA_COLOURS, v2, &p2, &w2);
                static const int PAINT_TYPE[] = {0, 1, 2, 3, 4, 5};   // normal, metallic, pearl, matte, metal, chrome
                if (layer == 0) { N(N_CLEAR_VEHICLE_CUSTOM_PRIMARY_COLOUR, v2); N(N_SET_VEHICLE_MOD_COLOR_1, v2, PAINT_TYPE[grp], col, p2); }
                else if (layer == 1) { N(N_CLEAR_VEHICLE_CUSTOM_SECONDARY_COLOUR, v2); N(N_SET_VEHICLE_MOD_COLOR_2, v2, PAINT_TYPE[grp], col); }
                else if (layer == 2) N(N_SET_VEHICLE_EXTRA_COLOURS, v2, col, w2);
                else if (layer == 3) N(N_SET_VEHICLE_EXTRA_COLOURS, v2, p2, col);
                else if (layer == 6) N(N_SET_VEHICLE_EXTRA_COLOUR_5, v2, col);
                else N(N_SET_VEHICLE_EXTRA_COLOUR_6, v2, col);
            }, "vehicle respray"));
        }
        return true;
    }
    case MS_WHEELS: {
        sub = "WHEELS";
        items.push_back({"Wheel Type", ">", "", [] { Open((MenuId)MS_WTYPE); }, [] { MsFocus(MF_SIDE); }});
        items.push_back({"Wheel Color", ">", "", [] { Open((MenuId)MS_PAINT, 3 * 10); }, [] { MsFocus(MF_SIDE); }});
        items.push_back({"Tires", ">", "Tire design, enhancements and smoke.", [] { Open((MenuId)MS_TYRES); }, [] { MsFocus(MF_SIDE); }});
        return true;
    }
    case MS_WTYPE: {
        sub = "WHEEL TYPE";
        for (int t = 0; t < 13; ++t) {
            if ((cls == 8) != (t == 6)) continue;   // bikes only take bike wheels
            items.push_back({WHEEL_TYPES[t], ">", "", [t] { Open((MenuId)MS_RIMS, t); }, [] { MsRevert(); MsFocus(MF_SIDE); }});
        }
        return true;
    }
    case MS_RIMS: {
        int wt = f.param;
        sub = WHEEL_TYPES[wt];
        for (auto &c : sub) c = (char)toupper((unsigned char)c);
        int ownedType = 0, ownedRim = -1;
        for (auto &e : Split(g_ms.owned, ';')) {
            if (e.rfind("wt=", 0) == 0) ownedType = std::atoi(e.c_str() + 3);
            if (e.rfind("m=", 0) == 0) for (auto &kv : Split(e.substr(2), ',')) { auto p = Split(kv, ':'); if (p.size() == 2 && std::atoi(p[0].c_str()) == 23) ownedRim = std::atoi(p[1].c_str()); }
        }
        int keepType = N<int>(N_GET_VEHICLE_WHEEL_TYPE, v);
        N(N_SET_VEHICLE_WHEEL_TYPE, v, wt);   // the rims of this type (the preview sets it again)
        int n = N<int>(N_GET_NUM_VEHICLE_MODS, v, 23);
        bool custom = N<int>(N_GET_VEHICLE_MOD_VARIATION, v, 23) != 0;
        float mult = wt == 7 || wt == 8 || wt == 9 ? 1.6f : 1.f;
        for (int i = 0; i < n; ++i) {
            std::string nm = MsModName(v, 23, i);
            items.push_back(MsItem(nm, MsRound((600.f + 120.f * i) * mult * MsFactor()), wt == ownedType && i == ownedRim, [wt, i, custom] {
                N(N_SET_VEHICLE_WHEEL_TYPE, g_ms.veh, wt); N(N_SET_VEHICLE_MOD, g_ms.veh, 23, i, custom ? 1 : 0);
                if (N<int>(N_GET_VEHICLE_CLASS, g_ms.veh) == 8) N(N_SET_VEHICLE_MOD, g_ms.veh, 24, i, custom ? 1 : 0);
            }, "vehicle modification"));
        }
        N(N_SET_VEHICLE_WHEEL_TYPE, v, keepType);
        return true;
    }
    case MS_TYRES: {
        sub = "TIRES";
        bool custom = N<int>(N_GET_VEHICLE_MOD_VARIATION, v, 23) != 0;
        int rim = N<int>(N_GET_VEHICLE_MOD, v, 23);
        if (rim >= 0) {
            items.push_back(MsItem("Stock Tires", 0, !custom, [rim] { N(N_SET_VEHICLE_MOD, g_ms.veh, 23, rim, 0); }, "vehicle modification"));
            items.push_back(MsItem("Custom Tires", MsRound(250.f * MsFactor()), custom, [rim] { N(N_SET_VEHICLE_MOD, g_ms.veh, 23, rim, 1); }, "vehicle modification"));
        }
        bool bp = !NB(N<int>(N_GET_VEHICLE_TYRES_CAN_BURST, v));
        items.push_back(MsItem("Bulletproof Tires", 25000, bp, [] { N(N_SET_VEHICLE_TYRES_CAN_BURST, g_ms.veh, 0); }, "vehicle modification", "Tires that never burst."));
        items.push_back({"Tire Smoke", ">", "", [] { Open((MenuId)MS_SMOKE); }, [] { MsRevert(); }});
        return true;
    }
    case MS_SMOKE: {
        sub = "TIRE SMOKE";
        bool on = NB(N<int>(N_IS_TOGGLE_MOD_ON, v, 20));
        int r = 0, g = 0, b = 0; N(N_GET_VEHICLE_TYRE_SMOKE_COLOR, v, &r, &g, &b);
        items.push_back(MsItem("Stock", 0, !on, [] { N(N_TOGGLE_VEHICLE_MOD, g_ms.veh, 20, 0); }, "vehicle modification"));
        for (auto &c : SMOKE_COLS) {
            int cr = c.r, cg = c.g, cb = c.b;
            items.push_back(MsItem(c.name, MsRound(2500.f * MsFactor()), on && r == cr && g == cg && b == cb, [cr, cg, cb] {
                N(N_TOGGLE_VEHICLE_MOD, g_ms.veh, 20, 1); N(N_SET_VEHICLE_TYRE_SMOKE_COLOR, g_ms.veh, cr, cg, cb); }, "vehicle modification"));
        }
        return true;
    }
    case MS_WINDOWS: {
        sub = "WINDOWS";
        static const struct { const char *name; int tint, price; } TINTS[] = {{"None", 0, 0}, {"Light Smoke", 3, 500}, {"Dark Smoke", 2, 1000}, {"Limo", 5, 1500},
                                                                              {"Pure Black", 1, 2000}, {"Green", 6, 1000}};
        int cur = N<int>(N_GET_VEHICLE_WINDOW_TINT, v);
        for (auto &t : TINTS) {
            int tint = t.tint;
            items.push_back(MsItem(t.name, t.price ? MsRound(t.price * MsFactor()) : 0, cur == tint || (tint == 0 && cur < 0),
                                   [tint] { N(N_SET_VEHICLE_WINDOW_TINT, g_ms.veh, tint); }, "vehicle modification"));
        }
        return true;
    }
    case MS_PLATES: {
        sub = "PLATES";
        static const char *PLATES[] = {"Blue on White 1", "Yellow on Black", "Yellow on Blue", "Blue on White 2", "Blue on White 3", "Yankton"};
        int cur = N<int>(N_GET_VEHICLE_NUMBER_PLATE_TEXT_INDEX, v);
        for (int i = 0; i < 6; ++i)
            items.push_back(MsItem(PLATES[i], i == 0 ? 0 : MsRound((200.f + 100.f * i) * MsFactor()), cur == i,
                                   [i] { N(N_SET_VEHICLE_NUMBER_PLATE_TEXT_INDEX, g_ms.veh, i); }, "vehicle modification"));
        return true;
    }
    case MS_LIVERY: {
        sub = "LIVERY";
        int n = N<int>(N_GET_VEHICLE_LIVERY_COUNT, v), cur = N<int>(N_GET_VEHICLE_LIVERY, v);
        for (int i = 0; i < n; ++i)
            items.push_back(MsItem("Livery " + std::to_string(i + 1), MsRound((2500.f + 500.f * i) * MsFactor()), cur == i,
                                   [i] { N(N_SET_VEHICLE_LIVERY, g_ms.veh, i); }, "vehicle modification"));
        return true;
    }
    case MS_BENNYUP: {
        sub = "UPGRADES";
        std::string mdl = ModelNameOf(v);
        for (const BennyUp &u : BENNY_UPS) {
            if (mdl != u.from) continue;
            const BennyUp *up = &u;
            std::string nm = Label(N<const char *>(N_GET_DISPLAY_NAME_FROM_VEHICLE_MODEL, Joaat(u.to)));
            items.push_back({nm.empty() || nm == "NULL" ? u.to : nm, Money(u.price), "Convert it into its Benny's custom version (the mods start over).",
                             [up] { MsConvert(*up); }, [] { MsFocus(MF_OVERVIEW); }});
        }
        return true;
    }
    }
    return false;
}

// ---- each frame: the entry areas, and the shop while you are in it
static void ModShopTick() {
    if (MsActive()) {
        MsCameraTick();
        N(N_DISABLE_CONTROL_ACTION, 0, 75, 1);   // no getting out
        N(N_HIDE_HUD_AND_RADAR_THIS_FRAME);
        if (!g_menuOpen) { Open((MenuId)MS_ROOT); g_menuOpen = true; }   // the menu is the shop: it cannot be closed with the menu key
        if (!Exists(g_ms.veh) || Dead(Me())) { CloseMenu(); g_menuBanner = nullptr; if (g_ms.cam) { N(N_RENDER_SCRIPT_CAMS, 0, 0, 0, 1, 0, 0); N(N_DESTROY_CAM, g_ms.cam, 0); } g_ms = MsState(); }
        return;
    }
    if (!YouActive() || !InAnyVehicle() || g_menuOpen || g_mActive || Now() - g_ms.leftAt < 3000) return;
    int v = MyVehicle(), me = Me();
    if (g_ms.lastShop >= 0) {   // just out of a shop: it opens again only once the car has left its entry area
        const MsShop &l = MS_SHOPS[g_ms.lastShop];
        if (NB(N<int>(N_IS_ENTITY_IN_ANGLED_AREA, v, l.ax, l.ay, l.az, l.bx, l.by, l.bz, l.w, 0, 1, 0))) return;
        g_ms.lastShop = -1;
    }
    if (N<int>(N_GET_PED_IN_VEHICLE_SEAT, v, -1, 0) != me) return;
    int cls = N<int>(N_GET_VEHICLE_CLASS, v);
    if (cls == 14 || cls == 15 || cls == 16 || cls == 21) return;
    for (int i = 0; i < NMS_SHOPS; ++i) {
        const MsShop &s = MS_SHOPS[i];
        if (!NB(N<int>(N_IS_ENTITY_IN_ANGLED_AREA, v, s.ax, s.ay, s.az, s.bx, s.by, s.bz, s.w, 0, 1, 0))) continue;
        if (N<int>(N_GET_PLAYER_WANTED_LEVEL, Player()) > 0) { Help(std::string("Lose the cops before going into ") + (s.kind ? "Benny's." : "Los Santos Customs.")); return; }
        if (cls == 18) { Help("Emergency vehicles cannot be modified here."); return; }
        if (N<float>(N_GET_ENTITY_SPEED, v) > 12.f) return;
        if (s.kind == 2 && !Owns("Arena Workshop: Maze Bank Arena")) { Help("Buy the ~b~Arena Workshop~s~ (Maze Bank Foreclosures) to upgrade arena vehicles here."); return; }
        MsEnter(i);
        return;
    }
}
