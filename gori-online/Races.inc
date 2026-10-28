// ---------------------------------------------------------------- Races (GTA Local Online API v6)
// GTA Online-style races: the tracks are Community Races XML files (Checkpoints, SpawnPoints, AvailableVehicles,
// LapsAvailable, DecorativeProps, Trigger, Name, Description) in GoriOnline\Races next to the .asi.
// Solo, or online as a race job (kind 1): the host picks the race, laps and vehicle, others join from the Jobs
// menu while it is in its lobby or countdown; the server starts everyone at the same time, ranks the racers
// from their checkpoint progress and pays them by position. Payout: 1500 + $100 a checkpoint (max $30,000);
// a solo race pays half of that (at least $1,000) when you finish.
// Transform races: an optional <Transforms><Transform><Checkpoint>i</Checkpoint><VehicleHash>m</VehicleHash></Transform>
// list; passing checkpoint i (0-based) swaps you into vehicle m at the same speed. Those checkpoints are purple.
// Vehicle classes: the host can swap a race's own vehicles for GTA Online's special vehicles or Hotring cars.
// Rockstar Created races: GTA Online's own 254 races, downloaded by the player from Rockstar's cloud and converted by
// tools/rockstar-races/convert.py into GoriOnline\Races\Rockstar (never shipped): they add each checkpoint's size and
// kind (air rings), split checkpoints (two routes, either counts), the stunt pads (boost / slow down), random transforms
// (Known Unknowns) and the race's own lap count, type and default vehicle.
struct RaceProp { Vec3 pos, rot; uint32_t hash = 0; bool dyn = false; int tex = -1, speed = 0; };
struct RaceCpExt { float size = 1.f, spSize = 1.f; bool air = false, split = false, spAir = false; Vec3 sp{}; };
struct RaceDef {
    std::string file, name, desc;
    std::vector<Vec3> cps;
    std::vector<std::pair<Vec3, float>> grid;
    std::vector<std::string> vehicles;
    bool laps = true;
    std::vector<RaceProp> props;
    Vec3 trigger{};
    std::map<int, std::string> transforms;   // checkpoint index -> the vehicle you turn into there ("random": one of tfVehicles)
    std::string type;                         // Rockstar Created: "Stunt Race", "Land Race"...; empty: a community race
    int defLaps = 0;                          // the race's own lap count (Rockstar's)
    std::vector<RaceCpExt> ext;               // per checkpoint (Rockstar's), may be empty
    std::vector<std::string> tfVehicles;
};
struct RaceClass { const char *name, *desc; std::vector<const char *> models; };
static const RaceClass RACE_CLASSES[] = {
    {"Race vehicles", "The vehicles this race was made for.", {}},
    {"Special Vehicles", "GTA Online's special vehicle races: rocket boosts, jumps, wings and water.",
     {"voltic2", "ruiner2", "blazer5", "scramjet", "vigilante", "oppressor", "deluxo", "stromberg", "toreador"}},
    {"Hotring", "Hotring Circuit stock cars.", {"hotring"}},
};
static const int NRACE_CLASSES = (int)(sizeof RACE_CLASSES / sizeof *RACE_CLASSES);
// what the Race Creator offers for a transform checkpoint
static const char *TRANSFORM_VEHICLES[] = {"elegy", "bati", "blazer", "trophytruck", "bmx", "seashark", "dinghy", "buzzard2", "duster", "besra"};
static const int NTRANSFORM_VEHICLES = (int)(sizeof TRANSFORM_VEHICLES / sizeof *TRANSFORM_VEHICLES);
enum { RM_LIST = 210, RM_RACE, RM_LOBBY, RM_RTYPE = 218 };   // RM_RTYPE: one kind of race (param: g_raceTypes index, -1 community)
static std::vector<std::string> g_raceTypes;
enum RacePhase { RP_NONE, RP_LOBBY, RP_COUNTDOWN, RP_RACING, RP_FINISHED, RP_RESULTS };
// Online's race lobby options besides laps and vehicles: time of day, weather, traffic, catch up, slipstream
struct RaceOpts { int tod = 0, weather = 0, traffic = 0; bool catchup = false, slip = true; };
static const char *RACE_TOD[] = {"Current", "Morning", "Noon", "Night"};
static const int RACE_TOD_HOUR[] = {-1, 7, 12, 0};
static const char *RACE_WEATHER[] = {"Current", "Clear", "Rain", "Smog", "Overcast", "Thunder", "Snow"};
static const char *RACE_WEATHER_TYPE[] = {"", "EXTRASUNNY", "RAIN", "SMOG", "OVERCAST", "THUNDER", "XMAS"};
static const char *RACE_TRAFFIC[] = {"Off", "Low", "Medium", "High"};
static const float RACE_TRAFFIC_DENSITY[] = {0.f, 0.3f, 0.7f, 1.0f};
static RaceOpts g_raceNextOpts;   // the lobby's choices for the next race loaded (a member's come with the host's race)
static std::string RaceOptsText(const RaceOpts &o) {
    return std::to_string(o.tod) + "|" + std::to_string(o.weather) + "|" + std::to_string(o.traffic) + "|" + (o.catchup ? "1" : "0") + "|" + (o.slip ? "1" : "0");
}
struct RaceRun {
    RaceDef def;
    RaceOpts opts;
    bool worldHeld = false;
    int lapStart = 0, lapBest = 0, pb = 0;   // this lap's start and the best lap (ms into the race); your record on this race (0 none)   // our time of day / weather is on (the server's world held off)
    int laps = 1, phase = RP_NONE;
    std::string vehicle, startVehicle;   // vehicle: the current one (transforms change it)
    bool online = false, host = false, air = false;
    int startAt = 0, cp = 0, total = 0, finishMs = 0, lastCpAt = 0, resultsUntil = 0, hostEndAt = 0, respawnHold = 0;
    bool voting = false;                                // the next job vote is up (NextJob.inc): it cleans the race up
    int cpHandle2 = 0, blip3 = 0;                       // a split checkpoint's other route
    std::vector<std::pair<int, int>> pads;              // stunt pads: object, strength (+ boost, - slow down)
    std::map<int, int> padAt;                           // object -> when it last fired
    std::vector<int> props;
    int veh = 0, cpHandle = 0, blip = 0, blip2 = 0, countdownMovie = 0, lastCount = -1;
    Vec3 back{}; float backH = 0.f;
    int payout = 0;
};
static RaceRun g_race;
static std::vector<RaceDef> g_raceList;   // headers (no props) of the files in GoriOnline\Races
static int g_raceListAt = -100000, g_raceSelLaps = 3, g_raceSelVeh = 0, g_raceSelClass = 0;

