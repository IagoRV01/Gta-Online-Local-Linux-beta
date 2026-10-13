// ---------------------------------------------------------------- Business computers (Rockstar's own scaleform apps)
// GTA Online's office / business computers are scaleform movies driven by a script (appsecuroserv, appbunkerbusiness...):
// the script feeds data with methods (ADD_WAREHOUSE, SET_STATS...), forwards the mouse / pad, and asks the movie
// GET_CURRENT_SELECTION after a click to know which button was pressed. The same is done here.

// a scaleform method argument from a test command: i:5 f:1.5 b:1 s:text_with_underscores l:LABEL
static void SfArg(const std::string &a) {
    std::string v = a.size() > 2 ? a.substr(2) : "";
    if (a.rfind("i:", 0) == 0) N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, std::atoi(v.c_str()));
    else if (a.rfind("f:", 0) == 0) N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, (float)std::atof(v.c_str()));
    else if (a.rfind("b:", 0) == 0) N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_BOOL, std::atoi(v.c_str()));
    else if (a.rfind("l:", 0) == 0) { N(N_BEGIN_TEXT_COMMAND_SCALEFORM_STRING, v.c_str()); N(N_END_TEXT_COMMAND_SCALEFORM_STRING); }
    else {
        if (a.rfind("s:", 0) != 0) v = a;
        for (char &c : v) if (c == '_') c = ' ';
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_TEXTURE_NAME_STRING, v.c_str());
    }
}

struct SfDebug { int movie = 0; std::string name; int selRv = 0; float lmx = -1, lmy = -1; } g_sfd;
static void SfDebugTick() {
    if (!g_sfd.movie) return;
    int mv = g_sfd.movie;
    N(N_DISABLE_ALL_CONTROL_ACTIONS, 0);
    N(N_HIDE_HUD_AND_RADAR_THIS_FRAME);
    N(N_SET_MOUSE_CURSOR_THIS_FRAME);
    float mx = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 239), my = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 240);
    if (mx != g_sfd.lmx || my != g_sfd.lmy) {
        N(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, "SET_MOUSE_INPUT"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, mx); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, my); N(N_END_SCALEFORM_MOVIE_METHOD);
        g_sfd.lmx = mx; g_sfd.lmy = my;
    }
    auto just = [](int c) { return NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 2, c)); };
    if (just(237) || just(201)) {
        N(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, "SET_INPUT_EVENT"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, just(237) ? 237 : 201); N(N_END_SCALEFORM_MOVIE_METHOD);
        N(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, "GET_CURRENT_SELECTION");
        g_sfd.selRv = N<int>(N_END_SCALEFORM_MOVIE_METHOD_RETURN_VALUE);
    }
    if (g_sfd.selRv && NB(N<int>(N_IS_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_READY, g_sfd.selRv))) {
        Log("sf %s: selection %d", g_sfd.name.c_str(), N<int>(N_GET_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_INT, g_sfd.selRv));
        g_sfd.selRv = 0;
    }
    N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, mv, 255, 255, 255, 255, 0);
}

// test commands: sf <MOVIE> | sfcall <METHOD> [args] | sfret <METHOD> [args] | sfoff
static bool SfDebugCommand(const std::vector<std::string> &a) {
    if (a[0] == "sf" && a.size() > 1) {
        if (g_sfd.movie) N(N_SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED, &g_sfd.movie);
        g_sfd = SfDebug();
        g_sfd.name = a[1];
        g_sfd.movie = N<int>(N_REQUEST_SCALEFORM_MOVIE, a[1].c_str());
        int t0 = Now();
        while (!NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, g_sfd.movie)) && Now() - t0 < 8000) Wait(0);
        Log("sf %s: handle %d loaded %d", a[1].c_str(), g_sfd.movie, N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, g_sfd.movie));
        return true;
    }
    if ((a[0] == "sfcall" || a[0] == "sfret") && a.size() > 1 && g_sfd.movie) {
        N(N_BEGIN_SCALEFORM_MOVIE_METHOD, g_sfd.movie, a[1].c_str());
        for (size_t i = 2; i < a.size(); ++i) SfArg(a[i]);
        if (a[0] == "sfcall") { N(N_END_SCALEFORM_MOVIE_METHOD); return true; }
        int rv = N<int>(N_END_SCALEFORM_MOVIE_METHOD_RETURN_VALUE);
        int t0 = Now();
        while (!NB(N<int>(N_IS_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_READY, rv)) && Now() - t0 < 2000) { SfDebugTick(); Wait(0); }
        Log("sf %s.%s -> %d", g_sfd.name.c_str(), a[1].c_str(), N<int>(N_GET_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_INT, rv));
        return true;
    }
    if (a[0] == "sfoff") { if (g_sfd.movie) N(N_SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED, &g_sfd.movie); g_sfd = SfDebug(); return true; }
    return false;
}
