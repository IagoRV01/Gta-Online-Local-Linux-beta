// ---------------------------------------------------------------- Survival (GTA Local Online, job kind 3; solo without it)
// GTA Online-style survivals: ten waves of enemies at one spot, armed men on foot, then cars full of them and
// attack helicopters. The host picks the place, the others join from Jobs while it is in its lobby or countdown.
// LocalOnline does not sync NPCs, so every player fights the waves on their own game, side by side: the host
// moves everyone to the next wave once all the players have cleared the current one or are down. A player who
// dies is down until the next wave; when everyone is down it is over. Each player reports kills and waves
// (RaceProgress: checkpoints = kills, finishMs = waves | SV_DOWN) and the server pays payout * waves / 10.
enum { SV_LIST = 290, SV_LOC };
struct SvLoc { const char *name; float x, y, z, r; };
static const SvLoc SV_LOCS[] = {
    {"Stab City", 66.f, 3705.f, 39.7f, 45.f}, {"Boneyard", 2390.f, 3100.f, 48.2f, 55.f},
    {"Elysian Island Docks", 1000.f, -3100.f, 5.9f, 55.f}, {"Galileo Observatory", -425.f, 1123.f, 325.9f, 45.f},
    {"Paleto Sawmill", -567.f, 5253.f, 70.5f, 55.f}, {"Del Perro Pier", -1700.f, -1130.f, 13.1f, 40.f},
    {"Lost MC Clubhouse", 975.f, -115.f, 74.4f, 45.f}, {"Construction Site", -150.f, -1000.f, 27.3f, 50.f},
};
static const int NSV_LOCS = (int)(sizeof SV_LOCS / sizeof *SV_LOCS);
static const int SV_WAVES = 10, SV_DOWN = 0x10000, SV_PAYOUT = 50000, SV_BREAK_MS = 15000;
static const char *SV_WEAPONS[] = {"WEAPON_PISTOL", "WEAPON_SMG", "WEAPON_PUMPSHOTGUN", "WEAPON_CARBINERIFLE"};
enum SvPhase { SVP_NONE, SVP_LOBBY, SVP_COUNTDOWN, SVP_WAVE, SVP_BREAK, SVP_DOWN, SVP_RESULTS };
struct SvRun {
    int phase = SVP_NONE, loc = 0, wave = 0, cleared = 0, kills = 0, spawned = 0, total = 0, payout = SV_PAYOUT;
    int startAt = 0, waveAt = 0, breakUntil = 0, resultsUntil = 0, outside = 0, nextSpawn = 0, hostClearedAt = 0, lastV = -1, lastK = -1;
    bool host = false, solo = false, ended = false, heli = false;
    std::vector<int> peds, blips, cars;
    std::vector<int> pedBorn;
    std::vector<uint32_t> given;
    Vec3 back{}; float backH = 0.f;
    int countdownMovie = 0, lastCount = -1;
};
static SvRun g_sv;

