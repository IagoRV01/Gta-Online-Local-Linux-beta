// ---------------------------------------------------------------- Arena War (mpchristmas2018)
// The arena (xs_x18_int_01 at 2800,-3800,100; IPL xs_arena_interior) is dressed the way fm_mission_controller does
// it: one scene set per theme (Set_Dystopian_Scene / Set_Scifi_Scene / Set_Wasteland_Scene), one of its maps
// (Set_Dystopian_01-17, Set_Scifi_01-10, Set_Wasteland_01-10), the crowd (Set_Crowd_A-D), a lighting set and the
// pit fence. Teleporting in does not put you in the interior's room, so the player, the car and the game camera are
// forced into it every frame (else only the sea outside renders).
// Carnage: vehicle combat in arena vehicles, against bots (solo) or the other players (a deathmatch job, kind 2,
// text "A|theme|map|light|minutes|vehicle"); kills, deaths, Arena Points, cash and RP. Apocalypse, Future Shock and
// Nightmare are the three variants of each arena vehicle.
enum { AW_MENU = 340 };
struct AwTheme { const char *name, *scene, *prefix; int maps; const char *variant; };
static const AwTheme AW_THEMES[] = {{"Apocalypse", "Set_Dystopian_Scene", "Set_Dystopian_", 17, "Apocalypse"},
                                    {"Sci-Fi", "Set_Scifi_Scene", "Set_Scifi_", 10, "Future Shock"},
                                    {"Consumer Wasteland", "Set_Wasteland_Scene", "Set_Wasteland_", 10, "Nightmare"}};
static const struct { const char *name, *set; } AW_LIGHTS[] = {{"Night", "Set_Lights_night"}, {"Evening", "Set_Lights_evening"},
    {"Midday", "Set_Lights_midday"}, {"Morning", "Set_Lights_morning"}, {"Storm", "Set_Lights_storm"}, {"Sandstorm", "Set_Lights_sandstorm"},
    {"Toxic", "Set_Lights_toxic"}, {"Hell", "Set_Lights_hell"}, {"Atlantis", "Set_Lights_atlantis"}, {"Saccharine", "Set_Lights_saccharine"}};
static const int NAW_LIGHTS = (int)(sizeof AW_LIGHTS / sizeof *AW_LIGHTS);
// each arena vehicle in its three variants (Apocalypse, Future Shock, Nightmare)
static const char *AW_VEHS[][4] = {
    {"Imperator", "imperator", "imperator2", "imperator3"}, {"Dominator", "dominator4", "dominator5", "dominator6"}, {"Issi", "issi4", "issi5", "issi6"},
    {"Slamvan", "slamvan4", "slamvan5", "slamvan6"}, {"Impaler", "impaler2", "impaler3", "impaler4"}, {"ZR380", "zr380", "zr3802", "zr3803"},
    {"Brutus", "brutus", "brutus2", "brutus3"}, {"Bruiser", "bruiser", "bruiser2", "bruiser3"}, {"Sasquatch", "monster3", "monster4", "monster5"},
    {"Cerberus", "cerberus", "cerberus2", "cerberus3"}, {"Deathbike", "deathbike", "deathbike2", "deathbike3"}, {"Scarab", "scarab", "scarab2", "scarab3"}};
static const int NAW_VEHS = (int)(sizeof AW_VEHS / sizeof *AW_VEHS);
static const uint32_t AW_ROOM = 0x113B4A40;   // the arena's room (GET_ROOM_KEY_FROM_ENTITY once inside)
static const Vec3 AW_CENTRE = V3(2800.f, -3800.f, 100.f);

