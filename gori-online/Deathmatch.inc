// ---------------------------------------------------------------- Deathmatch (GTA Local Online, job kind 2)
// GTA Online-style deathmatches between the players: the host picks an arena, a time limit and the weapons,
// the others join from Jobs while it is in its lobby or countdown, everyone starts at the server's start time
// and respawns inside the arena; the server counts the kills from the Death messages (LocalOnline's PvP) and
// pays everyone by position when the host ends it. Standings come through LocalOnline_GetRaceStandings
// ("name\tkills\tdeaths\tme").
enum { DM_LIST = 250, DM_ARENA };
struct DmArena { const char *name; float x, y, z, r; };
static const DmArena DM_ARENAS[] = {
    {"Vinewood Bowl", 683.f, 570.f, 129.5f, 60.f},   // was the Pillbox building site (our apartment complex now) {"Elysian Island Docks", 1000.f, -3060.f, 5.9f, 80.f},
    {"Legion Square", 195.f, -934.f, 30.7f, 50.f}, {"Sandy Shores Airfield", 1720.f, 3270.f, 41.1f, 90.f},
    {"Grapeseed Farm", 2440.f, 4975.f, 46.f, 60.f}, {"Paleto Sawmill", -570.f, 5300.f, 70.2f, 70.f},
    {"Vespucci Beach", -1600.f, -1100.f, 4.3f, 70.f}, {"Cypress Flats", 850.f, -2100.f, 30.f, 60.f},
};
static const int NDM_ARENAS = (int)(sizeof DM_ARENAS / sizeof *DM_ARENAS);
static const char *DM_LOADOUT_NAMES[3] = {"Pistols", "Rifles", "Heavy"};
static const std::vector<const char *> DM_LOADOUTS[3] = {
    {"WEAPON_PISTOL", "WEAPON_COMBATPISTOL", "WEAPON_PISTOL50"},
    {"WEAPON_PISTOL", "WEAPON_PUMPSHOTGUN", "WEAPON_CARBINERIFLE", "WEAPON_ASSAULTRIFLE"},
    {"WEAPON_PISTOL", "WEAPON_CARBINERIFLE", "WEAPON_COMBATMG", "WEAPON_GRENADE", "WEAPON_RPG"},
};
enum DmPhase { DP_NONE, DP_LOBBY, DP_COUNTDOWN, DP_PLAYING, DP_RESULTS };
struct DmRun {
    int phase = DP_NONE, arena = 0, minutes = 10, loadout = 1, startAt = 0, endAt = 0, resultsUntil = 0, outside = 0, payout = 0;
    bool host = false, wasDead = false, ended = false;
    bool voting = false;   // the next job vote is up (NextJob.inc): it cleans the deathmatch up
    std::vector<uint32_t> given;   // weapons we gave that the player did not own
    Vec3 back{}; float backH = 0.f;
    int countdownMovie = 0, lastCount = -1;
};
static DmRun g_dm;
static int g_dmSelMin = 10, g_dmSelLoad = 1;