struct SvScore { std::string name; int kills = 0, waves = 0; bool down = false, me = false; };
static std::vector<SvScore> SvScores() {
    std::vector<SvScore> out;
    if (g_sv.solo || !LO().getRaceStandings) { SvScore s; s.name = "you"; s.kills = g_sv.kills; s.waves = g_sv.cleared; s.down = g_sv.phase == SVP_DOWN; s.me = true; out.push_back(s); return out; }
    std::string t = CoopFetch(LO().getRaceStandings);
    size_t at = 0;
    while (at < t.size()) {
        size_t nl = t.find('\n', at);
        std::vector<std::string> f = SplitTabs(t.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
        at = nl == std::string::npos ? t.size() : nl + 1;
        if (f.size() < 4) continue;
        long long w = std::atoll(f[2].c_str());
        SvScore s; s.name = f[0]; s.kills = std::atoi(f[1].c_str()); s.waves = (int)(w & 0xFFFF); s.down = (w & SV_DOWN) != 0; s.me = f[3] == "1";
        out.push_back(s);
    }
    return out;
}
static void SvReport() {
    int v = g_sv.cleared | (g_sv.phase == SVP_DOWN ? SV_DOWN : 0);
    if (g_sv.solo || !LO().raceProgress || (v == g_sv.lastV && g_sv.kills == g_sv.lastK)) return;
    LO().raceProgress(g_sv.kills, v);
    g_sv.lastV = v; g_sv.lastK = g_sv.kills;
}
static std::string SvText(int wave) { return "S|" + std::to_string(g_sv.loc) + "|" + std::to_string(wave); }

// ---- the enemies
struct SvTier { const char *model, *weapon; int armor, accuracy; };
static SvTier SvTierFor(int wave, int i) {
    static const SvTier T[4][2] = {
        {{"g_m_y_lost_01", "WEAPON_PISTOL", 0, 15}, {"g_m_y_lost_02", "WEAPON_MICROSMG", 0, 15}},
        {{"g_m_y_mexgoon_01", "WEAPON_MICROSMG", 0, 20}, {"g_m_y_mexgoon_02", "WEAPON_PUMPSHOTGUN", 0, 20}},
        {{"s_m_y_blackops_01", "WEAPON_CARBINERIFLE", 50, 25}, {"s_m_y_blackops_01", "WEAPON_SMG", 50, 25}},
        {{"s_m_y_blackops_02", "WEAPON_SPECIALCARBINE", 100, 30}, {"s_m_y_blackops_03", "WEAPON_COMBATMG", 100, 30}},
    };
    int t = wave <= 2 ? 0 : wave <= 4 ? 1 : wave <= 7 ? 2 : 3;
    return T[t][i & 1];
}
static void SvArm(int ped, const SvTier &t) {
    N(N_SET_ENTITY_AS_MISSION_ENTITY, ped, 1, 1);
    N(N_GIVE_WEAPON_TO_PED, ped, Joaat(t.weapon), 999, 0, 1);
    N(N_SET_PED_RELATIONSHIP_GROUP_HASH, ped, g_hostile);
    N(N_SET_PED_ARMOUR, ped, t.armor);
    N(N_SET_PED_ACCURACY, ped, t.accuracy);
    N(N_SET_PED_COMBAT_ABILITY, ped, 2);
    N(N_SET_PED_COMBAT_ATTRIBUTES, ped, 46, 1);
    N(N_SET_PED_FLEE_ATTRIBUTES, ped, 0, 0);
    N(N_SET_PED_DROPS_WEAPONS_WHEN_DEAD, ped, 0);
    N(N_TASK_COMBAT_PED, ped, Me(), 0, 16);
    N(N_SET_PED_KEEP_TASK, ped, 1);
    int b = N<int>(N_ADD_BLIP_FOR_ENTITY, ped);
    N(N_SET_BLIP_COLOUR, b, 1); N(N_SET_BLIP_SCALE, b, 0.7f);
    g_sv.peds.push_back(ped); g_sv.blips.push_back(b); g_sv.pedBorn.push_back(Now());
}
static bool SvFootSpot(Vec3 &out) {   // on the edge of the area, out of the player's face
    const SvLoc &l = SV_LOCS[g_sv.loc];
    Vec3 me = Pos(Me());
    for (int tries = 0; tries < 10; ++tries) {
        float a = (float)Rand(6283) / 1000.f, r = l.r * (0.75f + (float)Rand(50) / 100.f);
        float x = l.x + std::cos(a) * r, y = l.y + std::sin(a) * r;
        Vec3 p{};
        if (!NB(N<int>(N_GET_SAFE_COORD_FOR_PED, x, y, l.z + 5.f, 0, &p, 16))) { float gz = GroundAt(x, y, l.z + 40.f, -1000.f); if (gz < -999.f) continue; p = V3(x, y, gz); }
        if (Dist(p, me) < 25.f || std::fabs(p.z - l.z) > 25.f) continue;
        out = p;
        return true;
    }
    return false;
}
static void SvSpawnFoot(int n) {
    for (int i = 0; i < n; ++i) {
        SvTier t = SvTierFor(g_sv.wave, g_sv.spawned);
        uint32_t m = Joaat(t.model);
        Vec3 p{};
        if (!LoadModel(m) || !SvFootSpot(p)) return;
        int ped = N<int>(N_CREATE_PED, 4, m, p.x, p.y, p.z + 1.f, (float)Rand(360), 0, 0);
        N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
        if (!ped) return;
        SvArm(ped, t);
        ++g_sv.spawned;
    }
}
static void SvSpawnCar() {   // three men in a car from a road out of sight
    const SvLoc &l = SV_LOCS[g_sv.loc];
    SvTier t = SvTierFor(g_sv.wave, g_sv.spawned);
    uint32_t cm = Joaat(g_sv.wave >= 8 ? "mesa3" : "granger"), pm = Joaat(t.model);
    if (!LoadModel(cm) || !LoadModel(pm)) return;
    float a = (float)Rand(6283) / 1000.f;
    Vec3 out{}; float h = 0.f;
    if (!NB(N<int>(N_GET_CLOSEST_VEHICLE_NODE_WITH_HEADING, l.x + std::cos(a) * (l.r + 110.f), l.y + std::sin(a) * (l.r + 110.f), l.z, &out, &h, 1, 3.0f, 0.0f))) return;
    int car = N<int>(N_CREATE_VEHICLE, cm, out.x, out.y, out.z, h, 0, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, cm);
    if (!car) return;
    N(N_SET_ENTITY_AS_MISSION_ENTITY, car, 1, 1);
    g_sv.cars.push_back(car);
    for (int seat = -1; seat <= 1 && g_sv.spawned < g_sv.total; ++seat) {
        int ped = N<int>(N_CREATE_PED_INSIDE_VEHICLE, car, 4, pm, seat, 0, 0);
        if (!ped) continue;
        SvArm(ped, t);
        if (seat == -1) { N(N_TASK_VEHICLE_DRIVE_TO_COORD_LONGRANGE, ped, car, l.x, l.y, l.z, 25.f, 786468, 20.f); N(N_SET_PED_KEEP_TASK, ped, 1); }
        ++g_sv.spawned;
    }
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, pm);
}
static void SvSpawnHeli() {   // an attack helicopter (waves 5 and 10)
    const SvLoc &l = SV_LOCS[g_sv.loc];
    uint32_t hm = Joaat("buzzard"), pm = Joaat("s_m_y_blackops_01");
    if (!LoadModel(hm) || !LoadModel(pm)) return;
    float a = (float)Rand(6283) / 1000.f;
    int heli = N<int>(N_CREATE_VEHICLE, hm, l.x + std::cos(a) * 300.f, l.y + std::sin(a) * 300.f, l.z + 80.f, a * 57.3f + 90.f, 0, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, hm);
    if (!heli) return;
    N(N_SET_ENTITY_AS_MISSION_ENTITY, heli, 1, 1);
    N(N_SET_HELI_BLADES_FULL_SPEED, heli);
    g_sv.cars.push_back(heli);
    int pilot = N<int>(N_CREATE_PED_INSIDE_VEHICLE, heli, 4, pm, -1, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, pm);
    if (!pilot) return;
    SvArm(pilot, {"s_m_y_blackops_01", "WEAPON_SMG", 100, 30});
    N(N_TASK_HELI_MISSION, pilot, heli, 0, Me(), 0.f, 0.f, 0.f, 6, 30.f, 25.f, -1.f, 45, 20, -1.f, 0);   // 6: attack the player
    N(N_SET_PED_KEEP_TASK, pilot, 1);
    ++g_sv.spawned;
    g_sv.heli = true;
    Notify("~r~Enemy helicopter~s~ incoming!");
}
static void SvClearEnemies() {
    for (int &b : g_sv.blips) RemoveBlip(b);
    for (int &p : g_sv.peds) DeleteEntity(p);
    for (int &c : g_sv.cars) DeleteEntity(c);
    g_sv.peds.clear(); g_sv.blips.clear(); g_sv.cars.clear(); g_sv.pedBorn.clear();
}