enum AwPhase { AWP_NONE, AWP_LOBBY, AWP_COUNTDOWN, AWP_PLAYING, AWP_RESULTS };
struct AwBot { int veh = 0, ped = 0, blip = 0, respawnAt = 0, retaskAt = 0; };
struct AwRun {
    int phase = AWP_NONE, theme = 0, map = 0, light = 0, minutes = 5, veh = 0, bots = 4;
    bool host = false, solo = true, ended = false, wasDown = false;
    int startAt = 0, endAt = 0, resultsUntil = 0, payout = 0, interior = 0, myVeh = 0, downAt = 0;
    int kills = 0, deaths = 0, countdownMovie = 0, lastCount = -1;
    std::vector<AwBot> botList;
    Vec3 back{}; float backH = 0.f;
};
static AwRun g_aw;
static int g_awSel[6] = {0, 0, 0, 0, 2, 0};   // menu choices: theme, map, light, vehicle, bots (x2), minutes index
static const int AW_MINUTES[] = {5, 10, 15};

static std::string AwModel(int theme, int veh) { return AW_VEHS[veh][1 + theme]; }

// ---- the arena itself
static int AwLoadArena(int theme, int map, int light) {
    const InteriorDef *d = FindInterior("arena");
    if (!d) return 0;
    int id = LoadInterior(*d);
    if (!id) return 0;
    for (const AwTheme &t : AW_THEMES) {   // everything off, then the chosen dressing on
        N(N_DEACTIVATE_INTERIOR_ENTITY_SET, id, t.scene);
        for (int i = 1; i <= t.maps; ++i) { char nm[40]; std::snprintf(nm, sizeof nm, "%s%02d", t.prefix, i); N(N_DEACTIVATE_INTERIOR_ENTITY_SET, id, nm); }
    }
    for (auto &l : AW_LIGHTS) N(N_DEACTIVATE_INTERIOR_ENTITY_SET, id, l.set);
    for (const char *c : {"Set_Crowd_A", "Set_Crowd_B", "Set_Crowd_C", "Set_Crowd_D"}) { N(N_ACTIVATE_INTERIOR_ENTITY_SET, id, c); N(N_SET_INTERIOR_ENTITY_SET_TINT_INDEX, id, c, theme + 1); }
    const AwTheme &t = AW_THEMES[theme];
    char nm[40]; std::snprintf(nm, sizeof nm, "%s%02d", t.prefix, map + 1);
    N(N_ACTIVATE_INTERIOR_ENTITY_SET, id, t.scene);
    N(N_ACTIVATE_INTERIOR_ENTITY_SET, id, nm);
    N(N_ACTIVATE_INTERIOR_ENTITY_SET, id, AW_LIGHTS[light].set);
    N(N_ACTIVATE_INTERIOR_ENTITY_SET, id, "Set_Pit_Fence_Closed");
    N(N_REFRESH_INTERIOR, id);
    Log("arena: %s, map %s, lights %s (interior %d)", t.name, nm, AW_LIGHTS[light].set, id);
    return id;
}
static void AwForceRoom() {   // every frame in the arena: player, car and camera in its room
    if (!g_aw.interior) return;
    N(N_FORCE_ROOM_FOR_ENTITY, Me(), g_aw.interior, AW_ROOM);
    if (Exists(g_aw.myVeh)) N(N_FORCE_ROOM_FOR_ENTITY, g_aw.myVeh, g_aw.interior, AW_ROOM);
    for (auto &b : g_aw.botList) if (Exists(b.veh)) N(N_FORCE_ROOM_FOR_ENTITY, b.veh, g_aw.interior, AW_ROOM);
    N(N_FORCE_ROOM_FOR_GAME_VIEWPORT, g_aw.interior, AW_ROOM);
    N(N_SET_RADAR_AS_INTERIOR_THIS_FRAME, Joaat("xs_x18_int_01"), AW_CENTRE.x, AW_CENTRE.y, 0, 0);
    N(N_HIDE_MINIMAP_EXTERIOR_MAP_THIS_FRAME);
}
static Vec3 AwSpawnPoint(float &heading) {   // on the arena floor around the centre, level with it (not on a ramp), facing in
    float base = GroundAt(AW_CENTRE.x, AW_CENTRE.y, 130.f, 103.1f);
    for (int tries = 0; tries < 40; ++tries) {
        float a = (float)Rand(6283) / 1000.f, r = 10.f + (float)Rand(tries < 25 ? 22 : 38);
        float x = AW_CENTRE.x + std::cos(a) * r, y = AW_CENTRE.y + std::sin(a) * r;
        float gz = GroundAt(x, y, 130.f, -1000.f);
        if (gz < 95.f || gz > 125.f || (tries < 30 && std::fabs(gz - base) > 0.8f)) continue;
        bool clear = true;   // and not right on top of the slopes: the ground 3 m around is level too
        for (int k = 0; k < 4 && clear; ++k) { float g2 = GroundAt(x + (k == 0 ? 3.f : k == 1 ? -3.f : 0.f), y + (k == 2 ? 3.f : k == 3 ? -3.f : 0.f), 130.f, -1000.f); clear = std::fabs(g2 - gz) < 0.8f; }
        if (!clear && tries < 35) continue;
        heading = std::atan2(-(AW_CENTRE.x - x), AW_CENTRE.y - y) * 180.f / 3.14159265f;
        return V3(x, y, gz + 1.0f);
    }
    heading = 0.f;
    return V3(AW_CENTRE.x, AW_CENTRE.y, 104.5f);
}
static int AwCar(const std::string &model, Vec3 p, float h) {
    uint32_t m = Joaat(model.c_str());
    if (!LoadModel(m)) { m = Joaat("imperator"); if (!LoadModel(m)) return 0; }
    int v = N<int>(N_CREATE_VEHICLE, m, p.x, p.y, p.z, h, 0, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
    if (!v) return 0;
    N(N_SET_ENTITY_AS_MISSION_ENTITY, v, 1, 1);
    N(N_SET_VEHICLE_MOD_KIT, v, 0);
    N(N_FORCE_ROOM_FOR_ENTITY, v, g_aw.interior, AW_ROOM);
    N<int>(N_SET_VEHICLE_ON_GROUND_PROPERLY, v, 5.0f);
    return v;
}
static void AwPlacePlayer() {   // a fresh car on the floor; your own arena vehicle's mods if you own that model
    float h = 0.f;
    Vec3 p = AwSpawnPoint(h);
    if (Exists(g_aw.myVeh)) DeleteEntity(g_aw.myVeh);
    std::string model = AwModel(g_aw.theme, g_aw.veh);
    g_aw.myVeh = AwCar(model, p, h);
    for (auto &g : g_garage) if (g.model == model && !g.data.empty() && Exists(g_aw.myVeh)) { ApplyVehicle(g_aw.myVeh, g.data); break; }
    N(N_SET_ENTITY_COORDS, Me(), p.x, p.y, p.z + 1.f, 0, 0, 0, 0);
    N(N_FORCE_ROOM_FOR_ENTITY, Me(), g_aw.interior, AW_ROOM);
    if (Exists(g_aw.myVeh)) N(N_SET_PED_INTO_VEHICLE, Me(), g_aw.myVeh, -1);
    N(N_SET_ENTITY_HEALTH, Me(), N<int>(N_GET_ENTITY_MAX_HEALTH, Me()), 0, 0);
    N(N_SET_PLAYER_WANTED_LEVEL, Player(), 0, 0); N(N_SET_PLAYER_WANTED_LEVEL_NOW, Player(), 0);
}

// ---- bots (solo): arena vehicles driven at you
static void AwSpawnBot(AwBot &b) {
    HostileGroup();
    float h = 0.f;
    Vec3 p = AwSpawnPoint(h);
    b.veh = AwCar(AwModel(g_aw.theme, Rand(NAW_VEHS - 2)), p, h);   // no Deathbike / Scarab for bots
    if (!b.veh) return;
    uint32_t pm = Joaat("a_m_y_motox_01");
    if (!LoadModel(pm)) return;
    b.ped = N<int>(N_CREATE_PED_INSIDE_VEHICLE, b.veh, 4, pm, -1, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, pm);
    N(N_SET_ENTITY_AS_MISSION_ENTITY, b.ped, 1, 1);
    N(N_SET_PED_RELATIONSHIP_GROUP_HASH, b.ped, g_hostile);
    N(N_SET_PED_COMBAT_ATTRIBUTES, b.ped, 3, 0);   // never get out
    N(N_SET_DRIVER_ABILITY, b.ped, 1.0f); N(N_SET_DRIVER_AGGRESSIVENESS, b.ped, 1.0f);
    N(N_SET_PED_KEEP_TASK, b.ped, 1);
    b.blip = N<int>(N_ADD_BLIP_FOR_ENTITY, b.veh);
    N(N_SET_BLIP_COLOUR, b.blip, 1); N(N_SET_BLIP_SCALE, b.blip, 0.8f);
    b.retaskAt = 0; b.respawnAt = 0;
}
static void AwRemoveBot(AwBot &b) {
    RemoveBlip(b.blip);
    if (Exists(b.ped)) DeleteEntity(b.ped);
    if (Exists(b.veh)) DeleteEntity(b.veh);
    b = AwBot();
}
static void AwBotsTick() {
    int me = Me(), mine = g_aw.myVeh;
    for (auto &b : g_aw.botList) {
        if (b.respawnAt) { if (Now() >= b.respawnAt) AwSpawnBot(b); continue; }
        if (!Exists(b.veh)) { b.respawnAt = Now() + 4000; continue; }
        bool dead = Dead(b.veh) || !Exists(b.ped) || Dead(b.ped) || !NB(N<int>(N_IS_VEHICLE_DRIVEABLE, b.veh, 0));
        if (dead) {
            bool mineKill = NB(N<int>(N_HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY, b.veh, me, 1)) || (Exists(mine) && NB(N<int>(N_HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY, b.veh, mine, 1)))
                         || (Exists(b.ped) && N<int>(N_GET_PED_SOURCE_OF_DEATH, b.ped) == me);
            if (mineKill) { ++g_aw.kills; Sound("Kill_List_Score", "DLC_AW_Frontend_Sounds"); Notify("~r~Bot wrecked~s~  +10 AP"); }
            RemoveBlip(b.blip);
            if (Exists(b.veh)) N(N_EXPLODE_VEHICLE, b.veh, 1, 0);
            Wait(0);
            AwRemoveBot(b);
            b.respawnAt = Now() + 4000;
            continue;
        }
        if (Now() >= b.retaskAt) {   // after you, again and again
            N(N_TASK_VEHICLE_MISSION_PED_TARGET, b.ped, b.veh, me, 6, 32.f, 786469, 3.f, 1.f, 1);
            b.retaskAt = Now() + 5000;
        }
    }
}

// ---- start and end
static void AwBegin(int startsInMs) {
    g_aw.phase = AWP_COUNTDOWN;
    g_aw.startAt = Now() + std::max(2000, startsInMs);
    g_aw.endAt = g_aw.startAt + g_aw.minutes * 60000;
    g_aw.back = Pos(Me()); g_aw.backH = N<float>(N_GET_ENTITY_HEADING, Me());
    g_aw.kills = g_aw.deaths = 0;
    N(N_DO_SCREEN_FADE_OUT, 500); Wait(550);
    if (InAnyVehicle()) N(N_SET_ENTITY_COORDS, Me(), g_aw.back.x, g_aw.back.y, g_aw.back.z + 1.f, 0, 0, 0, 0);
    N(N_SET_ENTITY_COORDS, Me(), AW_CENTRE.x, AW_CENTRE.y, 110.f, 0, 0, 0, 0);
    N(N_FREEZE_ENTITY_POSITION, Me(), 1);
    g_aw.interior = AwLoadArena(g_aw.theme, g_aw.map, g_aw.light);
    N(N_REQUEST_COLLISION_AT_COORD, AW_CENTRE.x, AW_CENTRE.y, 104.f);
    for (int i = 0; i < 40; ++i) { AwForceRoom(); Wait(50); }
    N(N_FREEZE_ENTITY_POSITION, Me(), 0);
    AwPlacePlayer();
    if (Exists(g_aw.myVeh)) N(N_FREEZE_ENTITY_POSITION, g_aw.myVeh, 1);
    g_aw.botList.assign(g_aw.solo ? g_aw.bots : 0, AwBot());
    for (auto &b : g_aw.botList) { AwSpawnBot(b); if (Exists(b.veh)) N(N_FREEZE_ENTITY_POSITION, b.veh, 1); }
    g_aw.countdownMovie = N<int>(N_REQUEST_SCALEFORM_MOVIE, "COUNTDOWN");
    g_aw.lastCount = -1;
    N(N_DO_SCREEN_FADE_IN, 600);
    Shard("~o~ARENA WAR", std::string("Carnage - ") + AW_THEMES[g_aw.theme].name);
    Log("arena war: carnage, %s map %d, %s, %d min, %d bots, %s", AW_THEMES[g_aw.theme].name, g_aw.map + 1, AwModel(g_aw.theme, g_aw.veh).c_str(),
        g_aw.minutes, (int)g_aw.botList.size(), g_aw.solo ? "solo" : "online");
}
static void AwCleanup() {
    for (auto &b : g_aw.botList) AwRemoveBot(b);
    if (g_aw.countdownMovie) N(N_SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED, &g_aw.countdownMovie);
    N(N_DO_SCREEN_FADE_OUT, 500); Wait(550);
    if (InAnyVehicle()) N(N_SET_ENTITY_COORDS, Me(), g_aw.back.x, g_aw.back.y, g_aw.back.z + 1.f, 0, 0, 0, 0);
    if (Exists(g_aw.myVeh)) DeleteEntity(g_aw.myVeh);
    N(N_CLEAR_ROOM_FOR_ENTITY, Me());
    Vec3 b = g_aw.back; float h = g_aw.backH;
    g_aw = AwRun();
    Teleport(b.x, b.y, b.z, h);
    N(N_DO_SCREEN_FADE_IN, 500);
}
static int AwApGet() { return AcidGet("arena_ap"); }   // the Arena Points total (Gori's per-character custom store)
static void AwFinish() {
    if (g_aw.phase == AWP_RESULTS) return;
    g_aw.phase = AWP_RESULTS; g_aw.resultsUntil = Now() + 12000;
    int ap = 20 + 10 * g_aw.kills;
    AcidSet("arena_ap", AwApGet() + ap);
    if (g_aw.solo) { int cash = std::min(60000, 5000 + 1500 * g_aw.kills); AddCash(cash); Notify("~g~" + Money(cash) + "~s~ for the show."); }
    GiveRP(300 + 50 * g_aw.kills, "Arena War");
    Save();
    Shard("~o~ARENA WAR", std::to_string(g_aw.kills) + " kills, " + std::to_string(g_aw.deaths) + " deaths  +" + std::to_string(ap) + " AP");
}

// ---- host / join (online: a deathmatch job, kind 2, text "A|...")
static std::string AwText() {
    char b[64]; std::snprintf(b, sizeof b, "A|%d|%d|%d|%d|%d", g_aw.theme, g_aw.map, g_aw.light, g_aw.minutes, g_aw.veh);
    return b;
}
static void AwStart(bool online) {
    if (g_mActive || g_race.phase != RP_NONE || g_dm.phase != DP_NONE || g_sv.phase != SVP_NONE || g_aw.phase != AWP_NONE) { Notify("~r~Finish your race, deathmatch or job first."); return; }
    g_aw = AwRun();
    g_aw.theme = g_awSel[0]; g_aw.map = g_awSel[1] % AW_THEMES[g_aw.theme].maps; g_aw.light = g_awSel[2]; g_aw.veh = g_awSel[3];
    g_aw.bots = g_awSel[4] * 2; g_aw.minutes = AW_MINUTES[g_awSel[5]]; g_aw.host = true;
    if (!online || !LO().jobOpenKind || !CoopAvailable()) { g_aw.solo = true; AwBegin(4000); return; }
    g_aw.solo = false;
    g_aw.payout = 12000 * g_aw.minutes / 5;
    if (!LO().jobOpenKind((std::string("Arena War: ") + AW_THEMES[g_aw.theme].name).c_str(), -376.f, -1862.f, 20.f, g_aw.payout, 2)) {
        Notify("~r~Could not open the Arena War lobby."); g_aw = AwRun(); return;
    }
    g_aw.phase = AWP_LOBBY;
    Wait(300);
    if (LO().jobUpdate) LO().jobUpdate(COOP_LOBBY, 0, -376.f, -1862.f, 20.f, AwText().c_str());
    Notify("Arena War lobby open: other players join from ~b~Jobs~s~. Start it from the ~b~Arena War~s~ menu.");
}
static void AwHostStart() {
    if (!g_aw.host || g_aw.phase != AWP_LOBBY || !LO().jobUpdate) return;
    LO().jobUpdate(COOP_RUNNING, 0, -376.f, -1862.f, 20.f, AwText().c_str());
    g_aw.phase = AWP_NONE;   // AwFollowJob picks up the start time
}
static void AwFollowJob() {
    if (g_aw.phase != AWP_NONE || !g_coop.id || g_coop.kind != 2) return;
    if (g_coop.status != COOP_RUNNING || g_coop.startsIn <= 0 || g_coop.text.compare(0, 2, "A|")) return;
    std::vector<std::string> f;
    std::string cur;
    for (char c : g_coop.text + "|") { if (c == '|') { f.push_back(cur); cur.clear(); } else cur += c; }
    if (f.size() < 6) return;
    bool host = g_coop.hosting || g_coop.host == "you";
    g_aw = AwRun();
    g_aw.host = host; g_aw.solo = false;
    g_aw.theme = std::max(0, std::min(2, std::atoi(f[1].c_str())));
    g_aw.map = std::max(0, std::atoi(f[2].c_str())) % AW_THEMES[g_aw.theme].maps;
    g_aw.light = std::max(0, std::min(NAW_LIGHTS - 1, std::atoi(f[3].c_str())));
    g_aw.minutes = std::max(1, std::atoi(f[4].c_str()));
    g_aw.veh = host ? std::max(0, std::min(NAW_VEHS - 1, std::atoi(f[5].c_str()))) : g_awSel[3];   // members drive their own pick
    g_aw.payout = g_coop.payout;
    AwBegin((int)g_coop.startsIn);
}

// ---- HUD, results, each frame
static void AwHud() {
    int row = 0;
    RaceBar(row++, "TIME LEFT", RaceTime(std::max(0, g_aw.endAt - Now())).substr(0, 5));
    RaceBar(row++, "DEATHS", std::to_string(g_aw.deaths));
    RaceBar(row++, "KILLS", std::to_string(g_aw.kills), true);
}
static void AwResults() {
    float x = 0.5f, y = 0.3f, w = 0.4f;
    Rect(x, y, w, 0.05f, 255, 120, 0, 230);
    Text(std::string("Arena War: ") + AW_THEMES[g_aw.theme].name, x, y - 0.018f, 0.5f, 1, 255, 255, 255, 255, 0);
    const char *rows[] = {"Kills", "Deaths", "Arena Points", "Arena Points total"};
    int vals[] = {g_aw.kills, g_aw.deaths, 20 + 10 * g_aw.kills, AwApGet()};
    for (int i = 0; i < 4; ++i) {
        float ry = y + 0.05f + 0.038f * i;
        Rect(x, ry, w, 0.036f, 0, 0, 0, 190);
        Text(rows[i], x - w / 2 + 0.01f, ry - 0.014f, 0.35f, 4, 255, 255, 255, 255, 1);
        Text(std::to_string(vals[i]), x + w / 2 - 0.01f, ry - 0.014f, 0.35f, 4, 255, 255, 255, 255, 2, x + w / 2 - 0.01f);
    }
}
static void ArenaWarTick() {
    AwFollowJob();
    if (g_aw.phase == AWP_NONE || g_aw.phase == AWP_LOBBY) return;
    int me = Me();
    AwForceRoom();
    if (!g_aw.solo && (!g_coop.id || g_coop.kind != 2 || g_coop.status == COOP_CLOSED) && g_aw.phase != AWP_RESULTS) {
        if (g_aw.phase == AWP_PLAYING) AwFinish(); else { Notify("The Arena War event was cancelled."); AwCleanup(); return; }
    }
    if (g_aw.phase == AWP_COUNTDOWN) {
        int left = g_aw.startAt - Now();
        int n = left > 3000 ? -1 : left > 0 ? left / 1000 + 1 : 0;
        if (n != g_aw.lastCount && n >= 0 && NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, g_aw.countdownMovie))) {
            g_aw.lastCount = n;
            N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, g_aw.countdownMovie, "SET_MESSAGE");
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_TEXTURE_NAME_STRING, n ? std::to_string(n).c_str() : "GO");
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, 255); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, n ? 140 : 90); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, 0);
            N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, 1);
            N(N_END_SCALEFORM_MOVIE_METHOD);
            Sound(n ? "3_2_1" : "GO", "HUD_MINI_GAME_SOUNDSET");
        }
        if (g_aw.lastCount >= 0) N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, g_aw.countdownMovie, 255, 255, 255, 255, 0);
        AwHud();
        if (left <= 0) {
            if (Exists(g_aw.myVeh)) N(N_FREEZE_ENTITY_POSITION, g_aw.myVeh, 0);
            for (auto &b : g_aw.botList) if (Exists(b.veh)) N(N_FREEZE_ENTITY_POSITION, b.veh, 0);
            g_aw.phase = AWP_PLAYING;
        }
        return;
    }
    if (g_aw.phase == AWP_PLAYING) {
        if (Now() - g_aw.startAt < 1500) N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, g_aw.countdownMovie, 255, 255, 255, 255, 0);
        N(N_DISABLE_CONTROL_ACTION, 0, 75, 1);   // stay in the car
        // wrecked or killed: back in a fresh car (LocalOnline respawns YOU at a hospital if you die)
        bool down = Dead(me) || !Exists(g_aw.myVeh) || Dead(g_aw.myVeh) || !NB(N<int>(N_IS_VEHICLE_DRIVEABLE, g_aw.myVeh, 0));
        if (down && !g_aw.wasDown) { g_aw.wasDown = true; g_aw.downAt = Now(); ++g_aw.deaths; Notify("~r~Wrecked.~s~ Back in the arena in a moment."); }
        if (g_aw.wasDown && !Dead(me) && Now() - g_aw.downAt > 2500) {
            g_aw.wasDown = false;
            N(N_DO_SCREEN_FADE_OUT, 300); Wait(350);
            N(N_SET_ENTITY_COORDS, me, AW_CENTRE.x, AW_CENTRE.y, 110.f, 0, 0, 0, 0);
            for (int i = 0; i < 10; ++i) { AwForceRoom(); Wait(30); }
            AwPlacePlayer();
            N(N_DO_SCREEN_FADE_IN, 400);
        }
        if (g_aw.solo) AwBotsTick();
        AwHud();
        if (Now() >= g_aw.endAt) {
            if (!g_aw.solo && g_aw.host && LO().jobUpdate && !g_aw.ended) { g_aw.ended = true; LO().jobUpdate(COOP_PASSED, 0, -376.f, -1862.f, 20.f, ""); }
            AwFinish();
        }
        return;
    }
    if (g_aw.phase == AWP_RESULTS) {
        AwResults();
        if (Now() > g_aw.resultsUntil) AwCleanup();
    }
}

