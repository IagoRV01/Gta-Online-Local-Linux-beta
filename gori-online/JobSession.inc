// ---------------------------------------------------------------- the job session (like GTA Online's)
// A job (race, deathmatch, survival, Arena War, mission, heist) is played in a session of its own, as in GTA Online:
//  - the players: the server (LocalOnline) already puts a running job's players in the job's session and a solo
//    activity in a private one, so only the job's players are drawn and only they show on the map;
//  - the rest of freemode goes too: every blip on the map and the minimap that is not the job's is hidden while it
//    lasts (stores, apartments, businesses, other jobs' coronas, the personal vehicle...) and the freemode modules stop
//    (shops, properties, the casino, ambient events, invites...): what is left is the job;
//  - going in and coming back out is Online's transition: the camera flies up out of where you are, the job loads,
//    and it comes down onto you (SWITCH_TO_MULTI_FIRSTPART / SECONDPART), instead of a fade.
// The activities call JobSkyUp() where they used to fade out before moving you, and JobSkyDown() where they faded back
// in. Blips made while in a job are the job's (CoordBlip / AddBlip mark them); the rest is snapshotted and hidden when
// the job starts, and shown again when it ends.
static bool g_jobOn = false;
static std::vector<std::pair<int, int>> g_jobHidden;   // freemode blips hidden by the job: blip, its display

static void JobHideFreemode() {
    g_jobHidden.clear();
    int players = 0;
    for (int sprite = 0; sprite < 900; ++sprite)
        for (int b = N<int>(N_GET_FIRST_BLIP_INFO_ID, sprite); b && NB(N<int>(N_DOES_BLIP_EXIST, b)); b = N<int>(N_GET_NEXT_BLIP_INFO_ID, sprite)) {
            if (g_jobBlips.count(b)) continue;                                  // the job's own
            if (N<int>(N_GET_BLIP_INFO_ID_TYPE, b) == 2) { ++players; continue; } // on a ped: the players (the session decides) and the job's people
            int disp = N<int>(N_GET_BLIP_INFO_ID_DISPLAY, b);
            if (disp == 0) continue;
            g_jobHidden.push_back({b, disp});
            N(N_SET_BLIP_DISPLAY, b, 0);
        }
    Log("job session: in, %d freemode blips hidden (%d on peds kept)", (int)g_jobHidden.size(), players);
}
static void JobShowFreemode() {
    int n = 0;
    for (auto &[b, disp] : g_jobHidden) if (NB(N<int>(N_DOES_BLIP_EXIST, b))) { N(N_SET_BLIP_DISPLAY, b, disp); ++n; }
    g_jobHidden.clear();
    g_jobBlips.clear();
    Log("job session: out, %d freemode blips back", n);
}
static void JobEnter() {   // the job starts: freemode goes
    if (g_jobOn) return;
    g_jobOn = true;
    g_jobStartedAt = Now();
    JobHideFreemode();
}
// Online's transition, part one: up into the sky from where you are; the caller then sets the job up (or takes you back)
static void JobSkyUp() {
    JobEnter();
    if (g_jobSky || NB(N<int>(N_IS_PLAYER_SWITCH_IN_PROGRESS))) return;
    N(N_SWITCH_TO_MULTI_FIRSTPART, Me(), 0, 1);
    g_jobSky = true;
    for (int i = 0; i < 120 && !NB(N<int>(N_IS_SWITCH_TO_MULTI_FIRSTPART_FINISHED)); ++i) Wait(50);   // up there, waiting
}
// part two: down onto you at the job (or back in freemode), until it lands; a fade in when there was no flight up
static void JobSkyDown() {
    if (!g_jobSky) { N(N_DO_SCREEN_FADE_IN, 500); return; }
    if (NB(N<int>(N_IS_SCREEN_FADED_OUT))) N(N_DO_SCREEN_FADE_IN, 0);
    N(N_SWITCH_TO_MULTI_SECONDPART, Me());
    for (int i = 0; i < 240 && NB(N<int>(N_IS_PLAYER_SWITCH_IN_PROGRESS)); ++i) Wait(50);
    g_jobSky = false;
}
// every frame, before the other modules: a job that started without JobSkyUp (a mission, a member following the
// host) still gets its session; when no job is left, freemode comes back
static void JobSessionTick() {
    bool act = InActivity();
    if (act && !g_jobOn) JobEnter();
    else if (!act && g_jobOn) { g_jobOn = false; JobShowFreemode(); }
    if (g_jobSky && !NB(N<int>(N_IS_PLAYER_SWITCH_IN_PROGRESS))) g_jobSky = false;   // a flight nobody finished
}
static bool Freemode() { return !g_jobOn; }   // the freemode modules run

