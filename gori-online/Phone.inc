// ---------------------------------------------------------------- The phone, GTA Online's home screen
// The phone's home screen is GTA's own (cellphone_flashhand) and draws the app table Global_9509
// (35 apps x 15 script vars: label, position, script name, its hash, icon, page, flags). Story mode
// fills it with the story apps (cellphone_controller func_144, Global_80337 false); while YOU plays we
// write GTA Online's table (the same function's Online branch) and put the story one back afterwards.
// Online-only apps that cannot run in story mode open Gori Online's equivalents.
static uint64_t *(*shv_getGlobalPtr)(int) = nullptr;
static uint64_t *GlobalPtr(int id) {
    if (!shv_getGlobalPtr) {
        HMODULE h = GetModuleHandleA("ScriptHookV.dll");
        if (h) shv_getGlobalPtr = reinterpret_cast<uint64_t *(*)(int)>(GetProcAddress(h, "?getGlobalPtr@@YAPEA_KH@Z"));
    }
    return shv_getGlobalPtr ? shv_getGlobalPtr(id) : nullptr;
}
// arrays carry their size first: Global_9509[i] is var 9510 + i * 15 (checked in game: phonescan / globals)
static const int PH_APPS = 35, PH_STRIDE = 15, PH_TABLE = 9510;
static const int PH_POS_APP = 10051, PH_POS_DRAWN = 10088;   // flashhand: Global_10050[pos] = app slot, Global_10087[pos] = drawn
struct PhoneApp { int slot; const char *label; int pos; const char *script; int icon; int page; };
// cellphone_controller func_144, Online branch (no Darnell, no special job apps, no CEO yet: SecuroServ)
static const PhoneApp PHONE_ONLINE[] = {
    {7, "CELL_5", 0, "appMPEmail", 4, 1}, {1, "CELL_1", 1, "appTextMessage", 2, 1}, {0, "CELL_0", 2, "appContacts", 5, 1},
    {21, "CELL_37", 3, "AppJIPMP", 14, 1}, {14, "CELL_29", 4, "appMPJobListNEW", 12, 1}, {10, "CELL_16", 5, "appSettings", 24, 1},
    {3, "CELL_7", 6, "appCamera", 1, 1}, {2, "CELL_2", 7, "appInternet", 6, 1}, {24, "CELL_BOSSAGE", 8, "appMPBossAgency", 57, 1},
};
static std::vector<uint64_t> g_phoneStory;   // the story table, saved while ours is in
static bool g_phoneOnline = false;
static void PhoneLabel(uint64_t *at, const char *s) {   // a TEXT_LABEL_31: 4 script vars
    char buf[32] = {};
    std::strncpy(buf, s, 31);
    std::memcpy(at, buf, 32);
}
static bool PhoneTableValid(uint64_t *t) {   // Settings sits in slot 10 in both tables: the global index matches this build
    char lab[33] = {}, scr[33] = {};
    std::memcpy(lab, t + 10 * PH_STRIDE, 32);
    std::memcpy(scr, t + 10 * PH_STRIDE + 5, 32);
    return !std::strcmp(lab, "CELL_16") && !_stricmp(scr, "appSettings");
}
static bool g_phoneBad = false;
static void PhoneWriteOnline() {
    uint64_t *t = GlobalPtr(PH_TABLE);
    if (!t || g_phoneBad) return;
    if (!PhoneTableValid(t)) {
        char lab[33] = {}; std::memcpy(lab, t + 10 * PH_STRIDE, 32);
        Log("phone: Global_%d does not look like the phone's app table here (slot 10 label '%.32s'): left alone", PH_TABLE, lab);
        g_phoneBad = true;
        return;
    }
    if (g_phoneStory.empty()) g_phoneStory.assign(t, t + PH_APPS * PH_STRIDE);
    for (int i = 0; i < PH_APPS; ++i) t[i * PH_STRIDE + 4] = (uint64_t)(int64_t)-99;   // every app hidden
    for (const PhoneApp &a : PHONE_ONLINE) {
        uint64_t *e = t + a.slot * PH_STRIDE;
        PhoneLabel(e + 0, a.label);
        e[4] = (uint64_t)(int64_t)a.pos;
        PhoneLabel(e + 5, a.script);
        e[9] = (uint64_t)(int64_t)(int32_t)Joaat(a.script);
        e[10] = (uint64_t)(int64_t)a.icon;
        e[11] = (uint64_t)(int64_t)a.page;
        e[12] = 1; e[13] = 0; e[14] = 0;
    }
}
static void PhoneRestoreStory() {
    uint64_t *t = GlobalPtr(PH_TABLE);
    if (t && g_phoneStory.size() == (size_t)(PH_APPS * PH_STRIDE)) std::memcpy(t, g_phoneStory.data(), g_phoneStory.size() * sizeof(uint64_t));
    g_phoneStory.clear();
}
static bool PhoneTableIsOnline() {
    uint64_t *t = GlobalPtr(PH_TABLE);
    if (!t) return false;
    char buf[33] = {};
    std::memcpy(buf, t + 14 * PH_STRIDE + 5, 32);   // the Job List slot runs appMPJobListNEW only in ours
    return !std::strcmp(buf, "appMPJobListNEW") && (int64_t)t[14 * PH_STRIDE + 4] == 4;
}
// an Online app was launched: what Gori Online does instead
// close the app and put the phone away: state 3 as cellphone_controller's func_98 does (left at 6/10 the
// controller keeps the phone "up": it eats the back button and never opens the phone again)
static void PhoneAway(const char *script) {
    N(N_TERMINATE_ALL_SCRIPTS_WITH_THIS_NAME, script);
    N(N_DESTROY_MOBILE_PHONE);
    if (uint64_t *st = GlobalPtr(21650)) *st = 3;   // Global_21649.f_1
}
static bool PhoneIntercept(const char *script, std::function<void()> act) {
    uint32_t h = Joaat(script);
    if (N<int>(N_GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH, h) <= 0) return false;
    PhoneAway(script);
    act();
    Log("phone: %s -> Gori Online", script);
    return true;
}
// ---- contacts: GTA Online's (the charsheet Global_2350, 29 vars each; owner 3 = multiplayer):
// f_12[owner] = in that phone book, f_24[owner] = name known (else appContacts lists the bare number)
static const int PH_CHARS = 2350, PH_CHAR_STRIDE = 29, PH_OWNER = 21649, PH_STATE = 21650, PH_CALLEE = 8817;   // Global_2349[] elements, Global_21649 (.f_1), Global_8817
struct PhoneContact { int id; MenuId menu; const char *says; };
static std::vector<PhoneContact> &PhoneContacts();
static std::map<int, std::pair<uint64_t, uint64_t>> g_phoneKnownSaved;   // contact -> its story values
static char g_lesterPic[16] = {};   // Lester's story picture label, while YOU plays
static void PhoneContactsOnline(bool on) {
    for (const PhoneContact &c : PhoneContacts()) {
        uint64_t *e = GlobalPtr(PH_CHARS + c.id * PH_CHAR_STRIDE);
        if (!e) return;
        uint64_t *own = GlobalPtr(PH_OWNER);
        int owner = own ? (int)(int64_t)*own : 3;
        if (owner < 0 || owner > 3) owner = 3;
        uint64_t &known = e[13 + owner], &named = e[25 + owner];
        if (on) {
            if (!g_phoneKnownSaved.count(c.id)) {
                g_phoneKnownSaved[c.id] = {known, named};
                if (c.id == 12) {   // Lester: CHAR_LESTER stays a silhouette in this phone; Online's own picture of him does not
                    std::memcpy(g_lesterPic, e + 7, 16);
                    char pic[16] = "CELL_E_311"; std::memcpy(e + 7, pic, 16);
                }
                // the picture (f_7) is a text label (CELL_311) naming the texture dictionary: stream that,
                // the story only loads its own cast's
                char lab[17] = {}; std::memcpy(lab, e + 7, 16);
                std::string txd = Label(lab);
                if (!txd.empty()) N(N_REQUEST_STREAMED_TEXTURE_DICT, txd.c_str(), 0);
                Log("phone: contact %d picture %s -> %s", c.id, lab, txd.c_str());
            }
            known = 1; named = 1;
        } else if (g_phoneKnownSaved.count(c.id)) {
            known = g_phoneKnownSaved[c.id].first; named = g_phoneKnownSaved[c.id].second;
            if (c.id == 12 && g_lesterPic[0]) std::memcpy(e + 7, g_lesterPic, 16);
        }
    }
    if (!on) g_phoneKnownSaved.clear();
}
static int g_phoneCallAt = 0, g_phoneCallee = -1;
static void OpenMenuAt(MenuId menu) { g_menuOpen = true; g_stack.clear(); Open(M_MAIN); if (menu != M_MAIN) Open(menu); }
static std::vector<PhoneContact> &PhoneContacts() {
    static std::vector<PhoneContact> c = {
        // Simeon, Lamar, Ron, Martin, Gerald: their contact missions (ContactMissions.inc: menu 271 + the contact's index)
        {12, M_SERVICES, "Lester: \"What do you need?\""}, {18, (MenuId)272, "Simeon: \"My friend! I have work for you.\""},
        {19, (MenuId)273, "Lamar: \"Wassup, homie?\""}, {20, (MenuId)275, "Ron: \"They're watching us...\""}, {31, (MenuId)274, "Martin: \"Talk.\""},
        {86, (MenuId)271, "Gerald: \"Ain't nobody got time for small talk.\""}, {84, M_INV, "Brucie: \"Bull Shark, bro!\""},
        {89, M_VEH, "Mechanic: \"Which one do you want brought round?\""}, {41, M_VEH, "Pegasus: \"Pegasus Lifestyle Management.\""},
        {85, M_SERVICES, "Merryweather Security: \"State your request.\""}, {91, M_VEH, "Mors Mutual Insurance: \"How can we help?\""},
        {76, M_HEISTS, "Agent 14: \"Go ahead.\""}, {74, M_BIZ, "Assistant: \"Good to hear from you, boss.\""},
        {151, M_BIZ, "Tony: \"Speak to me, partner.\""}, {157, M_MAIN, "Ms. Baker: \"The Diamond Casino & Resort.\""},
    };
    return c;
}
static void PhoneCallTick() {
    if (N<int>(N_GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH, Joaat("appContacts")) <= 0) { g_phoneCallAt = 0; return; }
    uint64_t *st = GlobalPtr(PH_STATE), *callee = GlobalPtr(PH_CALLEE);
    if (!st || !callee) return;
    if ((int64_t)*st != 10) { g_phoneCallAt = 0; return; }   // 10: dialing / on a call
    int who = (int)(int64_t)*callee;
    const PhoneContact *c = nullptr;
    for (const PhoneContact &x : PhoneContacts()) if (x.id == who) c = &x;
    if (!c) return;
    if (!g_phoneCallAt || g_phoneCallee != who) { g_phoneCallAt = Now(); g_phoneCallee = who; return; }
    if (Now() - g_phoneCallAt < 1800) return;   // let it ring
    g_phoneCallAt = 0;
    PhoneAway("appContacts");
    Notify(c->says);
    OpenMenuAt(c->menu);
    Log("phone: called contact %d", who);
}
static void PhoneTick() {
    bool you = YouActive();
    if (you) {
        // keep GTA Online's table in (the story controller rebuilds its own after a character switch)
        // every frame: the story controller moves apps around (Job List, spare slots) while the phone is up
        if (!g_phoneBad) { PhoneWriteOnline(); if (!g_phoneBad) { if (!g_phoneOnline) Log("phone: GTA Online home screen"); g_phoneOnline = true; } }
        PhoneContactsOnline(true);
        // the home screen was drawn from the story table (the controller rebuilds it as the phone opens):
        // draw GTA Online's apps the way flashhand does (SET_DATA_SLOT 1, pos, icon, 0, label) and point
        // its position -> app map (what a selection launches) at ours
        if (!g_phoneBad) {
            uint64_t *posApp = GlobalPtr(PH_POS_APP), *drawn = GlobalPtr(PH_POS_DRAWN), *movie = GlobalPtr(21630), *sel = GlobalPtr(21629);
            bool stale = false;
            uint64_t *pst = GlobalPtr(PH_STATE);
            bool home = !pst || (int64_t)*pst < 6;   // 6+: an app is up (redrawing the home screen would hide it)
            if (posApp && drawn && movie && home)
                for (const PhoneApp &a : PHONE_ONLINE) if (drawn[a.pos] && (int64_t)posApp[a.pos] != a.slot) stale = true;
            if (stale) {
                int mv = (int)(int64_t)*movie;
                for (const PhoneApp &a : PHONE_ONLINE) {
                    posApp[a.pos] = (uint64_t)(int64_t)a.slot;
                    drawn[a.pos] = 1;
                    N(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, "SET_DATA_SLOT");
                    N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, 1);
                    N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, a.pos);
                    N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, a.icon);
                    N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, 0);
                    N(N_BEGIN_TEXT_COMMAND_SCALEFORM_STRING, a.label);
                    N(N_END_TEXT_COMMAND_SCALEFORM_STRING);
                    N(N_END_SCALEFORM_MOVIE_METHOD);
                }
                N(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, "DISPLAY_VIEW");
                N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, 1);
                N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, sel ? (int)(int64_t)*sel : 4);
                N(N_END_SCALEFORM_MOVIE_METHOD);
                Log("phone: home screen redrawn with GTA Online's apps");
            }
        }
        PhoneCallTick();
        PhoneIntercept("appMPJobListNEW", [] { OpenMenuAt(M_HEISTS); });
        PhoneIntercept("AppJIPMP", [] { OpenMenuAt(M_HEISTS); });
        PhoneIntercept("appMPBossAgency", [] { OpenMenuAt(M_BIZ); });
    } else if (g_phoneOnline) {
        PhoneRestoreStory();
        PhoneContactsOnline(false);
        g_phoneOnline = false;
        Log("phone: story home screen back");
    }
}
