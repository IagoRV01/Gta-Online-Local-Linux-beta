// ---------------------------------------------------------------- every activity starts from its lobby (Corona.inc)
// Races, Survival, Deathmatch, Arena War and the heists: the activity's own settings on the settings page; Closed
// plays alone, Open opens the activity's own lobby job (the other players join from Jobs), Play starts it.

// ---- races: laps, vehicle class, vehicle
static void OpenRaceCorona(int idx) {
    if (idx < 0 || idx >= (int)g_raceList.size()) return;
    const RaceDef &r = g_raceList[idx];
    CoronaDef d;
    d.title = r.name;
    d.desc = (r.desc.empty() ? std::string(r.laps ? "A circuit race." : "A point-to-point race.") : r.desc) +
             (r.transforms.empty() ? "" : "  Transform race: purple checkpoints change your vehicle.") + "  Paid by position.";
    d.theme = 1; d.maxPlayers = 8; d.payout = 12000;
    Vec3 t = r.trigger.x != 0.f ? r.trigger : (r.grid.empty() ? Pos(Me()) : r.grid[0].first);
    d.at = t;
    bool transform = !r.transforms.empty();
    if (r.laps) { CoronaOpt o{"Laps", {}, std::clamp(r.defLaps > 0 ? r.defLaps - 1 : 2, 0, 9), "How many laps."}; for (int i = 1; i <= 10; ++i) o.values.push_back(std::to_string(i)); d.opts.push_back(o); }
    if (!transform) { CoronaOpt o{"Vehicle Class", {}, 0, "The class everyone races in."}; for (int i = 0; i < NRACE_CLASSES; ++i) o.values.push_back(RACE_CLASSES[i].name); d.opts.push_back(o); }
    d.opts.push_back({transform ? "Start Vehicle" : "Vehicle", {"elegy"}, 0, "Your vehicle."});
    std::string file = r.file;
    bool laps = r.laps;
    d.onChange = [idx, transform, laps](CoronaDef &def) {   // the vehicle list follows the class
        const RaceDef &rr = g_raceList[idx];
        int cls = transform ? 0 : def.opts[laps ? 1 : 0].sel;
        std::vector<std::string> vehs;
        if (cls == 0) vehs = rr.vehicles; else for (const char *m : RACE_CLASSES[cls].models) vehs.push_back(m);
        if (vehs.empty()) vehs.push_back("elegy");
        CoronaOpt &v = def.opts.back();
        if (v.values != vehs) { v.values = vehs; v.sel = 0; }
    };
    auto args = [laps](const std::vector<int> &sel, int &lapsOut, std::string &veh) {
        lapsOut = laps && !sel.empty() ? sel[0] + 1 : 1;
        veh = g_cor.def.opts.back().values[std::min((int)g_cor.def.opts.back().values.size() - 1, sel.back())];
    };
    d.onPlay = [file, args](const std::vector<int> &sel) { int l; std::string v; args(sel, l, v); RaceStartSolo(file, l, v); };
    d.openLobby = [file, args](const std::vector<int> &sel) { int l; std::string v; args(sel, l, v); RaceHostOnline(file, l, v); return g_race.phase == RP_LOBBY; };
    d.startLobby = [] { RaceHostStart(); };
    d.cancelLobby = [] { if (LO().jobUpdate) LO().jobUpdate(COOP_CLOSED, 0, 0, 0, 0, ""); g_race = RaceRun(); };
    OpenCorona(d);
}

// ---- Survival: the location's ten waves
static void OpenSvCorona(int loc) {
    const SvLoc &l = SV_LOCS[loc];
    CoronaDef d;
    d.title = std::string("Survival: ") + l.name;
    d.desc = "Ten waves of enemies, each harder than the last. Paid by the waves you survive (" + Money(SV_PAYOUT) + " for all ten). Dying alone ends it.";
    d.theme = 3; d.maxPlayers = 4; d.payout = SV_PAYOUT;
    d.at = V3(l.x, l.y, l.z);
    d.onPlay = [loc](const std::vector<int> &) { SvHost(loc, true); };
    d.openLobby = [loc](const std::vector<int> &) { SvHost(loc, false); return g_sv.phase == SVP_LOBBY; };
    d.startLobby = [] { SvHostStart(); };
    d.cancelLobby = [] { if (LO().jobUpdate) LO().jobUpdate(COOP_CLOSED, 0, 0, 0, 0, ""); g_sv = SvRun(); };
    OpenCorona(d);
}

// ---- Deathmatch: time limit, weapons (needs other players)
static void OpenDmCorona(int arena) {
    const DmArena &a = DM_ARENAS[arena];
    CoronaDef d;
    d.title = std::string("Deathmatch: ") + a.name;
    d.desc = "Every player for themselves. Most kills wins; paid by position.";
    d.theme = 2; d.maxPlayers = 8; d.payout = 10000; d.solo = false;
    d.at = V3(a.x, a.y, a.z);
    d.opts.push_back({"Time Limit", {"5 min", "10 min", "15 min"}, 1, ""});
    d.opts.push_back({"Weapons", {DM_LOADOUT_NAMES[0], DM_LOADOUT_NAMES[1], DM_LOADOUT_NAMES[2]}, 1, "Given to everyone (with full armor) at every spawn."});
    d.openLobby = [arena](const std::vector<int> &sel) { DmHost(arena, 5 * (sel[0] + 1), sel[1]); return g_dm.phase == DP_LOBBY; };
    d.startLobby = [] { DmHostStart(); };
    d.cancelLobby = [] { if (LO().jobUpdate) LO().jobUpdate(COOP_CLOSED, 0, 0, 0, 0, ""); g_dm = DmRun(); };
    OpenCorona(d);
}