// file names may be UTF-8 ("Stunt - 45°.xml"): Windows paths go through UTF-16
static std::wstring Widen(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
static std::string Narrow(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}
static bool ReadUtf8File(const std::string &path, std::string &out) {
    FILE *f = _wfopen(Widen(path).c_str(), L"rb");
    if (!f) return false;
    out.clear();
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}
static bool WriteUtf8File(const std::string &path, const std::string &data) {
    FILE *f = _wfopen(Widen(path).c_str(), L"wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    return ok;
}

// ---- Community Races XML (a fixed, simple layout: plain tags, no attributes we need)
static std::string XmlInner(const std::string &s, const std::string &tag, size_t from = 0, size_t *end = nullptr) {
    std::string open = "<" + tag + ">", close = "</" + tag + ">";
    size_t a = s.find(open, from);
    if (a == std::string::npos) { if (end) *end = std::string::npos; return ""; }
    a += open.size();
    size_t b = s.find(close, a);
    if (b == std::string::npos) { if (end) *end = std::string::npos; return ""; }
    if (end) *end = b + close.size();
    return s.substr(a, b - a);
}
static std::vector<std::string> XmlAll(const std::string &s, const std::string &tag) {
    std::vector<std::string> out;
    size_t at = 0, end = 0;
    for (;;) { std::string in = XmlInner(s, tag, at, &end); if (end == std::string::npos) break; out.push_back(in); at = end; }
    return out;
}
static Vec3 XmlVec(const std::string &s) {
    return V3((float)std::atof(XmlInner(s, "X").c_str()), (float)std::atof(XmlInner(s, "Y").c_str()), (float)std::atof(XmlInner(s, "Z").c_str()));
}
static std::string RaceVehLabel(const std::string &m) {   // what the menus show: GTA's own name for a model name or hash
    const char *dn = N<const char *>(N_GET_DISPLAY_NAME_FROM_VEHICLE_MODEL, ModelHash(m));
    if (!dn || !*dn || !std::strcmp(dn, "CARNOTFOUND")) return m;
    std::string l = Label(dn);
    return l.empty() ? std::string(dn) : l;
}
static RaceCpExt RaceCpKind(const std::string &s) {
    RaceCpExt e;
    e.size = (float)std::atof(XmlInner(s, "Size").c_str()); if (e.size <= 0.f) e.size = 1.f;
    e.air = XmlInner(s, "Kind").find("air") != std::string::npos;
    std::string sp = XmlInner(s, "Split");
    if (!sp.empty()) {
        e.split = true; e.sp = XmlVec(sp);
        e.spSize = (float)std::atof(XmlInner(sp, "Size").c_str()); if (e.spSize <= 0.f) e.spSize = e.size;
        e.spAir = XmlInner(sp, "Kind").find("air") != std::string::npos;
    }
    return e;
}
static bool LoadRaceFile(const std::string &file, RaceDef &d, bool withProps) {
    std::string s;
    if (!ReadUtf8File(g_dir + "GoriOnline\\Races\\" + file, s)) return false;
    d = RaceDef();
    d.file = file;
    d.name = XmlInner(s, "Name");
    d.desc = XmlInner(s, "Description");
    if (d.name.empty()) d.name = file.substr(0, file.size() - 4);
    for (const std::string &v : XmlAll(XmlInner(s, "Checkpoints"), "Vector3")) d.cps.push_back(XmlVec(v));
    for (const std::string &sp : XmlAll(XmlInner(s, "SpawnPoints"), "SpawnPoint"))
        d.grid.push_back({XmlVec(XmlInner(sp, "Position")), (float)std::atof(XmlInner(sp, "Heading").c_str())});
    for (const std::string &v : XmlAll(XmlInner(s, "AvailableVehicles"), "VehicleHash")) d.vehicles.push_back(v);
    d.laps = XmlInner(s, "LapsAvailable") != "false";
    d.type = XmlInner(s, "Type");
    d.defLaps = std::atoi(XmlInner(s, "Laps").c_str());
    std::string dv = XmlInner(s, "DefaultVehicle");
    if (!dv.empty()) { d.vehicles.erase(std::remove(d.vehicles.begin(), d.vehicles.end(), dv), d.vehicles.end()); d.vehicles.insert(d.vehicles.begin(), dv); }
    for (const std::string &v : XmlAll(XmlInner(s, "TransformVehicles"), "VehicleHash")) d.tfVehicles.push_back(v);
    if (withProps) for (const std::string &c : XmlAll(XmlInner(s, "CheckpointData"), "Cp")) d.ext.push_back(RaceCpKind(c));
    d.trigger = XmlVec(XmlInner(s, "Trigger"));
    for (const std::string &t : XmlAll(XmlInner(s, "Transforms"), "Transform")) {
        std::string m = XmlInner(t, "VehicleHash"), c = XmlInner(t, "Checkpoint");
        if (!m.empty() && !c.empty()) d.transforms[std::atoi(c.c_str())] = m;
    }
    if (withProps)
        for (const std::string &p : XmlAll(XmlInner(s, "DecorativeProps"), "SavedProp")) {
            RaceProp rp;
            rp.pos = XmlVec(XmlInner(p, "Position"));
            rp.rot = XmlVec(XmlInner(p, "Rotation"));
            rp.hash = (uint32_t)std::strtoll(XmlInner(p, "Hash").c_str(), nullptr, 10);
            rp.dyn = XmlInner(p, "Dynamic") == "true";
            std::string t = XmlInner(p, "Texture");
            rp.tex = t.empty() ? -1 : std::atoi(t.c_str());
            rp.speed = std::atoi(XmlInner(p, "Speed").c_str());
            d.props.push_back(rp);
        }
    return !d.cps.empty() && !d.grid.empty();
}
static void RefreshRaceList() {
    if (Now() - g_raceListAt < 10000) return;
    g_raceListAt = Now();
    g_raceList.clear();
    std::set<std::string> rockstar;
    for (const char *sub : {"Rockstar\\", ""}) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(Widen(g_dir + "GoriOnline\\Races\\" + sub + "*.xml").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            RaceDef d;
            if (!LoadRaceFile(sub + Narrow(fd.cFileName), d, false)) continue;
            if (*sub) { if (d.type.empty()) d.type = "Race"; rockstar.insert(d.name); }
            else { d.type.clear(); if (rockstar.count(d.name)) continue; }   // Rockstar's own version wins
            g_raceList.push_back(d);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(g_raceList.begin(), g_raceList.end(), [](const RaceDef &a, const RaceDef &b) { return a.name < b.name; });
}
// the races on the map (GTA Online's race flag blip, short range) and "press E" at their start
static std::vector<int> g_raceBlips;
static int g_raceBlipsAt = -100000;
static void RaceBlipsTick() {
    bool want = YouActive() && g_race.phase == RP_NONE && Freemode();
    if (!want) { for (int &b : g_raceBlips) RemoveBlip(b); g_raceBlips.clear(); return; }
    if (g_raceBlips.empty() && Now() - g_raceBlipsAt > 30000) {
        g_raceBlipsAt = Now();
        RefreshRaceList();
        for (const RaceDef &d : g_raceList) {
            Vec3 t = d.trigger.x != 0.f ? d.trigger : d.grid[0].first;
            g_raceBlips.push_back(CoordBlip(t, 315, 0, "Race: " + d.name, true));
        }
    }
    if (g_menuOpen || g_mActive) return;
    Vec3 p = Pos(Me());
    for (int i = 0; i < (int)g_raceList.size(); ++i) {
        const RaceDef &d = g_raceList[i];
        Vec3 t = d.trigger.x != 0.f ? d.trigger : d.grid[0].first;
        float dist = Dist(p, t);
        if (dist < 40.f) Marker(t, 240, 200, 80);
        if (dist > 4.f) continue;
        Help("Press ~INPUT_CONTEXT~ to race ~y~" + d.name + "~s~.");
        if (JustPressed(51)) { g_raceSelLaps = 3; g_raceSelVeh = 0; g_menuOpen = true; g_stack.clear(); Open((MenuId)RM_LIST); Open((MenuId)RM_RACE, i); }
        break;
    }
}
static std::string RaceTime(int ms) {
    if (ms < 0) ms = 0;
    char b[32]; std::snprintf(b, sizeof b, "%02d:%02d.%03d", ms / 60000, ms / 1000 % 60, ms % 1000);
    return b;
}
static std::string Ordinal(int n) {
    const char *suf = (n % 100 >= 11 && n % 100 <= 13) ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
    return std::to_string(n) + suf;
}
static int RacePayout(const RaceRun &r) { return std::min(30000, 1500 + 100 * r.total); }
// racing alone pays like GTA Online's solo races: half of what first place gets online (the server pays online races)
static int RaceSoloPay(const RaceRun &r) { return std::max(1000, RacePayout(r) / 2); }

// ---- standings from LocalOnline ("name\tcheckpoints\tfinishMs\tme")
struct RaceStanding { std::string name; int cps = 0, finishMs = 0; bool me = false; };
static std::vector<RaceStanding> RaceStandings() {
    std::vector<RaceStanding> out;
    if (!g_race.online || !LO().getRaceStandings) return out;
    std::string t = CoopFetch(LO().getRaceStandings);
    size_t at = 0;
    while (at < t.size()) {
        size_t nl = t.find('\n', at);
        std::vector<std::string> f = SplitTabs(t.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
        at = nl == std::string::npos ? t.size() : nl + 1;
        if (f.size() < 4) continue;
        out.push_back({f[0], std::atoi(f[1].c_str()), std::atoi(f[2].c_str()), f[3] == "1"});
    }
    return out;
}
static int MyRacePosition(int *count) {
    std::vector<RaceStanding> st = RaceStandings();
    if (count) *count = st.empty() ? 1 : (int)st.size();
    for (size_t i = 0; i < st.size(); ++i) if (st[i].me) return (int)i + 1;
    return 1;
}

// ---- the track: props, checkpoints, blips
static void RaceClearMarkers() {
    if (g_race.cpHandle) { N(N_DELETE_CHECKPOINT, g_race.cpHandle); g_race.cpHandle = 0; }
    if (g_race.cpHandle2) { N(N_DELETE_CHECKPOINT, g_race.cpHandle2); g_race.cpHandle2 = 0; }
    RemoveBlip(g_race.blip); RemoveBlip(g_race.blip2); RemoveBlip(g_race.blip3);
}
static RaceCpExt RaceCpInfo(int i) {   // Rockstar's per-checkpoint data, or the defaults
    const auto &e = g_race.def.ext;
    return e.empty() ? RaceCpExt() : e[(size_t)i % e.size()];
}
static float RaceCpRadius(int i, bool split = false) {   // how close counts as through it
    RaceCpExt e = RaceCpInfo(i);
    bool air = g_race.air || (split ? e.spAir : e.air);
    return (air ? 22.f : 13.f) * std::clamp(split ? e.spSize : e.size, 0.6f, 3.f);
}
static int RaceMarker(const Vec3 &p, const Vec3 &nx, bool last, bool air, float size, int cr, int cg, int cb) {
    int h;
    if (air) h = N<int>(N_CREATE_CHECKPOINT, last ? 9 : 6, p.x, p.y, p.z, nx.x, nx.y, nx.z, 20.f * size, cr, cg, cb, 160, 0);
    else {
        h = N<int>(N_CREATE_CHECKPOINT, last ? 4 : 1, p.x, p.y, p.z - 1.0f, nx.x, nx.y, nx.z, 12.f * size, cr, cg, cb, 160, 0);
        N(N_SET_CHECKPOINT_CYLINDER_HEIGHT, h, 9.f * size, 9.f * size, 12.f * size);
    }
    return h;
}
static Vec3 RaceCp(int i) { return g_race.def.cps[(size_t)i % g_race.def.cps.size()]; }
static const std::string *RaceTransformAt(int i) {   // the vehicle checkpoint i turns you into, if any
    auto it = g_race.def.transforms.find(i % (int)g_race.def.cps.size());
    return it == g_race.def.transforms.end() ? nullptr : &it->second;
}
static void RaceShowCheckpoint() {
    RaceClearMarkers();
    if (g_race.cp >= g_race.total) return;
    bool last = g_race.cp == g_race.total - 1;
    Vec3 p = RaceCp(g_race.cp), nx = last ? p : RaceCp(g_race.cp + 1);
    bool tf = !last && RaceTransformAt(g_race.cp);   // transform checkpoints are purple, like Online's
    int cr = tf ? 180 : 254, cg = tf ? 110 : 235, cb = tf ? 255 : 169;
    RaceCpExt e = RaceCpInfo(g_race.cp);
    // air races and Rockstar's air checkpoints: rings, like Online's
    g_race.cpHandle = RaceMarker(p, nx, last, g_race.air || e.air, std::clamp(e.size, 0.6f, 3.f), cr, cg, cb);
    if (e.split) {   // the other route of a split: either one counts
        g_race.cpHandle2 = RaceMarker(e.sp, nx, last, g_race.air || e.spAir, std::clamp(e.spSize, 0.6f, 3.f), cr, cg, cb);
        g_race.blip3 = CoordBlip(e.sp, last ? 38 : 1, tf ? 27 : 5, "Checkpoint", false);
    }
    g_race.blip = CoordBlip(p, last ? 38 : 1, tf ? 27 : 5, last ? "Finish" : tf ? "Transform" : "Checkpoint", false);
    N(N_SET_BLIP_ROUTE, g_race.blip, 1);
    N(N_SET_BLIP_ROUTE_COLOUR, g_race.blip, 5);
    if (!last) {
        g_race.blip2 = CoordBlip(nx, g_race.cp + 1 == g_race.total - 1 ? 38 : 1, 5, "Next checkpoint", false);
        N(N_SET_BLIP_SCALE, g_race.blip2, 0.6f);
        N(N_SET_BLIP_ALPHA, g_race.blip2, 150);
    }
}
static void RaceSpawnProps() {
    int made = 0;
    for (const RaceProp &p : g_race.def.props) {
        if (!LoadModel(p.hash)) continue;
        int o = N<int>(N_CREATE_OBJECT_NO_OFFSET, p.hash, p.pos.x, p.pos.y, p.pos.z, 0, 1, 0);
        if (!o) continue;
        N(N_SET_ENTITY_ROTATION, o, p.rot.x, p.rot.y, p.rot.z, 2, 1);
        if (p.tex >= 0) N(N_SET_OBJECT_TINT_INDEX, o, p.tex);
        if (!p.dyn) N(N_FREEZE_ENTITY_POSITION, o, 1);
        N(N_SET_ENTITY_LOD_DIST, o, 3000);
        g_race.props.push_back(o);
        if (p.speed) g_race.pads.push_back({o, p.speed});
        if (++made % 25 == 0) Wait(0);
    }
    for (const RaceProp &p : g_race.def.props) N(N_SET_MODEL_AS_NO_LONGER_NEEDED, p.hash);
}
static int RaceVehicleAt(const Vec3 &p, float h) {
    uint32_t m = ModelHash(g_race.vehicle);
    if (!LoadModel(m)) { m = Joaat("elegy"); if (!LoadModel(m)) return 0; }
    int v = N<int>(N_CREATE_VEHICLE, m, p.x, p.y, p.z, h, 0, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
    if (!v) return 0;
    N(N_SET_ENTITY_AS_MISSION_ENTITY, v, 1, 1);
    N<int>(N_SET_VEHICLE_ON_GROUND_PROPERLY, v, 5.0f);
    N(N_SET_VEHICLE_ENGINE_ON, v, 1, 1, 0);
    N(N_SET_PED_INTO_VEHICLE, Me(), v, -1);
    return v;
}
static bool RaceAirClass(int v) { int cls = N<int>(N_GET_VEHICLE_CLASS, v); return cls == 15 || cls == 16; }
static void RaceTransform(const std::string &model) {   // into the new vehicle at the same place, heading and speed
    RaceRun &r = g_race;
    int old = r.veh, me = Me();
    int from = old && Exists(old) ? old : me;
    Vec3 p = Pos(from), vel = N<Vec3>(N_GET_ENTITY_VELOCITY, from);
    float h = N<float>(N_GET_ENTITY_HEADING, from), speed = std::sqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
    uint32_t m = ModelHash(model);
    if (!LoadModel(m)) { Log("race: transform model %s not found", model.c_str()); return; }
    int newCls = N<int>(N_GET_VEHICLE_CLASS_FROM_NAME, m);
    float up = newCls == 15 ? 8.f : newCls == 16 ? 30.f : 0.5f;   // aircraft appear in the air: on water they sink and throw you out
    int v = N<int>(N_CREATE_VEHICLE, m, p.x, p.y, p.z + up, h, 0, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
    if (!v) return;
    N(N_SET_ENTITY_AS_MISSION_ENTITY, v, 1, 1);
    if (old && Exists(old)) N(N_SET_ENTITY_COORDS, old, p.x, p.y, p.z - 50.f, 0, 0, 0, 0);   // out of the way before the swap
    N(N_SET_PED_INTO_VEHICLE, me, v, -1);
    N(N_SET_VEHICLE_ENGINE_ON, v, 1, 1, 0);
    r.veh = v; r.vehicle = model; r.air = RaceAirClass(v);
    int cls = N<int>(N_GET_VEHICLE_CLASS, v);
    if (cls == 15) { N(N_SET_HELI_BLADES_FULL_SPEED, v); vel.z = std::max(vel.z, 2.f); }
    if (cls == 16) { N(N_CONTROL_LANDING_GEAR, v, 3); speed = std::max(speed, 45.f); }
    N(N_SET_ENTITY_VELOCITY, v, vel.x, vel.y, vel.z);
    N(N_SET_VEHICLE_FORWARD_SPEED, v, speed);
    if (old && Exists(old)) DeleteEntity(old);
    N(N_ANIMPOSTFX_PLAY, "RaceTurbo", 0, 0);   // the short flash of Online's stunt races
    Sound("CHECKPOINT_PERFECT", "HUD_MINI_GAME_SOUNDSET");
    Log("race: transform -> %s at checkpoint %d", model.c_str(), r.cp);
}
// your records (saved with the character): the best time on a race with its laps, and its best lap
static std::string RacePbKey(const RaceRun &r) { return "@racepb|" + r.def.file + "|" + std::to_string(r.def.laps ? r.laps : 1); }
static int RacePb(const RaceRun &r) { auto it = g_cust.find(RacePbKey(r)); return it == g_cust.end() ? 0 : it->second; }
static void RaceRecord(RaceRun &r) {   // finished: a new personal best? (the end screen says so, by how much)
    if (r.pb && r.finishMs < r.pb) g_celPbBy = r.pb - r.finishMs;
    if (!r.pb || r.finishMs < r.pb) { g_cust[RacePbKey(r)] = r.finishMs; Log("race: personal best %s (was %s)", RaceTime(r.finishMs).c_str(), r.pb ? RaceTime(r.pb).c_str() : "none"); }
    std::string lk = "@racelap|" + r.def.file;
    auto it = g_cust.find(lk);
    if (r.lapBest && (it == g_cust.end() || r.lapBest < it->second)) g_cust[lk] = r.lapBest;
}
// the lobby's time of day and weather, for the race (LocalOnline holds the server's world off meanwhile), slipstream
static void RaceWorldApply(RaceRun &r) {
    int tod = std::clamp(r.opts.tod, 0, 3), w = std::clamp(r.opts.weather, 0, 6);
    if ((tod || w) && LO().setWorldHold) { LO().setWorldHold(1); r.worldHeld = true; }
    if (tod) { N(N_SET_CLOCK_TIME, RACE_TOD_HOUR[tod], 0, 0); N(N_PAUSE_CLOCK, 1); }
    if (w) { N(N_CLEAR_OVERRIDE_WEATHER); N(N_SET_WEATHER_TYPE_NOW_PERSIST, RACE_WEATHER_TYPE[w]); }
    N(N_SET_ENABLE_VEHICLE_SLIPSTREAMING, r.opts.slip ? 1 : 0);
    Log("race: %s, %s, traffic %s, catch up %s, slipstream %s", RACE_TOD[tod], RACE_WEATHER[w], RACE_TRAFFIC[std::clamp(r.opts.traffic, 0, 3)], r.opts.catchup ? "on" : "off", r.opts.slip ? "on" : "off");
}
static void RaceWorldRestore(RaceRun &r) {
    N(N_SET_ENABLE_VEHICLE_SLIPSTREAMING, 0);
    if (!r.worldHeld) return;
    N(N_PAUSE_CLOCK, 0);
    N(N_CLEAR_WEATHER_TYPE_PERSIST);
    if (LO().setWorldHold) LO().setWorldHold(0);   // the server's clock and weather come back
    r.worldHeld = false;
}
// every frame of the race: the traffic, and catch up (online: the ones behind get more power, the further behind the more)
static void RaceOptsFrame(RaceRun &r) {
    float d = RACE_TRAFFIC_DENSITY[std::clamp(r.opts.traffic, 0, 3)];
    N(N_SET_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME, d);
    N(N_SET_RANDOM_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME, d);
    N(N_SET_PARKED_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME, d);
    if (r.opts.catchup && r.online && r.phase == RP_RACING && Exists(r.veh)) {
        int count = 1, pos = MyRacePosition(&count);
        N(N_SET_VEHICLE_CHEAT_POWER_INCREASE, r.veh, pos > 1 ? 1.f + 0.12f * std::min(pos - 1, 4) : 1.f);
    }
}
static void RaceCleanup() {
    RaceWorldRestore(g_race);
    RaceClearMarkers();
    N(N_ANIMPOSTFX_STOP_ALL);
    if (g_race.countdownMovie) N(N_SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED, &g_race.countdownMovie);
    JobSkyUp();   // back to freemode: up from the finish...
    int me = Me();
    if (InAnyVehicle()) N(N_SET_ENTITY_COORDS, me, g_race.back.x, g_race.back.y, g_race.back.z, 0, 0, 0, 0);   // out of the race car
    if (Exists(g_race.veh)) DeleteEntity(g_race.veh);
    for (int &o : g_race.props) DeleteEntity(o);
    g_race.props.clear();
    Teleport(g_race.back.x, g_race.back.y, g_race.back.z, g_race.backH);
    g_race = RaceRun();
    JobSkyDown();   // ...down where you were
}

// ---- start: everyone (solo, host and members) gets here with the start time
static int RaceGridSlot() {   // host 0, members 1.. in the order they joined
    if (!g_race.online || g_race.host) return 0;
    std::vector<std::string> names;
    std::string cur;
    for (char c : g_coop.members + ",") { if (c == ',') { if (!cur.empty()) names.push_back(cur); cur.clear(); } else cur += c; }
    for (size_t i = 0; i < names.size(); ++i) if (names[i] == "you") return (int)i + 1;
    return 1;
}
static void RaceBegin(int startsInMs) {
    RaceRun &r = g_race;
    r.total = (int)r.def.cps.size() * (r.def.laps ? std::max(1, r.laps) : 1);
    r.startAt = Now() + std::max(1000, startsInMs);
    r.cp = 0; r.finishMs = 0;
    r.lapStart = 0; r.lapBest = 0; r.pb = RacePb(r);
    r.vehicle = r.startVehicle.empty() ? r.vehicle : r.startVehicle;   // transforms start over
    r.phase = RP_COUNTDOWN;
    r.payout = RacePayout(r);
    JobSkyUp();   // Online's transition: up from here...
    RaceWorldApply(r);   // the lobby's time of day and weather while up there
    if (InAnyVehicle()) N(N_SET_ENTITY_COORDS, Me(), r.back.x, r.back.y, r.back.z + 1.f, 0, 0, 0, 0);
    int slot = RaceGridSlot();
    const auto &g = r.def.grid[(size_t)slot % r.def.grid.size()];
    Vec3 gp = g.first;
    if (slot >= (int)r.def.grid.size()) { gp.x += 3.f * (slot / (int)r.def.grid.size()); }   // more racers than grid spots
    N(N_SET_ENTITY_COORDS, Me(), gp.x, gp.y, gp.z + 1.f, 0, 0, 0, 0);
    N(N_REQUEST_COLLISION_AT_COORD, gp.x, gp.y, gp.z);
    RaceSpawnProps();
    for (int i = 0; i < 40 && !NB(N<int>(N_HAS_COLLISION_LOADED_AROUND_ENTITY, Me())); ++i) Wait(50);
    r.veh = RaceVehicleAt(gp, g.second);
    if (r.veh) {
        r.air = RaceAirClass(r.veh);
        if (!r.air) N(N_FREEZE_ENTITY_POSITION, r.veh, 1);   // aircraft are held by the countdown only
    }
    N(N_SET_PLAYER_WANTED_LEVEL, Player(), 0, 0); N(N_SET_PLAYER_WANTED_LEVEL_NOW, Player(), 0);
    N(N_SET_GAMEPLAY_CAM_RELATIVE_HEADING, 0.f);
    RaceShowCheckpoint();   // after the vehicle: its class picks the checkpoint style
    r.countdownMovie = N<int>(N_REQUEST_SCALEFORM_MOVIE, "COUNTDOWN");
    r.lastCount = -1;
    JobSkyDown();   // ...and down onto the grid
    if (!r.online) r.startAt = std::max(r.startAt, Now() + 3200);   // alone: the 3-2-1 once you have landed, like Online's
    Log("race: %s, %d laps, %d checkpoints, slot %d, %s, starts in %d ms", r.def.name.c_str(), r.laps, r.total, slot, r.online ? "online" : "solo", startsInMs);
}
static bool RaceLoad(const std::string &file, int laps, const std::string &vehicle) {
    RaceRun r;
    if (!LoadRaceFile(file, r.def, true)) { Notify("~r~This race could not be loaded:~s~ " + file); return false; }
    r.laps = r.def.laps ? std::max(1, laps) : 1;
    r.vehicle = vehicle.empty() ? (r.def.vehicles.empty() ? std::string("elegy") : r.def.vehicles[0]) : vehicle;
    r.startVehicle = r.vehicle;
    r.back = Pos(Me()); r.backH = N<float>(N_GET_ENTITY_HEADING, Me());
    r.opts = g_raceNextOpts;
    g_raceNextOpts = RaceOpts();
    g_race = r;
    return true;
}
static void RaceStartSolo(const std::string &file, int laps, const std::string &vehicle) {
    if (g_mActive) { Notify("~r~Finish or quit your current job first."); return; }
    if (!RaceLoad(file, laps, vehicle)) return;
    RaceBegin(6000);
}
static void RaceHostOnline(const std::string &file, int laps, const std::string &vehicle) {
    if (g_mActive) { Notify("~r~Finish or quit your current job first."); return; }
    if (!LO().jobOpenKind || !CoopAvailable()) { Notify("~r~Online races need GTA Local Online."); return; }
    if (!RaceLoad(file, laps, vehicle)) return;
    g_race.online = true; g_race.host = true;
    g_race.total = (int)g_race.def.cps.size() * (g_race.def.laps ? g_race.laps : 1);
    Vec3 t = g_race.def.trigger.x != 0.f ? g_race.def.trigger : g_race.def.grid[0].first;
    if (!LO().jobOpenKind(g_race.def.name.c_str(), t.x, t.y, t.z, RacePayout(g_race), 1)) { g_race = RaceRun(); Notify("~r~Could not open the race."); return; }
    g_race.phase = RP_LOBBY;
    // the race setup for the members: "R|file|laps|vehicle|tod|weather|traffic|catchup|slipstream"
    std::string cfg = "R|" + file + "|" + std::to_string(g_race.laps) + "|" + g_race.vehicle + "|" + RaceOptsText(g_race.opts);
    Wait(300);
    if (LO().jobUpdate) LO().jobUpdate(COOP_LOBBY, 0, t.x, t.y, t.z, cfg.c_str());
    Notify("Race lobby open: other players can join from ~b~Jobs~s~. Start it from the ~b~Races~s~ menu.");
}
static void RaceHostStart() {
    if (!g_race.host || g_race.phase != RP_LOBBY || !LO().jobUpdate) return;
    std::string cfg = "R|" + g_race.def.file + "|" + std::to_string(g_race.laps) + "|" + g_race.vehicle + "|" + RaceOptsText(g_race.opts);
    Vec3 t = g_race.def.grid[0].first;
    LO().jobUpdate(COOP_RUNNING, 0, t.x, t.y, t.z, cfg.c_str());
    g_race.phase = RP_NONE;   // RaceTick picks up the start time from the job state
}

// ---- members: the host's race job told us to race
static void RaceFollowJob() {
    if (!g_race.online && g_race.phase != RP_NONE) return;   // a solo race of ours
    if (!g_coop.id || g_coop.kind != 1) return;
    if (g_race.phase == RP_NONE && g_coop.status == COOP_RUNNING && g_coop.startsIn > 0) {
        if (g_coop.text.compare(0, 2, "R|")) return;
        std::vector<std::string> f;
        std::string cur;
        for (char c : g_coop.text + "|") { if (c == '|') { f.push_back(cur); cur.clear(); } else cur += c; }
        if (f.size() < 4) return;
        bool host = g_coop.hosting || g_coop.host == "you";
        if (!host && f.size() >= 9) {   // the host's lobby options
            g_raceNextOpts.tod = std::atoi(f[4].c_str()); g_raceNextOpts.weather = std::atoi(f[5].c_str()); g_raceNextOpts.traffic = std::atoi(f[6].c_str());
            g_raceNextOpts.catchup = f[7] == "1"; g_raceNextOpts.slip = f[8] == "1";
        }
        if (!host && !RaceLoad(f[1], std::atoi(f[2].c_str()), f[3])) return;
        if (host && g_race.def.file != f[1] && !RaceLoad(f[1], std::atoi(f[2].c_str()), f[3])) return;
        g_race.online = true; g_race.host = host;
        RaceBegin((int)g_coop.startsIn);
    }
}

// ---- the HUD (bottom right, like Online's timer bars)
// Online's own timer bar: the "all_black_bg" gradient of the timerbars dictionary, the label right-aligned in the middle,
// the value at the right edge (races, deathmatches, survival and Arena War all use it)
static void RaceBar(int row, const std::string &label, const std::string &value, bool big = false) {
    static bool asked = false;
    if (!asked) { N(N_REQUEST_STREAMED_TEXTURE_DICT, "timerbars", 1); asked = true; }
    float y = 0.885f - row * 0.0399f;
    if (NB(N<int>(N_HAS_STREAMED_TEXTURE_DICT_LOADED, "timerbars"))) N(N_DRAW_SPRITE, "timerbars", "all_black_bg", 0.918f, y, 0.165f, 0.0355f, 0.f, 255, 255, 255, 160, 0, 0);
    else Rect(0.918f, y, 0.165f, 0.0355f, 0, 0, 0, 140);
    Text(label, 0.80f, y - 0.0115f, 0.288f, 0, 240, 240, 240, 255, 2, 0.905f);
    Text(value, 0.80f, y - (big ? 0.0205f : 0.0165f), big ? 0.55f : 0.43f, 0, 255, 255, 255, 255, 2, 0.993f);
}
static void RaceHud() {
    RaceRun &r = g_race;
    int count = 1, pos = MyRacePosition(&count);
    int t = r.phase >= RP_FINISHED ? r.finishMs : r.phase == RP_RACING ? Now() - r.startAt : 0;
    int lapCps = (int)r.def.cps.size();
    int lap = std::min(r.laps, r.cp / std::max(1, lapCps) + 1);
    if (JobCelebrating()) return;
    int row = 0;   // like Online's: the time at the bottom, your record, the current lap, the lap (a sprint: the checkpoints), the position on top
    RaceBar(row++, "TIME", RaceTime(t));
    if (r.pb) RaceBar(row++, "PERSONAL RECORD", RaceTime(r.pb));
    if (r.def.laps && r.laps > 1 && r.phase == RP_RACING) RaceBar(row++, "CURRENT LAP", RaceTime(t - r.lapStart));
    if (r.def.laps && r.laps > 1) RaceBar(row++, "LAP", std::to_string(lap) + "/" + std::to_string(r.laps));
    else RaceBar(row++, "CHECKPOINT", std::to_string(std::min(r.cp, r.total)) + "/" + std::to_string(r.total));
    RaceBar(row++, "POSITION", std::to_string(pos) + "/" + std::to_string(std::max(1, count)), true);
}
static void RaceResults() {
    std::vector<RaceStanding> st = RaceStandings();
    if (st.empty()) st.push_back({"you", g_race.cp, g_race.finishMs, true});
    float x = 0.5f, y = 0.25f, w = 0.36f;
    Rect(x, y, w, 0.05f, 30, 30, 30, 230);
    Text(g_race.def.name, x, y - 0.018f, 0.55f, 1, 255, 255, 255, 255, 0);
    static const double share[8] = {1.0, 0.75, 0.6, 0.5, 0.4, 0.35, 0.3, 0.25};
    for (size_t i = 0; i < st.size(); ++i) {
        float ry = y + 0.05f + 0.038f * i;
        bool me = st[i].me;
        Rect(x, ry, w, 0.036f, me ? 60 : 0, me ? 110 : 0, me ? 170 : 0, 190);
        Text(Ordinal((int)i + 1), x - w / 2 + 0.01f, ry - 0.014f, 0.35f, 4, 255, 255, 255, 255, 1);
        Text(st[i].name == "you" ? "You" : st[i].name, x - w / 2 + 0.06f, ry - 0.014f, 0.35f, 4, 255, 255, 255, 255, 1);
        std::string right = st[i].finishMs ? RaceTime(st[i].finishMs) : "DNF";
        if (g_race.online) right += st[i].finishMs ? "   " + Money((long long)(g_race.payout * share[i < 8 ? i : 7])) : "";
        else if (st[i].finishMs) right += "   " + Money(RaceSoloPay(g_race));
        Text(right, x + w / 2 - 0.01f, ry - 0.014f, 0.35f, 4, 255, 255, 255, 255, 2, x + w / 2 - 0.01f);
    }
}
static void RaceCountdownDraw(int left) {
    int n = left > 3000 ? -1 : left > 0 ? left / 1000 + 1 : 0;
    if (n != g_race.lastCount && n >= 0 && NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, g_race.countdownMovie))) {
        g_race.lastCount = n;
        N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, g_race.countdownMovie, "SET_MESSAGE");
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_TEXTURE_NAME_STRING, n ? std::to_string(n).c_str() : "GO");
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, n ? 240 : 114);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, n ? 200 : 204);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, n ? 80 : 114);
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, 1);
        N(N_END_SCALEFORM_MOVIE_METHOD);
        if (n) Sound("3_2_1", "HUD_MINI_GAME_SOUNDSET"); else Sound("GO", "HUD_MINI_GAME_SOUNDSET");
    }
    if (g_race.lastCount >= 0 && left > -1500) N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, g_race.countdownMovie, 255, 255, 255, 255, 0);
}

