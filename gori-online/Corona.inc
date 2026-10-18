// ---------------------------------------------------------------- the job lobby ("corona"), like GTA Online's
// An activity starts here: its title and description over an aerial, blurred view of where it happens, and three
// columns. Left: the host's settings, then the invite page (lobby type, invite, Play); middle: the players with
// their status (HOST / JOINED) and rank; right: your character (name, rank, skill bars). An Open lobby is a
// server job (kind 0) the other players join from Jobs; when the host plays, everyone goes into the job's own
// session (the server's v11 sessions). At the end: the results board (position, kills, deaths, RP, cash) and
// "Continue". Members joining a lobby see the same screen and wait for the host.
struct CoronaOpt { std::string name; std::vector<std::string> values; int sel = 0; std::string desc; };
struct CoronaDef {
    std::string title, desc;
    int theme = 0;                 // 0 mission (blue), 1 race (pink), 2 versus (red), 3 survival (green)
    int maxPlayers = 4, payout = 0;
    Vec3 at{};                     // where it happens: the aerial view
    std::vector<CoronaOpt> opts;
    std::function<void(const std::vector<int> &)> onPlay;   // host: start it alone (Closed), with the settings' choices
    // activities with their own lobby job (races, deathmatches, survivals, Arena War): Open opens it, Play starts it
    std::function<bool(const std::vector<int> &)> openLobby;
    std::function<void()> startLobby, cancelLobby;
    std::function<void(CoronaDef &)> onChange;   // options that depend on others (a vehicle class's vehicles, a theme's maps)
    bool solo = true;                            // false: needs Open (a deathmatch)
};
enum CoronaPage { CRP_NONE, CRP_SETTINGS, CRP_INVITE, CRP_MEMBER, CRP_RESULTS };
struct CoronaResultRow { std::string name; int rank = 0, kills = 0, deaths = 0, rp = 0, cash = 0; bool me = false; };
struct CoronaState {
    CoronaDef def;
    int page = CRP_NONE, sel = 0, cam = 0, lobbyType = 0;   // lobbyType 0 Closed (alone), 1 Open (a server job)
    bool jobOpen = false;
    unsigned memberJob = 0;
    int resultsUntil = 0; bool passed = false; std::string why;
    std::vector<CoronaResultRow> results;
    int navHeld = 0, navNext = 0;
};
static CoronaState g_cor;
static const int COR_THEME[4][3] = {{44, 94, 159}, {197, 44, 132}, {178, 40, 40}, {72, 140, 60}};

