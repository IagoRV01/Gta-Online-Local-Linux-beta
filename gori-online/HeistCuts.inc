// ---------------------------------------------------------------- Online's heist cutscenes (MPH_* / APA_*), played in your apartment
// As fmmc_launcher.c does (func_8181, func_10522, func_10525, func_10539): the cut file gives the number of sections; the crew's
// roles are MP_1..MP_4 (you as MP_1, without helmet, hat or mask, nobody in the others; the Fleeca Job has only two roles; Online
// uses a clone of you, but in story mode the scene takes the player's own ped for MP_1 whatever is registered, so it's you who
// plays it, in your clothes (the scene would dress the role in its own), and you're put back where you were); each section is placed by SET_CUTSCENE_ORIGIN at the apartment's metadata point 304, and the second part of the APA_
// intros at point 546. Those two also have a playback list for each kind of apartment. A scene you have already watched can
// be skipped; the watched ones are kept in the profile. Our own apartments have no such points: no cutscene there.
enum { CUT_INT, CUT_MID, CUT_FIN };
static const char *HeistCutName(int h, int which) {
    static const char *const NAMES[5][3] = {
        {"MPH_TUT_INT", "MPH_TUT_MID", "MPH_TUT_FIN_INT"}, {"APA_PRI_INT", "MPH_PRI_MID", "MPH_PRI_FIN_INT"},
        {"MPH_HUM_INT", "MPH_HUM_MID", "APA_HUM_FIN_INT"}, {"APA_NAR_INT", "APA_NAR_MID", "MPH_NAR_FIN_INT"},
        {"MPH_PAC_INT", "MPH_PAC_MID", "MPH_PAC_FIN_INT"}};
    return h >= 0 && h < 5 && which >= 0 && which < 3 ? NAMES[h][which] : nullptr;
}
static int CutPlaylist(const char *name, int map) {
    bool pri = !std::strcmp(name, "APA_PRI_INT"), nar = !std::strcmp(name, "APA_NAR_INT");
    if (!pri && !nar) return -1;
    switch (map) {
        case 1: return pri ? 482 : 69;   // the Del Perro Heights kind
        case 2: return pri ? 488 : 81;   // stilt houses
        case 3: return pri ? 484 : 73;
        case 4: return pri ? 496 : 97;   // the custom apartments
        default: return pri ? 481 : 67;
    }
}
static void HbFrame();
static void HbDrawOnlineSpot();
static void HbSetHeist(int h);
static bool g_hbInCut = false;   // the board is being drawn for a scene: no prompt
struct CutOutfit { int d[12], t[12], pal[12], pd[8], pt[8]; };
static CutOutfit CutOutfitOf(int me) {
    CutOutfit f{};
    for (int k = 0; k < 12; ++k) { f.d[k] = N<int>(N_GET_PED_DRAWABLE_VARIATION, me, k); f.t[k] = N<int>(N_GET_PED_TEXTURE_VARIATION, me, k); f.pal[k] = N<int>(N_GET_PED_PALETTE_VARIATION, me, k); }
    for (int k = 0; k < 8; ++k) { f.pd[k] = N<int>(N_GET_PED_PROP_INDEX, me, k); f.pt[k] = N<int>(N_GET_PED_PROP_TEXTURE_INDEX, me, k); }
    return f;
}
static void CutOutfitRestore(int me, const CutOutfit &f) {
    for (int k = 0; k < 12; ++k) N(N_SET_PED_COMPONENT_VARIATION, me, k, f.d[k], f.t[k], f.pal[k]);
    for (int k = 0; k < 8; ++k) { if (f.pd[k] >= 0) N(N_SET_PED_PROP_INDEX, me, k, f.pd[k], f.pt[k], 1); else N(N_CLEAR_PED_PROP, me, k, 1); }
}
// in the scene: your clothes again whenever it changes them, no mask, no hat
static void CutDress(int me, const CutOutfit &f) {
    for (int k = 2; k < 12; ++k)
        if (N<int>(N_GET_PED_DRAWABLE_VARIATION, me, k) != f.d[k] || N<int>(N_GET_PED_TEXTURE_VARIATION, me, k) != f.t[k]) N(N_SET_PED_COMPONENT_VARIATION, me, k, f.d[k], f.t[k], f.pal[k]);
    if (N<int>(N_GET_PED_DRAWABLE_VARIATION, me, 1) != 0) N(N_SET_PED_COMPONENT_VARIATION, me, 1, 0, 0, 0);
    if (N<int>(N_GET_PED_PROP_INDEX, me, 0) >= 0) N(N_CLEAR_PED_PROP, me, 0, 1);
}
// the crew in the scene, as Online's: the other players of the heist's job (its host and members, in the lobby's order) play
// MP_2..MP_4 with their own characters, the puppets LocalOnline draws for them here (held still while the scene moves them)
static std::vector<int> CutCrewPeds() {
    std::vector<int> out;
    if (!LO().getPlayerPed || !LO().getPlayers || !g_coop.id) return out;
    if (g_myName.empty() && LO().getPlayerName) { char b[64] = {0}; LO().getPlayerName(b, sizeof b); g_myName = b; }
    std::vector<std::string> names;
    if (!g_coop.hosting && g_coop.host != "you" && !g_coop.host.empty()) names.push_back(g_coop.host);
    for (auto &n : Split(g_coop.members, ',')) if (!n.empty() && n != g_myName && n != "you") names.push_back(n);
    std::string t = CoopFetch(LO().getPlayers);
    for (auto &n : names) {
        for (auto &line : Split(t, '\n')) {
            std::vector<std::string> f = Split(line, '|');
            if (f.size() < 2 || f[1] != n) continue;
            int ped = LO().getPlayerPed((unsigned)std::strtoul(f[0].c_str(), nullptr, 10));
            if (ped && Exists(ped) && Dist(Pos(ped), Pos(Me())) < 60.f) out.push_back(ped);
            break;
        }
    }
    return out;
}
static int g_cutDebug = 0;   // cutdbg: log who plays the crew's roles (test)
// world: a scene that plays where it was made (the setups' ends, fm_mission_controller's plain START_CUTSCENE), not in the apartment
static bool PlayCut(const char *name, int roles, bool canSkip, bool world = false, int prop = -1) {
    Vec3 o{}, o2{}; float oh = 0.f, oh2 = 0.f;
    int inside = prop >= 0 ? prop : g_inside;   // prop: the leader's apartment (a member watching the leader's scene)
    if (!world && (inside < 0 || !OnlinePoint(inside, 304, o, oh))) { Log("cut %s: not in an Online apartment (inside %d)", name, inside); return false; }
    bool two = !world && OnlinePoint(inside, 546, o2, oh2);
    const OInterior *t = PIntOf(inside);
    int sections = 1, t0 = Now();
    while (Now() - t0 < 8000) {
        N(N_REQUEST_CUT_FILE, name);
        if (NB(N<int>(N_HAS_CUT_FILE_LOADED, name))) { sections = N<int>(N_GET_CUT_FILE_CONCAT_COUNT, name); N(N_REMOVE_CUT_FILE, name); break; }
        Wait(0);
    }
    int list = CutPlaylist(name, t ? t->map : 0);
    Log("cut %s: %d sections, list %d, origin %.2f %.2f %.2f h %.1f", name, sections, list, o.x, o.y, o.z, oh);
    if (list >= 0) N(N_REQUEST_CUTSCENE_WITH_PLAYBACK_LIST, name, list, 8);
    else N(N_REQUEST_CUTSCENE, name, 8);
    const char *const R[] = {"MP_1", "MP_2", "MP_3", "MP_4"};
    t0 = Now(); bool flags = false;
    while (Now() - t0 < 45000 && !NB(N<int>(N_HAS_CUTSCENE_LOADED))) {
        if (!flags && NB(N<int>(N_CAN_REQUEST_ASSETS_FOR_CUTSCENE_ENTITY))) {
            flags = true;
            for (int i = 0; i < roles; ++i) N(N_SET_CUTSCENE_ENTITY_STREAMING_FLAGS, R[i], 0, 1);
        }
        Wait(0);
    }
    if (!NB(N<int>(N_HAS_CUTSCENE_LOADED))) { Log("cut %s: NOT loaded after 45 s", name); N(N_REMOVE_CUTSCENE); return false; }
    int me = Me();
    Vec3 at = Pos(me); float heading = N<float>(N_GET_ENTITY_HEADING, me);
    CutOutfit outfit = CutOutfitOf(me);
    N(N_SET_PED_HELMET, me, 0); N(N_REMOVE_PED_HELMET, me, 1);
    std::vector<int> crew = CutCrewPeds();
    std::vector<CutOutfit> crewOutfit;
    if (!crew.empty()) LO().holdPuppets(3000);
    for (int i = 0; i < roles; ++i) {
        if (i == 0) N(N_REGISTER_ENTITY_FOR_CUTSCENE, me, R[i], 0, 0, 67);
        else if (i - 1 < (int)crew.size()) {
            int p = crew[i - 1];
            crewOutfit.push_back(CutOutfitOf(p));
            N(N_REGISTER_ENTITY_FOR_CUTSCENE, p, R[i], 0, 0, 67);
        } else N(N_REGISTER_ENTITY_FOR_CUTSCENE, 0, R[i], 3, 0, 67);
    }
    if (crew.size() > (size_t)std::max(0, roles - 1)) crew.resize(std::max(0, roles - 1));
    Log("cut %s: crew in the scene %d", name, (int)crew.size());
    N(N_SET_ENTITY_INVINCIBLE, me, 1);   // nothing can hurt you meanwhile
    N(N_START_CUTSCENE, 0);
    bool pri = !std::strcmp(name, "APA_PRI_INT"), nar = !std::strcmp(name, "APA_NAR_INT");
    for (int i = 0; !world && i < std::max(sections, 1); ++i) {
        bool second = two && ((pri && i >= 2 && i <= 4) || (nar && i >= 3 && i <= 5));
        if (second) N(N_SET_CUTSCENE_ORIGIN, o2.x, o2.y, o2.z, oh2, i);
        else N(N_SET_CUTSCENE_ORIGIN, o.x, o.y, o.z, oh, i);
    }
    t0 = Now();
    while (Now() - t0 < 15000 && !NB(N<int>(N_IS_CUTSCENE_PLAYING))) Wait(0);
    // a lobby's Play fades the screen out before the finale's scene: it is seen, as in Online (it played on black, 2026-11-07)
    if (NB(N<int>(N_IS_SCREEN_FADED_OUT)) || NB(N<int>(N_IS_SCREEN_FADING_OUT))) N(N_DO_SCREEN_FADE_IN, 500);
    N(N_BUSYSPINNER_OFF);
    int t1 = Now(), dressed = 0; bool skipped = false, told = false;
    // the board on the wall: in the midpoints and the finales' scenes; in Fleeca's midpoint Lester puts it up as he talks (MARKER_SCALEFORM)
    bool boardOn = std::strstr(name, "_MID") || std::strstr(name, "FIN_INT"), staged = !std::strcmp(name, "MPH_TUT_MID");
    g_hbInCut = true;
    while (!NB(N<int>(N_HAS_CUTSCENE_FINISHED)) && NB(N<int>(N_IS_CUTSCENE_ACTIVE)) && Now() - t1 < 600000) {
        if (Now() >= dressed) {
            CutDress(me, outfit);
            for (size_t k = 0; k < crew.size(); ++k) if (Exists(crew[k])) CutDress(crew[k], crewOutfit[k]);
            if (!crew.empty()) LO().holdPuppets(3000);
            dressed = Now() + 100;
        }
        if (staged) {
            int lester = N<int>(N_GET_ENTITY_INDEX_OF_CUTSCENE_ENTITY, "Lester", 0);
            if (lester && Exists(lester) && NB(N<int>(N_HAS_ANIM_EVENT_FIRED, lester, Joaat("MARKER_SCALEFORM")))) { staged = false; Log("cut %s: the board goes up after %d ms", name, Now() - t1); }
        }
        if (boardOn && !staged) { HbFrame(); HbDrawOnlineSpot(); }
        if (Now() > g_cutDebug && g_cutDebug) {
            g_cutDebug = Now() + 5000;
            Log("cutdbg: me %d comps %d %d mask %d hat %d", me, N<int>(N_GET_PED_DRAWABLE_VARIATION, me, 3), N<int>(N_GET_PED_DRAWABLE_VARIATION, me, 11), N<int>(N_GET_PED_DRAWABLE_VARIATION, me, 1), N<int>(N_GET_PED_PROP_INDEX, me, 0));
            for (const char *r : R) {
                int e = N<int>(N_GET_ENTITY_INDEX_OF_CUTSCENE_ENTITY, r, 0);
                Log("cutdbg: %s exists %d entity %d model %08x visible %d comps %d %d at %.1f %.1f %.1f", r, N<int>(N_DOES_CUTSCENE_ENTITY_EXIST, r, 0), e, e ? N<int>(N_GET_ENTITY_MODEL, e) : 0, e ? N<int>(N_IS_ENTITY_VISIBLE, e) : 0, e ? N<int>(N_GET_PED_DRAWABLE_VARIATION, e, 3) : 0, e ? N<int>(N_GET_PED_DRAWABLE_VARIATION, e, 11) : 0, e ? Pos(e).x : 0.f, e ? Pos(e).y : 0.f, e ? Pos(e).z : 0.f);
            }
        }
        if (canSkip) {
            if (!told) { told = true; Help("As Heist leader press ~INPUT_SKIP_CUTSCENE~ to skip previously watched scenes."); }
            if (Now() - t1 > 1000 && (NB(N<int>(N_IS_CONTROL_JUST_PRESSED, 0, 18)) || NB(N<int>(N_IS_CONTROL_JUST_PRESSED, 2, 18)))) {
                skipped = true;
                N(N_DO_SCREEN_FADE_OUT, 500); Wait(600);
                N(N_STOP_CUTSCENE_IMMEDIATELY);
                break;
            }
        }
        Wait(0);
    }
    g_hbInCut = false;
    N(N_CLEAR_ALL_HELP_MESSAGES);
    Vec3 end = Pos(me);
    Log("cut %s: ended after %d ms%s, you at %.2f %.2f %.2f", name, Now() - t1, skipped ? " (skipped)" : "", end.x, end.y, end.z);
    N(N_REMOVE_CUTSCENE);
    CutOutfitRestore(me, outfit);
    for (size_t k = 0; k < crew.size(); ++k) if (Exists(crew[k])) { CutOutfitRestore(crew[k], crewOutfit[k]); N(N_SET_ENTITY_VISIBLE, crew[k], 1, 0); }
    if (!crew.empty()) LO().holdPuppets(0);   // LocalOnline drives them again, back where their players are
    // back where you were, on a floor that's there
    N(N_CLEAR_PED_TASKS_IMMEDIATELY, me);
    N(N_FREEZE_ENTITY_POSITION, me, 1);
    N(N_SET_ENTITY_COORDS_NO_OFFSET, me, at.x, at.y, at.z, 0, 0, 0); N(N_SET_ENTITY_HEADING, me, heading);
    t0 = Now();
    do { N(N_REQUEST_COLLISION_AT_COORD, at.x, at.y, at.z); Wait(0); } while (Now() - t0 < 5000 && !NB(N<int>(N_HAS_COLLISION_LOADED_AROUND_ENTITY, me)));
    N(N_FREEZE_ENTITY_POSITION, me, 0); N(N_SET_ENTITY_VISIBLE, me, 1, 0); N(N_SET_ENTITY_INVINCIBLE, me, 0);
    if (skipped || NB(N<int>(N_IS_SCREEN_FADED_OUT))) N(N_DO_SCREEN_FADE_IN, 500);
    return true;
}
// The crew watches the leader's scenes, as in Online: the leader sends "hcut" (scene|world|apartment|roles|leader) to its
// session; a member of the leader's job (or of its last one, the end scenes come after it) plays the same scene on its own
// game, the leader in it as MP_2 (story mode always makes you MP_1), and can't skip it (Buzzer.inc HeistCutsMemberTick).
static std::string g_lastJobHost;   // the host of the last job we were a member of, and when we last were
static int g_lastJobHostAt = 0;
static void HeistCutShare(const char *name, bool world, int roles) {
    if (!LO().sendPlayerMessage || !g_coop.id || !(g_coop.hosting || g_coop.host == "you") || g_coop.members.empty()) return;
    if (g_myName.empty() && LO().getPlayerName) { char b[64] = {0}; LO().getPlayerName(b, sizeof b); g_myName = b; }
    std::string t = std::string(name) + "|" + (world ? "1" : "0") + "|" + std::to_string(g_inside) + "|" + std::to_string(roles) + "|" + g_myName;
    LO().sendPlayerMessage(0, "hcut", t.c_str());
    Log("cut %s: shared with the crew (%s)", name, g_coop.members.c_str());
}
// the scene of a heist before a job: the intro before its first setup, the midpoint halfway through them, the finale's before it
static void HeistCutFor(int h, int which) {
    const char *name = HeistCutName(h, which);
    if (!name) return;
    int bit = 1 << (h * 3 + which);
    HbSetHeist(h);
    HeistCutShare(name, false, h == 0 ? 2 : 4);
    if (PlayCut(name, h == 0 ? 2 : 4, (g_heistCuts & bit) != 0)) { g_heistCuts |= bit; Save(); }
}
static void HeistCutBeforeSetup(int h) {
    if (h < 0 || h >= 5) return;
    int st = g_heistStage[h], setups = HEISTS[h].stages - 1;
    if (st == 0) HeistCutFor(h, CUT_INT);
    else if (st == setups / 2 && st < setups) HeistCutFor(h, CUT_MID);
}
// the end of a job: the setup's own scene where it was made (fm_mission_controller's plain START_CUTSCENE, the _EXT scenes),
// then after a finale the crew celebrating. Online's job data names which scene ends which job; here they go by the job they
// show. Left out: the three that play in the apartment (Station, Wet Work, Key Codes: you're far from it at the end), and
// MPH_PRI_FIN_EXT / MPH_HUM_DEL_EXT, which play almost empty here; MPH_PRI_EMP_EXT and MPH_FIN_CEL_BEA don't load in story mode.
// Which celebration follows which finale is our rule (MPH_FIN_CEL_APT, _BAR, _TRE).
static bool g_heistScenes = true;   // hbscenes 0: none (tests)
static const char *HeistExtName(int h, int st) {
    static const char *const EXT[5][6] = {
        {"MPH_TUT_EXT", "MPH_TUT_CAR_EXT", nullptr},
        {"MPH_PRI_PLA_EXT", "MPH_PRI_BUS_EXT", nullptr, nullptr, nullptr},
        {nullptr, "MPH_HUM_ARM_EXT", "MPH_HUM_EMP_EXT", "MPH_HUM_VAL_EXT", nullptr, "MPH_HUM_FIN_EXT"},
        {"MPH_NAR_COK_EXT", "MPH_NAR_TRA_EXT", "MPH_NAR_BIK_EXT", "MPH_NAR_WEE_EXT", "MPH_NAR_MET_EXT", "MPH_NAR_FIN_EXT"},
        {nullptr, "MPH_PAC_PHO_EXT", "MPH_PAC_HAC_EXT", "MPH_PAC_CON_EXT", "MPH_PAC_BIK_EXT", "MPH_PAC_FIN_EXT"}};
    return h >= 0 && h < 5 && st >= 0 && st < 6 && st < HEISTS[h].stages ? EXT[h][st] : nullptr;
}
static void HeistEndScenes(int h, int st, bool finale) {
    if (!g_heistScenes || h < 0 || h >= 5) return;
    int roles = h == 0 ? 2 : 4;
    if (const char *ext = HeistExtName(h, st)) {
        int bit = 1 << (h * 6 + st);
        HeistCutShare(ext, true, roles);
        if (PlayCut(ext, roles, (g_heistCutsX & bit) != 0, true)) { g_heistCutsX |= bit; Save(); }
    }
    if (finale) {
        static const char *const CEL[5] = {"MPH_FIN_CEL_APT", "MPH_FIN_CEL_BAR", "MPH_FIN_CEL_BAR", "MPH_FIN_CEL_TRE", "MPH_FIN_CEL_BAR"};
        HeistCutShare(CEL[h], true, roles);
        PlayCut(CEL[h], roles, true, true);
    }
}