// ---- Arena War: mode, theme, map (the theme's playable ones), lighting, vehicle, bots, time, traps
static void OpenAwCorona() {
    CoronaDef d;
    d.title = "Arena War";
    d.desc = "Events in the Maze Bank Arena: Carnage online against the other players, or any mode against bots.";
    d.theme = 2; d.maxPlayers = 8; d.payout = 12000;
    d.at = V3(-376.f, -1862.f, 20.f);
    CoronaOpt mode{"Mode", {}, g_awSel[6] % AWM_COUNT, ""}; for (int i = 0; i < AWM_COUNT; ++i) mode.values.push_back(AW_MODES[i].name);
    CoronaOpt theme{"Theme", {"Apocalypse", "Sci-Fi", "Consumer Wasteland"}, g_awSel[0] % 3, "The arena's dressing and the vehicles' versions."};
    CoronaOpt light{"Lighting", {}, g_awSel[2] % NAW_LIGHTS, ""}; for (int i = 0; i < NAW_LIGHTS; ++i) light.values.push_back(AW_LIGHTS[i].name);
    CoronaOpt veh{"Vehicle", {}, g_awSel[3] % NAW_VEHS, "If you own this model, its upgrades come with it."}; for (int i = 0; i < NAW_VEHS; ++i) veh.values.push_back(AW_VEHS[i][0]);
    d.opts = {mode, theme, {"Map", {"1"}, 0, "The arena's layout."}, light, veh,
              {"Bots", {"0", "2", "4", "6", "8"}, g_awSel[4] % 5, "AI drivers in arena vehicles (playing alone)."},
              {"Time", {"5 min", "10 min", "15 min"}, g_awSel[5] % 3, ""},
              {"Traps", {"Off", "On"}, g_awSel[7] ? 1 : 0, "Flippers, fire pits, mines and spikes on the floor (playing alone)."}};
    d.onChange = [](CoronaDef &def) {   // the theme's playable maps; the mode's description
        int th = def.opts[1].sel;
        std::vector<std::string> maps;
        for (int m = 0; m < AW_THEMES[th].maps; ++m) if (!AwMapBare(th, m)) maps.push_back(std::to_string(m + 1));
        if (def.opts[2].values != maps) { def.opts[2].values = maps; def.opts[2].sel = 0; }
        def.opts[0].desc = AW_MODES[def.opts[0].sel].desc;
        def.solo = true;
    };
    auto apply = [](const std::vector<int> &sel) {
        g_awSel[6] = sel[0]; g_awSel[0] = sel[1];
        int th = sel[1], k = 0;
        for (int m = 0; m < AW_THEMES[th].maps; ++m) if (!AwMapBare(th, m)) { if (k == sel[2]) g_awSel[1] = m; ++k; }
        g_awSel[2] = sel[3]; g_awSel[3] = sel[4]; g_awSel[4] = sel[5]; g_awSel[5] = sel[6]; g_awSel[7] = sel[7];
    };
    d.onPlay = [apply](const std::vector<int> &sel) { apply(sel); AwStart(false); };
    d.openLobby = [apply](const std::vector<int> &sel) {
        if (sel[0] != AWM_CARNAGE) { Notify("~r~Online, Arena War is Carnage: the other modes are against bots."); return false; }
        apply(sel); AwStart(true); return g_aw.phase == AWP_LOBBY;
    };
    d.startLobby = [] { AwHostStart(); };
    d.cancelLobby = [] { if (LO().jobUpdate) LO().jobUpdate(COOP_CLOSED, 0, 0, 0, 0, ""); g_aw = AwRun(); };
    OpenCorona(d);
}

// ---- heists: the next stage (prep or finale), co-op through the mission's job
static void OpenHeistCorona(int h) {
    std::string why;
    if (!HeistUnlocked(h, why)) { Notify("~r~" + why); return; }
    int st = g_heistStage[h];
    bool finale = st == HEISTS[h].stages - 1;
    CoronaDef d;
    d.title = std::string(HEISTS[h].name) + (finale ? " - Finale" : std::string(" - ") + HEISTS[h].stageNames[st]);
    d.desc = std::string("Lester: ") + HEISTS[h].desc + (finale ? "  The finale: " + Money(HEISTS[h].payout) + "." : "  A prep for the finale.") +
             (st == 0 ? "  Setup cost " + Money(HEISTS[h].fee) + "." : "");
    d.theme = 0; d.maxPlayers = 4; d.payout = finale ? HEISTS[h].payout : 20000;
    d.at = Pos(Me());
    d.onPlay = [h](const std::vector<int> &) { StartHeist(h); };
    OpenCorona(d);
}
