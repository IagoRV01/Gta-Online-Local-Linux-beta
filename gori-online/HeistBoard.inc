// ---------------------------------------------------------------- the heist planning board (GTA Online's HEIST_MP)
// In Online the board in a high-end apartment is the hei_prop_dlc_heist_board prop (a dark panel and a whiteboard) with
// the HEIST_MP movie (4.2 x 2.4 m) and the HEISTMAP_MP blueprint of the city (3.815 x 2.27 m) drawn on it in 3D
// (fmmc_launcher.c); here both are drawn with the solid 3D draw, which keeps them in depth order (the plain one draws
// one movie over the other whatever the depth), the map behind, the photos pinned on it: the planning board while a heist's setups are open (one slot a
// setup, ticked when done), the heist board for the finale (the crew, roles, cuts, the take's pie chart, the leader's
// setup cost, LAUNCH HEIST). Here it hangs in front of the planning spot of our apartments (AptActs 'P'), always drawn
// while you are near it; using it (E) puts the camera on it: left / right pick a slot, up / down another heist, Enter
// opens the heist's lobby (OpenHeistCorona, as the old menu did), Backspace leaves. The calls and their order follow
// fmmc_launcher.c (SET_HEIST_NAME, BLANK_PLANNING_SLOT, INITIALISE_PLANNINGBOARD, UPDATE_PLANNING_SLOT; SET_LABELS,
// ADD_CREW_MEMBER, ADD_PIECHART, ADD_TODO_LIST, INITIALISE_HEISTBOARD, SET_LEADER_COST, UPDATE_CREW_MEMBER).
struct HeistBoard {
    int movie = 0, heist = -1, built = -1, builtStage = -1, sel = 0;
    std::string builtCrew;   // the lobby's members when it was drawn: a new member redraws it
    int prop = 0;            // Online's board itself (hei_prop_dlc_heist_board): the movie is drawn on it
    int map = 0;             // and the heist map on its dark panel (HEISTMAP_MP, 3.815 x 2.27 m in fmmc_launcher.c)
    bool using_ = false;
    bool hid = false;        // you, hidden from the board camera while using it
    Vec3 pos{}; float h = 0.f;   // the planning spot it hangs in front of
};
static HeistBoard g_hb;
static float g_hbRot = 180.f, g_hbAhead = 0.52f, g_hbUp = 1.6f, g_hbW = 4.2f;   // where the movie hangs (hbtune): its centre, facing the spot
static float g_hbPropAhead = 0.60f, g_hbPropZ = 1.6f, g_hbPropRot = 180.f;   // the 4.54 x 2.38 m board prop: ahead of the spot, its centre up, turned (hbprop; tested in dt10)
static uint32_t g_hbRoom = 0;   // the room the board prop was put in
// the map: on, drawn before the movie, solid, behind the movie by, its rotation, its centre off the board's, its scale
static int g_hbMapOn = 1, g_hbMapFirst = 1, g_hbMapSolid = 1;
static float g_hbMapAhead = 0.02f, g_hbMapRX = 0.f, g_hbMapRY = 0.f, g_hbMapRZ = 180.f, g_hbMapSide = 0.f, g_hbMapUp = 0.f, g_hbMapScale = 1.f;
static int g_hbMode = 1, g_hbCorner = 0;   // 1 the solid 3D draw, 0 the plain one (placed by a corner, g_hbCorner: 0 grows right+down, 1 left+down, 2 right+up, 3 left+up), 2 full screen (tests)
static float g_hbRotX = 0.f;   // the plain 3D draw's y axis points up: turned over (180) it reads right
static bool HbUsing() { return g_hb.using_; }
// the solid draw turns the other way round: its yaw for a movie at heading h turned by rot (180: facing the spot) is
// 180 - (h + rot) (tested in dt10 at 90, dt12 at 0, dt09 at 180, hg_sky at 70.5)
static float HbSolidYaw(float h, float rot, bool solid) { return solid ? 180.f - (h + rot) : h + rot; }

