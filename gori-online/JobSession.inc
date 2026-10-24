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