// ---- the race itself, every frame
// Online's stunt pads: driving over a boost pad (1..5) throws you forward, a slow-down pad (-1..-3) brakes you
static void RacePadsTick() {
    RaceRun &r = g_race;
    if (r.pads.empty() || !Exists(r.veh) || MyVehicle() != r.veh) return;
    Vec3 vp = Pos(r.veh);
    for (auto &[o, s] : r.pads) {
        if (!Exists(o) || Dist(vp, Pos(o)) > 14.f) continue;
        if (!NB(N<int>(N_IS_ENTITY_TOUCHING_ENTITY, r.veh, o))) continue;
        int &last = r.padAt[o];
        if (Now() - last < 1200) continue;
        last = Now();
        float v = N<float>(N_GET_ENTITY_SPEED, r.veh);
        Log("race: %s pad %+d at %.0f m/s", s > 0 ? "boost" : "slow-down", s, v);
        if (s > 0) {
            N(N_SET_VEHICLE_FORWARD_SPEED, r.veh, std::min(95.f, std::max(v, 20.f) + 7.f + 4.f * s));
            N(N_ANIMPOSTFX_PLAY, "RaceTurbo", 0, 0);
            Sound("CHECKPOINT_PERFECT", "HUD_MINI_GAME_SOUNDSET");
        } else {
            N(N_SET_VEHICLE_FORWARD_SPEED, r.veh, v * std::max(0.3f, 1.f + 0.2f * s));
            Sound("CHECKPOINT_MISSED", "HUD_MINI_GAME_SOUNDSET");
        }
    }
}
static std::string RaceRandomVehicle() {   // a random transform (Known Unknowns): one of the race's transform vehicles
    const std::vector<std::string> &l = g_race.def.tfVehicles.empty() ? g_race.def.vehicles : g_race.def.tfVehicles;
    if (l.empty()) return "elegy";
    for (int k = 0; k < 8; ++k) { const std::string &m = l[(size_t)Rand((int)l.size())]; if (m != g_race.vehicle) return m; }
    return l[0];
}
static void RaceRespawn() {
    RaceRun &r = g_race;
    Vec3 p = r.cp > 0 ? RaceCp(r.cp - 1) : r.def.grid[0].first;
    Vec3 nx = RaceCp(r.cp);
    float h = std::atan2(-(nx.x - p.x), nx.y - p.y) * 57.2958f;
    N(N_DO_SCREEN_FADE_OUT, 250); Wait(300);
    if (InAnyVehicle()) N(N_SET_ENTITY_COORDS, Me(), p.x, p.y, p.z + 2.f, 0, 0, 0, 0);
    if (Exists(r.veh)) DeleteEntity(r.veh);
    r.veh = RaceVehicleAt(p, h);   // the current vehicle: after a transform, the transformed one
    if (r.veh) {
        r.air = RaceAirClass(r.veh);
        int cls = N<int>(N_GET_VEHICLE_CLASS, r.veh);
        if (cls == 15) N(N_SET_HELI_BLADES_FULL_SPEED, r.veh);
        if (cls == 16) N(N_CONTROL_LANDING_GEAR, r.veh, 3);
        N(N_SET_VEHICLE_FORWARD_SPEED, r.veh, r.air ? 30.f : 10.f);
    }
    N(N_DO_SCREEN_FADE_IN, 300);
}
static void CreatorTick();
static bool CreatorOn();
static void RaceTick() {
    CreatorTick();
    if (!CreatorOn()) RaceBlipsTick();
    RaceFollowJob();
    RaceRun &r = g_race;
    if (r.phase == RP_NONE || r.phase == RP_LOBBY) return;
    int me = Me();
    // the host gave up / the job went away while racing
    if (r.online && (!g_coop.id || g_coop.kind != 1 || g_coop.status == COOP_CLOSED) && r.phase != RP_RESULTS) {
        Notify("The race was cancelled."); RaceCleanup(); return;
    }
    if (r.phase != RP_RESULTS) RaceOptsFrame(r);
    if (r.phase == RP_COUNTDOWN) {
        int left = r.startAt - Now();
        N(N_DISABLE_CONTROL_ACTION, 0, 75, 1);
        RaceCountdownDraw(left);
        RaceHud();
        if (left <= 0) {
            if (Exists(r.veh)) { N(N_FREEZE_ENTITY_POSITION, r.veh, 0); N(N_SET_VEHICLE_ENGINE_ON, r.veh, 1, 1, 0); }
            r.phase = RP_RACING;
            r.lastCpAt = Now();
        }
        return;
    }
    if (r.phase == RP_RACING) {
        RaceCountdownDraw(r.startAt - Now());
        N(N_DISABLE_CONTROL_ACTION, 0, 75, 1);   // F/Y: hold to respawn instead of getting out
        RacePadsTick();
        Vec3 p = Pos(me), c = RaceCp(r.cp);
        RaceCpExt ce = RaceCpInfo(r.cp);
        if (Dist(p, c) < RaceCpRadius(r.cp) || (ce.split && Dist(p, ce.sp) < RaceCpRadius(r.cp, true))) {
            const std::string *tf = r.cp + 1 < r.total ? RaceTransformAt(r.cp) : nullptr;
            if (tf && *tf == "random") RaceTransform(RaceRandomVehicle());
            else if (tf && *tf != r.vehicle) RaceTransform(*tf);
            ++r.cp;
            r.lastCpAt = Now();
            bool done = r.cp >= r.total;
            int lapCps = (int)r.def.cps.size();
            if (r.def.laps && lapCps && r.cp % lapCps == 0) {   // a lap done: Online's CURRENT LAP starts again
                int at = Now() - r.startAt, lapMs = at - r.lapStart;
                if (!r.lapBest || lapMs < r.lapBest) r.lapBest = lapMs;
                r.lapStart = at;
            }
            if (done) { r.finishMs = Now() - r.startAt; RaceRecord(r); }
            if (r.online && LO().raceProgress) LO().raceProgress(r.cp, done ? r.finishMs : 0);
            if (done) {
                RaceClearMarkers();
                r.phase = RP_FINISHED;
                int count = 1, pos = MyRacePosition(&count);
                Sound("FIRST_PLACE", "HUD_MINI_GAME_SOUNDSET");
                Shard(r.online ? "~y~" + Ordinal(pos) : "~y~FINISHED", RaceTime(r.finishMs));
                if (r.host) r.hostEndAt = Now() + 60000;   // the others get a minute to finish
                if (!r.online) r.resultsUntil = Now() + 8000;
            } else {
                Sound("CHECKPOINT_NORMAL", "HUD_MINI_GAME_SOUNDSET");
                RaceShowCheckpoint();
            }
        }
        // hold F (Y on a pad) to respawn at the last checkpoint
        if (NB(N<int>(N_IS_DISABLED_CONTROL_PRESSED, 0, 75))) {
            if (!r.respawnHold) r.respawnHold = Now();
            int held = Now() - r.respawnHold;
            Rect(0.5f, 0.8f, 0.12f * std::min(1.f, held / 1000.f), 0.01f, 240, 200, 80, 220);
            if (held > 1000) { r.respawnHold = 0; RaceRespawn(); }
        } else r.respawnHold = 0;
        if (Exists(r.veh) && (Dead(r.veh) || !NB(N<int>(N_IS_VEHICLE_DRIVEABLE, r.veh, 0)))) Help("Hold ~INPUT_VEH_EXIT~ to respawn.");
        else if (MyVehicle() != r.veh) Help("Hold ~INPUT_VEH_EXIT~ to respawn in your race vehicle.");
        RaceHud();
        return;
    }
    if (r.phase == RP_FINISHED) {
        RaceHud();
        if (r.online) {
            std::vector<RaceStanding> st = RaceStandings();
            bool all = !st.empty();
            for (auto &s : st) if (!s.finishMs) all = false;
            if (r.host && (all || Now() > r.hostEndAt) && LO().jobUpdate) {   // the host ends the race for everyone
                Vec3 t = r.def.grid[0].first;
                LO().jobUpdate(COOP_PASSED, r.total, t.x, t.y, t.z, "");
                r.phase = RP_RESULTS; r.resultsUntil = Now() + 10000;
            } else if (!r.host && g_coop.status >= COOP_PASSED) { r.phase = RP_RESULTS; r.resultsUntil = Now() + 10000; }
            else Text(r.host ? "Waiting for the other racers..." : "Waiting for the race to end...", 0.5f, 0.9f, 0.4f, 4, 255, 255, 255, 230, 0, 1.f, true);
        } else { r.phase = RP_RESULTS; }
        if (r.phase == RP_RESULTS) {
            int count = 1, pos = MyRacePosition(&count);
            int rpBefore = g_rp, rp = std::max(100, (r.online ? 1200 / pos : 300) + 10 * r.total);
            GiveRP(rp, nullptr);
            long long cash = 0;
            if (!r.online) {   // online races are paid by the server, into the bank
                cash = RaceSoloPay(r);
                AddCash((int)cash);
                Save();
                Log("race: solo payout %lld", cash);
            } else if (r.finishMs) {
                static const double share[8] = {1.0, 0.75, 0.6, 0.5, 0.4, 0.35, 0.3, 0.25};
                cash = (long long)(r.payout * share[std::min(7, std::max(0, pos - 1))]);
            }
            JobCelebrate(r.def.name, r.finishMs ? pos : count, true, r.finishMs, rpBefore, rp, cash, "");
            r.resultsUntil = Now() + 9000 + 9000;   // the celebration, then the board
        }
        return;
    }
    if (r.phase == RP_RESULTS) {
        if (!JobCelebrating() && !NjActive()) RaceResults();
        if (Now() > r.resultsUntil && !r.voting) { r.voting = true; NextJobRace(r.def.file, r.def.type, r.online, !r.online || r.host); }   // then Online's vote on the next job
    }
}