struct DmScore { std::string name; int kills = 0, deaths = 0; bool me = false; };
static std::vector<DmScore> DmScores() {
    std::vector<DmScore> out;
    if (!LO().getRaceStandings) return out;
    std::string t = CoopFetch(LO().getRaceStandings);
    size_t at = 0;
    while (at < t.size()) {
        size_t nl = t.find('\n', at);
        std::vector<std::string> f = SplitTabs(t.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
        at = nl == std::string::npos ? t.size() : nl + 1;
        if (f.size() >= 4) out.push_back({f[0], std::atoi(f[1].c_str()), std::atoi(f[2].c_str()), f[3] == "1"});
    }
    return out;
}
static Vec3 DmSpawnPoint(int arena) {
    const DmArena &a = DM_ARENAS[arena];
    for (int tries = 0; tries < 8; ++tries) {
        float ang = (float)Rand(6283) / 1000.f, r = a.r * (0.3f + (float)Rand(60) / 100.f);
        float x = a.x + std::cos(ang) * r, y = a.y + std::sin(ang) * r;
        Vec3 out{};
        if (NB(N<int>(N_GET_SAFE_COORD_FOR_PED, x, y, a.z + 5.f, 1, &out, 16)) && Dist(out, V3(a.x, a.y, a.z)) < a.r) return out;
        float gz = GroundAt(x, y, a.z + 40.f, -1000.f);
        if (gz > -999.f) return V3(x, y, gz + 1.f);
    }
    return V3(a.x, a.y, a.z + 1.f);
}
static void DmLoadout() {
    int me = Me();
    for (const char *w : DM_LOADOUTS[g_dm.loadout]) {
        uint32_t h = Joaat(w);
        if (!NB(N<int>(N_HAS_PED_GOT_WEAPON, me, h, 0)) && std::find(g_dm.given.begin(), g_dm.given.end(), h) == g_dm.given.end()) g_dm.given.push_back(h);
        N(N_GIVE_WEAPON_TO_PED, me, h, 9999, 0, 1);
    }
    N(N_SET_ENTITY_HEALTH, me, N<int>(N_GET_ENTITY_MAX_HEALTH, me), 0, 0);
    N(N_SET_PED_ARMOUR, me, 100);
}
static void DmPlaceInArena() {
    Vec3 p = DmSpawnPoint(g_dm.arena);
    int me = Me();
    if (InAnyVehicle()) N(N_TASK_LEAVE_VEHICLE, me, MyVehicle(), 16);
    N(N_SET_ENTITY_COORDS, me, p.x, p.y, p.z, 0, 0, 0, 0);
    N(N_SET_ENTITY_HEADING, me, (float)Rand(360));
    N(N_SET_PLAYER_WANTED_LEVEL, Player(), 0, 0); N(N_SET_PLAYER_WANTED_LEVEL_NOW, Player(), 0);
}
static void DmBegin(int startsInMs) {
    g_dm.phase = DP_COUNTDOWN;
    g_dm.startAt = Now() + std::max(1000, startsInMs);
    g_dm.endAt = g_dm.startAt + g_dm.minutes * 60000;
    g_dm.back = Pos(Me()); g_dm.backH = N<float>(N_GET_ENTITY_HEADING, Me());
    g_dm.given.clear();
    JobSkyUp();
    const DmArena &a = DM_ARENAS[g_dm.arena];
    N(N_REQUEST_COLLISION_AT_COORD, a.x, a.y, a.z);
    DmPlaceInArena();
    for (int i = 0; i < 40 && !NB(N<int>(N_HAS_COLLISION_LOADED_AROUND_ENTITY, Me())); ++i) Wait(50);
    DmLoadout();
    N(N_FREEZE_ENTITY_POSITION, Me(), 1);
    g_dm.countdownMovie = N<int>(N_REQUEST_SCALEFORM_MOVIE, "COUNTDOWN");
    g_dm.lastCount = -1;
    JobSkyDown();
    Log("dm: %s, %d min, %s, starts in %d ms", a.name, g_dm.minutes, DM_LOADOUT_NAMES[g_dm.loadout], startsInMs);
}
static void DmCleanup() {
    int me = Me();
    N(N_FREEZE_ENTITY_POSITION, me, 0);
    for (uint32_t w : g_dm.given) N(N_REMOVE_WEAPON_FROM_PED, me, w);
    if (g_dm.countdownMovie) N(N_SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED, &g_dm.countdownMovie);
    Vec3 b = g_dm.back; float h = g_dm.backH;
    g_dm = DmRun();
    JobSkyUp();
    Teleport(b.x, b.y, b.z, h);
    JobSkyDown();
}
static void DmHost(int arena, int minutes, int loadout) {
    if (g_mActive || g_race.phase != RP_NONE) { Notify("~r~Finish your race or job first."); return; }
    if (!LO().jobOpenKind || !CoopAvailable()) { Notify("~r~Deathmatches need GTA Local Online."); return; }
    const DmArena &a = DM_ARENAS[arena];
    int payout = 10000 * minutes / 5;
    if (!LO().jobOpenKind(a.name, a.x, a.y, a.z, payout, 2)) { Notify("~r~Could not open the deathmatch."); return; }
    g_dm = DmRun();
    g_dm.phase = DP_LOBBY; g_dm.host = true; g_dm.arena = arena; g_dm.minutes = minutes; g_dm.loadout = loadout; g_dm.payout = payout;
    std::string cfg = "D|" + std::to_string(arena) + "|" + std::to_string(minutes) + "|" + std::to_string(loadout);
    Wait(300);
    if (LO().jobUpdate) LO().jobUpdate(COOP_LOBBY, 0, a.x, a.y, a.z, cfg.c_str());
    Notify("Deathmatch lobby open: other players join from ~b~Jobs~s~. Start it from the ~b~Deathmatch~s~ menu.");
}
static void DmHostStart() {
    if (!g_dm.host || g_dm.phase != DP_LOBBY || !LO().jobUpdate) return;
    const DmArena &a = DM_ARENAS[g_dm.arena];
    std::string cfg = "D|" + std::to_string(g_dm.arena) + "|" + std::to_string(g_dm.minutes) + "|" + std::to_string(g_dm.loadout);
    LO().jobUpdate(COOP_RUNNING, 0, a.x, a.y, a.z, cfg.c_str());
    g_dm.phase = DP_NONE;   // DmFollowJob picks up the start time
}
static void DmFollowJob() {
    if (g_dm.phase != DP_NONE || !g_coop.id || g_coop.kind != 2) return;
    if (g_coop.status != COOP_RUNNING || g_coop.startsIn <= 0 || g_coop.text.compare(0, 2, "D|")) return;
    std::vector<std::string> f;
    std::string cur;
    for (char c : g_coop.text + "|") { if (c == '|') { f.push_back(cur); cur.clear(); } else cur += c; }
    if (f.size() < 4) return;
    bool host = g_coop.hosting || g_coop.host == "you";
    g_dm.host = host;
    g_dm.arena = std::max(0, std::min(NDM_ARENAS - 1, std::atoi(f[1].c_str())));
    g_dm.minutes = std::max(1, std::atoi(f[2].c_str()));
    g_dm.loadout = std::max(0, std::min(2, std::atoi(f[3].c_str())));
    g_dm.payout = g_coop.payout;
    DmBegin((int)g_coop.startsIn);
}
static void DmHud() {
    std::vector<DmScore> sc = DmScores();
    int pos = 1, kills = 0, deaths = 0;
    for (size_t i = 0; i < sc.size(); ++i) if (sc[i].me) { pos = (int)i + 1; kills = sc[i].kills; deaths = sc[i].deaths; }
    int left = std::max(0, g_dm.endAt - Now());
    int row = 0;
    RaceBar(row++, "TIME LEFT", RaceTime(left).substr(0, 5));
    RaceBar(row++, "DEATHS", std::to_string(deaths));
    RaceBar(row++, "KILLS", std::to_string(kills));
    RaceBar(row++, "POSITION", std::to_string(pos) + "/" + std::to_string(std::max<size_t>(1, sc.size())), true);
}
static void DmResults() {
    std::vector<DmScore> sc = DmScores();
    float x = 0.5f, y = 0.25f, w = 0.4f;
    Rect(x, y, w, 0.05f, 30, 30, 30, 230);
    Text(std::string("Deathmatch: ") + DM_ARENAS[g_dm.arena].name, x, y - 0.018f, 0.5f, 1, 255, 255, 255, 255, 0);
    static const double share[8] = {1.0, 0.75, 0.6, 0.5, 0.4, 0.35, 0.3, 0.25};
    for (size_t i = 0; i < sc.size(); ++i) {
        float ry = y + 0.05f + 0.038f * i;
        Rect(x, ry, w, 0.036f, sc[i].me ? 60 : 0, sc[i].me ? 110 : 0, sc[i].me ? 170 : 0, 190);
        Text(Ordinal((int)i + 1), x - w / 2 + 0.01f, ry - 0.014f, 0.35f, 4, 255, 255, 255, 255, 1);
        Text(sc[i].name == "you" ? "You" : sc[i].name, x - w / 2 + 0.06f, ry - 0.014f, 0.35f, 4, 255, 255, 255, 255, 1);
        std::string right = std::to_string(sc[i].kills) + " K  " + std::to_string(sc[i].deaths) + " D   " +
                            ((sc[i].kills || sc[i].deaths) ? Money((long long)(g_dm.payout * share[i < 8 ? i : 7])) : std::string("-"));
        Text(right, x + w / 2 - 0.01f, ry - 0.014f, 0.35f, 4, 255, 255, 255, 255, 2, x + w / 2 - 0.01f);
    }
}
static void DmTick() {
    DmFollowJob();
    if (g_dm.phase == DP_NONE || g_dm.phase == DP_LOBBY) return;
    int me = Me();
    if ((!g_coop.id || g_coop.kind != 2 || g_coop.status == COOP_CLOSED) && g_dm.phase != DP_RESULTS) { Notify("The deathmatch was cancelled."); DmCleanup(); return; }
    if (g_dm.phase == DP_COUNTDOWN) {
        int left = g_dm.startAt - Now();
        int n = left > 3000 ? -1 : left > 0 ? left / 1000 + 1 : 0;
        if (n != g_dm.lastCount && n >= 0 && NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, g_dm.countdownMovie))) {
            g_dm.lastCount = n;
            N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, g_dm.countdownMovie, "SET_MESSAGE");
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_TEXTURE_NAME_STRING, n ? std::to_string(n).c_str() : "GO");
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, n ? 240 : 114); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, n ? 200 : 204);
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, n ? 80 : 114); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, 1);
            N(N_END_SCALEFORM_MOVIE_METHOD);
            Sound(n ? "3_2_1" : "GO", "HUD_MINI_GAME_SOUNDSET");
        }
        if (g_dm.lastCount >= 0) N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, g_dm.countdownMovie, 255, 255, 255, 255, 0);
        DmHud();
        if (left <= 0) { N(N_FREEZE_ENTITY_POSITION, me, 0); g_dm.phase = DP_PLAYING; }
        return;
    }
    if (g_dm.phase == DP_PLAYING) {
        if (Now() - g_dm.startAt < 1500) N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, g_dm.countdownMovie, 255, 255, 255, 255, 0);
        // back into the arena after each death (LocalOnline respawns YOU at a hospital)
        bool dead = Dead(me);
        if (dead) g_dm.wasDead = true;
        else if (g_dm.wasDead) { g_dm.wasDead = false; Wait(1500); DmPlaceInArena(); DmLoadout(); }
        const DmArena &a = DM_ARENAS[g_dm.arena];
        float d = std::hypot(Pos(me).x - a.x, Pos(me).y - a.y);
        if (!dead && d > a.r * 1.3f) {
            if (!g_dm.outside) g_dm.outside = Now();
            int left = 10 - (Now() - g_dm.outside) / 1000;
            Text("Get back to the arena: " + std::to_string(std::max(0, left)), 0.5f, 0.85f, 0.5f, 4, 255, 80, 80, 255, 0, 1.f, true);
            if (left <= 0) { g_dm.outside = 0; DmPlaceInArena(); }
        } else g_dm.outside = 0;
        DmHud();
        if (Now() >= g_dm.endAt) {
            if (g_dm.host && LO().jobUpdate && !g_dm.ended) { g_dm.ended = true; LO().jobUpdate(COOP_PASSED, 0, a.x, a.y, a.z, ""); }
            if (g_dm.host || g_coop.status >= COOP_PASSED || Now() > g_dm.endAt + 15000) {
                g_dm.phase = DP_RESULTS; g_dm.resultsUntil = Now() + 10000;
                std::vector<DmScore> sc = DmScores();
                int pos = 1, kills = 0;
                for (size_t i = 0; i < sc.size(); ++i) if (sc[i].me) { pos = (int)i + 1; kills = sc[i].kills; }
                int rpBefore = g_rp, rp = std::max(100, 1000 / pos + 50 * kills);
                GiveRP(rp, nullptr);
                JobCelebrate(DM_ARENAS[g_dm.arena].name, pos, true, g_dm.minutes * 60000, rpBefore, rp, 0, "");   // online: the server pays
                g_dm.resultsUntil += 9000;
            }
        }
        return;
    }
    if (g_dm.phase == DP_RESULTS) {
        if (!JobCelebrating() && !NjActive()) DmResults();
        if (Now() > g_dm.resultsUntil && !g_dm.voting) { g_dm.voting = true; NextJobDeathmatch(g_dm.arena, g_dm.host); }   // then Online's vote on the next job
    }
}
static bool DmMenu(const Frame &f, std::string &title, std::string &sub, std::vector<Item> &items) {
    switch ((int)f.id) {
    case DM_LIST: {
        title = "Deathmatch"; sub = "ARENAS";
        if (g_dm.host && g_dm.phase == DP_LOBBY) {
            int n = g_coop.members.empty() ? 1 : 2;
            for (char c : g_coop.members) if (c == ',') ++n;
            items.push_back({"Start Deathmatch", std::to_string(n) + (n == 1 ? " player" : " players"), "Everyone in the lobby starts together.", [] { CloseMenu(); DmHostStart(); }});
            items.push_back({"Cancel Deathmatch", "", "", [] { if (LO().jobUpdate) LO().jobUpdate(COOP_CLOSED, 0, 0, 0, 0, ""); g_dm = DmRun(); CloseMenu(); }});
            return true;
        }
        for (int i = 0; i < NDM_ARENAS; ++i) items.push_back({DM_ARENAS[i].name, ">", "Host a deathmatch here.", [i] { Open((MenuId)DM_ARENA, i); }});
        return true;
    }
    case DM_ARENA: {
        int a = f.param;
        title = DM_ARENAS[a].name; sub = "DEATHMATCH";
        items.push_back({"Time Limit", "< " + std::to_string(g_dmSelMin) + " min >", "", [] { g_dmSelMin = g_dmSelMin >= 15 ? 5 : g_dmSelMin + 5; }});
        items.push_back({"Weapons", std::string("< ") + DM_LOADOUT_NAMES[g_dmSelLoad] + " >", "Given to everyone (with full armor) at every spawn.", [] { g_dmSelLoad = (g_dmSelLoad + 1) % 3; }});
        items.push_back({"Play", Money(10000 * g_dmSelMin / 5) + " to the winner", "The deathmatch's lobby: the other players join from Jobs.", [a] { CloseMenu(); OpenDmCorona(a); }});
        items.push_back({"Set GPS", "", "", [a] { N(N_SET_NEW_WAYPOINT, DM_ARENAS[a].x, DM_ARENAS[a].y); CloseMenu(); }});
        return true;
    }
    }
    return false;
}
