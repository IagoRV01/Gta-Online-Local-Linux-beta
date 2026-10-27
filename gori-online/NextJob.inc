// ---------------------------------------------------------------- the next job vote (GTA Online's mp_next_job_selection)
// At the end of a job, after the results, Online puts up "Vote on the next Job": six jobs of the same kind as cards,
// and under them Replay (the last job), Refresh (six others, a few times) and Freemode. The details of the job under
// the cursor (rating, creator, players, type, area) show at the side. Calls and arguments as fm_survival_controller.c
// (SET_TITLE, SET_GRID_ITEM, SET_SELECTION, SET_DETAILS_ITEM, SET_GRID_ITEM_VOTE, SHOW_PLAYER_VOTE). Voting for a job
// opens that job's lobby, as Online goes on to the next job's corona; Freemode (or no vote when the time runs out)
// goes back to freemode. Grid slots 0-5 the jobs, 6 Replay, 7 Refresh, 8 Freemode.
struct NjJob {
    std::string name, desc, type;   // type: a game label (FM_NXT_MIS Mission, FM_NXT_RAC Race, FMMC_RSTAR_STR Stunt Race)
    int icon = 0, minP = 1, maxP = 4;   // icon: the movie's job type icon (0 mission, 2 race, 19 stunt race)
    bool rockstar = true;               // Rockstar Created, or a community race (GoriOnline\Races)
    Vec3 at{};
    std::function<void()> open;     // its lobby
};
static struct {
    bool on = false;
    int mv = 0, sel = 0, vote = -1, until = 0, refreshes = 3, decideAt = 0;
    bool built = false;
    std::vector<NjJob> jobs;
    NjJob replay;
    std::function<std::vector<NjJob>()> pick;   // six jobs (Refresh asks again)
    std::function<void()> before;               // the ended job's cleanup, once the vote is decided (a race's)
} g_nj;
static bool NjActive() { return g_nj.on; }
static void NjBegin(const char *m) { N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, g_nj.mv, m); }
static void NjEnd() { N(N_END_SCALEFORM_MOVIE_METHOD); }
static void NjLabel(const char *label) { N(N_BEGIN_TEXT_COMMAND_SCALEFORM_STRING, label); N(N_END_TEXT_COMMAND_SCALEFORM_STRING); }
static void NjRaw(const std::string &t) {   // literal text in a label slot
    N(N_BEGIN_TEXT_COMMAND_SCALEFORM_STRING, "STRING");
    N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, t.c_str());
    N(N_END_TEXT_COMMAND_SCALEFORM_STRING);
}
static void NjLabelInt(const char *label, int a, int b = -1) {
    N(N_BEGIN_TEXT_COMMAND_SCALEFORM_STRING, label);
    N(N_ADD_TEXT_COMPONENT_INTEGER, a);
    if (b >= 0) N(N_ADD_TEXT_COMPONENT_INTEGER, b);
    N(N_END_TEXT_COMMAND_SCALEFORM_STRING);
}
static void NjTex(const char *t) { N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_TEXTURE_NAME_STRING, t); }
static void NjInt(int v) { N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, v); }
static void NjFloat(float v) { N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, v); }
static void NjBool(bool v) { N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, v ? 1 : 0); }