// ---- the player
static void SvLoadout() {
    int me = Me();
    for (const char *w : SV_WEAPONS) {
        uint32_t h = Joaat(w);
        if (!NB(N<int>(N_HAS_PED_GOT_WEAPON, me, h, 0)) && std::find(g_sv.given.begin(), g_sv.given.end(), h) == g_sv.given.end()) g_sv.given.push_back(h);
        N(N_GIVE_WEAPON_TO_PED, me, h, 9999, 0, 0);
    }
    N(N_SET_ENTITY_HEALTH, me, N<int>(N_GET_ENTITY_MAX_HEALTH, me), 0, 0);
    N(N_SET_PED_ARMOUR, me, 100);
}
static void SvPlaceInArea() {
    const SvLoc &l = SV_LOCS[g_sv.loc];
    int me = Me();
    Vec3 p = V3(l.x, l.y, l.z + 1.f), out{};
    if (NB(N<int>(N_GET_SAFE_COORD_FOR_PED, l.x + (float)Rand(9) - 4.f, l.y + (float)Rand(9) - 4.f, l.z + 3.f, 1, &out, 16)) && Dist(out, p) < 20.f) p = V3(out.x, out.y, out.z + 0.5f);
    if (InAnyVehicle()) N(N_TASK_LEAVE_VEHICLE, me, MyVehicle(), 16);
    N(N_SET_ENTITY_COORDS, me, p.x, p.y, p.z, 0, 0, 0, 0);
    N(N_SET_ENTITY_HEADING, me, (float)Rand(360));
    N(N_SET_PLAYER_WANTED_LEVEL, Player(), 0, 0); N(N_SET_PLAYER_WANTED_LEVEL_NOW, Player(), 0);
}
static void SvStartWave(int wave) {
    const SvLoc &l = SV_LOCS[g_sv.loc];
    if (g_sv.phase == SVP_DOWN) {   // back in for the next wave
        N(N_DO_SCREEN_FADE_OUT, 400); Wait(450);
        N(N_REQUEST_COLLISION_AT_COORD, l.x, l.y, l.z);
        SvPlaceInArea();
        for (int i = 0; i < 40 && !NB(N<int>(N_HAS_COLLISION_LOADED_AROUND_ENTITY, Me())); ++i) Wait(50);
        N(N_DO_SCREEN_FADE_IN, 500);
    }
    SvClearEnemies();
    SvLoadout();
    g_sv.wave = wave; g_sv.phase = SVP_WAVE; g_sv.waveAt = Now();
    g_sv.total = 8 + 2 * wave; g_sv.spawned = 0; g_sv.nextSpawn = 0; g_sv.heli = false; g_sv.hostClearedAt = 0;
    N(N_CLEAR_AREA_OF_PEDS, l.x, l.y, l.z, l.r + 40.f, 1);
    N(N_CLEAR_AREA_OF_COPS, l.x, l.y, l.z, l.r + 80.f, 0);
    HostileGroup();
    Shard("~r~WAVE " + std::to_string(wave), std::string(SV_LOCS[g_sv.loc].name) + " - survive!");
    Sound("CHECKPOINT_PERFECT", "HUD_MINI_GAME_SOUNDSET");
    SvReport();
    Log("survival: wave %d (%d enemies)", wave, g_sv.total);
}
static void SvBegin(int startsInMs) {
    g_sv.phase = SVP_COUNTDOWN;
    g_sv.startAt = Now() + std::max(1000, startsInMs);
    g_sv.back = Pos(Me()); g_sv.backH = N<float>(N_GET_ENTITY_HEADING, Me());
    g_sv.given.clear();
    g_sv.wave = 0; g_sv.cleared = 0; g_sv.kills = 0; g_sv.lastV = g_sv.lastK = -1;
    JobSkyUp();
    const SvLoc &l = SV_LOCS[g_sv.loc];
    N(N_REQUEST_COLLISION_AT_COORD, l.x, l.y, l.z);
    SvPlaceInArea();
    for (int i = 0; i < 40 && !NB(N<int>(N_HAS_COLLISION_LOADED_AROUND_ENTITY, Me())); ++i) Wait(50);
    SvLoadout();
    N(N_FREEZE_ENTITY_POSITION, Me(), 1);
    g_sv.countdownMovie = N<int>(N_REQUEST_SCALEFORM_MOVIE, "COUNTDOWN");
    g_sv.lastCount = -1;
    JobSkyDown();
    if (g_sv.solo) g_sv.startAt = std::max(g_sv.startAt, Now() + 3200);   // alone: the countdown once you have landed
    Log("survival: %s, starts in %d ms%s", l.name, startsInMs, g_sv.solo ? " (solo)" : "");
}
static void SvCleanup() {
    int me = Me();
    SvClearEnemies();
    N(N_FREEZE_ENTITY_POSITION, me, 0);
    for (uint32_t w : g_sv.given) N(N_REMOVE_WEAPON_FROM_PED, me, w);
    if (g_sv.countdownMovie) N(N_SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED, &g_sv.countdownMovie);
    Vec3 b = g_sv.back; float h = g_sv.backH;
    g_sv = SvRun();
    JobSkyUp();
    Teleport(b.x, b.y, b.z, h);
    JobSkyDown();
}
static void SvFinish() {   // over for everyone: the results, RP (and the cash when solo: the server pays online)
    if (g_sv.phase == SVP_RESULTS) return;
    SvClearEnemies();
    int waves = g_sv.cleared;
    g_sv.phase = SVP_RESULTS; g_sv.resultsUntil = Now() + 12000 + 9000;
    long long cash = g_sv.solo && waves ? (long long)g_sv.payout * waves / SV_WAVES : 0;
    if (cash) AddCash((int)cash);
    int rpBefore = g_rp, rp = 150 * waves + 10 * g_sv.kills;
    if (rp) GiveRP(rp, nullptr);
    JobCelebrate("Survival - " + std::string(SV_LOCS[g_sv.loc].name), 0, waves >= SV_WAVES, g_jobStartedAt ? Now() - g_jobStartedAt : 0, rpBefore, rp, cash,
                 std::to_string(waves) + "/" + std::to_string(SV_WAVES) + " waves, " + std::to_string(g_sv.kills) + " kills");
    Save();
}

