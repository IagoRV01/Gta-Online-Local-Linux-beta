// ---------------------------------------------------------------- Arena War (mpchristmas2018)
// The arena (xs_x18_int_01 at 2800,-3800,100; IPL xs_arena_interior) is dressed the way fm_mission_controller does
// it: one scene set per theme (Set_Dystopian_Scene / Set_Scifi_Scene / Set_Wasteland_Scene), one of its maps
// (Set_Dystopian_01-17, Set_Scifi_01-10, Set_Wasteland_01-10), the crowd (Set_Crowd_A-D), a lighting set and the
// pit fence. Teleporting in does not put you in the interior's room, so the player, the car and the game camera are
// forced into it every frame (else only the sea outside renders).
// Modes: Carnage (vehicle combat, against bots or online against the other players: a deathmatch job, kind 2, text
// "A|theme|map|light|minutes|vehicle"), and against bots: Hot Bomb (a timed bomb passed by ramming; survive the
// rounds), Here Come the Monsters (a small car against monster trucks) and Games Masters (checkpoints across the
// arena while the bots hunt you). Kills, deaths, Arena Points, cash and RP. Apocalypse, Future Shock and Nightmare
// are the three variants of each arena vehicle.
enum { AW_MENU = 340 };
struct AwTheme { const char *name, *scene, *prefix; int maps; const char *variant; };
static const AwTheme AW_THEMES[] = {{"Apocalypse", "Set_Dystopian_Scene", "Set_Dystopian_", 17, "Apocalypse"},
                                    {"Sci-Fi", "Set_Scifi_Scene", "Set_Scifi_", 10, "Future Shock"},
                                    {"Consumer Wasteland", "Set_Wasteland_Scene", "Set_Wasteland_", 10, "Nightmare"}};
static const struct { const char *name, *set; } AW_LIGHTS[] = {{"Night", "Set_Lights_night"}, {"Evening", "Set_Lights_evening"},
    {"Midday", "Set_Lights_midday"}, {"Morning", "Set_Lights_morning"}, {"Storm", "Set_Lights_storm"}, {"Sandstorm", "Set_Lights_sandstorm"},
    {"Toxic", "Set_Lights_toxic"}, {"Hell", "Set_Lights_hell"}, {"Atlantis", "Set_Lights_atlantis"}, {"Saccharine", "Set_Lights_saccharine"}};
static const int NAW_LIGHTS = (int)(sizeof AW_LIGHTS / sizeof *AW_LIGHTS);
// Apocalypse 11 and 13-16 are bare sets (only the base floor at 90.7: their layouts are props in Rockstar's jobs). Checked with awfloors / awview, 2026-10-18.
static bool AwMapBare(int th, int m) { return th == 0 && (m == 10 || (m >= 12 && m <= 15)); }
static int AwFixMap(int th, int m) {   // the next playable map from m on
    int n = AW_THEMES[th].maps;
    m = ((m % n) + n) % n;
    for (int i = 0; i < n && AwMapBare(th, m); ++i) m = (m + 1) % n;
    return m;
}
// each arena vehicle in its three variants (Apocalypse, Future Shock, Nightmare)
static const char *AW_VEHS[][4] = {
    {"Imperator", "imperator", "imperator2", "imperator3"}, {"Dominator", "dominator4", "dominator5", "dominator6"}, {"Issi", "issi4", "issi5", "issi6"},
    {"Slamvan", "slamvan4", "slamvan5", "slamvan6"}, {"Impaler", "impaler2", "impaler3", "impaler4"}, {"ZR380", "zr380", "zr3802", "zr3803"},
    {"Brutus", "brutus", "brutus2", "brutus3"}, {"Bruiser", "bruiser", "bruiser2", "bruiser3"}, {"Sasquatch", "monster3", "monster4", "monster5"},
    {"Cerberus", "cerberus", "cerberus2", "cerberus3"}, {"Deathbike", "deathbike", "deathbike2", "deathbike3"}, {"Scarab", "scarab", "scarab2", "scarab3"}};
static const int NAW_VEHS = (int)(sizeof AW_VEHS / sizeof *AW_VEHS);
enum { AWM_CARNAGE, AWM_HOTBOMB, AWM_MONSTERS, AWM_GAMESMASTERS, AWM_FLAGWAR, AWM_COUNT };
static const struct { const char *name, *desc; } AW_MODES[AWM_COUNT] = {
    {"Carnage", "Wreck the others: most kills wins. Online against the other players, solo against bots."},
    {"Hot Bomb", "A bomb on a timer: ram someone to pass it on. Whoever holds it at zero goes up. Survive the rounds."},
    {"Here Come the Monsters", "A small car against monster trucks. Stay in one piece until the time runs out."},
    {"Games Masters", "Drive through the checkpoints across the arena while the bots hunt you."},
    {"Flag War", "Grab the flag and drive it to your base. Ram the carrier to steal it. The bots have a base too."}};
// Traps: the arena's own trap props, with our own scripted effects (Online's live in Rockstar's job data).
enum { AWT_FLIPPER, AWT_FIRE, AWT_MINE, AWT_SPIKES, AWT_COUNT };
static const char *AW_TRAP_MODELS[AWT_COUNT][3] = {
    {"xs_prop_arena_flipper_large_01a", "xs_prop_arena_flipper_large_01a_sf", "xs_prop_arena_flipper_large_01a_wl"},
    {"xs_prop_arena_pit_fire_01a", "xs_prop_arena_pit_fire_01a_sf", "xs_prop_arena_pit_fire_01a_wl"},
    {"xs_prop_arena_landmine_01a", "xs_prop_arena_landmine_01c_sf", "xs_prop_arena_landmine_01c_wl"},
    {"xs_prop_arena_spikes_01a", "xs_prop_arena_spikes_01a_sf", "xs_prop_arena_spikes_01a"}};
static const int AW_TRAP_COUNT[AWT_COUNT] = {3, 2, 4, 2};
static const float AW_TRAP_RADIUS[AWT_COUNT] = {3.5f, 3.f, 2.4f, 2.6f};
struct AwTrap { int kind = 0, obj = 0, nextAt = 0, until = 0, fire = 0, launched = 0; Vec3 p{}; float h = 0.f; };
static const uint32_t AW_ROOM = 0x113B4A40;   // the arena's room (GET_ROOM_KEY_FROM_ENTITY once inside)
static const Vec3 AW_CENTRE = V3(2800.f, -3800.f, 100.f);

