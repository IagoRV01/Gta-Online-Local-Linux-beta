// ---------------------------------------------------------------- GTA Online's contact missions
// The contacts' jobs (Gerald, Simeon, Lamar, Martin, Ron, Trevor), with their GTA Online names, recreated with
// Gori's mission steps from a few templates (eliminate, steal a vehicle, grab a package, destroy, rescue).
// Like Online they unlock by rank and pay more the higher the rank needed; every one is a co-op job.
// Interaction Menu > Contact Missions, or call the contact.
enum CmKind { CM_ELIM, CM_STEAL, CM_PACKAGE, CM_DESTROY, CM_RESCUE };
struct ContactMission {
    const char *name; int kind, rank, pay;
    float x, y, z;                 // the target
    const char *enemy, *weapon;    // the opposition
    const char *vehicle;           // CM_STEAL: what to steal
    const char *text;              // the briefing
};
struct ContactDef { const char *name; int phoneId; const char *greeting; const ContactMission *missions; int count; };
static const ContactMission CM_GERALD[] = {
    {"Pier Pressure", CM_PACKAGE, 1, 6000, -1820.f, -1210.f, 13.f, "g_m_y_mexgoon_02", "weapon_pistol", nullptr, "Gerald: \"Vagos got my stash under the pier. Go get it.\""},
    {"Death Metal", CM_ELIM, 3, 7000, 1290.f, -1700.f, 54.f, "g_m_y_ballaeast_01", "weapon_microsmg", nullptr, "Gerald: \"Some Ballas think they run El Burro. Correct them.\""},
    {"Chasing the Dragon", CM_STEAL, 5, 8000, 470.f, -1310.f, 29.f, "g_m_y_mexgoon_01", "weapon_pistol", "speedo", "Gerald: \"A van full of product. It's mine now.\""},
    {"Rich Men in Richman", CM_PACKAGE, 8, 9000, -1530.f, 110.f, 56.f, "s_m_m_highsec_01", "weapon_carbinerifle", nullptr, "Gerald: \"A rich boy owes me. Take it from his mansion.\""},
    {"Hot Property", CM_DESTROY, 12, 10000, 1140.f, -1290.f, 34.f, "g_m_y_mexgoon_03", "weapon_assaultrifle", nullptr, "Gerald: \"Burn the Vagos' lab. Leave nothing.\""},
    {"Out of Court Settlement", CM_RESCUE, 16, 12000, 1855.f, 3690.f, 34.f, "s_m_y_sheriff_01", "weapon_pistol", nullptr, "Gerald: \"My cousin's in Sandy Shores lockup. Bust him out.\""},
};
static const ContactMission CM_SIMEON[] = {
    {"Blow Up", CM_DESTROY, 1, 5000, -45.f, -1100.f, 26.f, "s_m_m_security_01", "weapon_pistol", nullptr, "Simeon: \"Some customers don't pay. Make their cars... disappear.\""},
    {"Repo - Under the Bridge", CM_STEAL, 3, 7000, 640.f, -440.f, 24.f, "g_m_y_lost_01", "weapon_sawnoffshotgun", "baller2", "Simeon: \"A Baller under the bridge. Repossess it.\""},
    {"Repo - Lost Boys", CM_STEAL, 6, 8000, 980.f, -110.f, 74.f, "g_m_y_lost_02", "weapon_pumpshotgun", "daemon", "Simeon: \"The Lost took one of my bikes. Bring it back.\""},
    {"Blow Up II", CM_DESTROY, 9, 9000, -1150.f, -1520.f, 4.f, "g_m_y_mexgoon_02", "weapon_microsmg", nullptr, "Simeon: \"Another bad customer. Vespucci this time.\""},
    {"Repo - Ticket to Ride", CM_STEAL, 13, 11000, -1040.f, -2730.f, 20.f, "s_m_m_security_01", "weapon_carbinerifle", "luxor", "Simeon: \"A jet at the airport is overdue. Very overdue.\""},
    {"Blow Up III", CM_DESTROY, 18, 13000, 1220.f, -3000.f, 5.9f, "s_m_y_dockwork_01", "weapon_assaultrifle", nullptr, "Simeon: \"A whole shipment of lemons. Blow them up.\""},
};
static const ContactMission CM_LAMAR[] = {
    {"Lost My Mind", CM_ELIM, 1, 5500, 975.f, -120.f, 74.f, "g_m_y_lost_01", "weapon_sawnoffshotgun", nullptr, "Lamar: \"The Lost disrespected the hood. Pay 'em a visit, homie.\""},
    {"Ballas to the Wall", CM_ELIM, 4, 7000, 100.f, -1940.f, 20.f, "g_m_y_ballaorig_01", "weapon_pistol", nullptr, "Lamar: \"Ballas on Grove Street. Not on my watch.\""},
    {"Gentrification", CM_PACKAGE, 7, 8500, -50.f, -1450.f, 32.f, "g_m_y_famca_01", "weapon_microsmg", nullptr, "Lamar: \"Some fool's sitting on our money in Strawberry.\""},
    {"Rooftop Rumble", CM_PACKAGE, 10, 9500, -150.f, -590.f, 167.f, "s_m_m_highsec_02", "weapon_carbinerifle", nullptr, "Lamar: \"A deal on a rooftop downtown. Take the merch.\""},
    {"Bust Out", CM_RESCUE, 14, 11000, 400.f, -1630.f, 29.f, "s_m_y_cop_01", "weapon_pistol", nullptr, "Lamar: \"They got my boy at the impound. Bust him out!\""},
};
static const ContactMission CM_MARTIN[] = {
    {"Base Invaders", CM_STEAL, 15, 15000, -2140.f, 3240.f, 32.8f, "s_m_y_marine_01", "weapon_carbinerifle", "crusader", "Martin: \"Zancudo has something I want. Bring it to me.\""},
    {"Cleaning the Cat House", CM_ELIM, 10, 12000, -1840.f, 4400.f, 48.f, "a_m_m_hillbilly_01", "weapon_pumpshotgun", nullptr, "Martin: \"Some hillbillies are stepping on my business.\""},
    {"Diamonds are for Never", CM_PACKAGE, 12, 13000, -630.f, -230.f, 38.f, "s_m_m_security_01", "weapon_pistol", nullptr, "Martin: \"A courier with diamonds in Rockford Hills. Relieve him.\""},
    {"Lost Contact", CM_RESCUE, 18, 16000, 2350.f, 3060.f, 48.f, "g_m_y_lost_01", "weapon_assaultrifle", nullptr, "Martin: \"My man got grabbed by the Lost. Get him back.\""},
    {"Pier Pressure", CM_ELIM, 20, 18000, -1600.f, -1050.f, 13.f, "s_m_m_highsec_01", "weapon_assaultrifle", nullptr, "Martin: \"A rival's men on the pier. End them.\""},
};
static const ContactMission CM_RON[] = {
    {"Crank Up the Volume", CM_DESTROY, 5, 7000, 1700.f, 3290.f, 41.f, "g_m_y_lost_01", "weapon_sawnoffshotgun", nullptr, "Ron: \"The Lost's radio mast is mind-controlling Sandy Shores. Destroy it!\""},
    {"Daddy's Little Girl", CM_RESCUE, 8, 9000, 70.f, 3730.f, 39.7f, "g_m_y_lost_02", "weapon_pumpshotgun", nullptr, "Ron: \"A friend's daughter's in Stab City. Get her out.\""},
    {"Docks to Stock", CM_STEAL, 11, 10000, 1180.f, -3110.f, 5.9f, "s_m_y_dockwork_01", "weapon_microsmg", "mule", "Ron: \"A truck at the docks. Trevor wants it. Don't ask.\""},
    {"Trash Talk", CM_DESTROY, 15, 12000, -560.f, 5280.f, 70.f, "a_m_m_hillbilly_02", "weapon_pumpshotgun", nullptr, "Ron: \"Rednecks dumping chemicals at the sawmill. Shut it down.\""},
};
static const ContactMission CM_TREVOR[] = {
    {"Crystal Clear Out", CM_DESTROY, 6, 9000, 1390.f, 3600.f, 35.f, "g_m_y_mexgoon_01", "weapon_assaultrifle", nullptr, "Trevor: \"The Aztecas are cooking near MY lab. Burn theirs.\""},
    {"Meth'd Up", CM_STEAL, 9, 10000, 1960.f, 3820.f, 32.f, "g_m_y_lost_01", "weapon_sawnoffshotgun", "journey", "Trevor: \"Someone stole my mobile lab. Get it back. NOW.\""},
    {"Dry Docking", CM_ELIM, 13, 12000, 1300.f, 4310.f, 37.f, "g_m_y_mexgoon_02", "weapon_microsmg", nullptr, "Trevor: \"Rival dealers at Alamo Sea. Make them fish food.\""},
    {"Rally", CM_STEAL, 17, 13000, 2700.f, 4280.f, 45.f, "a_m_m_hillbilly_01", "weapon_pistol", "sandking", "Trevor: \"A Sandking. Fast. Bring it. Now go!\""},
    {"Trevor's Trail", CM_PACKAGE, 22, 15000, -2200.f, 4250.f, 46.f, "g_m_y_lost_02", "weapon_assaultrifle", nullptr, "Trevor: \"Something of mine is out in North Chumash. Fetch.\""},
};
static const ContactDef CONTACTS[] = {
    {"Gerald", 86, "Gerald: \"Ain't nobody got time for small talk.\"", CM_GERALD, 6},
    {"Simeon", 18, "Simeon: \"My friend! I have work for you.\"", CM_SIMEON, 6},
    {"Lamar", 19, "Lamar: \"Wassup, homie?\"", CM_LAMAR, 5},
    {"Martin", 31, "Martin: \"Talk.\"", CM_MARTIN, 5},
    {"Ron", 20, "Ron: \"They're watching us...\"", CM_RON, 4},
    {"Trevor", -1, "Trevor: \"What.\"", CM_TREVOR, 5},
};
static const int NCONTACTS = (int)(sizeof CONTACTS / sizeof *CONTACTS);
enum { CMM_LIST = 270, CMM_CONTACT = 271 };   // CMM_CONTACT + contact index