// ---- host / join
static void SvHost(int loc, bool solo) {
    if (g_mActive || g_race.phase != RP_NONE || g_dm.phase != DP_NONE || g_sv.phase != SVP_NONE) { Notify("~r~Finish your race, deathmatch or job first."); return; }
    if (!solo && (!LO().jobOpenKind || !CoopAvailable())) solo = true;
    const SvLoc &l = SV_LOCS[loc];
    g_sv = SvRun();
    g_sv.loc = loc; g_sv.host = true; g_sv.solo = solo;
    if (solo) { SvBegin(4000); return; }
    if (!LO().jobOpenKind((std::string("Survival - ") + l.name).c_str(), l.x, l.y, l.z, SV_PAYOUT, 3)) { Notify("~r~Could not open the survival."); g_sv = SvRun(); return; }
    g_sv.phase = SVP_LOBBY;
    Wait(300);
    if (LO().jobUpdate) LO().jobUpdate(COOP_LOBBY, 0, l.x, l.y, l.z, SvText(0).c_str());
    Notify("Survival lobby open: other players join from ~b~Jobs~s~. Start it from the ~b~Survival~s~ menu.");
}
static void SvHostStart() {
    if (!g_sv.host || g_sv.phase != SVP_LOBBY || !LO().jobUpdate) return;
    const SvLoc &l = SV_LOCS[g_sv.loc];
    LO().jobUpdate(COOP_RUNNING, 1, l.x, l.y, l.z, SvText(1).c_str());
    g_sv.phase = SVP_NONE;   // SvFollowJob picks up the start time
}
static int SvCoopWave() {   // the wave the host has announced ("S|loc|wave")
    size_t a = g_coop.text.rfind('|');
    return g_coop.text.compare(0, 2, "S|") || a == std::string::npos ? 0 : std::atoi(g_coop.text.c_str() + a + 1);
}
static void SvFollowJob() {
    if (g_sv.phase != SVP_NONE || !g_coop.id || g_coop.kind != 3) return;
    if (g_coop.status != COOP_RUNNING || g_coop.startsIn <= 0 || g_coop.text.compare(0, 2, "S|")) return;
    g_sv = SvRun();
    g_sv.host = g_coop.hosting || g_coop.host == "you";
    g_sv.loc = std::max(0, std::min(NSV_LOCS - 1, std::atoi(g_coop.text.c_str() + 2)));
    g_sv.payout = g_coop.payout ? g_coop.payout : SV_PAYOUT;
    SvBegin((int)g_coop.startsIn);
}
static void SvHostAnnounce(int wave) {
    const SvLoc &l = SV_LOCS[g_sv.loc];
    if (LO().jobUpdate) LO().jobUpdate(COOP_RUNNING, wave, l.x, l.y, l.z, SvText(wave).c_str());
}
static void SvHostEnd() {
    if (g_sv.ended) return;
    g_sv.ended = true;
    const SvLoc &l = SV_LOCS[g_sv.loc];
    if (!g_sv.solo && LO().jobUpdate) LO().jobUpdate(COOP_PASSED, g_sv.cleared, l.x, l.y, l.z, "");
}
// the host's referee: the next wave once everyone has cleared this one (or is down), the end when all are down
static void SvHostReferee() {
    if (!g_sv.host || g_sv.ended || g_sv.wave == 0) return;
    std::vector<SvScore> sc = SvScores();
    if (sc.empty()) return;
    bool allDown = true, allDone = true;
    for (auto &s : sc) {
        if (!s.down) allDown = false;
        if (!s.down && s.waves < g_sv.wave) allDone = false;
    }
    if (allDown) { SvHostEnd(); return; }
    if (!allDone) {   // a straggler: give them two minutes after the host has cleared
        if (!(g_sv.phase == SVP_BREAK && g_sv.hostClearedAt && Now() - g_sv.hostClearedAt > 120000)) return;
    }
    if (g_sv.phase != SVP_BREAK && g_sv.phase != SVP_DOWN) return;
    if (g_sv.wave >= SV_WAVES) { SvHostEnd(); return; }
    if (!g_sv.breakUntil) g_sv.breakUntil = Now() + SV_BREAK_MS;
    if (Now() >= g_sv.breakUntil) { g_sv.breakUntil = 0; SvHostAnnounce(g_sv.wave + 1); }
}