enum AwPhase { AWP_NONE, AWP_LOBBY, AWP_COUNTDOWN, AWP_PLAYING, AWP_RESULTS };
struct AwBot { int veh = 0, ped = 0, blip = 0, respawnAt = 0, retaskAt = 0, idleSince = 0; };
struct AwRun {
    int phase = AWP_NONE, theme = 0, map = 0, light = 0, minutes = 5, veh = 0, bots = 4;
    bool host = false, solo = true, ended = false, wasDown = false;
    int startAt = 0, endAt = 0, resultsUntil = 0, payout = 0, interior = 0, myVeh = 0, downAt = 0, stuckSince = 0;
    float floorZ = 104.f;                                     // this map's main floor (they go from about 100 to 140)
    int kills = 0, deaths = 0, countdownMovie = 0, lastCount = -1;
    int mode = AWM_CARNAGE, score = 0;                          // score: Hot Bomb rounds survived / Games Masters checkpoints
    int bombHolder = -2, bombEnds = 0, lastPass = 0;           // Hot Bomb: -1 you, >= 0 a bot, -2 nobody yet
    std::vector<Vec3> cps; int cpHandle = 0, cpBlip = 0;      // Games Masters
    bool traps = true; std::vector<AwTrap> trapList;
    Vec3 flag{}, myBase{}, botBase{}; int flagCarrier = -3, flagAt = 0, botScore = 0, flagBlip = 0, myBaseBlip = 0, botBaseBlip = 0, lastSteal = 0;   // Flag War: -3 not out yet, -2 on the ground, -1 you, >= 0 a bot
    std::vector<AwBot> botList;
    Vec3 back{}; float backH = 0.f;
};
static AwRun g_aw;
static int g_awSel[8] = {0, 0, 0, 0, 2, 0, 0, 1};   // menu choices: theme, map, light, vehicle, bots (x2), minutes index, mode, traps
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
    for (auto &t : g_aw.trapList) if (Exists(t.obj)) N(N_FORCE_ROOM_FOR_ENTITY, t.obj, g_aw.interior, AW_ROOM);
    N(N_FORCE_ROOM_FOR_GAME_VIEWPORT, g_aw.interior, AW_ROOM);
    N(N_SET_RADAR_AS_INTERIOR_THIS_FRAME, Joaat("xs_x18_int_01"), AW_CENTRE.x, AW_CENTRE.y, 0, 0);
    N(N_HIDE_MINIMAP_EXTERIOR_MAP_THIS_FRAME);
}
// Each map has its floor at its own height (about 100 to 140, over the bare base at 90.7) and bridges, roofs and
// ramps on top: every surface down a column, and the map's floor is the height found most often across the arena.
static std::vector<float> AwSurfaces(float x, float y) {
    std::vector<float> out;
    float top = 170.f;
    for (float t : {170.f, 150.f, 145.f, 135.f}) if (GroundAt(x, y, t, -1000.f) > 92.f) { top = t; break; }   // some start heights find nothing
    for (int i = 0; i < 6; ++i) {
        float gz = GroundAt(x, y, top, -1000.f);
        if (gz < 92.f) break;   // the bare base under every map
        out.push_back(gz);
        top = gz - 1.5f;
    }
    return out;
}
static float AwFindFloor() {
    std::map<int, std::pair<int, float>> bins;   // 1 m bins: count, sum
    for (float dx = -40.f; dx <= 40.f; dx += 8.f)
        for (float dy = -40.f; dy <= 40.f; dy += 8.f)
            for (float gz : AwSurfaces(AW_CENTRE.x + dx, AW_CENTRE.y + dy)) { auto &b = bins[(int)std::floor(gz)]; ++b.first; b.second += gz; }
    int most = 0;
    for (auto &[k, b] : bins) most = std::max(most, b.first);
    float z = 104.f;   // the lowest big level (raised rings, decks and roofs can cover as much)
    for (auto &[k, b] : bins) if (b.first * 10 >= most * 4) { z = b.second / b.first; break; }
    std::string h;
    for (auto &[k, b] : bins) if (b.first >= 4) h += " " + std::to_string(k) + ":" + std::to_string(b.first);
    Log("arena floor %.1f (heights:%s)", z, h.c_str());
    return z;
}
static float AwSurfaceNear(float x, float y, float z, float tol) {   // the surface in that column closest to z (within tol), or -1000
    float best = -1000.f;
    for (float gz : AwSurfaces(x, y)) if (std::fabs(gz - z) <= tol && std::fabs(gz - z) < std::fabs(best - z)) best = gz;
    return best;
}
static Vec3 AwSpawnPoint(float &heading) {   // on the map's floor around the centre, level (not on a ramp), facing in
    float fz = g_aw.floorZ;
    for (int tries = 0; tries < 50; ++tries) {
        float a = (float)Rand(6283) / 1000.f, r = 8.f + (float)Rand(tries < 30 ? 24 : 40);
        float x = AW_CENTRE.x + std::cos(a) * r, y = AW_CENTRE.y + std::sin(a) * r;
        float gz = AwSurfaceNear(x, y, fz, tries < 35 ? 0.8f : 3.f);
        if (gz < -999.f) continue;
        bool clear = true;   // and not right on top of the slopes: the ground 3 m around is level too
        for (int k = 0; k < 4 && clear; ++k) clear = AwSurfaceNear(x + (k == 0 ? 3.f : k == 1 ? -3.f : 0.f), y + (k == 2 ? 3.f : k == 3 ? -3.f : 0.f), gz, 0.8f) > -999.f;
        if (!clear && tries < 45) continue;
        heading = std::atan2(-(AW_CENTRE.x - x), AW_CENTRE.y - y) * 180.f / 3.14159265f;
        return V3(x, y, gz + 1.0f);
    }
    heading = 0.f;
    return V3(AW_CENTRE.x, AW_CENTRE.y, fz + 1.5f);
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
    std::string model = g_aw.mode == AWM_MONSTERS ? AwModel(g_aw.theme, 2) : AwModel(g_aw.theme, g_aw.veh);   // the monsters' prey: an Issi
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
    b.veh = AwCar(AwModel(g_aw.theme, g_aw.mode == AWM_MONSTERS ? 8 : Rand(NAW_VEHS - 2)), p, h);   // Sasquatches for the monsters; no Deathbike / Scarab
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
        if (NB(N<int>(N_IS_VEHICLE_STUCK_ON_ROOF, b.veh)) || NB(N<int>(N_IS_VEHICLE_STUCK_TIMER_UP, b.veh, 3, 5000))) {   // flipped or wedged: back in play
            AwRemoveBot(b);
            AwSpawnBot(b);
            continue;
        }
        if (Pos(b.veh).z < g_aw.floorZ - 6.f && Pos(b.veh).z < 99.f) { AwRemoveBot(b); AwSpawnBot(b); continue; }   // fell through the floor
        bool idle = N<float>(N_GET_ENTITY_SPEED, b.veh) < 1.f && Dist(Pos(b.veh), Pos(me)) > 25.f;   // parked far away (lost the path): back in play
        if (!idle) b.idleSince = 0;
        else if (!b.idleSince) b.idleSince = Now();
        else if (Now() - b.idleSince > 8000) { AwRemoveBot(b); AwSpawnBot(b); continue; }
        if (Now() >= b.retaskAt) {   // after you, again and again; in Hot Bomb the holder hunts and everybody else runs from it
            int target = me, mission = 6;
            if (g_aw.mode == AWM_HOTBOMB) {
                int idx = (int)(&b - g_aw.botList.data());
                if (g_aw.bombHolder >= 0 && g_aw.bombHolder != idx && Exists(g_aw.botList[g_aw.bombHolder].ped)) { target = g_aw.botList[g_aw.bombHolder].ped; mission = 8; }
                else if (g_aw.bombHolder == -1) mission = 8;
            }
            int idx2 = (int)(&b - g_aw.botList.data());
            if (g_aw.mode == AWM_FLAGWAR && (g_aw.flagCarrier == -2 || g_aw.flagCarrier == idx2)) {   // the flag on the ground: race for it; carrying it: home (the others go for you)
                Vec3 to = g_aw.flagCarrier == idx2 ? g_aw.botBase : g_aw.flag;
                N(N_TASK_VEHICLE_MISSION_COORS_TARGET, b.ped, b.veh, to.x, to.y, to.z, 4, 30.f, 786469, 2.f, 200.f, 1);   // straight at it: no road nodes in the arena
                b.retaskAt = Now() + 1500;
                continue;
            }
            N(N_TASK_VEHICLE_MISSION_PED_TARGET, b.ped, b.veh, target, mission, 32.f, 786469, mission == 8 ? 60.f : 3.f, 1.f, 1);
            b.retaskAt = Now() + (g_aw.mode == AWM_HOTBOMB || g_aw.mode == AWM_FLAGWAR ? 1500 : 5000);
        }
    }
}

