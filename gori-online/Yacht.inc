// ---------------------------------------------------------------- Galaxy Super Yacht (Docktease)
// Bought on Docktease's own configurator (YACHT pages 19-28): appinternet func_8838's data
// (warstock_specials.h, feeds "yacht" / "yacht_owned"), the choices back in GameGlobals YACHT (11):
// colour bits 6-11, lighting 12-17, flag 18-23, model 24-25, fittings 26-27.
// In the world it is GTA Online's: the apa_yacht_grpNN_M ymap (hull and interior) at one of the 36
// moorings (yachts.h), the hull tinted with the colour and freemode's own parts on it (func_1825/func_1833):
// the model's decks, lights, rails, radars, jacuzzi and the flag.
// Saved in g_cust: "@Yacht|config" (the website's flag value) and "@Yacht|spot" (YACHT_SPOTS index).
#include "yachts.h"
static const char *YACHT_CFG = "@Yacht|config", *YACHT_SPOT = "@Yacht|spot";
static const char *YACHT_MODELS[3] = {"The Orion", "The Pisces", "The Aquarius"};
struct YachtCfg { int colour, lighting, flag, model, fittings; };
static YachtCfg YachtDecode(int f) { return {(f >> 6) & 63, (f >> 12) & 63, (f >> 18) & 63, (f >> 24) & 3, (f >> 26) & 3}; }
static bool YachtOwned() { return g_cust.count(YACHT_CFG) > 0; }
static int YachtSpotIdx() { auto it = g_cust.find(YACHT_SPOT); return it == g_cust.end() || it->second < 0 || it->second >= NYACHT_SPOTS ? 0 : it->second; }
static std::string YachtPlace(int spot) {
    const YachtSpot &y = YACHT_SPOTS[spot];
    std::string z = Label(N<const char *>(N_GET_NAME_OF_ZONE, y.x, y.y, y.z));
    return z.empty() ? "Mooring " + std::to_string(y.group) : z;
}

// freemode's part tables (func_1823 decks, func_1824 which exist, func_1829 radars, func_1819 lights, func_1817 rails)
static const char *YachtDeck(int model, int i) {
    static const char *T[3][6] = {{"apa_mp_apa_yacht_option1", "apa_mp_apa_yacht_option1_cola"},
        {"apa_mp_apa_yacht_option2", "apa_mp_apa_yacht_option2_cola", "apa_mp_apa_yacht_option2_colb"},
        {"apa_mp_apa_yacht_option3", "apa_mp_apa_yacht_option3_cola", "apa_mp_apa_yacht_option3_colb", "apa_mp_apa_yacht_option3_colc",
         "apa_mp_apa_yacht_option3_cold", "apa_mp_apa_yacht_option3_cole"}};
    return model >= 0 && model < 3 && i >= 0 && i < 6 ? T[model][i] : nullptr;
}
static const float YACHT_RADAR[3][3][4] = {   // x, y, z offset and heading, per model
    {{0.953125f, -2.17139f, 9.60403f, 90.f}, {1.27979f, -1.99268f, 13.4305f, -180.f}, {5.48218f, -1.98486f, 18.1568f, -90.f}},
    {{-2.24878f, -1.99268f, 17.32f, -90.f}, {1.6189f, -1.99268f, 14.0505f, -180.f}, {7.63501f, -1.99268f, 10.3491f, 90.f}},
    {{10.8328f, -1.99268f, 9.85299f, 90.f}, {-0.226318f, -1.96289f, 12.8964f, 180.f}, {-15.052f, -1.99463f, 9.06741f, 90.f}}};
static std::string YachtLights(int model, int lighting) {
    static const char *L[8] = {"l1a", "l1b", "l1c", "l1d", "l2a", "l2b", "l2c", "l2d"};
    return "apa_mp_apa_y" + std::to_string(model + 1) + "_" + L[lighting & 7];
}
static std::string YachtRails(int model, int fittings) { return "apa_mp_apa_yacht_o" + std::to_string(model + 1) + (fittings ? "_rail_b" : "_rail_a"); }