static void StartContactMission(int c, int i, int diff = 1) {   // diff: 0 easy, 1 normal, 2 hard (the lobby's setting)
    const ContactMission &cm = CONTACTS[c].missions[i];
    Vec3 t = V3(cm.x, cm.y, cm.z);
    Vec3 back = PickSpot(DROPS);
    Mission m;
    m.name = cm.name;
    for (auto &ch : m.name) ch = (char)toupper((unsigned char)ch);
    m.timeLimitMs = 25 * 60000;
    int n = std::max(2, (5 + cm.rank / 4) * (diff == 0 ? 6 : diff == 2 ? 14 : 10) / 10);
    switch (cm.kind) {
    case CM_ELIM:
        m.steps.push_back(Enemies(Goto("Go to the ~y~target area~s~.", t, 25.f), n + 2, cm.enemy, cm.weapon));
        m.steps.back().snapFoot = true;
        { Step e = Escape("Lose the ~r~cops~s~."); e.wanted = 2; m.steps.push_back(e); }
        break;
    case CM_STEAL: {
        Step s = Steal(std::string("Steal the ~g~vehicle~s~."), t, 0.f, cm.vehicle);
        m.steps.push_back(Enemies(s, n, cm.enemy, cm.weapon));
        Step d = Deliver("Deliver it to the ~y~drop-off~s~.", back);
        d.chasers = 1 + cm.rank / 10;
        m.steps.push_back(d);
        break;
    }
    case CM_PACKAGE: {
        Step p = Pickup("Grab the ~g~package~s~.", t, 1, "prop_box_wood02a");
        p.snapFoot = true;
        m.steps.push_back(Enemies(p, n, cm.enemy, cm.weapon));
        Step g = Goto("Deliver it to the ~y~drop-off~s~.", back, 6.f);
        g.wanted = cm.rank >= 8 ? 2 : 0;
        m.steps.push_back(g);
        break;
    }
    case CM_DESTROY: {
        m.steps.push_back(Enemies(Goto("Go to the ~y~target~s~.", t, 15.f), n, cm.enemy, cm.weapon));
        Step h = Hold("Plant the ~g~explosives~s~.", t, 12);
        h.snapFoot = true;
        h.onDone = [t] { N(N_ADD_EXPLOSION, t.x, t.y, t.z, 2, 5.f, 1, 0, 1.f, 0); };
        m.steps.push_back(h);
        Step e = Escape("Get away and lose the ~r~cops~s~."); e.wanted = 3;
        m.steps.push_back(e);
        break;
    }
    case CM_RESCUE: {
        m.steps.push_back(Enemies(Goto("Find the ~b~hostage~s~.", t, 10.f), n, cm.enemy, cm.weapon));
        m.steps.back().snapFoot = true;
        Step e = Escape("Get the hostage to safety.", t, 300.f);
        e.ally = "a_m_y_downtown_01"; e.wanted = 2;
        m.steps.push_back(e);
        m.steps.push_back(Goto("Drop the hostage at the ~y~safehouse~s~.", back, 6.f));
        break;
    }
    }
    int pay = cm.pay * (diff == 0 ? 8 : diff == 2 ? 13 : 10) / 10, rp = 500 + pay / 20;
    std::string name = cm.name, who = CONTACTS[c].name;
    m.coopPay = pay / 2;
    m.corona = true; m.resultCash = pay; m.resultRp = rp;
    m.onSuccess = [pay, rp] { AddCash(pay); GiveRP(rp, "contact mission"); };
    Notify(cm.text);
    StartMission(m);
}
// the lobby for a contact mission, like Online's: difficulty, then the invite page
static CoronaDef ContactCorona(int c, int i) {
    const ContactMission &cm = CONTACTS[c].missions[i];
    CoronaDef d;
    d.title = cm.name;
    std::string brief = cm.text;
    size_t q = brief.find('"');
    if (q != std::string::npos) brief = brief.substr(q + 1, brief.size() - q - 2);   // the contact's line, without "Name: "
    d.desc = std::string(CONTACTS[c].name) + ": " + brief + "  Rank " + std::to_string(cm.rank) + ", " + Money(cm.pay) + ". 1-4 players.";
    d.theme = 0; d.maxPlayers = 4; d.payout = cm.pay;
    d.at = V3(cm.x, cm.y, cm.z);
    d.opts.push_back({"Difficulty", {"Easy", "Normal", "Hard"}, 1, "Easy: fewer enemies, less pay. Hard: more of them, more pay."});
    d.onPlay = [c, i](const std::vector<int> &sel) { StartContactMission(c, i, sel.empty() ? 1 : sel[0]); };
    return d;
}
// the contact's "Request Job": a call, then an invite to one of their jobs at your rank (accept with E)
static int g_cmInviteC = -1, g_cmInviteI = -1, g_cmInviteUntil = 0;
static void ContactRequestJob(int c) {
    int rank = LevelOf(g_rp);
    std::vector<int> ok;
    for (int i = 0; i < CONTACTS[c].count; ++i) if (rank >= CONTACTS[c].missions[i].rank) ok.push_back(i);
    if (ok.empty()) { Notify(std::string(CONTACTS[c].name) + ": \"Come back when you've got some rank.\""); return; }
    g_cmInviteC = c; g_cmInviteI = ok[Rand((int)ok.size())]; g_cmInviteUntil = Now() + 45000;
    const char *pics[] = {"CHAR_MP_GERALD", "CHAR_SIMEON", "CHAR_LAMAR", "CHAR_MARTIN", "CHAR_RON", "CHAR_TREVOR"};
    const char *pic = c < 6 ? pics[c] : "CHAR_DEFAULT";
    N(N_REQUEST_STREAMED_TEXTURE_DICT, pic, 0);
    for (int k = 0; k < 20 && !NB(N<int>(N_HAS_STREAMED_TEXTURE_DICT_LOADED, pic)); ++k) Wait(25);
    N(N_BEGIN_TEXT_COMMAND_THEFEED_POST, "STRING");
    N(N_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME, (std::string("Invite to ~y~") + CONTACTS[c].missions[g_cmInviteI].name + "~s~.").c_str());
    N(N_END_TEXT_COMMAND_THEFEED_POST_MESSAGETEXT, pic, pic, 1, 1, CONTACTS[c].name, "Job Invite");
    N(N_END_TEXT_COMMAND_THEFEED_POST_TICKER, 0, 1);
    Sound("Text_Arrive_Tone", "Phone_SoundSet_Default");
}
static void ContactInviteTick() {
    if (g_cmInviteC < 0) return;
    if (Now() > g_cmInviteUntil || g_mActive || CoronaActive()) { g_cmInviteC = -1; return; }
    if (!g_menuOpen) Help("Press ~INPUT_CONTEXT~ to accept the job invite: ~y~" + std::string(CONTACTS[g_cmInviteC].missions[g_cmInviteI].name));
    if (!g_menuOpen && NB(N<int>(N_IS_CONTROL_JUST_PRESSED, 0, 51))) {
        int c = g_cmInviteC, i = g_cmInviteI;
        g_cmInviteC = -1;
        OpenCorona(ContactCorona(c, i));
    }
}
// the results board for a mission started from a lobby
static void ContactResults(bool passed, const std::string &why, const Mission &m) {
    std::vector<CoronaResultRow> rows;
    CoronaResultRow me; me.name = g_myName.empty() ? "You" : g_myName; me.rank = LevelOf(g_rp); me.kills = g_mKills; me.deaths = passed ? 0 : 1;
    me.rp = passed ? m.resultRp : 0; me.cash = passed ? (g_lastHeistPay >= 0 ? g_lastHeistPay : m.resultCash) : 0; me.me = true;
    g_lastHeistPay = -1;
    rows.push_back(me);
    for (auto &n : Split(g_coop.members, ',')) if (!n.empty()) { CoronaResultRow r; r.name = n; rows.push_back(r); }
    std::string title = m.name;
    CoronaResults(title, passed, why, rows);
    bool shared = rows.size() > 1;
    g_cor.after = [title, shared] { NextJobContact(title, shared, true); };   // then Online's vote on the next job, with the crew
}
static bool ContactMenu(const Frame &f, std::string &title, std::string &sub, std::vector<Item> &items) {
    int id = (int)f.id;
    if (id == CMM_LIST) {
        title = "Contact Missions"; sub = "CONTACTS";
        for (int c = 0; c < NCONTACTS; ++c) items.push_back({CONTACTS[c].name, std::to_string(CONTACTS[c].count) + " >", CONTACTS[c].greeting, [c] { Open((MenuId)(CMM_CONTACT + c)); }});
        return true;
    }
    if (id >= CMM_CONTACT && id < CMM_CONTACT + NCONTACTS) {
        int c = id - CMM_CONTACT;
        title = CONTACTS[c].name; sub = "CONTACT MISSIONS";
        int rank = LevelOf(g_rp);
        items.push_back({"Request Job", "", std::string("Ask ") + CONTACTS[c].name + " for a job at your rank: an invite arrives.", [c] { CloseMenu(); ContactRequestJob(c); }});
        for (int i = 0; i < CONTACTS[c].count; ++i) {
            const ContactMission &cm = CONTACTS[c].missions[i];
            bool ok = rank >= cm.rank;
            items.push_back({cm.name, ok ? Money(cm.pay) : "Rank " + std::to_string(cm.rank), cm.text,
                             ok ? std::function<void()>([c, i] { CloseMenu(); OpenCorona(ContactCorona(c, i)); }) : nullptr});
        }
        return true;
    }
    return false;
}
// the phone: calling one of these contacts opens his missions (Phone.inc's contact table points here)
static MenuId ContactMenuFor(int phoneId) {
    for (int c = 0; c < NCONTACTS; ++c) if (CONTACTS[c].phoneId == phoneId) return (MenuId)(CMM_CONTACT + c);
    return M_MAIN;
}
// wiring: the phone's job contacts, the results boards (host and members)
static void ContactsInit() {
    g_contactCall = [](int c) { ContactRequestJob(c); };
    g_missionEnd = [](bool passed, const std::string &why, const Mission &m) { ContactResults(passed, why, m); };
    g_coopMemberEnd = [](bool passed, const CoopState &s) {
        std::vector<CoronaResultRow> rows;
        CoronaResultRow h; h.name = s.host; rows.push_back(h);
        for (auto &n : Split(s.members, ',')) {
            if (n.empty()) continue;
            CoronaResultRow r; r.name = n == "you" ? (g_myName.empty() ? "You" : g_myName) : n;
            if (n == "you") { r.me = true; r.rank = LevelOf(g_rp); r.cash = passed ? s.payout : 0; }
            rows.push_back(r);
        }
        CoronaResults(g_coopTitle.empty() ? "Job" : g_coopTitle, passed, passed ? "" : s.host + "'s job failed.", rows);
        std::string t = g_coopTitle;
        g_cor.after = [t] { NextJobContact(t, true, false); };   // the host's vote on the next job (a contact mission's)
    };
}