// ---- traps
static void AwTrapPlace(AwTrap &t) {
    for (int tries = 0; tries < 20; ++tries) {   // on the floor, clear of the others and of your car
        float h = 0.f; Vec3 p = AwSpawnPoint(h);
        bool ok = !Exists(g_aw.myVeh) || Dist(p, Pos(g_aw.myVeh)) > 15.f;
        for (auto &o : g_aw.trapList) if (&o != &t && o.obj && Dist(p, o.p) < 12.f) ok = false;
        if (g_aw.mode == AWM_FLAGWAR && (Dist(p, g_aw.myBase) < 12.f || Dist(p, g_aw.botBase) < 12.f)) ok = false;
        if (!ok && tries < 19) continue;
        t.p = V3(p.x, p.y, p.z - 1.f); t.h = (float)Rand(360);
        break;
    }
    uint32_t m = Joaat(AW_TRAP_MODELS[t.kind][std::max(0, std::min(2, g_aw.theme))]);
    if (!LoadModel(m)) { m = Joaat(AW_TRAP_MODELS[t.kind][0]); if (!LoadModel(m)) return; }
    t.obj = N<int>(N_CREATE_OBJECT, m, t.p.x, t.p.y, t.p.z, 0, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
    if (!Exists(t.obj)) { t.obj = 0; return; }
    N(N_SET_ENTITY_HEADING, t.obj, t.h);
    N(N_FREEZE_ENTITY_POSITION, t.obj, 1);
    t.nextAt = Now() + 2000 + Rand(4000);
}
static void AwTrapsSpawn() {
    g_aw.trapList.clear();
    if (!g_aw.traps || !g_aw.solo) return;   // online, each player would see different traps
    for (int k = 0; k < AWT_COUNT; ++k) for (int i = 0; i < AW_TRAP_COUNT[k]; ++i) { AwTrap t; t.kind = k; g_aw.trapList.push_back(t); }
    for (auto &t : g_aw.trapList) AwTrapPlace(t);
}
static void AwTrapsClear() {
    for (auto &t : g_aw.trapList) { if (t.fire) N(N_REMOVE_SCRIPT_FIRE, t.fire); if (Exists(t.obj)) DeleteEntity(t.obj); }
    g_aw.trapList.clear();
}
static void AwTrapsTick() {
    std::vector<int> cars;
    if (Exists(g_aw.myVeh)) cars.push_back(g_aw.myVeh);
    for (auto &b : g_aw.botList) if (Exists(b.veh)) cars.push_back(b.veh);
    for (auto &t : g_aw.trapList) {
        if (!Exists(t.obj)) { if (t.kind == AWT_MINE && t.nextAt && Now() >= t.nextAt) AwTrapPlace(t); continue; }
        int hit = 0;
        for (int v : cars) {
            Vec3 c = Pos(v);
            if (std::hypot(c.x - t.p.x, c.y - t.p.y) < AW_TRAP_RADIUS[t.kind] && std::fabs(c.z - t.p.z) < 2.5f) { hit = v; break; }
        }
        if (t.kind == AWT_FLIPPER) {   // launches whatever drives onto it, then resets
            if (t.launched && Now() < t.until - 400 && Exists(t.launched)) {   // keep pushing for 200 ms: one frame can be eaten by the physics
                Vec3 vel = N<Vec3>(N_GET_ENTITY_VELOCITY, t.launched);
                if (vel.z < 12.f) N(N_SET_ENTITY_VELOCITY, t.launched, vel.x, vel.y, 13.f);
                N(N_APPLY_FORCE_TO_ENTITY, t.launched, 5, 0.f, 0.f, 0.f, 0.f, 3.f, 0.f, 0, 1, 1, 1, 0, 1);   // and a roll
            }
            if (t.until && Now() > t.until) { t.until = 0; t.launched = 0; }
            if (hit && Now() >= t.nextAt) {   // (tilting the frozen plate pushed cars through the floor: the launch alone)
                t.launched = hit;
                t.until = Now() + 600; t.nextAt = Now() + 4000;
                Log("trap: flipper launched %s", hit == g_aw.myVeh ? "you" : "a bot");
            }
        } else if (t.kind == AWT_FIRE) {   // 4 s quiet, then 3 s of fire (a warning glow first)
            if (!t.until && Now() >= t.nextAt - 1200 && Now() < t.nextAt)
                N(N_DRAW_MARKER, 25, t.p.x, t.p.y, t.p.z + 0.15f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 5.f, 5.f, 1.f, 255, 90, 0, 140, 0, 0, 2, 0, 0, 0, 0);
            if (!t.until && Now() >= t.nextAt) { t.fire = N<int>(N_START_SCRIPT_FIRE, t.p.x, t.p.y, t.p.z + 0.2f, 6, 0); t.until = Now() + 3000; }
            if (t.until && hit && !t.launched) { t.launched = 1; Log("trap: fire pit caught %s", hit == g_aw.myVeh ? "you" : "a bot"); }
            if (t.until && Now() >= t.until) { if (t.fire) N(N_REMOVE_SCRIPT_FIRE, t.fire); t.fire = 0; t.until = 0; t.launched = 0; t.nextAt = Now() + 4000 + Rand(2000); }
            if (t.until && hit) N(N_SET_ENTITY_HEALTH, hit, std::max(1, N<int>(N_GET_ENTITY_HEALTH, hit) - 4));   // sitting in it hurts
        } else if (t.kind == AWT_MINE) {   // goes off under a car, comes back elsewhere 20 s later
            if (hit && Now() >= t.nextAt) {
                N(N_ADD_EXPLOSION, t.p.x, t.p.y, t.p.z + 0.3f, 4, 0.4f, 1, 0, 1.f, 0);   // 40%: armoured arena cars take a couple
                Log("trap: mine under %s", hit == g_aw.myVeh ? "you" : "a bot");
                DeleteEntity(t.obj);
                t.nextAt = Now() + 20000;
            }
        } else if (t.kind == AWT_SPIKES && hit) {   // tyres out
            if (t.launched != hit) {   // once per car: arena cars come with bulletproof tyres, so let them burst first
                t.launched = hit;
                N(N_SET_VEHICLE_TYRES_CAN_BURST, hit, 1);
                for (int w : {0, 1, 4, 5}) N(N_SET_VEHICLE_TYRE_BURST, hit, w, 0, 1000.f);
                Log("trap: spikes under %s (tyre burst %d)", hit == g_aw.myVeh ? "you" : "a bot", (int)NB(N<int>(N_IS_VEHICLE_TYRE_BURST, hit, 0, 0)));
            }
        }
    }
}

// ---- Flag War
static void AwFlagReset() {   // the flag goes back out, near the middle
    float h = 0.f;
    g_aw.flag = AwSpawnPoint(h);
    for (int i = 0; i < 10 && (Dist(g_aw.flag, g_aw.myBase) < 25.f || Dist(g_aw.flag, g_aw.botBase) < 25.f); ++i) g_aw.flag = AwSpawnPoint(h);
    g_aw.flagCarrier = -2;
    for (auto &b : g_aw.botList) b.retaskAt = 0;
}
static void AwFlagSetup() {
    float h = 0.f;   // the two bases: the farthest-apart pair of 25 points on the floor
    std::vector<Vec3> pts;
    for (int i = 0; i < 25; ++i) pts.push_back(AwSpawnPoint(h));
    g_aw.myBase = pts[0]; g_aw.botBase = pts[1];
    for (size_t i = 0; i < pts.size(); ++i)
        for (size_t j = i + 1; j < pts.size(); ++j)
            if (Dist(pts[i], pts[j]) > Dist(g_aw.myBase, g_aw.botBase)) { g_aw.myBase = pts[i]; g_aw.botBase = pts[j]; }
    g_aw.myBaseBlip = CoordBlip(g_aw.myBase, 38, 3, "Your Base", false);
    g_aw.botBaseBlip = CoordBlip(g_aw.botBase, 38, 1, "Enemy Base", false);
    AwFlagReset();
}
static void AwFlagClear() { RemoveBlip(g_aw.flagBlip); RemoveBlip(g_aw.myBaseBlip); RemoveBlip(g_aw.botBaseBlip); }
static int AwCarrierVeh() { return g_aw.flagCarrier == -1 ? g_aw.myVeh : g_aw.flagCarrier >= 0 && g_aw.flagCarrier < (int)g_aw.botList.size() ? g_aw.botList[g_aw.flagCarrier].veh : 0; }
static void AwFlagTake(int who) {
    g_aw.flagCarrier = who; g_aw.lastSteal = Now();
    if (who == -1) { Notify("~b~You have the flag!~s~ Drive it to your base."); Sound("CHECKPOINT_NORMAL", "HUD_MINI_GAME_SOUNDSET"); }
    else Notify("~r~A bot has the flag.~s~ Ram it to steal it back.");
    for (auto &b : g_aw.botList) b.retaskAt = 0;
}
static void AwFlagTick() {
    if (g_aw.flagCarrier == -3) return;
    if (g_aw.flagAt && Now() < g_aw.flagAt) return;   // after a capture, a moment before the next flag
    if (g_aw.flagAt) { g_aw.flagAt = 0; AwFlagReset(); }
    for (Vec3 b : {g_aw.myBase, g_aw.botBase}) {
        bool mine = b.x == g_aw.myBase.x && b.y == g_aw.myBase.y;
        N(N_DRAW_MARKER, 1, b.x, b.y, b.z - 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 12.f, 12.f, 1.2f, mine ? 60 : 230, mine ? 140 : 40, mine ? 255 : 40, 120, 0, 0, 2, 0, 0, 0, 0);
    }
    int cv = AwCarrierVeh();
    if (g_aw.flagCarrier >= -1 && (!Exists(cv) || Dead(cv))) {   // the carrier got wrecked: the flag drops where it was
        if (Exists(cv)) g_aw.flag = Pos(cv);
        g_aw.flagCarrier = -2; cv = 0;
        for (auto &b : g_aw.botList) b.retaskAt = 0;
    }
    if (g_aw.flagCarrier >= -1) g_aw.flag = Pos(cv);   // where it drops if the carrier is wrecked
    Vec3 fp = g_aw.flag;
    N(N_DRAW_MARKER, 4, fp.x, fp.y, fp.z + (g_aw.flagCarrier == -2 ? 1.2f : 2.6f), 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 2.f, 2.f, 2.f, 255, 255, 255, 230, 1, 1, 2, 0, 0, 0, 0);
    RemoveBlip(g_aw.flagBlip);
    g_aw.flagBlip = CoordBlip(fp, 38, g_aw.flagCarrier == -1 ? 3 : g_aw.flagCarrier >= 0 ? 1 : 5, "Flag", false);
    auto close = [](int v, Vec3 p, float r) { if (!Exists(v)) return false; Vec3 c = Pos(v); return std::hypot(c.x - p.x, c.y - p.y) < r && std::fabs(c.z - p.z) < 4.f; };
    if (g_aw.flagCarrier == -2) {   // on the ground: first to drive over it takes it
        if (close(g_aw.myVeh, g_aw.flag, 4.5f)) AwFlagTake(-1);
        else for (int i = 0; i < (int)g_aw.botList.size(); ++i) if (close(g_aw.botList[i].veh, g_aw.flag, 4.5f)) { AwFlagTake(i); break; }
        return;
    }
    if (Now() - g_aw.lastSteal > 2000) {   // ram the carrier to steal it
        if (g_aw.flagCarrier >= 0 && Exists(g_aw.myVeh) && NB(N<int>(N_IS_ENTITY_TOUCHING_ENTITY, cv, g_aw.myVeh))) { AwFlagTake(-1); return; }
        if (g_aw.flagCarrier == -1)
            for (int i = 0; i < (int)g_aw.botList.size(); ++i)
                if (Exists(g_aw.botList[i].veh) && NB(N<int>(N_IS_ENTITY_TOUCHING_ENTITY, cv, g_aw.botList[i].veh))) { AwFlagTake(i); return; }
    }
    bool mineHome = g_aw.flagCarrier == -1 && close(cv, g_aw.myBase, 6.f), botHome = g_aw.flagCarrier >= 0 && close(cv, g_aw.botBase, 6.f);
    if (mineHome || botHome) {   // captured
        if (mineHome) { ++g_aw.score; Shard("~b~FLAG CAPTURED", std::to_string(g_aw.score) + " - " + std::to_string(g_aw.botScore)); Sound("CHECKPOINT_PERFECT", "HUD_MINI_GAME_SOUNDSET"); }
        else { ++g_aw.botScore; Shard("~r~THE BOTS SCORED", std::to_string(g_aw.score) + " - " + std::to_string(g_aw.botScore)); }
        g_aw.flagCarrier = -2; g_aw.flag = V3(0, 0, -100); g_aw.flagAt = Now() + 3000;
        RemoveBlip(g_aw.flagBlip);
    }
}

// ---- start and end
static void AwBegin(int startsInMs) {
    g_aw.phase = AWP_COUNTDOWN;
    g_aw.startAt = Now() + std::max(2000, startsInMs);
    g_aw.endAt = g_aw.startAt + g_aw.minutes * 60000;
    g_aw.back = Pos(Me()); g_aw.backH = N<float>(N_GET_ENTITY_HEADING, Me());
    g_aw.kills = g_aw.deaths = 0;
    JobSkyUp();
    if (InAnyVehicle()) N(N_SET_ENTITY_COORDS, Me(), g_aw.back.x, g_aw.back.y, g_aw.back.z + 1.f, 0, 0, 0, 0);
    N(N_SET_ENTITY_COORDS, Me(), AW_CENTRE.x, AW_CENTRE.y, 150.f, 0, 0, 0, 0);
    N(N_FREEZE_ENTITY_POSITION, Me(), 1);
    g_aw.interior = AwLoadArena(g_aw.theme, g_aw.map, g_aw.light);
    N(N_REQUEST_COLLISION_AT_COORD, AW_CENTRE.x, AW_CENTRE.y, 120.f);
    for (int i = 0; i < 40; ++i) { AwForceRoom(); Wait(50); }
    g_aw.floorZ = AwFindFloor();
    N(N_FREEZE_ENTITY_POSITION, Me(), 0);
    AwPlacePlayer();
    if (Exists(g_aw.myVeh)) N(N_FREEZE_ENTITY_POSITION, g_aw.myVeh, 1);
    g_aw.botList.assign(g_aw.solo ? g_aw.bots : 0, AwBot());
    for (auto &b : g_aw.botList) { AwSpawnBot(b); if (Exists(b.veh)) N(N_FREEZE_ENTITY_POSITION, b.veh, 1); }
    g_aw.countdownMovie = N<int>(N_REQUEST_SCALEFORM_MOVIE, "COUNTDOWN");
    g_aw.lastCount = -1;
    if (g_aw.mode == AWM_GAMESMASTERS) {   // eight checkpoints across the floor, in a loop
        g_aw.cps.clear();
        for (int t = 0; t < 40 && g_aw.cps.size() < 8; ++t) {   // spread out: at least 20 m from the others
            float h2 = 0.f; Vec3 c = AwSpawnPoint(h2);
            bool tooNear = false;
            for (const Vec3 &o : g_aw.cps) tooNear = tooNear || Dist(c, o) < 20.f;
            if (!tooNear) g_aw.cps.push_back(c);
        }
        if (g_aw.cps.size() < 3) {   // no open floor found: a ring around the centre
            g_aw.cps.clear();
            float gz = g_aw.floorZ;
            for (int i = 0; i < 6; ++i) { float a = i * 1.0472f; g_aw.cps.push_back(V3(AW_CENTRE.x + std::cos(a) * 28.f, AW_CENTRE.y + std::sin(a) * 28.f, gz + 1.f)); }
        }
    }
    if (g_aw.mode == AWM_FLAGWAR) AwFlagSetup();
    AwTrapsSpawn();
    JobSkyDown();
    if (g_aw.solo) {   // alone: the countdown once you have landed, the match as long as chosen
        int late = Now() + 3200 - g_aw.startAt;
        if (late > 0) { g_aw.startAt += late; g_aw.endAt += late; }
    }
    Shard("~o~ARENA WAR", std::string(AW_MODES[g_aw.mode].name) + " - " + AW_THEMES[g_aw.theme].name);
    Log("arena war: %s, %s map %d (floor %.1f), %s, %d min, %d bots, %d traps, %s", AW_MODES[g_aw.mode].name, AW_THEMES[g_aw.theme].name, g_aw.map + 1, g_aw.floorZ, AwModel(g_aw.theme, g_aw.veh).c_str(),
        g_aw.minutes, (int)g_aw.botList.size(), (int)g_aw.trapList.size(), g_aw.solo ? "solo" : "online");
}
static void AwCpClear() { if (g_aw.cpHandle) { N(N_DELETE_CHECKPOINT, g_aw.cpHandle); g_aw.cpHandle = 0; } RemoveBlip(g_aw.cpBlip); }
static void AwCleanup() {
    AwCpClear(); AwFlagClear(); AwTrapsClear();
    for (auto &b : g_aw.botList) AwRemoveBot(b);
    if (g_aw.countdownMovie) N(N_SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED, &g_aw.countdownMovie);
    JobSkyUp();
    if (InAnyVehicle()) N(N_SET_ENTITY_COORDS, Me(), g_aw.back.x, g_aw.back.y, g_aw.back.z + 1.f, 0, 0, 0, 0);
    if (Exists(g_aw.myVeh)) DeleteEntity(g_aw.myVeh);
    N(N_CLEAR_ROOM_FOR_ENTITY, Me());
    Vec3 b = g_aw.back; float h = g_aw.backH;
    g_aw = AwRun();
    Teleport(b.x, b.y, b.z, h);
    JobSkyDown();
}
static int AwApGet() { return AcidGet("arena_ap"); }   // the Arena Points total (Gori's per-character custom store)
static void AwFinish() {
    if (g_aw.phase == AWP_RESULTS) return;
    g_aw.phase = AWP_RESULTS; g_aw.resultsUntil = Now() + 12000 + 9000;
    AwCpClear(); AwFlagClear(); AwTrapsClear();
    int ap = 20 + 10 * g_aw.kills, cash = std::min(60000, 5000 + 1500 * g_aw.kills);
    std::string line = std::to_string(g_aw.kills) + " kills, " + std::to_string(g_aw.deaths) + " deaths";
    if (g_aw.mode == AWM_HOTBOMB) { ap = 20 + 15 * g_aw.score; cash = std::min(60000, 4000 + 2000 * g_aw.score); line = std::to_string(g_aw.score) + " rounds survived"; }
    if (g_aw.mode == AWM_MONSTERS) {
        ap = g_aw.deaths == 0 ? 80 : std::max(20, 60 - 10 * g_aw.deaths); cash = g_aw.deaths == 0 ? 16000 : std::max(4000, 12000 - 2000 * g_aw.deaths);
        line = g_aw.deaths == 0 ? "Survived in one piece!" : std::to_string(g_aw.deaths) + " times wrecked";
    }
    if (g_aw.mode == AWM_GAMESMASTERS) { ap = 20 + 10 * g_aw.score; cash = std::min(60000, 2000 + 1500 * g_aw.score); line = std::to_string(g_aw.score) + " checkpoints"; }
    if (g_aw.mode == AWM_FLAGWAR) {
        bool won = g_aw.score > g_aw.botScore;
        ap = 20 + 15 * g_aw.score + (won ? 20 : 0); cash = std::min(60000, 3000 + 2500 * g_aw.score + (won ? 5000 : 0));
        line = std::string(won ? "You won " : g_aw.score == g_aw.botScore ? "A draw, " : "The bots won ") + std::to_string(g_aw.score) + " - " + std::to_string(g_aw.botScore);
    }
    AcidSet("arena_ap", AwApGet() + ap);
    if (g_aw.solo) AddCash(cash);
    int rpBefore = g_rp, rp = 300 + 50 * (g_aw.kills + g_aw.score);
    GiveRP(rp, nullptr);
    Save();
    bool lost = g_aw.mode == AWM_FLAGWAR && g_aw.score < g_aw.botScore;
    JobCelebrate(std::string("Arena War - ") + AW_MODES[g_aw.mode].name, 0, !lost, g_jobStartedAt ? Now() - g_jobStartedAt : 0, rpBefore, rp,
                 g_aw.solo ? cash : 0, line + "  +" + std::to_string(ap) + " AP");
}

// ---- host / join (online: a deathmatch job, kind 2, text "A|...")
static std::string AwText() {
    char b[64]; std::snprintf(b, sizeof b, "A|%d|%d|%d|%d|%d", g_aw.theme, g_aw.map, g_aw.light, g_aw.minutes, g_aw.veh);
    return b;
}
static void AwStart(bool online) {
    if (g_mActive || g_race.phase != RP_NONE || g_dm.phase != DP_NONE || g_sv.phase != SVP_NONE || g_aw.phase != AWP_NONE) { Notify("~r~Finish your race, deathmatch or job first."); return; }
    g_aw = AwRun();
    g_aw.theme = g_awSel[0]; g_aw.map = AwFixMap(g_aw.theme, g_awSel[1]); g_aw.light = g_awSel[2]; g_aw.veh = g_awSel[3];
    g_aw.bots = g_awSel[4] * 2; g_aw.minutes = AW_MINUTES[g_awSel[5]]; g_aw.host = true; g_aw.mode = g_awSel[6] % AWM_COUNT; g_aw.traps = g_awSel[7] != 0;
    if (g_aw.mode == AWM_HOTBOMB || g_aw.mode == AWM_MONSTERS || g_aw.mode == AWM_FLAGWAR) g_aw.bots = std::max(g_aw.bots, 3);   // they need someone to play against
    if (g_aw.mode != AWM_CARNAGE) online = false;
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
    g_aw.map = AwFixMap(g_aw.theme, std::max(0, std::atoi(f[2].c_str())));
    g_aw.light = std::max(0, std::min(NAW_LIGHTS - 1, std::atoi(f[3].c_str())));
    g_aw.minutes = std::max(1, std::atoi(f[4].c_str()));
    g_aw.veh = host ? std::max(0, std::min(NAW_VEHS - 1, std::atoi(f[5].c_str()))) : g_awSel[3];   // members drive their own pick
    g_aw.payout = g_coop.payout;
    AwBegin((int)g_coop.startsIn);
}

// ---- the bot modes' rules
static int AwHolderVeh() { return g_aw.bombHolder == -1 ? g_aw.myVeh : g_aw.bombHolder >= 0 && g_aw.bombHolder < (int)g_aw.botList.size() ? g_aw.botList[g_aw.bombHolder].veh : 0; }
static void AwGiveBomb(int who) {
    g_aw.bombHolder = who; g_aw.lastPass = Now();
    if (who == -1) { Notify("~r~You have the bomb!~s~ Ram someone to pass it on."); Sound("Out_Of_Area", "DLC_Lowrider_Relay_Race_Sounds"); }
    else Sound("Bomb_Passed", "DLC_AW_Hot_Bomb_Sounds");
    for (auto &b : g_aw.botList) b.retaskAt = 0;   // the bots change their plan at once
}
static void AwModeTick() {
    int me = Me();
    if (g_aw.mode == AWM_HOTBOMB) {
        auto alive = [](int v) { return Exists(v) && !Dead(v) && NB(N<int>(N_IS_VEHICLE_DRIVEABLE, v, 0)); };
        if (g_aw.bombHolder == -2 && Now() < g_aw.bombEnds) return;   // a breather between rounds
        if (g_aw.bombHolder == -2 || !alive(AwHolderVeh())) {   // a new round (or the holder got wrecked): the bomb goes to someone at random
            int n = (int)g_aw.botList.size(), pick = Rand(n + 1) - 1;
            if (pick >= 0 && !alive(g_aw.botList[pick].veh)) pick = -1;
            if (pick == -1 && !alive(g_aw.myVeh)) return;
            g_aw.bombEnds = Now() + 25000;
            AwGiveBomb(pick);
        }
        int hv = AwHolderVeh();
        if (Exists(hv)) {   // the marker over the holder
            Vec3 p = Pos(hv);
            N(N_DRAW_MARKER, 0, p.x, p.y, p.z + 2.6f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.2f, 1.2f, 1.2f, 255, 40, 20, 220, 1, 0, 2, 1, 0, 0, 0);
            if (Now() - g_aw.lastPass > 1500) {   // passing: touch another car
                if (g_aw.bombHolder != -1 && Exists(g_aw.myVeh) && NB(N<int>(N_IS_ENTITY_TOUCHING_ENTITY, hv, g_aw.myVeh))) AwGiveBomb(-1);
                else for (int i = 0; i < (int)g_aw.botList.size(); ++i)
                    if (i != g_aw.bombHolder && Exists(g_aw.botList[i].veh) && NB(N<int>(N_IS_ENTITY_TOUCHING_ENTITY, hv, g_aw.botList[i].veh))) { AwGiveBomb(i); break; }
            }
        }
        if (Now() >= g_aw.bombEnds) {   // boom: whoever holds it goes up, everybody else survived the round
            int h2 = AwHolderVeh();
            if (Exists(h2)) N(N_EXPLODE_VEHICLE, h2, 1, 0);
            if (g_aw.bombHolder == -1) Shard("~r~BOOM", "You were holding the bomb.");
            else { ++g_aw.score; Shard("~g~ROUND SURVIVED", std::to_string(g_aw.score) + " so far"); }
            g_aw.bombHolder = -2; g_aw.bombEnds = Now() + 4000;
        }
        return;
    }
    if (g_aw.mode == AWM_GAMESMASTERS && !g_aw.cps.empty()) {
        Vec3 c = g_aw.cps[(size_t)g_aw.score % g_aw.cps.size()], nx = g_aw.cps[(size_t)(g_aw.score + 1) % g_aw.cps.size()];
        if (!g_aw.cpHandle) {
            g_aw.cpHandle = N<int>(N_CREATE_CHECKPOINT, 1, c.x, c.y, c.z - 1.f, nx.x, nx.y, nx.z, 10.f, 255, 140, 0, 170, 0);
            N(N_SET_CHECKPOINT_CYLINDER_HEIGHT, g_aw.cpHandle, 8.f, 8.f, 10.f);
            g_aw.cpBlip = CoordBlip(c, 1, 47, "Checkpoint", false);
        }
        Vec3 mp = Pos(me);
        if (std::hypot(mp.x - c.x, mp.y - c.y) < 10.f && std::fabs(mp.z - c.z) < 8.f) {
            ++g_aw.score;
            AwCpClear();
            Sound("CHECKPOINT_NORMAL", "HUD_MINI_GAME_SOUNDSET");
        }
    }
}

// ---- HUD, results, each frame
static void AwHud() {
    int row = 0;
    RaceBar(row++, "TIME LEFT", RaceTime(std::max(0, g_aw.endAt - Now())).substr(0, 5));
    if (g_aw.mode == AWM_HOTBOMB) {
        RaceBar(row++, "BOMB", g_aw.bombHolder == -1 ? "YOU" : g_aw.bombHolder >= 0 ? "BOT" : "-");
        RaceBar(row++, "FUSE", RaceTime(std::max(0, g_aw.bombEnds - Now())).substr(0, 5));
        RaceBar(row++, "ROUNDS", std::to_string(g_aw.score), true);
        return;
    }
    if (g_aw.mode == AWM_FLAGWAR) {
        RaceBar(row++, "FLAG", g_aw.flagCarrier == -1 ? "YOU" : g_aw.flagCarrier >= 0 ? "BOT" : "FREE");
        RaceBar(row++, "BOTS", std::to_string(g_aw.botScore));
        RaceBar(row++, "YOU", std::to_string(g_aw.score), true);
        return;
    }
    RaceBar(row++, g_aw.mode == AWM_MONSTERS ? "WRECKED" : "DEATHS", std::to_string(g_aw.deaths));
    if (g_aw.mode == AWM_GAMESMASTERS) RaceBar(row++, "CHECKPOINTS", std::to_string(g_aw.score), true);
    else if (g_aw.mode == AWM_CARNAGE) RaceBar(row++, "KILLS", std::to_string(g_aw.kills), true);
}
static void AwResults() {
    float x = 0.5f, y = 0.3f, w = 0.4f;
    Rect(x, y, w, 0.05f, 255, 120, 0, 230);
    Text(std::string(AW_MODES[g_aw.mode].name) + ": " + AW_THEMES[g_aw.theme].name, x, y - 0.018f, 0.5f, 1, 255, 255, 255, 255, 0);
    bool fw = g_aw.mode == AWM_FLAGWAR;
    const char *rows[] = {g_aw.mode == AWM_HOTBOMB ? "Rounds survived" : g_aw.mode == AWM_GAMESMASTERS ? "Checkpoints" : fw ? "Your flags" : "Kills",
                          g_aw.mode == AWM_MONSTERS ? "Times wrecked" : fw ? "Bots' flags" : "Deaths", "Bots", "Arena Points total"};
    int vals[] = {g_aw.mode == AWM_HOTBOMB || g_aw.mode == AWM_GAMESMASTERS || fw ? g_aw.score : g_aw.kills, fw ? g_aw.botScore : g_aw.deaths, (int)g_aw.botList.size(), AwApGet()};
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
            N(N_SET_ENTITY_COORDS, me, AW_CENTRE.x, AW_CENTRE.y, g_aw.floorZ + 6.f, 0, 0, 0, 0);
            for (int i = 0; i < 10; ++i) { AwForceRoom(); Wait(30); }
            AwPlacePlayer();
            N(N_DO_SCREEN_FADE_IN, 400);
        }
        // alive but out of the arena (LocalOnline's hospital respawn can land after our return): back in, every 3 s at most
        if (!g_aw.wasDown && !Dead(me) && std::hypot(Pos(me).x - AW_CENTRE.x, Pos(me).y - AW_CENTRE.y) > 150.f && Now() - g_aw.downAt > 3000) {
            g_aw.downAt = Now();
            Log("arena: you were outside the arena, bringing you back");
            N(N_DO_SCREEN_FADE_OUT, 300); Wait(350);
            N(N_SET_ENTITY_COORDS, me, AW_CENTRE.x, AW_CENTRE.y, g_aw.floorZ + 6.f, 0, 0, 0, 0);
            for (int i = 0; i < 10; ++i) { AwForceRoom(); Wait(30); }
            AwPlacePlayer();
            N(N_DO_SCREEN_FADE_IN, 400);
        }
        // on its roof or stuck: a fresh car after 3 s, like Online's arena (not a death)
        if (!down && Exists(g_aw.myVeh)) {
            bool stuck = NB(N<int>(N_IS_VEHICLE_STUCK_ON_ROOF, g_aw.myVeh)) || NB(N<int>(N_IS_VEHICLE_STUCK_TIMER_UP, g_aw.myVeh, 1, 4000)) ||
                         NB(N<int>(N_IS_VEHICLE_STUCK_TIMER_UP, g_aw.myVeh, 3, 4000));
            if (Pos(g_aw.myVeh).z < g_aw.floorZ - 6.f && Pos(g_aw.myVeh).z < 99.f) {   // fell through the floor: a new car now
                g_aw.stuckSince = 0;
                N(N_DO_SCREEN_FADE_OUT, 250); Wait(300);
                AwPlacePlayer();
                N(N_DO_SCREEN_FADE_IN, 300);
                stuck = false;
            }
            if (stuck) {
                if (!g_aw.stuckSince) g_aw.stuckSince = Now();
                Text("Stuck: a new car in " + std::to_string(std::max(0, 3 - (Now() - g_aw.stuckSince) / 1000)), 0.5f, 0.85f, 0.45f, 4, 255, 200, 80, 255, 0, 1.f, true);
                if (Now() - g_aw.stuckSince > 3000) {
                    g_aw.stuckSince = 0;
                    N(N_DO_SCREEN_FADE_OUT, 250); Wait(300);
                    AwPlacePlayer();
                    N(N_DO_SCREEN_FADE_IN, 300);
                }
            } else g_aw.stuckSince = 0;
        }
        if (g_aw.solo) AwBotsTick();
        AwTrapsTick();
        if (g_aw.mode == AWM_FLAGWAR) AwFlagTick();
        AwModeTick();
        AwHud();
        if (Now() >= g_aw.endAt) {
            if (!g_aw.solo && g_aw.host && LO().jobUpdate && !g_aw.ended) { g_aw.ended = true; LO().jobUpdate(COOP_PASSED, 0, -376.f, -1862.f, 20.f, ""); }
            AwFinish();
        }
        return;
    }
    if (g_aw.phase == AWP_RESULTS) {
        if (!JobCelebrating()) AwResults();
        if (Now() > g_aw.resultsUntil) AwCleanup();
    }
}