// ---------------------------------------------------------------- Race Creator (stunt tracks -> races)
// Map Editor / Menyoo track files (props only, e.g. alloc8or's 129 GTA Online stunt race maps) in
// GoriOnline\Tracks: load one, drive it, drop checkpoints with E, set the grid, save it as a race.
enum { RM_TRACKS = 216, RM_CREATOR };
struct CreatorState {
    bool on = false, laps = true;
    std::string track;                 // file name in Tracks
    std::vector<RaceProp> props;
    std::vector<int> objs, marks;
    std::vector<Vec3> cps;
    std::vector<std::pair<Vec3, float>> grid;
    std::map<int, std::string> transforms;   // checkpoint index -> vehicle
    int pendingTransform = -1;               // TRANSFORM_VEHICLES index for the next checkpoint dropped
    Vec3 back{}; float backH = 0.f;
};
static CreatorState g_cr;
static bool CreatorOn() { return g_cr.on; }
static std::vector<std::string> g_trackList;
static std::vector<std::string> ListUtf8(const std::string &pattern) {   // file names (UTF-8) matching a pattern
    std::vector<std::string> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(Widen(pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do out.push_back(Narrow(fd.cFileName)); while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}
static bool LoadTrack(const std::string &file, std::vector<RaceProp> &props, Vec3 &start, float &heading) {
    std::string s;
    if (!ReadUtf8File(g_dir + "GoriOnline\\Tracks\\" + file, s)) return false;
    props.clear();
    for (const std::string &o : XmlAll(XmlInner(s, "Objects"), "MapObject")) {
        if (XmlInner(o, "Type") != "Prop") continue;
        RaceProp p;
        p.pos = XmlVec(XmlInner(o, "Position"));
        p.rot = XmlVec(XmlInner(o, "Rotation"));
        p.hash = (uint32_t)std::strtoll(XmlInner(o, "Hash").c_str(), nullptr, 10);
        p.dyn = XmlInner(o, "Dynamic") == "true";
        props.push_back(p);
    }
    std::string meta = XmlInner(s, "Metadata");
    start = XmlVec(XmlInner(meta, "TeleportPoint"));
    heading = (float)std::atof(XmlInner(meta, "Heading").c_str());
    if (start.x == 0.f && start.y == 0.f && !props.empty()) start = V3(props[0].pos.x, props[0].pos.y, props[0].pos.z + 3.f);
    return !props.empty();
}
static void CreatorMarks() {
    for (int &m : g_cr.marks) N(N_DELETE_CHECKPOINT, m);
    g_cr.marks.clear();
    for (size_t i = 0; i < g_cr.cps.size(); ++i) {
        Vec3 p = g_cr.cps[i], nx = i + 1 < g_cr.cps.size() ? g_cr.cps[i + 1] : p;
        bool tf = g_cr.transforms.count((int)i) != 0;
        g_cr.marks.push_back(N<int>(N_CREATE_CHECKPOINT, i + 1 == g_cr.cps.size() ? 4 : 1, p.x, p.y, p.z - 1.f, nx.x, nx.y, nx.z, 12.f,
                                    tf ? 180 : 93, tf ? 110 : 182, tf ? 255 : 229, 120, 0));
    }
}
static void CreatorStart(const std::string &file) {
    if (g_race.phase != RP_NONE || g_mActive) { Notify("~r~Finish your race or job first."); return; }
    CreatorState c;
    Vec3 st; float h = 0.f;
    if (file.empty()) {   // a race anywhere on the map: no track props, it starts where you are
        int e = InAnyVehicle() ? MyVehicle() : Me();
        st = Pos(e); st.z -= 1.f; h = N<float>(N_GET_ENTITY_HEADING, e);
        for (int n = 1; n < 1000; ++n) {
            std::string name = "My Race " + std::to_string(n) + ".xml", s;
            if (!ReadUtf8File(g_dir + "GoriOnline\\Races\\" + name, s)) { c.track = name; break; }
        }
    } else if (!LoadTrack(file, c.props, st, h)) { Notify("~r~This track could not be loaded."); return; }
    c.on = true; if (!file.empty()) c.track = file;
    c.back = Pos(Me()); c.backH = N<float>(N_GET_ENTITY_HEADING, Me());
    N(N_DO_SCREEN_FADE_OUT, 400); Wait(450);
    int mover = InAnyVehicle() ? MyVehicle() : Me();
    N(N_SET_ENTITY_COORDS, mover, st.x, st.y, st.z + 1.f, 0, 0, 0, 0);
    N(N_SET_ENTITY_HEADING, mover, h);
    N(N_REQUEST_COLLISION_AT_COORD, st.x, st.y, st.z);
    g_cr = c;
    // the props, like a race's (RaceSpawnProps works on g_race)
    int made = 0;
    for (const RaceProp &p : g_cr.props) {
        if (!LoadModel(p.hash)) continue;
        int o = N<int>(N_CREATE_OBJECT_NO_OFFSET, p.hash, p.pos.x, p.pos.y, p.pos.z, 0, 1, 0);
        if (!o) continue;
        N(N_SET_ENTITY_ROTATION, o, p.rot.x, p.rot.y, p.rot.z, 2, 1);
        if (!p.dyn) N(N_FREEZE_ENTITY_POSITION, o, 1);
        N(N_SET_ENTITY_LOD_DIST, o, 3000);
        g_cr.objs.push_back(o);
        if (++made % 25 == 0) Wait(0);
    }
    for (int i = 0; i < 40 && !NB(N<int>(N_HAS_COLLISION_LOADED_AROUND_ENTITY, mover)); ++i) Wait(50);
    N(N_DO_SCREEN_FADE_IN, 500);
    Notify("~b~Race Creator:~s~ drive the track. ~b~E~s~ (D-pad right) drops a checkpoint. Set the grid and save from ~b~Races > Race Creator~s~.");
    Log("creator: %s, %d props", file.c_str(), (int)g_cr.objs.size());
}
static void CreatorStop() {
    for (int &m : g_cr.marks) N(N_DELETE_CHECKPOINT, m);
    N(N_DO_SCREEN_FADE_OUT, 400); Wait(450);
    for (int &o : g_cr.objs) DeleteEntity(o);
    Teleport(g_cr.back.x, g_cr.back.y, g_cr.back.z, g_cr.backH);
    N(N_DO_SCREEN_FADE_IN, 500);
    g_cr = CreatorState();
}
static void CreatorSetGrid() {   // 8 places behind the car, two columns, like a race grid
    int v = MyVehicle();
    int e = v ? v : Me();
    g_cr.grid.clear();
    float h = N<float>(N_GET_ENTITY_HEADING, e);
    for (int i = 0; i < 8; ++i) {
        float side = i % 2 ? 2.5f : -2.5f, back = -7.f * (i / 2);
        Vec3 p = N<Vec3>(N_GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS, e, side, back, 0.f);
        g_cr.grid.push_back({p, h});
    }
    Notify("Start grid set (8 places).");
}
static std::string XmlNum(float v) { char b[32]; std::snprintf(b, sizeof b, "%.4f", v); return b; }
static std::string XmlVecOut(const char *tag, const Vec3 &p, const char *ind) {
    return std::string(ind) + "<" + tag + ">\n" + ind + "  <X>" + XmlNum(p.x) + "</X>\n" + ind + "  <Y>" + XmlNum(p.y) + "</Y>\n" + ind + "  <Z>" + XmlNum(p.z) + "</Z>\n" + ind + "</" + tag + ">\n";
}
static void CreatorSave() {
    if (g_cr.cps.size() < 2) { Notify("~r~Drop at least 2 checkpoints first."); return; }
    if (g_cr.grid.empty()) { Notify("~r~Set the start grid first."); return; }
    std::string name = g_cr.track.substr(0, g_cr.track.size() - 4);
    std::string veh = MyVehicle() ? ModelNameOf(MyVehicle()) : "elegy";
    std::string x = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Race xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\n  <Checkpoints>\n";
    for (const Vec3 &c : g_cr.cps) x += XmlVecOut("Vector3", c, "    ");
    x += "  </Checkpoints>\n  <SpawnPoints>\n";
    for (auto &g : g_cr.grid) x += "    <SpawnPoint>\n" + XmlVecOut("Position", g.first, "      ") + "      <Heading>" + XmlNum(g.second) + "</Heading>\n    </SpawnPoint>\n";
    x += "  </SpawnPoints>\n  <AvailableVehicles>\n    <VehicleHash>" + veh + "</VehicleHash>\n  </AvailableVehicles>\n";
    x += std::string("  <LapsAvailable>") + (g_cr.laps ? "true" : "false") + "</LapsAvailable>\n  <DecorativeProps>\n";
    for (const RaceProp &p : g_cr.props)
        x += "    <SavedProp>\n" + XmlVecOut("Position", p.pos, "      ") + XmlVecOut("Rotation", p.rot, "      ") + "      <Hash>" + std::to_string((int32_t)p.hash) +
             "</Hash>\n      <Dynamic>" + (p.dyn ? "true" : "false") + "</Dynamic>\n    </SavedProp>\n";
    x += "  </DecorativeProps>\n";
    if (!g_cr.transforms.empty()) {
        x += "  <Transforms>\n";
        for (auto &t : g_cr.transforms)
            x += "    <Transform>\n      <Checkpoint>" + std::to_string(t.first) + "</Checkpoint>\n      <VehicleHash>" + t.second + "</VehicleHash>\n    </Transform>\n";
        x += "  </Transforms>\n";
    }
    std::string desc = !g_cr.transforms.empty() ? "Transform race made in Gori's Race Creator" : g_cr.props.empty() ? "Made in Gori's Race Creator"
                                                                                                                  : "Stunt track, checkpoints made in Gori's Race Creator";
    x += XmlVecOut("Trigger", g_cr.grid[0].first, "  ") + "  <Name>" + name + "</Name>\n  <Description>" + desc + "</Description>\n</Race>\n";
    std::string path = g_dir + "GoriOnline\\Races\\" + name + ".xml";
    if (!WriteUtf8File(path, x)) { Notify("~r~Could not save the race."); return; }
    g_raceListAt = -100000; g_raceBlipsAt = -100000;
    for (int &b : g_raceBlips) RemoveBlip(b);
    g_raceBlips.clear();
    Notify("~g~Race saved:~s~ " + name + " (" + std::to_string(g_cr.cps.size()) + " checkpoints). It is in ~b~Races~s~ now.");
    Log("creator: saved %s (%d cps, %d props, %d transforms, %s)", path.c_str(), (int)g_cr.cps.size(), (int)g_cr.props.size(), (int)g_cr.transforms.size(), veh.c_str());
}
static void CreatorDrop(const Vec3 &p) {   // a checkpoint here; a pending transform goes with it
    if (g_cr.pendingTransform >= 0) {
        g_cr.transforms[(int)g_cr.cps.size()] = TRANSFORM_VEHICLES[g_cr.pendingTransform];
        Notify("Checkpoint " + std::to_string(g_cr.cps.size() + 1) + " transforms into ~p~" + TRANSFORM_VEHICLES[g_cr.pendingTransform] + "~s~.");
        g_cr.pendingTransform = -1;
    }
    g_cr.cps.push_back(p);
    Sound("CHECKPOINT_NORMAL", "HUD_MINI_GAME_SOUNDSET");
    CreatorMarks();
}
static void CreatorTick() {
    if (!g_cr.on) return;
    for (size_t i = 0; i < g_cr.cps.size(); ++i) {
        auto tf = g_cr.transforms.find((int)i);
        Text3D(V3(g_cr.cps[i].x, g_cr.cps[i].y, g_cr.cps[i].z + 2.f), std::to_string(i + 1) + (tf == g_cr.transforms.end() ? "" : " > " + tf->second));
    }
    for (size_t i = 0; i < g_cr.grid.size(); ++i) Marker(g_cr.grid[i].first, 93, 182, 229);
    if (g_menuOpen) return;
    Text("RACE CREATOR  " + std::to_string(g_cr.cps.size()) + " checkpoints" + (g_cr.grid.empty() ? "  (no grid yet)" : ""), 0.5f, 0.03f, 0.45f, 4, 93, 182, 229, 255, 0, 1.f, true);
    if (JustPressed(51)) {
        Vec3 p = Pos(InAnyVehicle() ? MyVehicle() : Me());
        if (!g_cr.cps.empty() && Dist(p, g_cr.cps.back()) < 15.f) { Notify("Too close to the last checkpoint."); return; }
        CreatorDrop(p);
        return;
    }
}

// ---- menus
static bool RacesMenu(const Frame &f, std::string &title, std::string &sub, std::vector<Item> &items) {
    switch ((int)f.id) {
    case RM_LIST: {
        title = "Races"; sub = "RACES";
        RefreshRaceList();
        if (g_race.host && g_race.phase == RP_LOBBY) {
            int n = 1;
            for (char c : g_coop.members) if (c == ',') ++n;
            if (g_coop.members.empty()) n = 1; else ++n;
            items.push_back({"Start Race", std::to_string(n) + (n == 1 ? " racer" : " racers"), g_race.def.name + ": everyone in the lobby starts together.", [] { CloseMenu(); RaceHostStart(); }});
            items.push_back({"Cancel Race", "", "Close the lobby.", [] {
                if (LO().jobUpdate) LO().jobUpdate(COOP_CLOSED, 0, 0, 0, 0, "");
                g_race = RaceRun(); CloseMenu();
            }});
            return true;
        }
        // GTA Online's own races by kind (Rockstar Created), then the community ones
        std::map<std::string, int> kinds; int community = 0;
        for (const RaceDef &d : g_raceList) { if (d.type.empty()) ++community; else ++kinds[d.type]; }
        g_raceTypes.clear();
        for (auto &[k, n] : kinds) {
            int ti = (int)g_raceTypes.size(); g_raceTypes.push_back(k);
            items.push_back({"Rockstar Created: " + k + "s", std::to_string(n) + " >", "GTA Online's own " + k + "s, made by Rockstar Games.", [ti] { Open((MenuId)RM_RTYPE, ti); }});
        }
        if (community) items.push_back({"Community Races", std::to_string(community) + " >", "Races from GoriOnline\\Races: Community Races files and the ones you made.", [] { Open((MenuId)RM_RTYPE, -1); }});
        if (items.empty()) items.push_back({"No races installed", "", "Put Community Races .xml files in GoriOnline\\Races next to GTA5.exe.", nullptr});
        items.insert(items.begin(), {"Race Creator", g_cr.on ? "On >" : ">", "Turn a stunt track (GoriOnline\\Tracks) into a race: drive it and drop the checkpoints.",
                                     [] { Open((MenuId)(g_cr.on ? RM_CREATOR : RM_TRACKS)); }});
        return true;
    }
    case RM_RTYPE: {
        std::string kind = f.param >= 0 && f.param < (int)g_raceTypes.size() ? g_raceTypes[f.param] : "";
        title = f.param < 0 ? "Community Races" : kind + "s"; sub = f.param < 0 ? "COMMUNITY" : "ROCKSTAR CREATED";
        for (int i = 0; i < (int)g_raceList.size(); ++i) {
            const RaceDef &d = g_raceList[i];
            if (f.param < 0 ? !d.type.empty() : d.type != kind) continue;
            int laps = d.defLaps > 0 ? d.defLaps : 3;
            items.push_back({d.name, (d.transforms.empty() ? "" : "Transform  ") + (d.laps ? std::to_string(laps) + " laps" : std::string("Point to point")) + " >",
                             d.desc.empty() ? "A race." : d.desc,
                             [i, laps] { g_raceSelLaps = laps; g_raceSelVeh = 0; g_raceSelClass = 0; Open((MenuId)RM_RACE, i); }});
        }
        return true;
    }
    case RM_TRACKS: {
        title = "Race Creator"; sub = "STUNT TRACKS";
        g_trackList = ListUtf8(g_dir + "GoriOnline\\Tracks\\*.xml");
        for (const std::string &t : g_trackList) {
            std::string file = t;
            items.push_back({t.substr(0, t.size() - 4), "", "Load this track and start placing checkpoints.", [file] { CloseMenu(); CreatorStart(file); }});
        }
        items.insert(items.begin(), {"New Race Here (no track)", "", "Make a race anywhere on the map: it starts where you are. Good for Transform races.",
                                     [] { CloseMenu(); CreatorStart(""); }});
        return true;
    }
    case RM_CREATOR: {
        title = "Race Creator"; sub = "CREATING";
        if (!g_cr.on) { g_stack.pop_back(); return true; }
        items.push_back({"Drop Checkpoint Here", std::to_string(g_cr.cps.size()), "Also with E (D-pad right) while driving.", [] {
            CreatorDrop(Pos(InAnyVehicle() ? MyVehicle() : Me())); }});
        items.push_back({"Transform At Next Checkpoint", std::string("< ") + (g_cr.pendingTransform < 0 ? "None" : TRANSFORM_VEHICLES[g_cr.pendingTransform]) + " >",
                         "The next checkpoint you drop turns the racers into this vehicle (a Transform race). Drive the new vehicle from there on.",
                         [] { g_cr.pendingTransform = g_cr.pendingTransform + 1 >= NTRANSFORM_VEHICLES ? -1 : g_cr.pendingTransform + 1; }});
        items.push_back({"Undo Last Checkpoint", "", "", [] {
            if (!g_cr.cps.empty()) { g_cr.cps.pop_back(); g_cr.transforms.erase((int)g_cr.cps.size()); CreatorMarks(); } }});
        items.push_back({"Set Start Grid Here", g_cr.grid.empty() ? "" : "Set", "Stop where the race starts, facing the way to go.", [] { CreatorSetGrid(); }});
        items.push_back({"Laps", g_cr.laps ? "< Allowed >" : "< Sprint >", "A circuit (laps) or a point-to-point race.", [] { g_cr.laps = !g_cr.laps; }});
        items.push_back({"Save Race", "", "Saved in GoriOnline\\Races with the track's props. The race vehicle is the one you are in now: save from the start vehicle.", [] { CreatorSave(); }});
        items.push_back({"Exit Creator", "", "Remove the track and go back.", [] { CloseMenu(); CreatorStop(); }});
        return true;
    }
    case RM_RACE: {
        if (f.param < 0 || f.param >= (int)g_raceList.size()) { g_stack.pop_back(); return true; }
        const RaceDef &d = g_raceList[f.param];
        title = d.name; sub = "RACE";
        std::string file = d.file;
        int laps = d.laps ? g_raceSelLaps : 1;
        bool transform = !d.transforms.empty();
        if (transform) g_raceSelClass = 0;   // a transform race decides its own vehicles
        g_raceSelClass %= NRACE_CLASSES;
        std::vector<std::string> vehs;
        if (g_raceSelClass == 0) vehs = d.vehicles;
        else for (const char *m : RACE_CLASSES[g_raceSelClass].models) vehs.push_back(m);
        if (vehs.empty()) vehs.push_back("elegy");
        g_raceSelVeh = g_raceSelVeh % (int)vehs.size();
        std::string veh = vehs[g_raceSelVeh];
        if (transform) items.push_back({"Transform Race", std::to_string(d.transforms.size()) + " transforms", "Purple checkpoints change your vehicle.", nullptr});
        if (d.laps) items.push_back({"Laps", "< " + std::to_string(laps) + " >", "Select to change.", [] { g_raceSelLaps = g_raceSelLaps % 10 + 1; }});
        if (!transform)
            items.push_back({"Vehicle Class", std::string("< ") + RACE_CLASSES[g_raceSelClass].name + " >", RACE_CLASSES[g_raceSelClass].desc,
                             [] { g_raceSelClass = (g_raceSelClass + 1) % NRACE_CLASSES; g_raceSelVeh = 0; }});
        items.push_back({transform ? "Start Vehicle" : "Vehicle", "< " + RaceVehLabel(veh) + " >", "Select to change.", [] { ++g_raceSelVeh; }});
        int idx = f.param;
        items.push_back({"Play", "", "The race's lobby: its settings, alone or with the other players.", [idx] { CloseMenu(); OpenRaceCorona(idx); }});
        items.push_back({"Set GPS to the start", "", "", [d] {
            Vec3 t = d.trigger.x != 0.f ? d.trigger : d.grid[0].first;
            N(N_SET_NEW_WAYPOINT, t.x, t.y); CloseMenu();
        }});
        return true;
    }
    }
    return false;
}