// ---- the Arena War menu (Interaction Menu, or E at the Maze Bank Arena)
static bool ArenaWarMenu(const Frame &f, std::string &title, std::string &sub, std::vector<Item> &items) {
    if ((int)f.id != AW_MENU) return false;
    title = "Arena War"; sub = "CARNAGE";
    if (g_aw.host && g_aw.phase == AWP_LOBBY) {
        int n = g_coop.members.empty() ? 1 : 2;
        for (char c : g_coop.members) if (c == ',') ++n;
        items.push_back({"Start Arena War", std::to_string(n) + (n == 1 ? " player" : " players"), "Everyone in the lobby goes into the arena.", [] { CloseMenu(); AwHostStart(); }});
        items.push_back({"Cancel", "", "", [] { if (LO().jobUpdate) LO().jobUpdate(COOP_CLOSED, 0, 0, 0, 0, ""); g_aw = AwRun(); CloseMenu(); }});
        return true;
    }
    if (g_aw.phase != AWP_NONE) { items.push_back({"Leave the Arena", "", "", [] { CloseMenu(); if (!g_aw.solo && !g_aw.host && LO().jobJoin) LO().jobJoin(0); AwFinish(); }}); return true; }
    int th = g_awSel[0];
    g_awSel[1] %= AW_THEMES[th].maps;
    items.push_back({"Theme", std::string("< ") + AW_THEMES[th].name + " >", std::string("Arena vehicles in their ") + AW_THEMES[th].variant + " versions.",
                     [] { g_awSel[0] = (g_awSel[0] + 1) % 3; g_awSel[1] = 0; }});
    items.push_back({"Map", "< " + std::to_string(g_awSel[1] + 1) + " / " + std::to_string(AW_THEMES[th].maps) + " >", "The arena's layout.", [] { ++g_awSel[1]; }});
    items.push_back({"Lighting", std::string("< ") + AW_LIGHTS[g_awSel[2]].name + " >", "", [] { g_awSel[2] = (g_awSel[2] + 1) % NAW_LIGHTS; }});
    std::string vname = std::string(AW_VEHS[g_awSel[3]][0]) + " (" + AW_THEMES[th].variant + ")";
    items.push_back({"Vehicle", "< " + vname + " >", "If you own this model, its upgrades come with it.", [] { g_awSel[3] = (g_awSel[3] + 1) % NAW_VEHS; }});
    items.push_back({"Bots (solo)", "< " + std::to_string(g_awSel[4] * 2) + " >", "AI drivers that come for you.", [] { g_awSel[4] = (g_awSel[4] + 1) % 5; }});
    items.push_back({"Time", "< " + std::to_string(AW_MINUTES[g_awSel[5]]) + " min >", "", [] { g_awSel[5] = (g_awSel[5] + 1) % 3; }});
    items.push_back({"Play Solo", "", "Carnage against the bots.", [] { CloseMenu(); AwStart(false); }});
    if (CoopAvailable() && LO().jobOpenKind)
        items.push_back({"Host Online", Money(12000 * AW_MINUTES[g_awSel[5]] / 5) + " to the winner", "Open a lobby: other players join from Jobs. Paid by position.",
                         [] { CloseMenu(); AwStart(true); }});
    items.push_back({"Arena Points", std::to_string(AwApGet()), "Earned in every Arena War event.", nullptr});
    items.push_back({"Set GPS to the Maze Bank Arena", "", "", [] { N(N_SET_NEW_WAYPOINT, -376.f, -1862.f); CloseMenu(); }});
    return true;
}
// at the Maze Bank Arena: the Arena War blip and E to open the menu
static int g_awBlip = 0;
static void ArenaWarWorldTick() {
    if (!YouActive()) { RemoveBlip(g_awBlip); return; }
    if (!g_awBlip) {
        g_awBlip = CoordBlip(V3(-376.f, -1862.f, 20.f), 643, 17, "Arena War", true);
    }
    if (g_menuOpen || g_aw.phase != AWP_NONE || g_mActive || InAnyVehicle()) return;
    if (Dist(Pos(Me()), V3(-376.f, -1862.f, 20.f)) < 3.f) {
        Help("Press ~INPUT_CONTEXT~ for Arena War.");
        if (JustPressed(51)) { g_menuOpen = true; g_stack.clear(); Open((MenuId)AW_MENU); }
    }
}