// ---- HUD
static void SvHud() {
    int row = 0;
    if (g_sv.phase == SVP_BREAK && g_sv.breakUntil) RaceBar(row++, "NEXT WAVE", RaceTime(std::max(0, g_sv.breakUntil - Now())).substr(0, 5));
    int left = std::max(0, g_sv.total - g_sv.spawned) + (int)g_sv.peds.size();
    if (g_sv.phase == SVP_WAVE) RaceBar(row++, "ENEMIES", std::to_string(left));
    RaceBar(row++, "KILLS", std::to_string(g_sv.kills));
    if (!g_sv.solo) {
        std::vector<SvScore> sc = SvScores();
        int alive = 0;
        for (auto &s : sc) if (!s.down) ++alive;
        RaceBar(row++, "PLAYERS", std::to_string(alive) + "/" + std::to_string(std::max<size_t>(1, sc.size())));
    }
    RaceBar(row++, "WAVE", std::to_string(std::max(1, g_sv.wave)) + "/" + std::to_string(SV_WAVES), true);
}
static void SvResults() {
    std::vector<SvScore> sc = SvScores();
    float x = 0.5f, y = 0.25f, w = 0.42f;
    Rect(x, y, w, 0.05f, 30, 30, 30, 230);
    Text(std::string("Survival: ") + SV_LOCS[g_sv.loc].name, x, y - 0.018f, 0.5f, 1, 255, 255, 255, 255, 0);
    for (size_t i = 0; i < sc.size(); ++i) {
        float ry = y + 0.05f + 0.038f * i;
        Rect(x, ry, w, 0.036f, sc[i].me ? 60 : 0, sc[i].me ? 110 : 0, sc[i].me ? 170 : 0, 190);
        Text(sc[i].name == "you" ? "You" : sc[i].name, x - w / 2 + 0.01f, ry - 0.014f, 0.35f, 4, 255, 255, 255, 255, 1);
        int waves = std::min(sc[i].waves, SV_WAVES);
        std::string right = std::to_string(waves) + " waves  " + std::to_string(sc[i].kills) + " kills   " + Money((long long)g_sv.payout * waves / SV_WAVES);
        Text(right, x + w / 2 - 0.01f, ry - 0.014f, 0.35f, 4, 255, 255, 255, 255, 2, x + w / 2 - 0.01f);
    }
}