// ---- GTA Online's end-of-job screen (MP_CELEBRATION and its background / foreground movies): the result (the position,
// or passed / failed), the time, the RP with the rank bar and the cash, one after another, then the job's own results
struct CelArg { int t; int i; float f; std::string s; };   // t: 0 int, 1 float, 2 bool, 3 string
static CelArg CelI(int v) { return {0, v, 0.f, ""}; }
static CelArg CelF(float v) { return {1, 0, v, ""}; }
static CelArg CelB(bool v) { return {2, v ? 1 : 0, 0.f, ""}; }
static CelArg CelS(const std::string &v) { return {3, 0, 0.f, v}; }
static CelArg CelL(const std::string &label) { return {3, 0, 0.f, label}; }   // a game text label: its name, the movie looks it up (as Online passes them)
static struct { int mv[3] = {0, 0, 0}; int until = 0; bool on = false; } g_cel;
static void CelCall(const char *method, const std::vector<CelArg> &args) {   // the same call on all three movies, as Online does
    for (int m : g_cel.mv) {
        N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, m, method);
        for (const CelArg &a : args) {
            if (a.t == 0) N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, a.i);
            else if (a.t == 1) N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, a.f);
            else if (a.t == 2) N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, a.i);
            else N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_TEXTURE_NAME_STRING, a.s.c_str());
        }
        N(N_END_SCALEFORM_MOVIE_METHOD);
    }
}
// Online's word over PASSED / FAILED (fm_*_controller: the kind of job, not its name): a heist finale, a heist setup,
// a survival, Arena War, a mission
static const char *CelJobType(const std::string &job) {
    if (!job.compare(0, 8, "Survival")) return "CELEB_SURVIVAL";
    if (!job.compare(0, 9, "Arena War")) return "PM_ARENA";
    for (int h = 0; h < NHEISTS; ++h) {
        size_t n = std::strlen(HEISTS[h].name);
        if (!job.compare(0, n, HEISTS[h].name)) return job.find(" - Finale") != std::string::npos ? "CELEB_HEIST" : "FMMC_RSTAR_HP";
    }
    return "CELEB_MISSION";
}
// position > 0: a race / deathmatch place; 0: a mission (passed or failed, why: the line under it). rpBefore: RP before
// this job's RP was given.
static void JobCelebrate(const std::string &job, int position, bool passed, int timeMs, int rpBefore, int rpGained, long long cash, const std::string &why) {
    const char *names[3] = {"MP_CELEBRATION_BG", "MP_CELEBRATION_FG", "MP_CELEBRATION"};
    for (int k = 0; k < 3; ++k) g_cel.mv[k] = N<int>(N_REQUEST_SCALEFORM_MOVIE, names[k]);
    for (int i = 0; i < 60; ++i) {
        bool all = true;
        for (int m : g_cel.mv) if (!NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, m))) all = false;
        if (all) break;
        Wait(50);
    }
    const std::string w = "ch";
    CelCall("CLEANUP", {CelS(w)});
    CelCall("CREATE_STAT_WALL", {CelS(w), CelS("HUD_COLOUR_BLACK"), CelI(-1)});
    if (position > 0) CelCall("ADD_POSITION_TO_WALL", {CelS(w), CelI(position), CelS(job), CelB(true), CelB(false)});
    else CelCall("ADD_MISSION_RESULT_TO_WALL", {CelS(w), CelL(CelJobType(job)), CelL(passed ? "CELEB_PASSED" : "CELEB_FAILED"), CelS(why), CelB(true)});
    if (timeMs > 0 && g_celPbBy >= 0) CelCall("ADD_TIME_TO_WALL", {CelS(w), CelI(timeMs), CelS("PERSONAL BEST"), CelI(g_celPbBy)});   // Online's, a race beaten
    else if (timeMs > 0) CelCall("ADD_TIME_TO_WALL", {CelS(w), CelI(timeMs), CelS("TIME"), CelI(0)});
    g_celPbBy = -1;
    int lvl = LevelOf(rpBefore);
    if (rpGained > 0)
        CelCall("ADD_REP_POINTS_AND_RANK_BAR_TO_WALL", {CelS(w), CelI(rpGained), CelI(rpBefore), CelI(RpFor(lvl)), CelI(RpFor(lvl + 1)), CelI(lvl), CelI(lvl + 1),
                                                         CelS(std::to_string(lvl)), CelS(std::to_string(lvl + 1))});
    if (cash > 0) CelCall("ADD_CASH_TO_WALL", {CelS(w), CelI((int)std::min<long long>(cash, 2000000000LL)), CelI(1)});
    CelCall("ADD_BACKGROUND_TO_WALL", {CelS(w), CelI(75), CelI(0)});
    CelCall("SHOW_STAT_WALL", {CelS(w)});
    g_cel.on = true;
    g_cel.until = Now() + 9000;
    if (position > 0) Sound(position == 1 ? "FIRST_PLACE" : "CHECKPOINT_NORMAL", "HUD_MINI_GAME_SOUNDSET");
    else Sound(passed ? "Mission_Pass_Notify" : "ScreenFlash", passed ? "DLC_HEISTS_GENERAL_FRONTEND_SOUNDS" : "MissionFailedSounds");
    Log("celebration: %s, position %d, %s, %d ms, +%d RP, $%lld", job.c_str(), position, passed ? "passed" : "failed", timeMs, rpGained, cash);
}
static bool JobCelebrating() { return g_cel.on; }
static void JobCelebrationTick() {   // every frame while it shows
    if (!g_cel.on) return;
    if (Now() > g_cel.until) {
        g_cel.on = false;
        for (int &m : g_cel.mv) if (m) N(N_SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED, &m);
        return;
    }
    N(N_HIDE_HUD_AND_RADAR_THIS_FRAME);
    N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN_MASKED, g_cel.mv[0], g_cel.mv[1], 255, 255, 255, 255);
    N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, g_cel.mv[2], 255, 255, 255, 255, 0);
}