// scaleform parameters: a game text label, raw text in a label slot, raw text, numbers
static void HbLabel(const char *label) { N(N_BEGIN_TEXT_COMMAND_SCALEFORM_STRING, label); N(N_END_TEXT_COMMAND_SCALEFORM_STRING); }
static void HbRawLabel(const std::string &t) {
    N(N_BEGIN_TEXT_COMMAND_SCALEFORM_STRING, "STRING");
    N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, t.c_str());
    N(N_END_TEXT_COMMAND_SCALEFORM_STRING);
}
static void HbName(const std::string &t) { N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_PLAYER_NAME_STRING, t.c_str()); }
static void HbTex(const std::string &t) { N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_TEXTURE_NAME_STRING, t.c_str()); }
static void HbInt(int v) { N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, v); }
static void HbBool(bool v) { N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, v ? 1 : 0); }
static void HbBegin(const char *m) { N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, g_hb.movie, m); }
static void HbEnd() { N(N_END_SCALEFORM_MOVIE_METHOD); }

static int HbDefaultHeist() {   // the first unlocked heist (Online: the one Lester last called about)
    for (int h = 0; h < NHEISTS; ++h) { std::string why; if (HeistUnlocked(h, why)) return h; }
    return 0;
}
static bool HbFinale(int h) { return g_heistStage[h] >= HEISTS[h].stages - 1; }

// fills the movie for heist h: its setups, or the finale's heist board
static void HbBuild(int h) {
    const HeistDef &d = HEISTS[h];
    int st = g_heistStage[h], setups = d.stages - 1;
    HbBegin("CLEANUP"); HbEnd();
    if (!HbFinale(h)) {
        HbBegin("SET_HEIST_NAME"); HbName(d.name); HbLabel("HEIST_PP_INFO"); HbLabel(""); HbEnd();
        for (int i = 0; i < setups; ++i) { HbBegin("BLANK_PLANNING_SLOT"); HbEnd(); }
        HbBegin("INITIALISE_PLANNINGBOARD"); HbEnd();
        for (int i = 0; i < setups; ++i) {
            HbBegin("UPDATE_PLANNING_SLOT");
            HbInt(i); HbTex(""); HbName(d.stageNames[i]);
            HbTex(""); HbName(""); HbTex(""); HbName(""); HbTex(""); HbName("");
            HbInt(i < st ? 1 : 0);   // completed
            HbBool(i <= st);         // available: done, or the next one
            HbBool(i == g_hb.sel); HbBool(false);
            HbEnd();
        }
        g_hb.sel = std::clamp(g_hb.sel, 0, std::max(0, setups - 1));
    } else {
        HbBegin("SET_HEIST_NAME"); HbName(d.name); HbLabel(""); HbLabel(""); HbLabel("HTITLE_DEF"); HbEnd();
        HbBegin("SET_LABELS"); HbLabel(""); HbLabel("HEIST_LAUN_1"); HbLabel("HEIST_TAKE"); HbLabel("HEIST_PLAN"); HbLabel("HEIST_TOTAL");
        HbLabel("HEIST_COSTS"); HbLabel("HEIST_ROLE"); HbLabel("HEIST_CUT"); HbLabel("HEIST_STATUS"); HbLabel(""); HbEnd();
        for (int i = 0; i < 4; ++i) { HbBegin("ADD_CREW_MEMBER"); HbInt(i); HbEnd(); }
        // the crew: the heist lobby's members (Online's cuts: each at the lobby's Crew Cut, the leader the rest, min 15%)
        std::vector<std::string> crew;
        for (auto &n : Split(g_coop.members, ',')) if (!n.empty() && crew.size() < 3) crew.push_back(n);
        int cut = g_heistCut[h], leaderPct = std::max(15, 100 - cut * (int)crew.size());
        HbBegin("ADD_PIECHART"); HbInt(d.payout); HbInt(leaderPct);
        for (int i = 0; i < 3; ++i) HbInt(i < (int)crew.size() ? cut : 0);
        HbInt(0); HbEnd();
        HbBegin("ADD_TODO_LIST"); HbName(""); HbEnd();
        HbBegin("INITIALISE_HEISTBOARD"); HbEnd();
        HbBegin("SET_LEADER_COST"); HbLabel("HEIST_LCOST"); HbInt(d.fee); HbEnd();
        int players = 1 + (int)crew.size();
        auto member = [&](int slot, const std::string &name, int rank, const char *role, int pct) {
            HbBegin("UPDATE_CREW_MEMBER");
            HbInt(slot); HbName(name); HbInt(rank); HbTex("");
            HbRawLabel(role); HbInt(0); HbRawLabel("Ready"); HbInt(0);
            HbInt(d.payout * pct / 100); HbInt(pct); HbInt(0); HbRawLabel(""); HbRawLabel(""); HbInt(players); HbInt(0); HbBool(false);
            HbEnd();
        };
        member(0, g_myName.empty() ? "You" : g_myName, LevelOf(g_rp), "Leader", leaderPct);
        for (int i = 0; i < (int)crew.size(); ++i) member(i + 1, crew[i], 0, "Crew", cut);
        HbBegin("LAUNCH_BUTTON_VISIBLE"); HbBool(true); HbEnd();
        HbBegin("LAUNCH_BUTTON_ENABLED"); HbBool(true); HbEnd();
    }
    HbBegin("HIGHLIGHT_ITEM"); HbInt(g_hb.sel); HbInt(0); HbBool(false); HbBool(false); HbEnd();
    g_hb.built = h; g_hb.builtStage = st; g_hb.builtCrew = g_coop.members;
    Log("heist board: %s, %s (stage %d/%d)", d.name, HbFinale(h) ? "heist board" : "planning board", st + 1, d.stages);
}