static int g_yLoaded = -1, g_yHull = 0, g_yBlip = 0, g_yBlipSpot = -1, g_yHullTry = 0;
static std::vector<int> g_yParts;
static int YachtPart(const std::string &model, const Vec3 &p, float heading, int tint) {
    uint32_t m = Joaat(model.c_str());
    if (!LoadModel(m)) { Log("yacht: no model %s", model.c_str()); return 0; }
    int o = N<int>(N_CREATE_OBJECT_NO_OFFSET, m, p.x, p.y, p.z, 0, 0, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
    if (!o) return 0;
    N(N_SET_ENTITY_HEADING, o, heading);
    N(N_FREEZE_ENTITY_POSITION, o, 1);
    N(N_SET_ENTITY_DYNAMIC, o, 0);
    if (tint >= 0) N(N_SET_OBJECT_TINT_INDEX, o, tint);
    g_yParts.push_back(o);
    return o;
}
static void YachtUnload() {
    for (int &o : g_yParts) if (Exists(o)) N(N_DELETE_OBJECT, &o);
    g_yParts.clear();
    if (g_yLoaded >= 0) {
        const YachtSpot &y = YACHT_SPOTS[g_yLoaded];
        N(N_REMOVE_IPL, (std::string(y.ipl) + "_int").c_str());
        N(N_REMOVE_IPL, y.ipl);
    }
    g_yLoaded = -1; g_yHull = 0;
}
static float g_yHeading = 0.f;
static void YachtParts(int spot) {   // freemode's parts on the hull (func_1825 / func_1833)
    for (int &o : g_yParts) if (Exists(o)) N(N_DELETE_OBJECT, &o);
    g_yParts.clear();
    const YachtSpot &y = YACHT_SPOTS[spot];
    YachtCfg c = YachtDecode(g_cust[YACHT_CFG]);
    Vec3 a = V3(y.x, y.y, 0.f);
    float h = 0.f;
    int id = N<int>(N_GET_INTERIOR_AT_COORDS, y.ix, y.iy, y.iz + 1.f);
    if (id) h = N<float>(N_GET_INTERIOR_HEADING, id) * 57.29578f;   // the interior's heading (radians) is the yacht's
    if (g_yHull) { a = Pos(g_yHull); h = N<float>(N_GET_ENTITY_HEADING, g_yHull); N(N_SET_OBJECT_TINT_INDEX, g_yHull, c.colour); }   // the hull's heading wins (the interior's can be 180 off)
    g_yHeading = h;
    for (int i = 0; i < 6; ++i) if (const char *d = YachtDeck(c.model, i)) YachtPart(d, V3(a.x, a.y, 20.f), h, i == 0 ? c.colour : -1);
    auto off = [&](float x, float yy, float z) { return N<Vec3>(N_GET_OFFSET_FROM_COORD_AND_HEADING_IN_WORLD_COORDS, a.x, a.y, a.z, h, x, yy, z); };
    if (c.model > 0) YachtPart("apa_mp_apa_yacht_jacuzzi_ripple1", off(-50.8065f, -1.98013f, 0.136816f), h, c.colour);
    for (int i = 0; i < 3 && c.model < 3; ++i) {
        const float *r = YACHT_RADAR[c.model][i];
        float rh = h + r[3]; while (rh < 0.f) rh += 360.f; while (rh >= 360.f) rh -= 360.f;
        YachtPart("apa_mp_apa_yacht_radar_01a", off(r[0], r[1], r[2]), rh, c.colour);
    }
    YachtPart(YachtLights(c.model, c.lighting), V3(a.x, a.y, 20.005f), h, -1);
    YachtPart(YachtRails(c.model, c.fittings), V3(a.x, a.y, 20.f), h, -1);
    if (c.flag >= 0 && c.flag < NYACHT_FLAGS) {
        int f = YachtPart(YACHT_FLAGS[c.flag], off(-0.14f, -58.1f, -3.38f), h, -1);
        if (f) N(N_SET_ENTITY_ROTATION, f, 49.68f, 0.f, h, 2, 1);
    }
    Log("yacht: %s at %s (%s, model %d colour %d lights %d rails %d flag %d), hull %s heading %.1f", y.ipl, YachtPlace(spot).c_str(), YACHT_MODELS[c.model % 3],
        c.model, c.colour, c.lighting, c.fittings, c.flag, g_yHull ? "found" : "MISSING", h);
}
static void YachtBuild(int spot) {
    YachtUnload();
    const YachtSpot &y = YACHT_SPOTS[spot];
    N(N_REQUEST_IPL, y.ipl);
    N(N_REQUEST_IPL, (std::string(y.ipl) + "_int").c_str());
    g_yLoaded = spot;
    // the hull is an object of the ymap (freemode finds it the same way); it exists once it has streamed in
    for (int i = 0; i < 60 && !g_yHull; ++i) { g_yHull = N<int>(N_GET_CLOSEST_OBJECT_OF_TYPE, y.x, y.y, y.z, 100.f, Joaat("apa_mp_apa_yacht"), 0, 0, 0); if (!g_yHull) Wait(50); }
    YachtParts(spot);
}
static void YachtTravel() {
    const YachtSpot &y = YACHT_SPOTS[YachtSpotIdx()];
    N(N_REQUEST_IPL, y.ipl);
    N(N_REQUEST_IPL, (std::string(y.ipl) + "_int").c_str());
    InteriorDef d{"yacht", y.ix, y.iy, y.iz + 0.2f, 0.f, "", ""};
    TeleportInside(d);
    if (g_yLoaded != YachtSpotIdx()) YachtBuild(YachtSpotIdx());   // here, where the hull streams in
    Notify("Welcome aboard your ~b~Galaxy Super Yacht~s~.");
}
static void YachtMove(int spot) {
    if (!YachtOwned()) return;
    YachtUnload();
    g_cust[YACHT_SPOT] = spot; Save();
    g_yBlipSpot = -1;
    Notify("Your ~b~Super Yacht~s~ is moving to ~y~" + YachtPlace(spot) + "~s~.");
}
static int YachtNearestSpot() {
    Vec3 p = Pos(Me());
    int best = 0; float bd = 1e12f;
    for (int i = 0; i < NYACHT_SPOTS; ++i) { float d = Dist2(p, V3(YACHT_SPOTS[i].x, YACHT_SPOTS[i].y, 0.f)); if (d < bd) { bd = d; best = i; } }
    return best;
}
static void YachtTick() {
    if (!YouActive() || !YachtOwned()) { if (g_yLoaded >= 0) YachtUnload(); RemoveBlip(g_yBlip); g_yBlipSpot = -1; return; }
    int s = YachtSpotIdx();
    const YachtSpot &y = YACHT_SPOTS[s];
    if (g_yBlipSpot != s) { RemoveBlip(g_yBlip); g_yBlip = CoordBlip(V3(y.x, y.y, y.z), 455, 0, g_yachtName.empty() ? "Yacht" : g_yachtName, false); g_yBlipSpot = s; }
    float d = Dist(Pos(Me()), V3(y.x, y.y, y.z));
    if (d < 500.f && g_yLoaded != s) YachtBuild(s);
    if (g_yLoaded == s && !g_yHull && d < 250.f && Now() >= g_yHullTry) {   // the hull only exists once it has streamed in
        g_yHullTry = Now() + 2000;
        g_yHull = N<int>(N_GET_CLOSEST_OBJECT_OF_TYPE, y.x, y.y, y.z, 100.f, Joaat("apa_mp_apa_yacht"), 0, 0, 0);
        if (g_yHull) {
            float hh = N<float>(N_GET_ENTITY_HEADING, g_yHull), dh = std::fabs(hh - g_yHeading);
            if (dh > 1.f && dh < 359.f) YachtParts(s);   // built before the hull streamed in: turn the parts its way
            else N(N_SET_OBJECT_TINT_INDEX, g_yHull, YachtDecode(g_cust[YACHT_CFG]).colour);
            Log("yacht: hull found (heading %.1f, parts at %.1f)", hh, g_yHeading);
        }
    }
    else if (d > 700.f && g_yLoaded >= 0) YachtUnload();
}
static void YachtServiceItems(std::vector<Item> &items) {
    if (!YachtOwned()) return;
    items.push_back({"Yacht: Travel to your Yacht", "", "Go aboard your Galaxy Super Yacht (" + YachtPlace(YachtSpotIdx()) + ").", [] { CloseMenu(); YachtTravel(); }});
    items.push_back({"Yacht: Move your Yacht", YachtPlace(YachtSpotIdx()), "Choose one of the 12 moorings.", [] { Open((MenuId)SM_YMOVE); }});
}
static bool YachtMenu(const Frame &f, std::string &title, std::string &sub, std::vector<Item> &items) {
    if ((int)f.id != SM_YMOVE) return false;
    title = "Move Yacht"; sub = "MOORINGS";
    for (int i = 0; i < NYACHT_SPOTS; ++i) {
        if (YACHT_SPOTS[i].slot != 1) continue;
        items.push_back({YachtPlace(i), YachtSpotIdx() == i ? "Current" : "", "Your yacht moors here.", [i] { CloseMenu(); YachtMove(i); }});
    }
    return true;
}