static bool CoronaActive() { return g_cor.page != CRP_NONE; }
static void CoronaSetLobby(int type);
static void CoronaCamera(bool on) {
    if (on) {
        Vec3 a = g_cor.def.at;
        if (a.x == 0.f && a.y == 0.f) a = Pos(Me());
        if (!g_cor.cam) g_cor.cam = N<int>(N_CREATE_CAM_WITH_PARAMS, "DEFAULT_SCRIPTED_CAMERA", a.x, a.y - 60.f, a.z + 110.f, -58.f, 0.f, 0.f, 55.f, 0, 2);
        N(N_SET_CAM_ACTIVE, g_cor.cam, 1);
        N(N_RENDER_SCRIPT_CAMS, 1, 1, 800, 1, 0, 0);
        N(N_SET_FOCUS_POS_AND_VEL, a.x, a.y, a.z, 0.f, 0.f, 0.f);
        N(N_SET_TIMECYCLE_MODIFIER, "hud_def_blur");
        N(N_FREEZE_ENTITY_POSITION, Me(), 1);
    } else {
        if (g_cor.cam) { N(N_RENDER_SCRIPT_CAMS, 0, 1, 800, 1, 0, 0); N(N_DESTROY_CAM, g_cor.cam, 0); g_cor.cam = 0; }
        N(N_CLEAR_FOCUS);
        N(N_CLEAR_TIMECYCLE_MODIFIER);
        N(N_FREEZE_ENTITY_POSITION, Me(), 0);
    }
}
static void OpenCorona(const CoronaDef &d) {
    if (g_mActive || CoronaActive()) { Notify("~r~Finish your current job first."); return; }
    CloseMenu();
    g_cor = CoronaState();
    g_cor.def = d;
    g_cor.page = d.opts.empty() ? CRP_INVITE : CRP_SETTINGS;
    if (g_cor.def.onChange) g_cor.def.onChange(g_cor.def);
    if (g_cor.page == CRP_INVITE && !d.solo) CoronaSetLobby(1);
    CoronaCamera(true);
    Sound("SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET");
    Log("corona: %s", d.title.c_str());
}
static void CloseCorona(bool cancelJob) {
    if (cancelJob && g_cor.jobOpen) {
        if (g_cor.def.cancelLobby) g_cor.def.cancelLobby();
        else if (LO().jobUpdate) LO().jobUpdate(COOP_CLOSED, 0, 0, 0, 0, "");
        g_coopLobbyOpen = false;
    }
    CoronaCamera(false);
    g_cor.page = CRP_NONE;
}
static std::vector<int> CoronaSel() { std::vector<int> sel; for (auto &o : g_cor.def.opts) sel.push_back(o.sel); return sel; }
static void CoronaSetLobby(int type) {   // 1: Open, a server job the others see; 0: Closed
    if (type == 0 && !g_cor.def.solo) { Notify("~r~This one needs other players: the lobby stays open."); return; }
    g_cor.lobbyType = type;
    if (type == 1 && !g_cor.jobOpen && CoopAvailable()) {
        if (g_cor.def.openLobby) g_cor.jobOpen = g_cor.def.openLobby(CoronaSel());
        else {
            g_cor.jobOpen = LO().jobOpen(g_cor.def.title.c_str(), Pos(Me()).x, Pos(Me()).y, Pos(Me()).z, g_cor.def.payout / 2);
            g_coopLobbyOpen = g_cor.jobOpen;
        }
        if (g_cor.jobOpen) Notify("Lobby open: the other players can join from ~b~Jobs~s~.");
        else g_cor.lobbyType = 0;
    } else if (type == 0 && g_cor.jobOpen) {
        if (g_cor.def.cancelLobby) g_cor.def.cancelLobby();
        else if (LO().jobUpdate) LO().jobUpdate(COOP_CLOSED, 0, 0, 0, 0, "");
        g_cor.jobOpen = false; g_coopLobbyOpen = false;
    }
}
static void CoronaPlay() {
    if (!g_cor.def.solo && !g_cor.jobOpen) { Notify("~r~Open the lobby first: this one needs other players."); return; }
    std::vector<int> sel = CoronaSel();
    auto play = g_cor.def.onPlay;
    if (g_cor.jobOpen && g_cor.def.startLobby) { auto start = g_cor.def.startLobby; play = [start](const std::vector<int> &) { start(); }; }
    N(N_DO_SCREEN_FADE_OUT, 400); Wait(450);
    CoronaCamera(false);
    g_cor.page = CRP_NONE;
    Sound("CONTINUE", "HUD_FRONTEND_DEFAULT_SOUNDSET");
    if (play) play(sel);
    N(N_DO_SCREEN_FADE_IN, 600);
}
// a member of someone's lobby: the same screen, waiting for the host
static void CoronaMember(unsigned jobId, const std::string &title) {
    if (CoronaActive() || g_mActive) return;
    g_cor = CoronaState();
    g_cor.def.title = title.empty() ? "Job" : title;
    g_cor.def.desc = "Waiting for the host to start the job.";
    g_cor.memberJob = jobId;
    g_cor.page = CRP_MEMBER;
    for (const CoopJob &j : g_coopJobs) if (j.id == jobId) g_cor.def.at = j.start;
    CoronaCamera(true);
}

// ---- results ("Continue"), filled by the activity
static void CoronaResults(const std::string &title, bool passed, const std::string &why, const std::vector<CoronaResultRow> &rows) {
    g_cor = CoronaState();
    g_cor.def.title = title;
    g_cor.page = CRP_RESULTS; g_cor.passed = passed; g_cor.why = why; g_cor.results = rows;
    g_cor.resultsUntil = Now() + 15000;
    Sound(passed ? "Mission_Pass_Notify" : "ScreenFlash", passed ? "DLC_HEISTS_GENERAL_FRONTEND_SOUNDS" : "MissionFailedSounds");
}

