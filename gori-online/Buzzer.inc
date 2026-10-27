// ---------------------------------------------------------------- the apartment buzzer (GTA Online's)
// At the walk-in door of any apartment building where you have no home: Online's corona. Standing in it, E rings the
// buzzer: the other players of your session who own a home in the building answer (LocalOnline player messages,
// protocol v14: "aptq" to everyone, "apta" back), left / right picks one and E buzzes it ("buzz"). The owner, when at
// home, sees "<name> is at the door" and lets you in (E) or not ("buzzok" / "buzzno"); you walk into the same interior
// as a guest, so you meet there (the same freemode session). Owners who are out answer that they are not home.
struct BuzzHome { unsigned netId; int prop; std::string owner; };
static struct {
    int building = -1, askedAt = 0, choice = 0;      // the visitor: the building asked about, and the homes that answered
    std::vector<BuzzHome> homes;
    unsigned waitingFor = 0; int waitUntil = 0, waitProp = -1;
    unsigned visitor = 0; int visitorProp = -1, visitorUntil = 0; std::string visitorName;   // the owner: who is at the door
} g_buzz;
static std::vector<int> g_buzzBuildings;   // the buildings with homes in them (a walk-in door and an interior)

static bool BuzzSend(unsigned to, const char *kind, const std::string &text) {
    return LO().sendPlayerMessage && LO().sendPlayerMessage(to, kind, text.c_str());
}
static std::string BuzzMyName() {
    if (g_myName.empty() && LO().getPlayerName) { char b[64] = {0}; LO().getPlayerName(b, sizeof b); g_myName = b; }
    return g_myName.empty() ? "A player" : g_myName;
}
static void BuzzInitBuildings() {
    if (!g_buzzBuildings.empty()) return;
    std::set<int> seen;
    for (int i = 0; i < NPROPS; ++i)
        if (PROPS[i].ob >= 0 && PropInterior(i) && PROPS[i].type != T_GARAGE && seen.insert(PROPS[i].ob).second) g_buzzBuildings.push_back(PROPS[i].ob);
}
static bool BuzzIOwnIn(int ob) {
    for (auto &o : g_props) { int i = FindProp(o); if (i >= 0 && PROPS[i].ob == ob) return true; }
    return false;
}
// the messages: asked, answered, buzzed, let in
static void BuzzMessages() {
    if (!LO().takePlayerMessages) return;
    static char buf[8192];
    int n = LO().takePlayerMessages(buf, sizeof buf);
    if (n <= 0) return;
    for (const std::string &line : Split(std::string(buf, std::min(n, (int)sizeof buf - 1)), '\n')) {
        std::vector<std::string> f = Split(line, '\t');
        if (f.size() < 3) continue;
        unsigned from = (unsigned)std::strtoul(f[0].c_str(), nullptr, 10);
        const std::string &kind = f[1], &text = f[2];
        std::vector<std::string> a = Split(text, '|');
        if (kind == "aptq") {   // does anyone live in building <ob>? our homes there answer
            int ob = std::atoi(text.c_str());
            for (auto &o : g_props) {
                int i = FindProp(o);
                if (i >= 0 && PROPS[i].ob == ob && PropInterior(i)) BuzzSend(from, "apta", std::to_string(i) + "|" + BuzzMyName());
            }
        } else if (kind == "apta" && a.size() >= 2 && g_buzz.building >= 0) {
            int i = std::atoi(a[0].c_str());
            if (i >= 0 && i < NPROPS && PROPS[i].ob == g_buzz.building) g_buzz.homes.push_back({from, i, a[1]});
        } else if (kind == "buzz" && a.size() >= 2) {   // someone at the door of one of our homes
            int i = std::atoi(a[0].c_str());
            if (i < 0 || i >= NPROPS || !Owns(PROPS[i].name)) continue;
            if (g_inside != i || InActivity()) { BuzzSend(from, "buzzno", "not home"); continue; }
            g_buzz.visitor = from; g_buzz.visitorProp = i; g_buzz.visitorName = a[1]; g_buzz.visitorUntil = Now() + 20000;
            Sound("DOOR_BUZZ", "MP_PLAYER_APARTMENT");
            Notify("~b~" + a[1] + "~s~ is at the door of ~b~" + PROPS[i].name + "~s~.");
            Log("buzzer: %s buzzed %s", a[1].c_str(), PROPS[i].name);
        } else if (!kind.compare(0, 2, "nj")) {   // the next job vote's (NextJob.inc)
            NjMessage(from, kind, text);
        } else if ((kind == "buzzok" || kind == "buzzno") && from == g_buzz.waitingFor) {
            int i = g_buzz.waitProp;
            std::string owner;
            for (auto &h : g_buzz.homes) if (h.netId == from) owner = h.owner;
            g_buzz.waitingFor = 0; g_buzz.building = -1; g_buzz.homes.clear();
            if (kind == "buzzno") { Notify("~b~" + owner + "~s~ " + (text == "not home" ? "is not home." : "did not let you in.")); continue; }
            if (i >= 0 && i < NPROPS && !InAnyVehicle() && Freemode()) {
                Notify("~b~" + owner + "~s~ let you in.");
                Log("buzzer: %s let us into %s", owner.c_str(), PROPS[i].name);
                EnterProperty(i);
            }
        }
    }
}
// every frame (after the properties): the messages, the owner's answer, the visitor's corona and picker
static void BuzzTick() {
    BuzzMessages();
    // the owner: someone is at the door
    if (g_buzz.visitor) {
        if (Now() > g_buzz.visitorUntil || g_inside != g_buzz.visitorProp) { BuzzSend(g_buzz.visitor, "buzzno", "no answer"); g_buzz.visitor = 0; }
        else if (!g_menuOpen) {
            Help("~b~" + g_buzz.visitorName + "~s~ is at the door. ~INPUT_CONTEXT~ Let them in   ~INPUT_FRONTEND_CANCEL~ Ignore");
            if (JustPressed(51)) { BuzzSend(g_buzz.visitor, "buzzok", std::to_string(g_buzz.visitorProp)); Notify("You let ~b~" + g_buzz.visitorName + "~s~ in."); g_buzz.visitor = 0; }
            else if (JustPressed(202) || JustPressed(177)) { BuzzSend(g_buzz.visitor, "buzzno", "ignored"); g_buzz.visitor = 0; }
        }
    }
    // the visitor
    if (g_buzz.waitingFor && Now() > g_buzz.waitUntil) { Notify("No answer."); g_buzz.waitingFor = 0; g_buzz.building = -1; g_buzz.homes.clear(); }
    if (!YouActive() || !Freemode() || g_inside >= 0 || InAnyVehicle() || !LO().sendPlayerMessage) return;
    BuzzInitBuildings();
    Vec3 mp = Pos(Me());
    for (int ob : g_buzzBuildings) {
        if (BuzzIOwnIn(ob)) continue;   // your own building: its own corona (PropertiesTick)
        const OBuilding *b = BldAt(ob);
        if (!b) continue;
        for (int k = 0; k < b->ne; ++k) {
            const OEntrance &e = b->e[k];
            if (e.kind == 2) continue;   // the garage door
            float d = Dist(mp, V3(e.mx, e.my, e.mz));
            if (d > 40.f) continue;
            OnlineMarker(e);
            bool in = std::hypot(mp.x - e.mx, mp.y - e.my) < 1.1f && std::fabs(mp.z - e.mz) < 2.2f;
            if (!in) { if (g_buzz.building == ob && !g_buzz.waitingFor) { g_buzz.building = -1; g_buzz.homes.clear(); } continue; }
            if (g_menuOpen) return;
            if (g_buzz.waitingFor) { Help("Buzzing..."); return; }
            if (g_buzz.building != ob) {
                Help("Press ~INPUT_CONTEXT~ to use the buzzer.");
                if (JustPressed(51)) {
                    g_buzz.building = ob; g_buzz.homes.clear(); g_buzz.askedAt = Now(); g_buzz.choice = 0;
                    BuzzSend(0, "aptq", std::to_string(ob));
                    Sound("SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET");
                }
                return;
            }
            if (g_buzz.homes.empty()) {   // waiting for the answers
                if (Now() - g_buzz.askedAt < 1500) { Help("Buzzer..."); return; }
                Notify("Nobody in this session lives here.");
                g_buzz.building = -1;
                return;
            }
            int n = (int)g_buzz.homes.size();
            if (JustPressed(174)) g_buzz.choice = (g_buzz.choice + n - 1) % n;
            if (JustPressed(175)) g_buzz.choice = (g_buzz.choice + 1) % n;
            const BuzzHome &h = g_buzz.homes[g_buzz.choice % n];
            Help("Press ~INPUT_CONTEXT~ to buzz ~b~" + h.owner + "~s~ (" + PROPS[h.prop].name + ")." +
                 (n > 1 ? "~n~~INPUT_CELLPHONE_LEFT~ ~INPUT_CELLPHONE_RIGHT~ " + std::to_string(g_buzz.choice % n + 1) + "/" + std::to_string(n) : std::string()));
            if (JustPressed(51)) {
                BuzzSend(h.netId, "buzz", std::to_string(h.prop) + "|" + BuzzMyName());
                g_buzz.waitingFor = h.netId; g_buzz.waitProp = h.prop; g_buzz.waitUntil = Now() + 25000;
                Sound("DOOR_BUZZ", "MP_PLAYER_APARTMENT");
                Log("buzzer: buzzing %s (%s)", h.owner.c_str(), PROPS[h.prop].name);
            }
            return;
        }
    }
}
