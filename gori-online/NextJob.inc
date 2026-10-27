// ---------------------------------------------------------------- the next job vote (GTA Online's mp_next_job_selection)
// At the end of a job, after the results, Online puts up "Vote on the next Job": six jobs of the same kind as cards,
// and under them Replay (the last job), Refresh (six others, a few times) and Freemode. The details of the job under
// the cursor (rating, creator, players, type, area) show at the side. Calls and arguments as fm_survival_controller.c
// (SET_TITLE, SET_GRID_ITEM, SET_SELECTION, SET_DETAILS_ITEM, SET_GRID_ITEM_VOTE, SHOW_PLAYER_VOTE). Voting for a job
// opens that job's lobby, as Online goes on to the next job's corona; Freemode (or no vote when the time runs out)
// goes back to freemode. Grid slots 0-5 the jobs, 6 Replay, 7 Refresh, 8 Freemode.
// Played with others (a shared vote), it is one vote for the whole job, over LocalOnline's player messages (protocol
// v14, only between the players of the session): the host's six jobs are everyone's ("njl"), each player's vote shows
// on every screen ("njh" hello, "njv" vote, "njr" refreshed), and the host decides by majority when the time is up or
// everyone has voted ("njd"): the host opens the winning job's lobby and the players who voted for it join it; the
// ones who voted Freemode go back to freemode.
struct NjJob {
    std::string name, desc, type;   // type: a game label (FM_NXT_MIS Mission, FM_NXT_RAC Race, FMMC_RSTAR_STR Stunt Race)
    int icon = 0, minP = 1, maxP = 4;   // icon: the movie's job type icon (0 mission, 1 deathmatch, 2 race, 3 survival, 19 stunt race, 25 Arena War)
    bool rockstar = true;               // Rockstar Created, or a community race (GoriOnline\Races)
    Vec3 at{};
    std::string key;                    // which job it is, for the other players' screens ("c,contact,mission", "r,file"...)
    std::function<void()> open;         // its lobby
};
struct NjVoter { std::string name; int vote = -1; };
static struct {
    bool on = false;
    int mv = 0, sel = 0, vote = -1, until = 0, refreshes = 3, decideAt = 0;
    bool built = false;
    std::vector<NjJob> jobs;
    NjJob replay;
    std::function<std::vector<NjJob>()> pick;   // six jobs (Refresh asks again)
    std::function<void()> before;               // the ended job's cleanup, once the vote is decided (a race's)
    bool shared = false, host = false;          // a vote with the job's other players; this game decides it
    std::string tag;                            // the ended job (lower case): which vote a message belongs to
    std::map<unsigned, NjVoter> others;         // the other players on this vote: net id -> name, vote
} g_nj;
static std::map<std::string, std::map<int, std::string>> g_njLists;   // the host's jobs that came before our screen: tag -> slot -> key
static struct { std::string tag, host; int slot = -1, at = 0; } g_njDecided;   // the host's decision, if it came first
static struct { std::string host; std::set<unsigned> before; int until = 0; } g_njJoin;   // a member, joining the host's next lobby
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
static std::string NjMe() { return g_myName.empty() ? "You" : g_myName; }
static std::string NjLower(std::string s) { for (auto &ch : s) ch = (char)tolower((unsigned char)ch); return s; }
static void NjSend(const char *kind, const std::string &text) { if (LO().sendPlayerMessage) LO().sendPlayerMessage(0, kind, (g_nj.tag + "|" + text).c_str()); }