static void NjDetails() {   // the job under the cursor, at the side
    if (g_nj.sel > 5 || g_nj.sel >= (int)g_nj.jobs.size()) return;
    const NjJob &j = g_nj.jobs[g_nj.sel];
    NjBegin("SET_DETAILS_ITEM"); NjInt(0); NjInt(0); NjInt(0); NjInt(1); NjInt(0); NjInt(1); NjLabel("FM_NXT_RAT"); NjLabel("FM_NXT_NOT"); NjEnd();
    NjBegin("SET_DETAILS_ITEM"); NjInt(1); NjInt(0); NjInt(0); NjInt(3); NjInt(0); NjInt(1); NjLabel("FM_NXT_CRE"); if (j.rockstar) NjLabel("HUD_THUM_ROCK"); else NjRaw("Community"); NjLabel(""); NjBool(false); NjEnd();
    NjBegin("SET_DETAILS_ITEM"); NjInt(2); NjInt(0); NjInt(0); NjInt(0); NjInt(0); NjInt(1); NjLabel("FM_NXT_PLY"); NjLabelInt("LBD_NUM", j.minP, j.maxP); NjEnd();
    NjBegin("SET_DETAILS_ITEM"); NjInt(3); NjInt(0); NjInt(0); NjInt(2); NjInt(0); NjInt(1); NjLabel("FM_NXT_TYP"); NjLabel(j.type.c_str()); NjInt(j.icon); NjInt(9); NjBool(false); NjEnd();
    const char *zone = N<const char *>(N_GET_NAME_OF_ZONE, j.at.x, j.at.y, j.at.z);
    NjBegin("SET_DETAILS_ITEM"); NjInt(4); NjInt(0); NjInt(0); NjInt(0); NjInt(0); NjInt(1); NjLabel("FM_NXT_ARA"); NjLabel(zone ? zone : ""); NjEnd();
}
static void NjSelection() {
    NjBegin("SET_SELECTION");
    NjInt(g_nj.sel);
    if (g_nj.sel <= 5 && g_nj.sel < (int)g_nj.jobs.size()) { NjRaw(g_nj.jobs[g_nj.sel].name); NjRaw(g_nj.jobs[g_nj.sel].desc); }
    else if (g_nj.sel == 6) { NjRaw(g_nj.replay.name); NjLabel("FM_NXT_REP"); }
    else if (g_nj.sel == 7) { NjLabel("FM_NXT_SUB2"); NjLabel("FM_NXT_REF"); }
    else { NjLabel("FM_NXT_SUB3"); NjLabel("FM_NXT_GTAD"); }
    NjBool(false);
    NjEnd();
    NjDetails();
}
static void NjBuild() {
    NjBegin("SET_TITLE"); NjLabel("FM_NXT_J_TIT"); NjLabelInt("FM_NXT_VCNT", g_nj.vote >= 0 ? 1 : 0, 1); NjEnd();
    for (int i = 0; i < 6; ++i) {
        NjBegin("SET_GRID_ITEM");
        NjInt(i);
        if (i < (int)g_nj.jobs.size()) {   // a job without its photo: Online's "no texture" layout
            const NjJob &j = g_nj.jobs[i];
            NjRaw(j.name); NjTex(""); NjTex(""); NjInt(1); NjInt(0); NjInt(j.icon); NjBool(false); NjFloat(0.f); NjFloat(0.f); NjBool(false); NjInt(1); NjInt(0);
        } else {   // an empty slot
            NjRaw(" "); NjTex(""); NjTex(""); NjInt(1); NjInt(0); NjInt(-1); NjBool(false); NjFloat(0.f); NjFloat(0.f); NjBool(false); NjInt(1); NjInt(0);
        }
        NjEnd();
    }
    NjBegin("SET_GRID_ITEM"); NjInt(6); NjLabel("FM_NXT_TREP"); NjTex(""); NjTex(""); NjInt(-1); NjInt(0); NjInt(-2); NjBool(false); NjFloat(0.f); NjFloat(0.f); NjBool(!g_nj.replay.open); NjEnd();
    NjBegin("SET_GRID_ITEM"); NjInt(7); NjLabelInt("FM_NXT_TREFN", g_nj.refreshes); NjTex(""); NjTex(""); NjInt(-1); NjInt(0); NjInt(-2); NjBool(false); NjFloat(0.f); NjFloat(0.f); NjBool(g_nj.refreshes == 0); NjEnd();
    NjBegin("SET_GRID_ITEM"); NjInt(8); NjLabel("FM_NXT_GTA"); NjTex(""); NjTex(""); NjInt(-1); NjInt(0); NjInt(-2); NjBool(false); NjFloat(0.f); NjFloat(0.f); NjBool(false); NjEnd();
    for (int i = 0; i < 9; ++i) { NjBegin("SET_GRID_ITEM_VOTE"); NjInt(i); NjInt(g_nj.vote == i ? 1 : 0); NjInt(0); NjBool(g_nj.vote == i); NjBool(false); NjEnd(); }
    if (g_nj.vote >= 0) { NjBegin("SHOW_PLAYER_VOTE"); NjInt(g_nj.vote); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_PLAYER_NAME_STRING, (g_myName.empty() ? "You" : g_myName).c_str()); NjInt(93); NjInt(182); NjInt(229); NjEnd(); }
    NjSelection();
    g_nj.built = true;
}
// the screen, after a job's results: the next six jobs (pick), the last one (replay), the job's cleanup (before)
static void NextJobOpen(const NjJob &replay, std::function<std::vector<NjJob>()> pick, std::function<void()> before = nullptr) {
    g_nj.on = true; g_nj.built = false; g_nj.sel = 0; g_nj.vote = -1; g_nj.refreshes = 3; g_nj.decideAt = 0;
    g_nj.until = Now() + 30000;
    g_nj.replay = replay; g_nj.pick = pick; g_nj.before = before;
    g_nj.jobs = pick ? pick() : std::vector<NjJob>{};
    if (!g_nj.mv) g_nj.mv = N<int>(N_REQUEST_SCALEFORM_MOVIE, "mp_next_job_selection");
    N(N_SET_TIMECYCLE_MODIFIER, "hud_def_blur");   // the world blurred behind it, like the lobby
    Log("next job vote: %d jobs, replay %s", (int)g_nj.jobs.size(), replay.name.c_str());
}
static void NjDecide() {
    int v = g_nj.vote;
    std::function<void()> go;
    if (v >= 0 && v <= 5 && v < (int)g_nj.jobs.size()) go = g_nj.jobs[v].open;
    else if (v == 6) go = g_nj.replay.open;
    Log("next job vote: %s", v < 0 ? "no vote (freemode)" : v <= 5 ? g_nj.jobs[v].name.c_str() : v == 6 ? "replay" : "freemode");
    std::function<void()> before = g_nj.before;
    g_nj.on = false; g_nj.jobs.clear(); g_nj.pick = nullptr; g_nj.before = nullptr; g_nj.replay = NjJob();
    if (g_nj.mv) { NjBegin("CLEANUP_MOVIE"); NjEnd(); N(N_SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED, &g_nj.mv); g_nj.mv = 0; }
    N(N_CLEAR_TIMECYCLE_MODIFIER);
    if (before) before();
    if (go) go();
}
static void NextJobTick() {
    if (!g_nj.on) return;
    if (!NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, g_nj.mv))) { if (Now() > g_nj.until) NjDecide(); return; }
    if (!g_nj.built) NjBuild();
    N(N_HIDE_HUD_AND_RADAR_THIS_FRAME);
    for (int c = 0; c < 360; ++c) if (c != 249 && c != 245 && c != 1 && c != 2) N(N_DISABLE_CONTROL_ACTION, 0, c, 1);
    N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, g_nj.mv, 255, 255, 255, 255, 0);
    MenuButtons();
    int left = std::max(0, (g_nj.until - Now() + 999) / 1000);
    RaceBar(0, "TIME", "0:" + std::string(left < 10 ? "0" : "") + std::to_string(left), false);
    if (g_nj.decideAt) { if (Now() > g_nj.decideAt) NjDecide(); return; }   // voted: a moment to see it
    if (Now() > g_nj.until) { NjDecide(); return; }
    auto pressed = [](int c) { return NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 2, c)); };
    int s = g_nj.sel;
    if (pressed(189)) s = s <= 5 ? (s % 3 == 0 ? s + 2 : s - 1) : (s == 6 ? 8 : s - 1);   // left / right: along the row
    if (pressed(190)) s = s <= 5 ? (s % 3 == 2 ? s - 2 : s + 1) : (s == 8 ? 6 : s + 1);
    if (pressed(188)) s = s <= 2 ? s + 6 : s <= 5 ? s - 3 : s - 3;                       // up / down: the rows, then the buttons
    if (pressed(187)) s = s <= 2 ? s + 3 : s <= 5 ? s + 3 : s - 6;
    if (s != g_nj.sel) {
        if (s <= 5 && s >= (int)g_nj.jobs.size()) s = g_nj.sel;   // an empty card
        if (s != g_nj.sel) { g_nj.sel = s; NjSelection(); Sound("NAV_LEFT_RIGHT", "HUD_FRONTEND_DEFAULT_SOUNDSET"); }
    }
    if (pressed(201)) {
        if (g_nj.sel == 7) {   // Refresh: six others
            if (g_nj.refreshes > 0 && g_nj.pick) { --g_nj.refreshes; g_nj.jobs = g_nj.pick(); g_nj.vote = -1; g_nj.until = Now() + 30000; NjBuild(); Sound("SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET"); }
            else Sound("ERROR", "HUD_FRONTEND_DEFAULT_SOUNDSET");
        } else if (g_nj.sel == 6 && !g_nj.replay.open) Sound("ERROR", "HUD_FRONTEND_DEFAULT_SOUNDSET");
        else {   // your vote; alone (or the host), it decides
            g_nj.vote = g_nj.sel;
            NjBuild();
            Sound("SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET");
            g_nj.decideAt = Now() + 1500;
        }
    }
}