// ---- drawing
static void CorBar(float x, float y, float w, const std::string &t, const int *c) {   // a column header
    Rect(x + w / 2, y + 0.0145f, w, 0.029f, c[0], c[1], c[2], 255);
    Text(t, x + w / 2, y + 0.004f, 0.32f, 0, 255, 255, 255, 255, 0);
}
static void CorRow(float x, float y, float w, const std::string &l, const std::string &r, bool sel, bool off = false) {
    const float h = 0.0375f;
    Rect(x + w / 2, y + h / 2, w, h - 0.002f, sel ? 245 : 0, sel ? 245 : 0, sel ? 245 : 0, sel ? 255 : 170);
    int c = sel ? 0 : off ? 140 : 240;
    Text(l, x + 0.006f, y + 0.008f, 0.32f, 0, c, c, c, 255, 1);
    if (!r.empty()) Text(r, x, y + 0.008f, 0.32f, 0, c, c, c, 255, 2, x + w - 0.006f);
}
static void CorPill(float rx, float y, const std::string &t, int r, int g, int b) {   // a status pill, right-aligned at rx
    float w = 0.012f + 0.0062f * (float)t.size();
    Rect(rx - w / 2, y + 0.0188f, w, 0.024f, r, g, b, 255);
    Text(t, rx - w / 2, y + 0.0088f, 0.26f, 0, 255, 255, 255, 255, 0);
}
static std::vector<std::pair<std::string, int>> CoronaPlayers() {   // name, rank: the host first
    std::vector<std::pair<std::string, int>> out;
    std::map<std::string, int> ranks;
    if (LO().getPlayers) {
        std::vector<char> buf(8192);
        LO().getPlayers(buf.data(), (int)buf.size());
        std::string line;
        for (const char *c = buf.data(); ; ++c) {
            if (*c == '\n' || !*c) { auto f = Split(line, '|'); if (f.size() >= 3) ranks[f[1]] = std::atoi(f[2].c_str()); line.clear(); if (!*c) break; }
            else line += *c;
        }
    }
    int myRank = LevelOf(g_rp);
    if (g_cor.page == CRP_MEMBER) {
        out.push_back({g_coop.host, ranks.count(g_coop.host) ? ranks[g_coop.host] : 1});
        for (auto &m : Split(g_coop.members, ',')) if (!m.empty()) out.push_back({m == "you" ? g_myName : m, m == "you" ? myRank : ranks.count(m) ? ranks[m] : 1});
        bool meIn = false;
        for (auto &p : out) if (p.first == g_myName) meIn = true;
        if (!meIn) out.push_back({g_myName, myRank});
        return out;
    }
    out.push_back({g_myName.empty() ? "You" : g_myName, myRank});
    if (g_cor.jobOpen && g_coop.hosting) for (auto &m : Split(g_coop.members, ',')) if (!m.empty()) out.push_back({m, ranks.count(m) ? ranks[m] : 1});
    return out;
}
static void CoronaDraw() {
    N(N_HIDE_HUD_AND_RADAR_THIS_FRAME);
    const int *tc = COR_THEME[std::max(0, std::min(3, g_cor.def.theme))];
    const float L = 0.162f, W = 0.225f, G = 0.002f, top = 0.185f;
    // title and description
    Text(g_cor.def.title, L, 0.085f, 0.75f, 4, 255, 255, 255, 255, 1);
    N(N_SET_TEXT_WRAP, L, L + 3 * W + 2 * G);
    TextLong(g_cor.def.desc, L, 0.135f, 0.3f, 255, 255, 255, L, L + 3 * W + 2 * G);
    if (g_cor.page == CRP_RESULTS) {   // the results board
        float x = L, w = 3 * W + 2 * G, y = top;
        Text(g_cor.passed ? "~g~PASSED" : "~r~FAILED" + std::string(g_cor.why.empty() ? "" : "~s~  " + g_cor.why), L, 0.135f, 0.4f, 0, 255, 255, 255, 255, 1);
        Rect(x + w / 2, y + 0.0145f, w, 0.029f, 44, 94, 159, 255);
        const char *cols[] = {"POSITION", "KILLS", "DEATHS", "RP", "CASH"};
        const float cx[] = {x + 0.006f, x + w * 0.52f, x + w * 0.64f, x + w * 0.76f, x + w * 0.9f};
        Text(cols[0], cx[0], y + 0.004f, 0.3f, 0, 255, 255, 255, 255, 1);
        for (int i = 1; i < 5; ++i) Text(cols[i], cx[i], y + 0.004f, 0.3f, 0, 255, 255, 255, 255, 0);
        y += 0.031f;
        int pos = 1;
        for (auto &r : g_cor.results) {
            Rect(x + w / 2, y + 0.0185f, w, 0.035f, r.me ? tc[0] : 0, r.me ? tc[1] : 0, r.me ? tc[2] : 0, r.me ? 220 : 170);
            Text(std::to_string(pos++) + "   " + r.name, cx[0], y + 0.007f, 0.32f, 0, 255, 255, 255, 255, 1);
            Text(std::to_string(r.rank), x + w * 0.44f, y + 0.007f, 0.3f, 0, 255, 255, 255, 255, 0);
            Text(std::to_string(r.kills), cx[1], y + 0.007f, 0.32f, 0, 255, 255, 255, 255, 0);
            Text(std::to_string(r.deaths), cx[2], y + 0.007f, 0.32f, 0, 255, 255, 255, 255, 0);
            Text(std::to_string(r.rp), cx[3], y + 0.007f, 0.32f, 0, 255, 255, 255, 255, 0);
            Text(Money(r.cash), cx[4], y + 0.007f, 0.32f, 0, 114, 204, 114, 255, 0);
            y += 0.037f;
        }
        int left = std::max(0, (g_cor.resultsUntil - Now()) / 1000);
        Text("Continue (0:" + std::string(left < 10 ? "0" : "") + std::to_string(left) + ")", L + w, 0.9f, 0.35f, 0, 255, 255, 255, 255, 2, L + w);
        return;
    }
    // left: settings / invite (the host), or waiting (a member)
    float y = top;
    bool settings = g_cor.page == CRP_SETTINGS, member = g_cor.page == CRP_MEMBER;
    CorBar(L, y, W, settings ? "SETTINGS" : member ? "JOB" : "INVITE", tc);
    y += 0.031f;
    std::vector<std::pair<std::string, std::string>> rows;
    std::string desc;
    if (settings) {
        for (auto &o : g_cor.def.opts) rows.push_back({o.name, "< " + o.values[o.sel] + " >"});
        rows.push_back({"Confirm Settings", ""});
        desc = g_cor.sel < (int)g_cor.def.opts.size() ? g_cor.def.opts[g_cor.sel].desc : "Confirm the settings and go on to the invites.";
    } else if (member) {
        rows.push_back({"Host", g_coop.host});
        rows.push_back({"Leave", ""});
        desc = g_coop.host + " is the host. The job starts when they press Play.";
    } else {
        rows.push_back({"Lobby Type", g_cor.lobbyType ? "< Open >" : "< Closed >"});
        rows.push_back({"Invite All Players", ""});
        rows.push_back({"Play", ""});
        desc = g_cor.sel == 0 ? "Open: the other players see your job in Jobs and can join it. Closed: you play alone." :
               g_cor.sel == 1 ? "Send an invite to every player in the session." : "Start the job with the players in the lobby.";
    }
    for (int i = 0; i < (int)rows.size(); ++i) { CorRow(L, y, W, rows[i].first, rows[i].second, i == g_cor.sel); y += 0.0375f; }
    y += 0.004f;
    int lines = TextLines(desc, L + 0.006f, y + 0.006f, 0.3f, L + 0.006f, L + W - 0.02f);
    Rect(L + W / 2, y + (0.012f + 0.024f * lines) / 2, W, 0.012f + 0.024f * lines, 0, 0, 0, 170);
    TextLong(desc, L + 0.006f, y + 0.005f, 0.3f, 255, 255, 255, L + 0.006f, L + W - 0.02f);
    // middle: the players
    float mx = L + W + G;
    auto players = CoronaPlayers();
    CorBar(mx, top, W, "PLAYERS " + std::to_string(players.size()) + " OF 1-" + std::to_string(std::max(1, g_cor.def.maxPlayers)), tc);
    y = top + 0.031f;
    for (size_t i = 0; i < players.size(); ++i) {
        CorRow(mx, y, W, players[i].first, "", false);
        bool host = i == 0;
        CorPill(mx + W - 0.03f, y, host ? "HOST" : "JOINED", host ? tc[0] : 80, host ? tc[1] : 170, host ? tc[2] : 80);
        Text(std::to_string(players[i].second), mx + W - 0.013f, y + 0.008f, 0.3f, 0, 255, 255, 255, 255, 0);
        y += 0.0375f;
    }
    for (size_t i = players.size(); i < (size_t)std::max(4, g_cor.def.maxPlayers) && i < 8; ++i) { Rect(mx + W / 2, y + 0.0185f, W, 0.0355f, 0, 0, 0, 110); y += 0.0375f; }
    // right: your character
    float rx = mx + W + G;
    Rect(rx + W / 2, top + 0.0145f, W, 0.029f, 0, 0, 0, 200);
    Text("YOUR CHARACTER", rx + W / 2, top + 0.004f, 0.32f, 0, 255, 255, 255, 255, 0);
    y = top + 0.031f;
    Rect(rx + W / 2, y + 0.0175f, W, 0.035f, tc[0], tc[1], tc[2], 255);
    Text(g_myName.empty() ? "You" : g_myName, rx + W / 2, y + 0.006f, 0.34f, 0, 255, 255, 255, 255, 0);
    y += 0.037f;
    int rank = LevelOf(g_rp);
    Rect(rx + W / 2, y + 0.045f, W, 0.09f, 0, 0, 0, 170);
    Text(std::to_string(rank), rx + W / 2, y + 0.012f, 0.9f, 4, 255, 255, 255, 255, 0);
    Text("RANK", rx + W / 2, y + 0.068f, 0.26f, 0, 200, 200, 200, 255, 0);
    y += 0.092f;
    static const char *skills[] = {"Stamina", "Shooting", "Strength", "Stealth", "Flying", "Driving", "Lung Capacity"};
    for (int i = 0; i < 7; ++i) {   // five segments each, filling up with the rank
        Rect(rx + W / 2, y + 0.0145f, W, 0.029f, 0, 0, 0, 170);
        Text(skills[i], rx + 0.006f, y + 0.005f, 0.28f, 0, 255, 255, 255, 255, 1);
        int fill = std::min(5, 1 + (rank + i * 3) / 12);
        for (int s = 0; s < 5; ++s) {
            float sx = rx + W * 0.55f + s * W * 0.088f;
            Rect(sx + W * 0.04f, y + 0.0215f, W * 0.08f, 0.006f, s < fill ? tc[0] + 40 : 60, s < fill ? tc[1] + 60 : 60, s < fill ? std::min(255, tc[2] + 60) : 60, 255);
        }
        y += 0.031f;
    }
    if (member) Text(g_coop.host + " is the host", L + 3 * W + 2 * G, 0.9f, 0.32f, 0, 255, 255, 255, 255, 2, L + 3 * W + 2 * G);
}