// ---- the jobs
static NjJob NjContactJob(int c, int i) {
    const ContactMission &cm = CONTACTS[c].missions[i];
    NjJob j;
    j.name = cm.name; j.type = "FM_NXT_MIS"; j.icon = 0; j.minP = 1; j.maxP = 4;
    j.desc = ContactCorona(c, i).desc;
    j.at = V3(cm.x, cm.y, cm.z);
    j.key = "c," + std::to_string(c) + "," + std::to_string(i);
    j.open = [c, i] { OpenCorona(ContactCorona(c, i)); };
    return j;
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
    j.key = "r," + r.file;
    j.open = [idx] { OpenRaceCorona(idx); };
    return j;
}
static NjJob NjSurvivalJob(int loc) {
    NjJob j;
    j.name = std::string("Survival: ") + SV_LOCS[loc].name;
    j.type = "FM_NXT_SUR"; j.icon = 3; j.minP = 1; j.maxP = 4;
    j.desc = "Ten waves of enemies, each harder than the last.";
    j.at = V3(SV_LOCS[loc].x, SV_LOCS[loc].y, SV_LOCS[loc].z);
    j.key = "s," + std::to_string(loc);
    j.open = [loc] { OpenSvCorona(loc); };
    return j;
}
static NjJob NjDmJob(int arena) {
    NjJob j;
    j.name = std::string("Deathmatch: ") + DM_ARENAS[arena].name;
    j.type = "FM_NXT_DM"; j.icon = 1; j.minP = 2; j.maxP = 8;
    j.desc = "Every player for themselves. Most kills wins; paid by position.";
    j.at = V3(DM_ARENAS[arena].x, DM_ARENAS[arena].y, DM_ARENAS[arena].z);
    j.key = "d," + std::to_string(arena);
    j.open = [arena] { OpenDmCorona(arena); };
    return j;
}
static NjJob NjArenaJob(int mode) {   // Arena War's lobby opens on the mode voted for
    NjJob j;
    j.name = std::string("Arena War - ") + AW_MODES[mode].name;
    j.type = "PM_ARENA"; j.icon = 25; j.minP = 1; j.maxP = 8;
    j.desc = AW_MODES[mode].desc;
    j.at = V3(-376.f, -1862.f, 20.f);
    j.key = "a," + std::to_string(mode);
    j.open = [mode] { g_awSel[6] = mode; OpenAwCorona(); };
    return j;
}
static bool NjFromKey(const std::string &key, NjJob &out) {   // a job the host sent
    std::vector<std::string> f = Split(key, ',');
    if (f.size() < 2) return false;
    int a = std::atoi(f[1].c_str());
    if (f[0] == "c" && f.size() > 2) {
        int i = std::atoi(f[2].c_str());
        if (a < 0 || a >= NCONTACTS || i < 0 || i >= CONTACTS[a].count) return false;
        out = NjContactJob(a, i); return true;
    }
    if (f[0] == "r") {
        RefreshRaceList();
        std::string file = key.substr(2);
        for (int i = 0; i < (int)g_raceList.size(); ++i) if (g_raceList[i].file == file) { out = NjRaceJob(i); return true; }
        return false;   // a race this game doesn't have
    }
    if (f[0] == "s" && a >= 0 && a < NSV_LOCS) { out = NjSurvivalJob(a); return true; }
    if (f[0] == "d" && a >= 0 && a < NDM_ARENAS) { out = NjDmJob(a); return true; }
    if (f[0] == "a" && a >= 0 && a < AWM_COUNT) { out = NjArenaJob(a); return true; }
    return false;
}