// ---- each frame
static void SvWaveTick() {
    const SvLoc &l = SV_LOCS[g_sv.loc];
    int me = Me();
    // the dead: kills (ours when we were the source), blips off; stragglers far away or stuck for long are dropped
    for (size_t i = 0; i < g_sv.peds.size();) {
        int p = g_sv.peds[i];
        bool dead = !NB(N<int>(N_DOES_ENTITY_EXIST, p)) || Dead(p);
        bool lost = !dead && (Dist(Pos(p), V3(l.x, l.y, l.z)) > l.r + 250.f || Now() - g_sv.pedBorn[i] > 240000);
        if (dead || lost) {
            if (dead && N<int>(N_GET_PED_SOURCE_OF_DEATH, p) == me) ++g_sv.kills;
            RemoveBlip(g_sv.blips[i]);
            N(N_SET_PED_AS_NO_LONGER_NEEDED, &g_sv.peds[i]);
            if (lost) DeleteEntity(p);
            g_sv.peds.erase(g_sv.peds.begin() + i); g_sv.blips.erase(g_sv.blips.begin() + i); g_sv.pedBorn.erase(g_sv.pedBorn.begin() + i);
            SvReport();
        } else ++i;
    }
    // spawn in trickles: up to 6 + wave/2 alive at a time; cars from wave 3, the helicopter on waves 5 and 10
    int maxAlive = std::min(12, 6 + g_sv.wave / 2);
    if (g_sv.spawned < g_sv.total && (int)g_sv.peds.size() < maxAlive && Now() >= g_sv.nextSpawn) {
        if ((g_sv.wave == 5 || g_sv.wave == 10) && !g_sv.heli && g_sv.spawned >= g_sv.total / 2) SvSpawnHeli();
        else if (g_sv.wave >= 3 && g_sv.spawned > 0 && Rand(3) == 0 && g_sv.total - g_sv.spawned >= 3) SvSpawnCar();
        else SvSpawnFoot(std::min(3, std::min(maxAlive - (int)g_sv.peds.size(), g_sv.total - g_sv.spawned)));
        g_sv.nextSpawn = Now() + 2500;
    }
    if (g_sv.spawned >= g_sv.total && g_sv.peds.empty()) {   // wave cleared
        g_sv.cleared = g_sv.wave;
        g_sv.phase = SVP_BREAK; g_sv.hostClearedAt = Now(); g_sv.breakUntil = 0;
        for (int &c : g_sv.cars) N(N_SET_VEHICLE_AS_NO_LONGER_NEEDED, &c);
        g_sv.cars.clear();
        SvLoadout();
        Shard("~g~WAVE " + std::to_string(g_sv.wave) + " SURVIVED", g_sv.wave >= SV_WAVES ? "" : (g_sv.solo ? "Next wave in 15 seconds." : "Waiting for the other players."));
        Sound("CHECKPOINT_PERFECT", "HUD_MINI_GAME_SOUNDSET");
        SvReport();
    }
}
static void SvTick() {
    SvFollowJob();
    if (g_sv.phase == SVP_NONE || g_sv.phase == SVP_LOBBY) return;
    int me = Me();
    if (!g_sv.solo && (!g_coop.id || g_coop.kind != 3 || g_coop.status == COOP_CLOSED) && g_sv.phase != SVP_RESULTS) {
        if (g_sv.wave) SvFinish(); else { Notify("The survival was cancelled."); SvCleanup(); }
        return;
    }
    if (g_sv.phase == SVP_COUNTDOWN) {
        int left = g_sv.startAt - Now();
        int n = left > 3000 ? -1 : left > 0 ? left / 1000 + 1 : 0;
        if (n != g_sv.lastCount && n >= 0 && NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, g_sv.countdownMovie))) {
            g_sv.lastCount = n;
            N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, g_sv.countdownMovie, "SET_MESSAGE");
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_TEXTURE_NAME_STRING, n ? std::to_string(n).c_str() : "GO");
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, n ? 240 : 114); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, n ? 200 : 204);
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, n ? 80 : 114); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, 1);
            N(N_END_SCALEFORM_MOVIE_METHOD);
            Sound(n ? "3_2_1" : "GO", "HUD_MINI_GAME_SOUNDSET");
        }
        if (g_sv.lastCount >= 0) N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, g_sv.countdownMovie, 255, 255, 255, 255, 0);
        if (left <= 0) { N(N_FREEZE_ENTITY_POSITION, me, 0); SvStartWave(1); }
        return;
    }
    if (g_sv.phase == SVP_RESULTS) {
        if (!JobCelebrating()) SvResults();
        if (Now() > g_sv.resultsUntil) SvCleanup();
        return;
    }
    // over for everyone (the host has passed the job)?
    if (!g_sv.solo && g_coop.status >= COOP_PASSED) { SvFinish(); return; }
    if (Now() - g_sv.waveAt < 1500 && g_sv.wave == 1) N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, g_sv.countdownMovie, 255, 255, 255, 255, 0);
    N(N_SET_PLAYER_WANTED_LEVEL, Player(), 0, 0); N(N_SET_PLAYER_WANTED_LEVEL_NOW, Player(), 0);
    if (g_sv.phase == SVP_WAVE || g_sv.phase == SVP_BREAK) {
        if (Dead(me)) {   // down until the next wave (solo: over)
            SvClearEnemies();
            g_sv.phase = SVP_DOWN;
            SvReport();
            if (g_sv.solo) { SvFinish(); return; }
            Notify("~r~You're down.~s~ You'll be back for the next wave if your crew survives it.");
        } else {
            const SvLoc &l = SV_LOCS[g_sv.loc];
            float d = std::hypot(Pos(me).x - l.x, Pos(me).y - l.y);
            if (d > l.r * 1.4f) {
                if (!g_sv.outside) g_sv.outside = Now();
                int left = 10 - (Now() - g_sv.outside) / 1000;
                Text("Get back to the survival area: " + std::to_string(std::max(0, left)), 0.5f, 0.85f, 0.5f, 4, 255, 80, 80, 255, 0, 1.f, true);
                if (left <= 0) { g_sv.outside = 0; SvPlaceInArea(); }
            } else g_sv.outside = 0;
        }
    }
    if (g_sv.phase == SVP_WAVE) SvWaveTick();
    if (g_sv.phase == SVP_DOWN && !Dead(me)) Text("You're down: back in for the next wave.", 0.5f, 0.85f, 0.45f, 4, 255, 255, 255, 255, 0, 1.f, true);
    if (g_sv.solo && g_sv.phase == SVP_BREAK) {
        if (g_sv.wave >= SV_WAVES) { SvFinish(); return; }
        if (!g_sv.breakUntil) g_sv.breakUntil = Now() + SV_BREAK_MS;
        if (Now() >= g_sv.breakUntil) { g_sv.breakUntil = 0; SvStartWave(g_sv.wave + 1); return; }
    }
    if (!g_sv.solo) {
        SvHostReferee();
        int w = SvCoopWave();
        if (w > g_sv.wave && (g_sv.phase == SVP_BREAK || g_sv.phase == SVP_DOWN) && !Dead(me)) { SvStartWave(w); return; }
        if (g_sv.phase == SVP_BREAK && !g_sv.host) g_sv.breakUntil = 0;
    }
    SvHud();
}
static bool SvMenu(const Frame &f, std::string &title, std::string &sub, std::vector<Item> &items) {
    switch ((int)f.id) {
    case SV_LIST: {
        title = "Survival"; sub = "LOCATIONS";
        if (g_sv.host && g_sv.phase == SVP_LOBBY) {
            int n = g_coop.members.empty() ? 1 : 2;
            for (char c : g_coop.members) if (c == ',') ++n;
            items.push_back({"Start Survival", std::to_string(n) + (n == 1 ? " player" : " players"), "Everyone in the lobby starts together.", [] { CloseMenu(); SvHostStart(); }});
            items.push_back({"Cancel Survival", "", "", [] { if (LO().jobUpdate) LO().jobUpdate(COOP_CLOSED, 0, 0, 0, 0, ""); g_sv = SvRun(); CloseMenu(); }});
            return true;
        }
        if (g_sv.phase != SVP_NONE) {
            items.push_back({"Quit Survival", "", "Leave now: you keep what the waves you survived pay.", [] {
                CloseMenu();
                if (g_sv.host) SvHostEnd(); else if (!g_sv.solo && LO().jobJoin) LO().jobJoin(0);   // 0: leave
                SvFinish();
            }});
            return true;
        }
        for (int i = 0; i < NSV_LOCS; ++i) items.push_back({SV_LOCS[i].name, ">", "Ten waves of enemies. Paid by the waves you survive.", [i] { Open((MenuId)SV_LOC, i); }});
        return true;
    }
    case SV_LOC: {
        int l = f.param;
        title = SV_LOCS[l].name; sub = "SURVIVAL";
        items.push_back({"Play", Money(SV_PAYOUT) + " for 10 waves", "The survival's lobby: alone or with the other players.", [l] { CloseMenu(); OpenSvCorona(l); }});
        items.push_back({"Set GPS", "", "", [l] { N(N_SET_NEW_WAYPOINT, SV_LOCS[l].x, SV_LOCS[l].y); CloseMenu(); }});
        return true;
    }
    }
    return false;
}