// ---- each frame
static void CoronaTick() {
    // a member: into the lobby screen while the host's mission lobby is open; out (to the host) when it starts
    if (!CoronaActive() && g_coop.id && g_coop.status == COOP_LOBBY && !g_coop.hosting && g_coop.host != "you" && !g_mActive)
        CoronaMember(g_coop.id, g_coopTitle);
    if (g_cor.page == CRP_MEMBER && (!g_coop.id || g_coop.id != g_cor.memberJob || g_coop.status != COOP_LOBBY)) {
        bool started = g_coop.id == g_cor.memberJob && g_coop.status == COOP_RUNNING;
        Vec3 at = g_cor.def.at;
        N(N_DO_SCREEN_FADE_OUT, 400); Wait(450);
        CoronaCamera(false);
        g_cor.page = CRP_NONE;
        if (started && g_coop.kind == 0 && (at.x != 0.f || at.y != 0.f)) Teleport(at.x + 2.f, at.y + 2.f, at.z, 0.f);   // with the host, like Online's job start
        N(N_DO_SCREEN_FADE_IN, 600);
        if (!started) Notify("The host closed the lobby.");
    }
    if (!CoronaActive()) return;
    for (int c = 0; c < 360; ++c) if (c != 249 && c != 245 && c != 1 && c != 2) N(N_DISABLE_CONTROL_ACTION, 0, c, 1);   // the lobby has the controls (not chat / camera look)
    if (g_myName.empty() && LO().getPlayerName) { char b[64] = {0}; LO().getPlayerName(b, sizeof b); g_myName = b; }
    if (g_cor.page == CRP_RESULTS) {
        CoronaDraw();
        if (Now() > g_cor.resultsUntil || NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 2, 201))) { g_cor.page = CRP_NONE; Sound("CONTINUE", "HUD_FRONTEND_DEFAULT_SOUNDSET"); }
        return;
    }
    int rowsN = g_cor.page == CRP_SETTINGS ? (int)g_cor.def.opts.size() + 1 : g_cor.page == CRP_MEMBER ? 2 : 3;
    if (g_cor.sel >= rowsN) g_cor.sel = rowsN - 1;
    int now = Now();
    bool up = NB(N<int>(N_IS_DISABLED_CONTROL_PRESSED, 2, 188)), down = NB(N<int>(N_IS_DISABLED_CONTROL_PRESSED, 2, 187));
    bool left = NB(N<int>(N_IS_DISABLED_CONTROL_PRESSED, 2, 189)), right = NB(N<int>(N_IS_DISABLED_CONTROL_PRESSED, 2, 190));
    if (up || down || left || right) {
        if (!g_cor.navHeld || now >= g_cor.navNext) {
            if (up || down) { g_cor.sel = (g_cor.sel + (down ? 1 : -1) + rowsN) % rowsN; Sound("NAV_UP_DOWN", "HUD_FRONTEND_DEFAULT_SOUNDSET"); }
            else if (g_cor.page == CRP_SETTINGS && g_cor.sel < (int)g_cor.def.opts.size()) {
                CoronaOpt &o = g_cor.def.opts[g_cor.sel];
                o.sel = (o.sel + (right ? 1 : -1) + (int)o.values.size()) % (int)o.values.size();
                if (g_cor.def.onChange) g_cor.def.onChange(g_cor.def);
                Sound("NAV_LEFT_RIGHT", "HUD_FRONTEND_DEFAULT_SOUNDSET");
            } else if (g_cor.page == CRP_INVITE && g_cor.sel == 0) { CoronaSetLobby(!g_cor.lobbyType); Sound("NAV_LEFT_RIGHT", "HUD_FRONTEND_DEFAULT_SOUNDSET"); }
            g_cor.navNext = now + (g_cor.navHeld ? 120 : 350);
            g_cor.navHeld = 1;
        }
    } else g_cor.navHeld = 0;
    if (NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 2, 201))) {
        Sound("SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET");
        if (g_cor.page == CRP_SETTINGS) { if (g_cor.sel == rowsN - 1) { g_cor.page = CRP_INVITE; g_cor.sel = 2; if (!g_cor.def.solo) CoronaSetLobby(1); } }
        else if (g_cor.page == CRP_INVITE) {
            if (g_cor.sel == 0) CoronaSetLobby(!g_cor.lobbyType);
            else if (g_cor.sel == 1) { CoronaSetLobby(1); if (g_cor.jobOpen) Notify("Invites sent to the players in the session."); else Notify("~r~No other players to invite."); }
            else { CoronaPlay(); return; }
        } else if (g_cor.page == CRP_MEMBER && g_cor.sel == 1) { CoopJoin(0); }
    }
    if (NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 2, 202))) {
        Sound("BACK", "HUD_FRONTEND_DEFAULT_SOUNDSET");
        if (g_cor.page == CRP_INVITE && !g_cor.def.opts.empty()) { CoronaSetLobby(0); g_cor.lobbyType = 0; g_cor.page = CRP_SETTINGS; g_cor.sel = 0; }
        else if (g_cor.page == CRP_MEMBER) CoopJoin(0);
        else { CloseCorona(true); return; }
    }
    CoronaDraw();
    MenuButtons();
}