// ---- the screen
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
static int NjVotesFor(int slot) {
    int n = g_nj.vote == slot ? 1 : 0;
    for (auto &[id, v] : g_nj.others) if (v.vote == slot) ++n;
    return n;
}
static int NjVoted() { int n = g_nj.vote >= 0 ? 1 : 0; for (auto &[id, v] : g_nj.others) if (v.vote >= 0) ++n; return n; }
static void NjBuild() {
    if (!g_nj.mv || !NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, g_nj.mv))) return;
    NjBegin("SET_TITLE"); NjLabel("FM_NXT_J_TIT"); NjLabelInt("FM_NXT_VCNT", NjVoted(), 1 + (int)g_nj.others.size()); NjEnd();
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
    bool canRefresh = g_nj.refreshes > 0 && (!g_nj.shared || g_nj.host);
    NjBegin("SET_GRID_ITEM"); NjInt(6); NjLabel("FM_NXT_TREP"); NjTex(""); NjTex(""); NjInt(-1); NjInt(0); NjInt(-2); NjBool(false); NjFloat(0.f); NjFloat(0.f); NjBool(!g_nj.replay.open); NjEnd();
    NjBegin("SET_GRID_ITEM"); NjInt(7); NjLabelInt("FM_NXT_TREFN", g_nj.refreshes); NjTex(""); NjTex(""); NjInt(-1); NjInt(0); NjInt(-2); NjBool(false); NjFloat(0.f); NjFloat(0.f); NjBool(!canRefresh); NjEnd();
    NjBegin("SET_GRID_ITEM"); NjInt(8); NjLabel("FM_NXT_GTA"); NjTex(""); NjTex(""); NjInt(-1); NjInt(0); NjInt(-2); NjBool(false); NjFloat(0.f); NjFloat(0.f); NjBool(false); NjEnd();
    for (int i = 0; i < 9; ++i) { int n = NjVotesFor(i); NjBegin("SET_GRID_ITEM_VOTE"); NjInt(i); NjInt(n); NjInt(0); NjBool(g_nj.vote == i); NjBool(false); NjEnd(); }
    auto show = [](int slot, const std::string &name, bool me) {
        NjBegin("SHOW_PLAYER_VOTE"); NjInt(slot); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_PLAYER_NAME_STRING, name.c_str());
        NjInt(me ? 93 : 240); NjInt(me ? 182 : 240); NjInt(me ? 229 : 240); NjEnd();
    };
    for (auto &[id, v] : g_nj.others) if (v.vote >= 0) show(v.vote, v.name, false);
    if (g_nj.vote >= 0) show(g_nj.vote, NjMe(), true);
    NjSelection();
    g_nj.built = true;
}
static void NjApplyList(const std::map<int, std::string> &keys) {   // the host's six jobs replace ours
    std::vector<NjJob> jobs;
    for (auto &[slot, key] : keys) { NjJob j; if (slot >= 0 && slot < 6 && NjFromKey(key, j)) jobs.push_back(j); }
    if (jobs.empty()) return;
    g_nj.jobs = jobs;
    if (g_nj.vote >= 0 && g_nj.vote <= 5) g_nj.vote = -1;   // the cards changed under the vote
    if (g_nj.sel <= 5 && g_nj.sel >= (int)g_nj.jobs.size()) g_nj.sel = 0;
    g_nj.built = false;
}
static void NjSendList() { for (int i = 0; i < (int)g_nj.jobs.size(); ++i) NjSend("njl", std::to_string(i) + "|" + g_nj.jobs[i].key); }
// the screen, after a job's results: the next six jobs (pick), the last one (replay), the job's cleanup (before);
// shared: the job had other players (they vote too), host: this game decides
static void NextJobOpen(const NjJob &replay, std::function<std::vector<NjJob>()> pick, std::function<void()> before = nullptr, bool shared = false, bool host = true) {
    g_nj.on = true; g_nj.built = false; g_nj.sel = 0; g_nj.vote = -1; g_nj.refreshes = 3; g_nj.decideAt = 0;
    g_nj.until = Now() + 30000;
    g_nj.replay = replay; g_nj.pick = pick; g_nj.before = before;
    g_nj.jobs = pick ? pick() : std::vector<NjJob>{};
    g_nj.shared = shared && LO().sendPlayerMessage; g_nj.host = host || !g_nj.shared;
    g_nj.tag = NjLower(replay.name);
    g_nj.others.clear();
    if (!g_nj.mv) g_nj.mv = N<int>(N_REQUEST_SCALEFORM_MOVIE, "mp_next_job_selection");
    N(N_SET_TIMECYCLE_MODIFIER, "hud_def_blur");   // the world blurred behind it, like the lobby
    if (g_nj.shared) {
        NjSend("njh", NjMe());
        if (g_nj.host) NjSendList();
        else if (g_njLists.count(g_nj.tag)) NjApplyList(g_njLists[g_nj.tag]);
        g_njLists.clear();
    }
    Log("next job vote: %d jobs, replay %s%s", (int)g_nj.jobs.size(), replay.name.c_str(), g_nj.shared ? (g_nj.host ? " (shared, host)" : " (shared)") : "");
}
// the vote is decided (slot -1 / 8: freemode); a member follows the host into the winning job's lobby
static void NjClose(int slot, const std::string &hostName) {
    std::function<void()> go;
    bool job = (slot >= 0 && slot <= 5 && slot < (int)g_nj.jobs.size()) || (slot == 6 && g_nj.replay.open);
    if (job) go = slot == 6 ? g_nj.replay.open : g_nj.jobs[slot].open;
    Log("next job vote: %s", !job ? "freemode" : slot == 6 ? "replay" : g_nj.jobs[slot].name.c_str());
    bool shared = g_nj.shared, host = g_nj.host, othersGo = false;
    for (auto &[id, v] : g_nj.others) if (v.vote == slot) othersGo = true;
    if (!host && g_nj.vote == 8) job = false;   // voted Freemode: the others go without you
    std::function<void()> before = g_nj.before;
    g_nj.on = false; g_nj.jobs.clear(); g_nj.pick = nullptr; g_nj.before = nullptr; g_nj.replay = NjJob(); g_nj.others.clear();
    if (g_nj.mv) { NjBegin("CLEANUP_MOVIE"); NjEnd(); N(N_SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED, &g_nj.mv); g_nj.mv = 0; }
    N(N_CLEAR_TIMECYCLE_MODIFIER);
    if (before) before();
    if (!job) return;
    if (!shared || host) {
        if (go) go();
        if (shared && othersGo && CoronaActive()) {   // the players who voted for it come along: the lobby opens for them
            CoronaSetLobby(1);
            if (g_cor.jobOpen) { g_cor.page = CRP_INVITE; g_cor.sel = 2; }
        }
        return;
    }
    g_njJoin.host = hostName; g_njJoin.before.clear(); g_njJoin.until = Now() + 60000;   // a member: into the host's next lobby
    for (const CoopJob &j : g_coopJobs) g_njJoin.before.insert(j.id);
    Notify("Waiting for ~b~" + hostName + "~s~'s lobby...");
}
static void NjDecide() {   // this game decides (alone, or the host): the most votes; nobody: freemode
    int best = 8, bestN = 0;
    for (int s = 0; s < 9; ++s) { if (s == 7) continue; int n = NjVotesFor(s); if (n > bestN) { best = s; bestN = n; } }
    if (g_nj.shared) NjSend("njd", std::to_string(best) + "|" + NjMe());
    NjClose(best, NjMe());
}
// the other players' messages (Buzzer.inc reads them all)
static void NjMessage(unsigned from, const std::string &kind, const std::string &text) {
    std::vector<std::string> f = Split(text, '|');
    if (f.size() < 2) return;
    const std::string &tag = f[0];
    bool mine = g_nj.on && g_nj.shared && tag == g_nj.tag;
    if (kind == "njh") { if (mine) { g_nj.others[from].name = f[1]; g_nj.built = false; } return; }
    if (kind == "njl" && f.size() >= 3) {   // one of the host's jobs
        if (g_nj.on && g_nj.host) return;
        auto &l = g_njLists[tag];
        l[std::atoi(f[1].c_str())] = f[2];
        if (mine) NjApplyList(l);
        return;
    }
    if (!mine && kind == "njd") { g_njDecided.tag = tag; g_njDecided.slot = std::atoi(f[1].c_str()); g_njDecided.host = f.size() > 2 ? f[2] : ""; g_njDecided.at = Now(); return; }
    if (!mine) return;
    if (kind == "njv" && f.size() >= 3) { NjVoter &v = g_nj.others[from]; v.vote = std::atoi(f[1].c_str()); v.name = f[2]; g_nj.built = false; }
    else if (kind == "njr") { g_nj.vote = -1; for (auto &[id, v] : g_nj.others) v.vote = -1; g_nj.until = Now() + 30000; g_njLists.erase(tag); }
    else if (kind == "njd" && !g_nj.host) NjClose(std::atoi(f[1].c_str()), f.size() > 2 ? f[2] : "");
}
static void NextJobTick() {
    if (!g_njJoin.host.empty()) {   // a member: the host's new lobby, when it shows in Jobs
        if (Now() > g_njJoin.until) { Notify("~r~" + g_njJoin.host + "'s lobby didn't open."); g_njJoin.host.clear(); }
        else for (const CoopJob &j : g_coopJobs)
            if (j.host == g_njJoin.host && !g_njJoin.before.count(j.id) && j.status == COOP_LOBBY) { CoopJoin(j.id); g_njJoin.host.clear(); break; }
    }
    if (!g_nj.on) return;
    if (!g_nj.host && g_njDecided.slot >= 0 && g_njDecided.tag == g_nj.tag && Now() - g_njDecided.at < 60000) {   // the host decided before our screen
        int s = g_njDecided.slot; std::string h = g_njDecided.host; g_njDecided = {};
        NjClose(s, h); return;
    }
    // a member whose host never decides: freemode, a while after the time is up
    if (!g_nj.host && Now() > g_nj.until + 15000) { NjClose(8, ""); return; }
    if (!NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, g_nj.mv))) { if (g_nj.host && Now() > g_nj.until) NjDecide(); return; }
    if (!g_nj.built) NjBuild();
    N(N_HIDE_HUD_AND_RADAR_THIS_FRAME);
    for (int c = 0; c < 360; ++c) if (c != 249 && c != 245 && c != 1 && c != 2) N(N_DISABLE_CONTROL_ACTION, 0, c, 1);
    N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, g_nj.mv, 255, 255, 255, 255, 0);
    MenuButtons();
    int left = std::max(0, (g_nj.until - Now() + 999) / 1000);
    RaceBar(0, "TIME", "0:" + std::string(left < 10 ? "0" : "") + std::to_string(left), false);
    if (g_nj.decideAt) { if (Now() > g_nj.decideAt) NjDecide(); return; }   // everyone has voted: a moment to see it
    if (g_nj.host && Now() > g_nj.until) { NjDecide(); return; }
    auto pressed = [](int c) { return NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 2, c)); };
    int s = g_nj.sel;
    if (pressed(189)) s = s <= 5 ? (s % 3 == 0 ? s + 2 : s - 1) : (s == 6 ? 8 : s - 1);   // left / right: along the row
    if (pressed(190)) s = s <= 5 ? (s % 3 == 2 ? s - 2 : s + 1) : (s == 8 ? 6 : s + 1);
    if (pressed(188)) s = s <= 2 ? s + 6 : s - 3;                                         // up / down: the rows, then the buttons
    if (pressed(187)) s = s <= 5 ? s + 3 : s - 6;
    if (s != g_nj.sel) {
        if (s <= 5 && s >= (int)g_nj.jobs.size()) s = g_nj.sel;   // an empty card
        if (s != g_nj.sel) { g_nj.sel = s; NjSelection(); Sound("NAV_LEFT_RIGHT", "HUD_FRONTEND_DEFAULT_SOUNDSET"); }
    }
    if (pressed(201)) {
        if (g_nj.sel == 7) {   // Refresh: six others (the host's, when shared)
            if (g_nj.refreshes > 0 && g_nj.pick && g_nj.host) {
                --g_nj.refreshes; g_nj.jobs = g_nj.pick(); g_nj.vote = -1; g_nj.until = Now() + 30000;
                for (auto &[id, v] : g_nj.others) v.vote = -1;
                if (g_nj.shared) { NjSend("njr", "-"); NjSendList(); }
                NjBuild(); Sound("SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET");
            } else Sound("ERROR", "HUD_FRONTEND_DEFAULT_SOUNDSET");
        } else if (g_nj.sel == 6 && !g_nj.replay.open) Sound("ERROR", "HUD_FRONTEND_DEFAULT_SOUNDSET");
        else {
            g_nj.vote = g_nj.sel;
            if (g_nj.shared) NjSend("njv", std::to_string(g_nj.vote) + "|" + NjMe());
            NjBuild();
            Sound("SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET");
        }
    }
    // the host (or alone): decided once everyone has voted
    if (g_nj.host && g_nj.vote >= 0 && NjVoted() >= 1 + (int)g_nj.others.size() && !g_nj.decideAt) g_nj.decideAt = Now() + 1500;
}