// ---- the Arena War menu (Interaction Menu, or E at the Maze Bank Arena)
static bool ArenaWarMenu(const Frame &f, std::string &title, std::string &sub, std::vector<Item> &items) {
    if ((int)f.id != AW_MENU) return false;
    title = "Arena War"; sub = "EVENTS";
    if (g_aw.host && g_aw.phase == AWP_LOBBY) {
        int n = g_coop.members.empty() ? 1 : 2;
        for (char c : g_coop.members) if (c == ',') ++n;
        items.push_back({"Start Arena War", std::to_string(n) + (n == 1 ? " player" : " players"), "Everyone in the lobby goes into the arena.", [] { CloseMenu(); AwHostStart(); }});
        items.push_back({"Cancel", "", "", [] { if (LO().jobUpdate) LO().jobUpdate(COOP_CLOSED, 0, 0, 0, 0, ""); g_aw = AwRun(); CloseMenu(); }});
        return true;
    }
    if (g_aw.phase != AWP_NONE) { items.push_back({"Leave the Arena", "", "", [] { CloseMenu(); if (!g_aw.solo && !g_aw.host && LO().jobJoin) LO().jobJoin(0);
        if (g_aw.phase != AWP_RESULTS) Notify("You left the event: no rewards.");   // like Online: quitters get nothing
        AwCleanup(); }}); return true; }
    int th = g_awSel[0], mode = g_awSel[6] % AWM_COUNT;
    g_awSel[1] = AwFixMap(th, g_awSel[1]);
    int playable = 0, pos = 0;
    for (int m = 0; m < AW_THEMES[th].maps; ++m) if (!AwMapBare(th, m)) { ++playable; if (m == g_awSel[1]) pos = playable; }
    items.push_back({"Mode", std::string("< ") + AW_MODES[mode].name + " >", AW_MODES[mode].desc, [] { g_awSel[6] = (g_awSel[6] + 1) % AWM_COUNT; }});
    items.push_back({"Theme", std::string("< ") + AW_THEMES[th].name + " >", std::string("Arena vehicles in their ") + AW_THEMES[th].variant + " versions.",
                     [] { g_awSel[0] = (g_awSel[0] + 1) % 3; g_awSel[1] = 0; }});
    items.push_back({"Map", "< " + std::to_string(pos) + " / " + std::to_string(playable) + " >", "The arena's layout.", [] { g_awSel[1] = AwFixMap(g_awSel[0], g_awSel[1] + 1); }});
    items.push_back({"Lighting", std::string("< ") + AW_LIGHTS[g_awSel[2]].name + " >", "", [] { g_awSel[2] = (g_awSel[2] + 1) % NAW_LIGHTS; }});
    std::string vname = std::string(AW_VEHS[g_awSel[3]][0]) + " (" + AW_THEMES[th].variant + ")";
    items.push_back({"Vehicle", "< " + vname + " >", "If you own this model, its upgrades come with it.", [] { g_awSel[3] = (g_awSel[3] + 1) % NAW_VEHS; }});
    items.push_back({"Bots (solo)", "< " + std::to_string(g_awSel[4] * 2) + " >", "AI drivers that come for you.", [] { g_awSel[4] = (g_awSel[4] + 1) % 5; }});
    items.push_back({"Time", "< " + std::to_string(AW_MINUTES[g_awSel[5]]) + " min >", "", [] { g_awSel[5] = (g_awSel[5] + 1) % 3; }});
    items.push_back({"Traps (solo)", g_awSel[7] ? "< On >" : "< Off >", "Flippers, fire pits, mines and spikes on the arena floor.", [] { g_awSel[7] = !g_awSel[7]; }});
    items.push_back({"Play", "", "The event's lobby: mode, arena, vehicle and the rest, alone or online.", [] { CloseMenu(); OpenAwCorona(); }});
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