// the spot of the planning board of the apartment you are in (AptActs 'P'), if any
static bool HbSpot(Vec3 &p, float &h) {
    const char *id = AptIdInside();
    if (!id) return false;
    for (int i = 0; i < NNEW_APT_ACTS; ++i)
        if (NEW_APT_ACTS[i].kind == 'P' && !std::strcmp(NEW_APT_ACTS[i].id, id)) { p = V3(NEW_APT_ACTS[i].x, NEW_APT_ACTS[i].y, NEW_APT_ACTS[i].z); h = NEW_APT_ACTS[i].h; return true; }
    return false;
}
// using the board (the planning spot's E): the camera on it, the board's own controls
static void HbOpen() {
    if (g_hb.heist < 0) g_hb.heist = HbDefaultHeist();
    g_hb.sel = HbFinale(g_hb.heist) ? 0 : g_heistStage[g_hb.heist];
    g_hb.built = -1;
    g_hb.using_ = true;
    Vec3 p; float h;
    if (!g_hb.hid) { N(N_SET_ENTITY_VISIBLE, Me(), 0, 0); g_hb.hid = true; }   // out of the camera's way
    if (HbSpot(p, h)) ActCam(ActAhead(p, h, -2.4f, 1.6f), ActAhead(p, h, g_hbAhead, g_hbUp), 50.f);   // Online's: the whole board, straight on
    Sound("Highlight_Accept", "DLC_HEIST_PLANNING_BOARD_SOUNDS");
}
static void HbShowMe() { if (g_hb.hid) { N(N_SET_ENTITY_VISIBLE, Me(), 1, 0); g_hb.hid = false; } }
static void HbClose() {
    g_hb.using_ = false;
    HbShowMe();
    ActEnd(false);
    Sound("Highlight_Cancel", "DLC_HEIST_PLANNING_BOARD_SOUNDS");
}
// every frame inside an apartment with a planning spot: the board on the wall; while using it, its controls
static void HbFrame() {
    Vec3 p; float h;
    if (!HbSpot(p, h)) {
        if (g_hb.movie) { N(N_SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED, &g_hb.movie); g_hb.movie = 0; g_hb.built = -1; }
        if (g_hb.prop) DeleteEntity(g_hb.prop);
        if (g_hb.map) { N(N_SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED, &g_hb.map); g_hb.map = 0; }
        g_hb.using_ = false;
        HbShowMe();
        return;
    }
    // Online's board on the wall (the apartment script's hei_prop_dlc_heist_board): the cork board the movie is drawn on
    if (g_hb.prop && Dist(g_hb.pos, p) > 0.5f) DeleteEntity(g_hb.prop);   // another apartment's (napt moves you between them)
    if (!g_hb.prop || !Exists(g_hb.prop)) {
        g_hb.pos = p; g_hb.h = h;
        uint32_t m = Joaat("hei_prop_dlc_heist_board");
        if (LoadModel(m)) {
            Vec3 bp = ActAhead(p, h, g_hbPropAhead, g_hbPropZ);
            g_hb.prop = N<int>(N_CREATE_OBJECT_NO_OFFSET, m, bp.x, bp.y, bp.z, 0, 0, 0);
            if (g_hb.prop) {
                N(N_SET_ENTITY_ROTATION, g_hb.prop, 0.f, 0.f, h + g_hbPropRot, 2, 1);
                N(N_FREEZE_ENTITY_POSITION, g_hb.prop, 1);
                g_hbRoom = 0;
                Vec3 mn{}, mx{};
                N(N_GET_MODEL_DIMENSIONS, m, &mn, &mx);
                Log("heist board prop: %.2f x %.2f x %.2f (min %.2f %.2f %.2f max %.2f %.2f %.2f)", mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, mn.x, mn.y, mn.z, mx.x, mx.y, mx.z);
            }
            N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
        }
    }
    // it belongs to the planning room: the room you are in near the spot (made at the door, it would be the hall's and
    // not render in the planning room)
    if (g_hb.prop && Dist(Pos(Me()), p) < 5.f) {
        int in = N<int>(N_GET_INTERIOR_FROM_ENTITY, Me());
        uint32_t room = N<uint32_t>(N_GET_ROOM_KEY_FROM_ENTITY, Me());
        if (in && room && room != g_hbRoom) { N(N_FORCE_ROOM_FOR_ENTITY, g_hb.prop, in, room); g_hbRoom = room; }
    }
    if (!g_hb.movie) g_hb.movie = N<int>(N_REQUEST_SCALEFORM_MOVIE, "HEIST_MP");
    if (!NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, g_hb.movie))) return;
    if (g_hb.heist < 0) g_hb.heist = HbDefaultHeist();
    if (g_hb.built != g_hb.heist || g_hb.builtStage != g_heistStage[g_hb.heist] || g_hb.builtCrew != g_coop.members) HbBuild(g_hb.heist);
    // on the wall in front of the spot, its centre 1.55 m up, facing the spot (Online's board is 4.2 x 2.4 m, ours fits the
    // wall screen); the 3D movie is placed by its top-left corner
    float bw = g_hbW, bh = g_hbW * 2.4f / 4.2f;
    bool growsLeft = g_hbCorner & 1, growsUp = g_hbCorner & 2;
    Vec3 c = ActAhead(ActAhead(p, h, g_hbAhead, g_hbUp + (growsUp ? -bh : bh) / 2.f), h + 90.f, (growsLeft ? -bw : bw) / 2.f);
    // the heist map (the blueprint of the city the photos are pinned on), centred on the board like the planning movie
    if (!g_hb.map) g_hb.map = N<int>(N_REQUEST_SCALEFORM_MOVIE, "HEISTMAP_MP");
    auto drawMap = [&] {   // the solid draw is placed by the movie's centre (the plain 3D one would cover the planning movie)
        if (g_hbMode == 2 || !g_hbMapOn || !NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, g_hb.map))) return;
        float mw = 3.815f * g_hbMapScale, mh = 2.27f * g_hbMapScale;
        Vec3 mc = ActAhead(ActAhead(p, h, g_hbAhead + g_hbMapAhead, g_hbUp + g_hbMapUp), h + 90.f, g_hbMapSide);
        N(g_hbMapSolid ? N_DRAW_SCALEFORM_MOVIE_3D_SOLID : N_DRAW_SCALEFORM_MOVIE_3D, g_hb.map, mc.x, mc.y, mc.z, g_hbMapRX, g_hbMapRY, HbSolidYaw(h, g_hbMapRZ, g_hbMapSolid), 1.f, 1.f, 1.f, mw, mh, 1.f, 2);
    };
    if (g_hbMapFirst) drawMap();
    if (g_hbMode == 2) N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, g_hb.movie, 255, 255, 255, 255, 0);
    else if (g_hbMode == 1) {   // the solid draw: placed by its centre
        Vec3 cc = ActAhead(p, h, g_hbAhead, g_hbUp);
        N(N_DRAW_SCALEFORM_MOVIE_3D_SOLID, g_hb.movie, cc.x, cc.y, cc.z, g_hbRotX, 0.f, HbSolidYaw(h, g_hbRot, true), 1.f, 1.f, 1.f, bw, bh, 1.f, 2);
    }
    else N(N_DRAW_SCALEFORM_MOVIE_3D, g_hb.movie, c.x, c.y, c.z, g_hbRotX, 0.f, h + g_hbRot, 1.f, 1.f, 1.f, bw, bh, 1.f, 2);
    if (!g_hbMapFirst) drawMap();
    if (!g_hb.using_) { HbShowMe(); return; }
    N(N_DISABLE_ALL_CONTROL_ACTIONS, 0);
    int setups = HEISTS[g_hb.heist].stages - 1;
    bool fin = HbFinale(g_hb.heist);
    auto pressed = [](int c) { return NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 2, c)); };
    if (!fin && (pressed(174) || pressed(175))) {   // left / right: the setups
        g_hb.sel = (g_hb.sel + (pressed(175) ? 1 : setups - 1)) % std::max(1, setups);
        HbBegin("HIGHLIGHT_ITEM"); HbInt(g_hb.sel); HbInt(0); HbBool(false); HbBool(false); HbEnd();
        g_hb.built = -1;   // the highlight flag of the slots
        Sound("Highlight_Move", "DLC_HEIST_PLANNING_BOARD_SOUNDS");
    }
    if (pressed(172) || pressed(173)) {   // up / down: another heist
        g_hb.heist = (g_hb.heist + (pressed(173) ? 1 : NHEISTS - 1)) % NHEISTS;
        g_hb.sel = HbFinale(g_hb.heist) ? 0 : g_heistStage[g_hb.heist];
        Sound("Paper_Shuffle", "DLC_HEIST_PLANNING_BOARD_SOUNDS");
    }
    if (pressed(201)) {   // Enter: the heist's lobby (its next setup, or the finale)
        std::string why;
        int hh = g_hb.heist;
        if (!HeistUnlocked(hh, why)) { Notify("~r~" + why); Sound("Highlight_Error", "DLC_HEIST_PLANNING_BOARD_SOUNDS"); }
        else if (!fin && g_hb.sel != g_heistStage[hh]) {
            Notify(g_hb.sel < g_heistStage[hh] ? "That setup is done." : "Finish the earlier setups first.");
            Sound("Highlight_Error", "DLC_HEIST_PLANNING_BOARD_SOUNDS");
        } else { HbClose(); OpenHeistCorona(hh); return; }
    }
    if (pressed(202)) { HbClose(); return; }
    std::string help = fin ? "~INPUT_FRONTEND_ACCEPT~ Launch Heist   ~INPUT_FRONTEND_UP~ ~INPUT_FRONTEND_DOWN~ Heist   ~INPUT_FRONTEND_CANCEL~ Back"
                           : "~INPUT_FRONTEND_ACCEPT~ Start Setup   ~INPUT_FRONTEND_LEFT~ ~INPUT_FRONTEND_RIGHT~ Setup   ~INPUT_FRONTEND_UP~ ~INPUT_FRONTEND_DOWN~ Heist   ~INPUT_FRONTEND_CANCEL~ Back";
    Help(help);
}