// ---- the jobs offered, after each kind of job
static std::vector<NjJob> NjContactJobs(const std::string &except) {   // the contact missions you have the rank for
    std::vector<NjJob> all;
    int rank = LevelOf(g_rp);
    for (int c = 0; c < NCONTACTS; ++c)
        for (int i = 0; i < CONTACTS[c].count; ++i)
            if (CONTACTS[c].missions[i].rank <= rank && except != CONTACTS[c].missions[i].name) all.push_back(NjContactJob(c, i));
    std::shuffle(all.begin(), all.end(), std::mt19937(Now()));
    if (all.size() > 6) all.resize(6);
    return all;
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
template <class F> static std::vector<NjJob> NjOthers(int n, int except, F make) {   // up to six of the others, shuffled
    std::vector<int> idx;
    for (int i = 0; i < n; ++i) if (i != except) idx.push_back(i);
    std::shuffle(idx.begin(), idx.end(), std::mt19937(Now()));
    std::vector<NjJob> out;
    for (int i : idx) { out.push_back(make(i)); if (out.size() == 6) break; }
    return out;
}
// after a contact mission's results (not a heist's); a member's too, with the host's mission (its title)
static void NextJobContact(const std::string &missionName, bool shared, bool host) {
    for (int c = 0; c < NCONTACTS; ++c)
        for (int i = 0; i < CONTACTS[c].count; ++i) {
            if (NjLower(CONTACTS[c].missions[i].name) != NjLower(missionName)) continue;
            std::string except = CONTACTS[c].missions[i].name;
            NextJobOpen(NjContactJob(c, i), [except] { return NjContactJobs(except); }, nullptr, shared, host);
            return;
        }
}
// at the end of a race's results: the race is cleaned up once the vote is decided
static void NextJobRace(const std::string &file, const std::string &type, bool shared, bool host) {
    RefreshRaceList();
    NjJob replay;
    for (int i = 0; i < (int)g_raceList.size(); ++i) if (g_raceList[i].file == file) replay = NjRaceJob(i);
    if (replay.name.empty()) replay.name = file;
    NextJobOpen(replay, [file, type] { return NjRaceJobs(file, type); }, [] { RaceCleanup(); }, shared, host);
}
static void NextJobSurvival(int loc, bool shared, bool host) {
    NextJobOpen(NjSurvivalJob(loc), [loc] { return NjOthers(NSV_LOCS, loc, NjSurvivalJob); }, [] { SvCleanup(); }, shared, host);
}
static void NextJobDeathmatch(int arena, bool host) {
    NextJobOpen(NjDmJob(arena), [arena] { return NjOthers(NDM_ARENAS, arena, NjDmJob); }, [] { DmCleanup(); }, true, host);
}
static void NextJobArena(int mode, bool shared, bool host) {
    NextJobOpen(NjArenaJob(mode), [mode] { return NjOthers(AWM_COUNT, mode, NjArenaJob); }, [] { AwCleanup(); }, shared, host);
}
