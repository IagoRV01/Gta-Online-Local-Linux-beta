// ---------------------------------------------------------------- Traffic variety
// Story mode traffic only uses the vehicles of each area's popgroup, so most of the catalogue (and every Online-only
// car) never drives around. This swaps some ambient vehicles, out of the player's sight, for catalogue models:
// same place, speed and driver, who then keeps wandering. A rotating set of ~12 models stays streamed in.
// Included from GoriOnline.cpp.

static std::vector<std::pair<uint32_t, float>> g_tPool;      // model hash, weight
static std::vector<uint32_t> g_tActive;
static std::set<int> g_tSeen;
static int g_tNextScan = 0, g_tNextRotate = 0, g_tNextClean = 0, g_tSwapped = 0;

static bool TrafficExcluded(const std::string &m) {
    static const char *bad[] = {"arena", "rcbandito", "deathbike", "imperator", "bruiser", "brutus", "zr380", "scarab", "cerberus", "issi4", "issi5", "issi6",
        "dominator4", "dominator5", "dominator6", "impaler2", "impaler3", "impaler4", "monster", "slamvan4", "slamvan5", "slamvan6", "dune2", "dune3", "dune4",
        "dune5", "marshall", "blazer5", "ruiner2", "ruiner3", "boxville5", "technical", "insurgent", "halftrack", "apc", "tampa3", "nightshark", "deluxo",
        "stromberg", "toreador", "vigilante", "scramjet", "oppressor", "voltic2", "kosatka", "minitank", "khanjali", "rhino", "phantom2", "trophytruck",
        "veto", "zhaba", "tug", "shotaro", "lectro", "caddy", "kart", "rrocket", "pbus2", "speedo4", "mule4", "pounder2", "terbyte", "limo2", "stretch",
        "journey2", "patriot3", "jb7002", "brickade2", "freecrawler", "outlaw", "vagrant", "squaddie", "winky", "manchez3", "verus", "polgauntlet"};
    for (const char *b : bad) if (m.find(b) != std::string::npos) return true;
    return false;
}
static void TrafficInit() {
    // weights per vehicle class: everyday cars are common, supercars rare
    static const float W[13] = {3.f, 4.f, 4.f, 2.f, 2.5f, 1.f, 2.f, 0.8f, 1.2f, 1.5f, 0.f, 0.f, 2.f};
    for (const VehRow &r : VEH_ROWS) {
        if (r.cls > 12 || W[r.cls] <= 0.f || r.weapon || TrafficExcluded(r.model)) continue;
        uint32_t h = Joaat(r.model);
        if (!NB(N<int>(N_IS_MODEL_IN_CDIMAGE, h)) || !NB(N<int>(N_IS_MODEL_A_VEHICLE, h))) continue;
        g_tPool.push_back({h, W[r.cls]});
    }
    Log("traffic: %d models can join the traffic", (int)g_tPool.size());
}
static uint32_t TrafficPick() {
    float tot = 0.f; for (auto &p : g_tPool) tot += p.second;
    float r = Rand(1000000) / 1000000.f * tot;
    for (auto &p : g_tPool) { if (r < p.second) return p.first; r -= p.second; }
    return g_tPool.empty() ? 0 : g_tPool[0].first;
}
static bool IsBike(uint32_t m) { return NB(N<int>(N_IS_THIS_MODEL_A_BIKE, m)); }
static void TrafficRotate() {
    // drop a few streamed models, stream in new ones (8 cars + 2 bikes in the set)
    int cars = 0, bikes = 0;
    for (auto it = g_tActive.begin(); it != g_tActive.end();) {
        if (Rand(3) == 0) { N(N_SET_MODEL_AS_NO_LONGER_NEEDED, *it); it = g_tActive.erase(it); }
        else { (IsBike(*it) ? bikes : cars)++; ++it; }
    }
    for (int tries = 0; tries < 60 && (cars < 8 || bikes < 2); ++tries) {
        uint32_t m = TrafficPick();
        if (!m || std::find(g_tActive.begin(), g_tActive.end(), m) != g_tActive.end()) continue;
        bool bike = IsBike(m);
        if ((bike && bikes >= 2) || (!bike && cars >= 8)) continue;
        N(N_REQUEST_MODEL, m);
        g_tActive.push_back(m);
        (bike ? bikes : cars)++;
    }
}
static uint32_t TrafficLoaded(bool bike) {
    std::vector<uint32_t> ok;
    for (uint32_t m : g_tActive) if (IsBike(m) == bike && NB(N<int>(N_HAS_MODEL_LOADED, m))) ok.push_back(m);
    return ok.empty() ? 0 : ok[Rand((int)ok.size())];
}
static void RandomLook(int v) {
    if (Rand(4) != 0) return;      // a quarter of them come modded, like players' cars in Online
    N(N_SET_VEHICLE_MOD_KIT, v, 0);
    for (int t : {0, 1, 2, 3, 4, 6, 7, 10, 23}) { int n = N<int>(N_GET_NUM_VEHICLE_MODS, v, t); if (n > 0) N(N_SET_VEHICLE_MOD, v, t, Rand(n), 0); }
    if (Rand(2)) N(N_SET_VEHICLE_WINDOW_TINT, v, 1 + Rand(3));
    if (Rand(5) == 0) N(N_SET_VEHICLE_CUSTOM_PRIMARY_COLOUR, v, Rand(256), Rand(256), Rand(256));
}
static bool TrafficSwap(int v, int driver, bool bike) {
    uint32_t m = TrafficLoaded(bike);
    if (!m) return false;
    Vec3 p = Pos(v);
    float h = N<float>(N_GET_ENTITY_HEADING, v), spd = N<float>(N_GET_ENTITY_SPEED, v);
    int nv = N<int>(N_CREATE_VEHICLE, m, p.x, p.y, p.z + 6.f, h, 0, 0, 0);
    if (!nv) return false;
    int pass[3] = {0, 0, 0};
    for (int s = 0; s < 3; ++s) { int q = N<int>(N_GET_PED_IN_VEHICLE_SEAT, v, s, 0); if (q && !NB(N<int>(N_IS_PED_A_PLAYER, q))) pass[s] = q; }
    if (driver) N(N_SET_PED_INTO_VEHICLE, driver, nv, -1);
    int seats = N<int>(N_GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS, nv);
    for (int s = 0; s < 3; ++s) if (pass[s]) { if (s < seats) N(N_SET_PED_INTO_VEHICLE, pass[s], nv, s); else { int q = pass[s]; DeleteEntity(q); } }
    int old = v;
    N(N_SET_ENTITY_AS_MISSION_ENTITY, old, 1, 1);
    N(N_DELETE_VEHICLE, &old);
    N(N_SET_ENTITY_COORDS_NO_OFFSET, nv, p.x, p.y, p.z, 0, 0, 0);
    N(N_SET_ENTITY_HEADING, nv, h);
    N<int>(N_SET_VEHICLE_ON_GROUND_PROPERLY, nv, 5.0f);
    RandomLook(nv);
    if (driver) {
        N(N_SET_VEHICLE_ENGINE_ON, nv, 1, 1, 0);
        N(N_SET_VEHICLE_FORWARD_SPEED, nv, spd);
        N(N_TASK_VEHICLE_DRIVE_WANDER, driver, nv, std::max(spd, 14.f), 786603);
    }
    g_tSeen.insert(nv);
    N(N_SET_VEHICLE_AS_NO_LONGER_NEEDED, &nv);
    g_tSwapped++;
    return true;
}
static void TrafficTick() {
    int now = Now();
    if (g_tPool.empty()) return;
    if (now > g_tNextRotate) { g_tNextRotate = now + 75000; TrafficRotate(); }
    if (now > g_tNextClean) {
        g_tNextClean = now + 20000;
        for (auto it = g_tSeen.begin(); it != g_tSeen.end();) { if (!Exists(*it)) it = g_tSeen.erase(it); else ++it; }
    }
    if (now < g_tNextScan || g_mActive) return;
    g_tNextScan = now + 350;
    int me = Me(); Vec3 mp = Pos(me);
    if (mp.z < -30.f) return;                                  // interiors under the map
    static int vehs[256];
    int n = shv_worldGetAllVehicles ? shv_worldGetAllVehicles(vehs, 256) : 0;
    int mine = N<int>(N_GET_VEHICLE_PED_IS_IN, me, 1);
    for (int k = 0, start = n ? Rand(n) : 0; k < n; ++k) {
        int v = vehs[(start + k) % n];
        if (g_tSeen.count(v) || v == mine || v == g_personal || !Exists(v)) continue;
        float d = Dist(Pos(v), mp);
        if (d < 55.f || d > 230.f) continue;
        if (NB(N<int>(N_IS_ENTITY_ON_SCREEN, v)) || NB(N<int>(N_IS_ENTITY_A_MISSION_ENTITY, v))) continue;
        g_tSeen.insert(v);
        int cls = N<int>(N_GET_VEHICLE_CLASS, v);
        bool bike = cls == 8;
        if (!(cls <= 9 || cls == 12) || N<int>(N_GET_VEHICLE_NUMBER_OF_PASSENGERS, v) > 3) continue;
        int driver = N<int>(N_GET_PED_IN_VEHICLE_SEAT, v, -1, 0);
        if (driver && (NB(N<int>(N_IS_PED_A_PLAYER, driver)) || Dead(driver) || NB(N<int>(N_IS_ENTITY_A_MISSION_ENTITY, driver)))) continue;
        if (!driver && (Rand(100) >= 30 || N<float>(N_GET_ENTITY_SPEED, v) > 0.5f)) continue;   // parked cars: fewer swaps
        if (driver && Rand(100) >= 45) continue;
        if (TrafficSwap(v, driver, bike)) return;              // one swap per scan
    }
}