// ---- the jobs offered
static std::vector<NjJob> NjContactJobs(const std::string &except) {   // the contact missions you have the rank for
    std::vector<NjJob> all;
    int rank = LevelOf(g_rp);
    for (int c = 0; c < NCONTACTS; ++c)
        for (int i = 0; i < CONTACTS[c].count; ++i) {
            const ContactMission &cm = CONTACTS[c].missions[i];
            if (cm.rank > rank || except == cm.name) continue;
            NjJob j;
            j.name = cm.name; j.type = "FM_NXT_MIS"; j.icon = 0; j.minP = 1; j.maxP = 4;
            j.desc = ContactCorona(c, i).desc;
            j.at = V3(cm.x, cm.y, cm.z);
            j.open = [c, i] { OpenCorona(ContactCorona(c, i)); };
            all.push_back(j);
        }
    std::shuffle(all.begin(), all.end(), std::mt19937(Now()));
    if (all.size() > 6) all.resize(6);
    return all;
}
static NjJob NjRaceJob(int idx) {
    const RaceDef &r = g_raceList[idx];
    NjJob j;
    j.name = r.name;
    bool stunt = r.type == "Stunt Race";
    j.rockstar = !r.type.empty();
    j.type = stunt ? "FMMC_RSTAR_STR" : "FM_NXT_RAC"; j.icon = stunt ? 19 : 2; j.minP = 1; j.maxP = 8;
    j.desc = r.desc.empty() ? std::string(r.laps ? "A circuit race." : "A point-to-point race.") : r.desc;
    j.at = r.trigger.x != 0.f ? r.trigger : (r.grid.empty() ? Vec3{} : r.grid[0].first);
    j.open = [idx] { OpenRaceCorona(idx); };
    return j;
}
static std::vector<NjJob> NjRaceJobs(const std::string &exceptFile, const std::string &type) {   // races of the same kind
    RefreshRaceList();
    std::vector<int> idx;
    for (int i = 0; i < (int)g_raceList.size(); ++i) if (g_raceList[i].file != exceptFile && g_raceList[i].type == type) idx.push_back(i);
    if (idx.size() < 6) for (int i = 0; i < (int)g_raceList.size(); ++i) if (g_raceList[i].file != exceptFile && g_raceList[i].type != type) idx.push_back(i);
    std::shuffle(idx.begin(), idx.end(), std::mt19937(Now()));
    std::vector<NjJob> out;
    for (int i : idx) { out.push_back(NjRaceJob(i)); if (out.size() == 6) break; }
    return out;
}
// after a contact mission's results (not a heist's)
static void NextJobContact(const std::string &missionName) {
    for (int c = 0; c < NCONTACTS; ++c)
        for (int i = 0; i < CONTACTS[c].count; ++i) {
            std::string n = CONTACTS[c].missions[i].name;
            for (auto &ch : n) ch = (char)toupper((unsigned char)ch);
            if (n != missionName) continue;
            NjJob replay;
            replay.name = CONTACTS[c].missions[i].name;
            replay.open = [c, i] { OpenCorona(ContactCorona(c, i)); };
            std::string except = CONTACTS[c].missions[i].name;
            NextJobOpen(replay, [except] { return NjContactJobs(except); });
            return;
        }
}
// at the end of a race's results: the race is cleaned up once the vote is decided
static void NextJobRace(const std::string &file, const std::string &type) {
    RefreshRaceList();
    NjJob replay;
    for (int i = 0; i < (int)g_raceList.size(); ++i) if (g_raceList[i].file == file) replay = NjRaceJob(i);
    NextJobOpen(replay, [file, type] { return NjRaceJobs(file, type); }, [] { RaceCleanup(); });
}

static NjJob NjSurvivalJob(int loc) {
    NjJob j;
    j.name = std::string("Survival: ") + SV_LOCS[loc].name;
    j.type = "FM_NXT_SUR"; j.icon = 3; j.minP = 1; j.maxP = 4;
    j.desc = "Ten waves of enemies, each harder than the last.";
    j.at = V3(SV_LOCS[loc].x, SV_LOCS[loc].y, SV_LOCS[loc].z);
    j.open = [loc] { OpenSvCorona(loc); };
    return j;
}
// at the end of a survival's results: cleaned up once the vote is decided
static void NextJobSurvival(int loc) {
    NextJobOpen(NjSurvivalJob(loc), [loc] {
        std::vector<int> idx;
        for (int i = 0; i < NSV_LOCS; ++i) if (i != loc) idx.push_back(i);
        std::shuffle(idx.begin(), idx.end(), std::mt19937(Now()));
        std::vector<NjJob> out;
        for (int i : idx) { out.push_back(NjSurvivalJob(i)); if (out.size() == 6) break; }
        return out;
    }, [] { SvCleanup(); });
}
