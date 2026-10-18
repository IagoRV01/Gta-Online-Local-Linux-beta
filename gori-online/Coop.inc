// ---------------------------------------------------------------- Co-op jobs (GTA Local Online API v5)
// A mission started here while the freemode "YOU" character is online becomes a server job: other players
// see it (notification + Interaction Menu > Jobs), join it and follow the host's objective (blip, route,
// text). The host's game runs the mission; the server pays each member's bank when it passes.
// LocalOnline.asi text formats: see gta-local-online/client/gta-plugin/src/game/jobs.h.
struct CoopJob { unsigned id = 0; std::string host, title; Vec3 start{}; int members = 0, status = 0, kind = 0; };
struct CoopState { unsigned id = 0; std::string host, text, members; int status = -1, step = 0, payout = 0, kind = 0; long long startsIn = 0; Vec3 obj{}; bool hosting = false; };   // kind 1: a race (Races.inc)
enum { COOP_LOBBY, COOP_RUNNING, COOP_PASSED, COOP_FAILED, COOP_CLOSED };
static std::vector<CoopJob> g_coopJobs;
static std::vector<unsigned> g_coopSeen;   // jobs already announced
static CoopState g_coop;
static std::string g_coopTitle;            // title of the job we are a member of
static int g_coopRev = -1, g_coopBlip = 0, g_coopNext = 0;
static bool g_coopHosting = false;
static bool g_coopLobbyOpen = false;
static std::function<void(bool, const CoopState &)> g_coopMemberEnd;   // Corona.inc: a member's results board   // a lobby (Corona.inc) already opened the job: the mission takes it over
static int g_coopStep = -1;
static Vec3 g_coopPos{};

static bool CoopAvailable() { return LO().jobOpen && LO().getJobs && YouActive(); }
static std::vector<std::string> SplitTabs(const std::string &line) {
    std::vector<std::string> out(1);
    for (char c : line) { if (c == '\t') out.emplace_back(); else out.back() += c; }
    return out;
}
static std::string CoopFetch(int (*get)(char *, int)) {
    int n = get(nullptr, 0);
    if (n <= 0) return "";
    std::string buf((size_t)n + 1, '\0');
    get(&buf[0], n + 1);
    buf.resize((size_t)n);
    return buf;
}

// ---- host side (called by the mission code)
static void CoopOpen(const std::string &name, const Vec3 &start, int payout) {
    g_coopHosting = false; g_coopStep = -1;
    if (!CoopAvailable()) return;
    if (g_coopLobbyOpen) { g_coopLobbyOpen = false; g_coopHosting = true; Log("coop: the lobby's job runs \"%s\"", name.c_str()); return; }
    g_coopHosting = LO().jobOpen(name.c_str(), start.x, start.y, start.z, payout);
    if (g_coopHosting) Log("coop: opened job \"%s\" (members' payout %d)", name.c_str(), payout);
}
static void CoopReport(int status, int step, const Vec3 &p, const std::string &text) {
    if (!g_coopHosting || !LO().jobUpdate) return;
    LO().jobUpdate(status, step, p.x, p.y, p.z, text.c_str());
    if (status >= COOP_PASSED) { g_coopHosting = false; Log("coop: job ended (status %d)", status); }
}
// each frame of a running mission: report a new step, or its objective moving (snapped to a road, a car)
static void CoopTrack(int step, const Vec3 &p, const std::string &text) {
    if (!g_coopHosting) return;
    if (step == g_coopStep && Dist(p, g_coopPos) < 15.f) return;
    g_coopStep = step; g_coopPos = p;
    CoopReport(COOP_RUNNING, step, p, text);
}

// ---- member side
static void CoopRefresh() {
    g_coopJobs.clear();
    std::string list = CoopFetch(LO().getJobs);
    size_t at = 0;
    while (at < list.size()) {
        size_t nl = list.find('\n', at);
        std::vector<std::string> f = SplitTabs(list.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
        at = nl == std::string::npos ? list.size() : nl + 1;
        if (f.size() < 8) continue;
        CoopJob j;
        j.id = (unsigned)std::strtoul(f[0].c_str(), nullptr, 10); j.host = f[1]; j.title = f[2];
        j.start = V3((float)std::atof(f[3].c_str()), (float)std::atof(f[4].c_str()), (float)std::atof(f[5].c_str()));
        j.members = std::atoi(f[6].c_str()); j.status = std::atoi(f[7].c_str());
        if (f.size() > 8) j.kind = std::atoi(f[8].c_str());
        g_coopJobs.push_back(j);
        if (std::find(g_coopSeen.begin(), g_coopSeen.end(), j.id) == g_coopSeen.end()) {
            g_coopSeen.push_back(j.id);
            if (j.host != "you" && g_coop.id != j.id)
                Notify("~b~" + j.host + "~s~ " + (j.kind == 1 ? "is hosting the race ~y~" : j.kind == 2 ? "is hosting a deathmatch at ~y~" : j.kind == 3 ? "is hosting a survival: ~y~" : "started ~y~") + j.title + "~s~. Interaction Menu > ~b~Jobs~s~ to join.");
        }
    }
    CoopState s;
    std::vector<std::string> f = SplitTabs(CoopFetch(LO().getJobState));
    if (f.size() >= 11) {
        s.id = (unsigned)std::strtoul(f[0].c_str(), nullptr, 10); s.host = f[1];
        s.status = std::atoi(f[2].c_str()); s.step = std::atoi(f[3].c_str());
        s.obj = V3((float)std::atof(f[4].c_str()), (float)std::atof(f[5].c_str()), (float)std::atof(f[6].c_str()));
        s.payout = std::atoi(f[7].c_str()); s.text = f[8]; s.members = f[9]; s.hosting = f[10] == "1";
        if (f.size() >= 13) { s.kind = std::atoi(f[11].c_str()); s.startsIn = std::atoll(f[12].c_str()); }
    }
    if (s.kind >= 1) { g_coop = s; RemoveBlip(g_coopBlip); return; }   // races / deathmatches / survivals: Races.inc / Deathmatch.inc / Survival.inc draw everything
    if (s.hosting || s.host == "you") { g_coop = s; RemoveBlip(g_coopBlip); return; }   // our own mission draws itself
    for (const CoopJob &j : g_coopJobs) if (j.id == s.id) g_coopTitle = j.title;
    bool same = s.id && s.id == g_coop.id;
    if (s.id && !same) {
        Sound("Mission_Pass_Notify", "DLC_HEISTS_GENERAL_FRONTEND_SOUNDS");
        Shard("~y~" + (g_coopTitle.empty() ? std::string("JOB") : g_coopTitle), s.host + "'s job. Follow the objective.");
    }
    if (same && s.status != g_coop.status) {
        if ((s.status == COOP_PASSED || s.status == COOP_FAILED) && g_coopMemberEnd) g_coopMemberEnd(s.status == COOP_PASSED, s);   // the results board
        else if (s.status == COOP_PASSED) {
            Shard("~g~JOB PASSED", (g_coopTitle.empty() ? std::string("Job") : g_coopTitle) + "  " + Money(s.payout) + " to your bank");
            Sound("Mission_Pass_Notify", "DLC_HEISTS_GENERAL_FRONTEND_SOUNDS");
        } else if (s.status == COOP_FAILED) {
            Shard("~r~JOB FAILED", s.host + "'s job failed.");
            Sound("ScreenFlash", "MissionFailedSounds");
        } else if (s.status == COOP_CLOSED) Notify("~b~" + s.host + "~s~ ended the job.");
    }
    if (!s.id && g_coop.id && g_coop.status <= COOP_RUNNING) Notify("You left the job.");
    if (same && s.status == COOP_RUNNING && s.step != g_coop.step) Sound("CHECKPOINT_NORMAL", "HUD_MINI_GAME_SOUNDSET");
    g_coop = s;
    // the objective blip (yellow, with a GPS route), only while the job runs and has a place to go
    bool want = s.id && s.status <= COOP_RUNNING && (s.obj.x != 0.f || s.obj.y != 0.f);
    if (!want) RemoveBlip(g_coopBlip);
    else if (!g_coopBlip) {
        g_coopBlip = CoordBlip(s.obj, 1, 5, "Job objective", false);
        N(N_SET_BLIP_ROUTE, g_coopBlip, 1);
        N(N_SET_BLIP_ROUTE_COLOUR, g_coopBlip, 5);
    } else N(N_SET_BLIP_COORDS, g_coopBlip, s.obj.x, s.obj.y, s.obj.z);
}
static void CoopTick() {
    if (!LO().jobRevision) return;
    if (!YouActive()) { if (g_coopBlip) RemoveBlip(g_coopBlip); return; }
    int rev = LO().jobRevision();
    if (rev != g_coopRev || Now() > g_coopNext) { g_coopRev = rev; g_coopNext = Now() + 2000; CoopRefresh(); }   // + a slow poll for player names
    if (g_coop.id && g_coop.kind >= 1 && g_coop.status == COOP_LOBBY && !g_coop.hosting && g_coop.host != "you" && !g_menuOpen)
        Text("In ~b~" + g_coop.host + (g_coop.kind == 2 ? "~s~'s deathmatch lobby" : g_coop.kind == 3 ? "~s~'s survival lobby" : "~s~'s race lobby") + ": waiting for the start.", 0.5f, 0.905f, 0.45f, 4, 255, 255, 255, 255, 0, 1.0f, true);
    if (!g_coop.id || g_coop.kind >= 1 || g_coop.hosting || g_coop.host == "you" || g_coop.status > COOP_RUNNING || g_mActive || g_menuOpen) return;
    // objective text like the host's (Online shows the same objective to the whole crew)
    std::string t = g_coop.status == COOP_LOBBY ? "Wait for ~b~" + g_coop.host + "~s~ to start the job."
                  : !g_coop.text.empty() ? g_coop.text : "Help ~b~" + g_coop.host + "~s~.";
    Text(t, 0.5f, 0.905f, 0.45f, 4, 255, 255, 255, 255, 0, 1.0f, true);
    if (g_coopBlip) {
        Vec3 o = g_coop.obj;
        if (Dist(Pos(Me()), o) < 60.f) Marker(o, 240, 200, 80);
    }
}
static void CoopJoin(unsigned id) {
    if (!LO().jobJoin) return;
    LO().jobJoin(id);
    if (id) Notify("Joining the job...");
}
