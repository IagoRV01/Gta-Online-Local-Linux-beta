// ---------------------------------------------------------------- The Diamond Casino (GTA Online's interior, vw_casino_main)
// Chips bought at the cashier, Lucky Wheel (once per in-game day), the podium vehicle (changes every real week),
// roulette (American wheel), blackjack, three card poker, slot machines and Inside Track. Included from GoriOnline.cpp.

static const Vec3 CAS_DOOR = {926.9f, 0, 44.6f, 0, 81.1f, 0};          // walk-in marker next to the main doors
static const Vec3 CAS_CENTER = {1120.f, 0, 240.f, 0, -50.f, 0};
static const Vec3 CAS_CASHIER = {1111.9f, 0, 211.7f, 0, -49.4f, 0};
static const Vec3 CAS_WHEEL = {1111.052f, 0, 229.8492f, 0, -49.133f, 0};
static const Vec3 CAS_WHEELSPOT = {1111.05f, 0, 228.75f, 0, -49.6f, 0};
static const Vec3 CAS_PODIUM = {1100.f, 0, 220.f, 0, -49.75f, 0};
static const Vec3 CAS_TRACK = {1094.4f, 0, 262.0f, 0, -51.25f, 0};      // Inside Track seats in front of the big screen
struct CasTable { int game; float x, y, z, h; bool high; };             // game: 0 roulette, 1 blackjack, 2 three card poker
static const CasTable CAS_TABLES[] = {
    {0, 1133.13f, 262.16f, -52.04f, -45.f, false}, {0, 1129.59f, 267.21f, -52.04f, 135.f, false},
    {0, 1144.81f, 252.21f, -52.04f, 135.f, false}, {0, 1148.70f, 248.31f, -52.04f, -45.f, false},
    {0, 1144.43f, 268.33f, -52.84f, -135.f, true}, {0, 1150.36f, 262.72f, -52.84f, 45.f, true},
    {1, 1129.41f, 262.36f, -52.04f, -224.7f, false}, {1, 1144.43f, 247.34f, -52.04f, -224.7f, false},
    {1, 1148.84f, 269.75f, -52.84f, -134.7f, true}, {1, 1151.84f, 266.75f, -52.84f, 45.3f, true},
    {2, 1148.74f, 251.69f, -52.04f, -45.f, false}, {2, 1133.74f, 266.69f, -52.04f, -45.f, false},
    {2, 1146.33f, 261.25f, -52.84f, 45.f, true}, {2, 1143.34f, 264.25f, -52.84f, -135.f, true},
};
static const int NCASTABLES = sizeof(CAS_TABLES) / sizeof(CAS_TABLES[0]);
static int g_casObj[NCASTABLES] = {0}, g_casDealer[NCASTABLES] = {0};
static int g_casWheel = 0, g_casCar = 0, g_casBlip = 0, g_casTable = -1, g_casBet = 100;
static std::vector<int> g_casPeds;
static bool g_casSpawned = false;
static int g_casSlot = 0;            // slot machine object being played

static int Chips() { return g_chips; }
static bool PayChips(int n) {
    if (g_chips < n) { Notify("~r~Not enough chips.~s~ Buy more at the cashier."); Sound("ERROR", "HUD_FRONTEND_DEFAULT_SOUNDSET"); return false; }
    g_chips -= n; return true;
}
static void WinChips(int n) { g_chips += n; if (g_chips > 2000000000) g_chips = 2000000000; }
static long long EpochNow() { return (long long)std::time(nullptr); }

// the podium vehicle changes every real week (a super or sports car from the catalogue)
static std::string PodiumModel() {
    std::vector<const VehRow *> pool;
    for (const VehRow &r : VEH_ROWS) if ((r.cls == 6 || r.cls == 7) && !r.weapon && r.price >= 400000) pool.push_back(&r);
    if (pool.empty()) return "zentorno";
    unsigned week = (unsigned)(EpochNow() / 604800);
    return pool[(week * 2654435761u >> 7) % pool.size()]->model;
}
static void GiveVehicle(const std::string &model, const char *why) {
    std::string where = FreeGarage();
    if (where == "!") { Notify(std::string("~r~Your garages are full~s~, so the ") + why + " went to waste. Sell a vehicle first."); return; }
    GVeh g; g.prop = where; g.model = model; g.plate = "DIAMOND";
    g_garage.push_back(g);
    Save();
    Notify("~g~" + CarName(model) + "~s~ delivered to: ~b~" + GarageLabel(where) + "~s~.");
}

static int SpawnStaff(uint32_t m, float x, float y, float z, float h, const char *scenario) {
    if (!LoadModel(m)) return 0;
    int p = N<int>(N_CREATE_PED, 4, m, x, y, z, h, 0, 0);
    N(N_SET_PED_DEFAULT_COMPONENT_VARIATION, p);
    N(N_SET_BLOCKING_OF_NON_TEMPORARY_EVENTS, p, 1);
    N(N_SET_ENTITY_INVINCIBLE, p, 1);
    N(N_SET_PED_CAN_RAGDOLL, p, 0);
    if (scenario) N(N_TASK_START_SCENARIO_IN_PLACE, p, scenario, 0, 1);
    return p;
}
static Vec3 TableOffset(const CasTable &t, float lx, float ly) {
    float r = t.h * 3.14159265f / 180.f, c = std::cos(r), s = std::sin(r);
    return V3(t.x + lx * c - ly * s, t.y + lx * s + ly * c, t.z);
}
static void CasinoSpawn() {
    g_casSpawned = true;
    uint32_t wheel = Joaat("vw_prop_vw_luckywheel_02a");
    if (LoadModel(wheel)) {
        g_casWheel = N<int>(N_CREATE_OBJECT_NO_OFFSET, wheel, CAS_WHEEL.x, CAS_WHEEL.y, CAS_WHEEL.z, 0, 0, 0);
        N(N_SET_ENTITY_HEADING, g_casWheel, 0.f);
        N(N_FREEZE_ENTITY_POSITION, g_casWheel, 1);
    }
    uint32_t car = Joaat(PodiumModel().c_str());
    if (LoadModel(car)) {
        g_casCar = N<int>(N_CREATE_VEHICLE, car, CAS_PODIUM.x, CAS_PODIUM.y, CAS_PODIUM.z + 0.4f, 0.f, 0, 0, 0);
        N<int>(N_SET_VEHICLE_ON_GROUND_PROPERLY, g_casCar, 5.0f);
        N(N_FREEZE_ENTITY_POSITION, g_casCar, 1);
        N(N_SET_ENTITY_INVINCIBLE, g_casCar, 1);
        N(N_SET_VEHICLE_DOORS_LOCKED, g_casCar, 2);
        N(N_SET_VEHICLE_DIRT_LEVEL, g_casCar, 0.f);
    }
    uint32_t rou = Joaat("vw_prop_casino_roulette_01"), rouHi = Joaat("vw_prop_casino_roulette_01b");
    uint32_t dealers[2] = {Joaat("s_f_y_casino_01"), Joaat("s_m_y_casino_01")};
    for (int i = 0; i < NCASTABLES; ++i) {
        const CasTable &t = CAS_TABLES[i];
        if (t.game == 0) {
            uint32_t m = t.high ? rouHi : rou;
            if (LoadModel(m)) {
                g_casObj[i] = N<int>(N_CREATE_OBJECT_NO_OFFSET, m, t.x, t.y, t.z, 0, 0, 0);
                N(N_SET_ENTITY_HEADING, g_casObj[i], t.h);
                N(N_FREEZE_ENTITY_POSITION, g_casObj[i], 1);
            }
        }
        Vec3 d = TableOffset(t, 0.f, 0.95f);
        g_casDealer[i] = SpawnStaff(dealers[i % 2], d.x, d.y, d.z, t.h + 180.f, "WORLD_HUMAN_STAND_IMPATIENT_UPRIGHT");
    }
    // a few guests: bar, slots, Inside Track
    static const float guests[][4] = {{1116.0f, 236.0f, -49.8f, 120.f}, {1123.0f, 232.0f, -49.8f, 300.f}, {1105.0f, 232.5f, -49.8f, 90.f},
                                      {1131.0f, 250.0f, -51.0f, 200.f}, {1138.0f, 241.0f, -51.0f, 30.f}, {1092.0f, 258.0f, -51.2f, 45.f}};
    static const char *gm[] = {"a_f_y_bevhills_01", "a_m_y_bevhills_01", "a_f_m_bevhills_02", "a_m_m_bevhills_02", "a_f_y_vinewood_02", "a_m_y_vinewood_03"};
    static const char *gs[] = {"WORLD_HUMAN_STAND_MOBILE", "WORLD_HUMAN_DRINKING", "WORLD_HUMAN_HANG_OUT_STREET", "WORLD_HUMAN_STAND_IMPATIENT", "WORLD_HUMAN_DRINKING", "WORLD_HUMAN_SMOKING"};
    for (int i = 0; i < 6; ++i) { int p = SpawnStaff(Joaat(gm[i]), guests[i][0], guests[i][1], guests[i][2], guests[i][3], gs[i]); if (p) g_casPeds.push_back(p); }
    Log("casino: spawned wheel %d, podium %s %d, %d tables", g_casWheel, PodiumModel().c_str(), g_casCar, NCASTABLES);
}
static void CasinoDespawn() {
    g_casSpawned = false;
    DeleteEntity(g_casWheel);
    if (g_casCar) { N(N_SET_ENTITY_AS_MISSION_ENTITY, g_casCar, 1, 1); N(N_DELETE_VEHICLE, &g_casCar); g_casCar = 0; }
    for (int i = 0; i < NCASTABLES; ++i) { DeleteEntity(g_casObj[i]); DeleteEntity(g_casDealer[i]); }
    for (int &p : g_casPeds) DeleteEntity(p);
    g_casPeds.clear();
}

// ---- Lucky Wheel: 20 segments of 18 degrees, prizes in the order of the wheel's artwork
struct WheelPrize { int kind; int amount; const char *text; };          // kind: 0 cash, 1 chips, 2 RP, 3 vehicle, 4 mystery
static const WheelPrize WHEEL[20] = {
    {0, 15000, "$15,000"}, {2, 2500, "2,500 RP"}, {0, 20000, "$20,000"}, {1, 10000, "10,000 chips"}, {0, 25000, "$25,000"},
    {2, 5000, "5,000 RP"}, {0, 30000, "$30,000"}, {1, 15000, "15,000 chips"}, {0, 15000, "$15,000"}, {2, 7500, "7,500 RP"},
    {1, 20000, "20,000 chips"}, {4, 0, "Mystery prize"}, {0, 15000, "$15,000"}, {2, 10000, "10,000 RP"}, {0, 40000, "$40,000"},
    {1, 25000, "25,000 chips"}, {0, 15000, "$15,000"}, {2, 15000, "15,000 RP"}, {3, 0, "the Podium Vehicle"}, {0, 50000, "$50,000"},
};
static void SpinWheel() {
    long long now = EpochNow();
    if (now < g_wheelNext) {
        int mins = (int)((g_wheelNext - now + 59) / 60);
        Notify("The Lucky Wheel is ready again in ~b~" + std::to_string(mins) + " min~s~.");
        return;
    }
    int seg = Rand(20);
    if (WHEEL[seg].kind == 3 && Rand(3) != 0) seg = (seg + 1) % 20;     // the car is a bit rarer, like in Online
    g_wheelNext = now + 48 * 60;                                           // one in-game day
    Save();
    N(N_FREEZE_ENTITY_POSITION, Me(), 1);
    Sound("Spin_Start", "dlc_vw_casino_lucky_wheel_sounds");
    float target = 360.f * 5 + seg * 18.f, start = 0.f;
    int t0 = Now(), dur = 8000;
    for (;;) {
        float k = (Now() - t0) / (float)dur;
        if (k >= 1.f) break;
        float e = 1.f - (1.f - k) * (1.f - k) * (1.f - k);                // ease out
        if (Exists(g_casWheel)) N(N_SET_ENTITY_ROTATION, g_casWheel, 0.f, -(start + (target - start) * e), 0.f, 2, 1);
        DrawShard();
        Wait(0);
    }
    if (Exists(g_casWheel)) N(N_SET_ENTITY_ROTATION, g_casWheel, 0.f, -target, 0.f, 2, 1);
    N(N_FREEZE_ENTITY_POSITION, Me(), 0);
    WheelPrize p = WHEEL[seg];
    if (p.kind == 4) { static const WheelPrize myst[] = {{0, 100000, "$100,000"}, {1, 50000, "50,000 chips"}, {2, 25000, "25,000 RP"}}; p = myst[Rand(3)]; }
    Sound("Win", "dlc_vw_casino_lucky_wheel_sounds");
    Shard("~y~LUCKY WHEEL", std::string("You won ") + p.text + "!");
    if (p.kind == 0) AddCash(p.amount);
    else if (p.kind == 1) WinChips(p.amount);
    else if (p.kind == 2) GiveRP(p.amount, "Lucky Wheel");
    else if (p.kind == 3) GiveVehicle(PodiumModel(), "podium vehicle");
    Save();
}

// ---- cards
static int g_deck[52], g_deckPos = 52;
static void Shuffle() { for (int i = 0; i < 52; ++i) g_deck[i] = i; for (int i = 51; i > 0; --i) { int j = Rand(i + 1); std::swap(g_deck[i], g_deck[j]); } g_deckPos = 0; }
static int Draw() { if (g_deckPos >= 52) Shuffle(); return g_deck[g_deckPos++]; }
static int Rank(int c) { return c % 13 + 1; }       // 1 = ace ... 13 = king
static std::string CardName(int c) {
    static const char *r[] = {"", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
    static const char *s[] = {"~r~H~s~", "~r~D~s~", "C", "S"};
    return std::string(r[Rank(c)]) + s[c / 13];
}
static std::string HandName(const std::vector<int> &h, bool hideSecond = false) {
    std::string t;
    for (size_t i = 0; i < h.size(); ++i) t += (i ? " " : "") + (hideSecond && i == 1 ? std::string("??") : CardName(h[i]));
    return t;
}

// ---- blackjack (dealer stands on 17, blackjack pays 3:2, double on the first two cards)
struct BJ { bool active = false; int bet = 0; std::vector<int> me, dealer; std::string last; } g_bj;
static int BJValue(const std::vector<int> &h) {
    int v = 0, aces = 0;
    for (int c : h) { int r = Rank(c); if (r == 1) { aces++; v += 11; } else v += r > 10 ? 10 : r; }
    while (v > 21 && aces) { v -= 10; aces--; }
    return v;
}
static void BJFinish() {
    while (BJValue(g_bj.dealer) < 17) g_bj.dealer.push_back(Draw());
    int p = BJValue(g_bj.me), d = BJValue(g_bj.dealer);
    bool natural = g_bj.me.size() == 2 && p == 21, dnat = g_bj.dealer.size() == 2 && d == 21;
    int win = 0; std::string res;
    if (p > 21) res = "Bust";
    else if (natural && !dnat) { win = g_bj.bet + g_bj.bet * 3 / 2; res = "Blackjack!"; }
    else if (d > 21 || p > d) { win = g_bj.bet * 2; res = "You win"; }
    else if (p == d) { win = g_bj.bet; res = "Push"; }
    else res = "Dealer wins";
    WinChips(win);
    g_bj.last = res + " | You " + HandName(g_bj.me) + " (" + std::to_string(p) + ")  Dealer " + HandName(g_bj.dealer) + " (" + std::to_string(d) + ")";
    Notify("~b~Blackjack~s~: " + res + (win > g_bj.bet ? " ~g~+" + std::to_string(win - g_bj.bet) + " chips" : ""));
    Sound(win > g_bj.bet ? "WIN" : "LOSER", "HUD_AWARDS");
    g_bj.active = false;
    Save();
}
static void BJDeal(int bet) {
    if (!PayChips(bet)) return;
    if (g_deckPos > 40) Shuffle();
    g_bj = BJ(); g_bj.active = true; g_bj.bet = bet;
    g_bj.me = {Draw(), Draw()}; g_bj.dealer = {Draw(), Draw()};
    if (BJValue(g_bj.me) == 21 || BJValue(g_bj.dealer) == 21) BJFinish();
}

// ---- three card poker (ante + play, dealer qualifies with queen high, ante bonus for straights and better)
static int PokerScore(std::vector<int> h) {           // category * 10^6 + high cards
    std::vector<int> r; for (int c : h) { int k = Rank(c); r.push_back(k == 1 ? 14 : k); }
    std::sort(r.begin(), r.end(), std::greater<int>());
    bool flush = h[0] / 13 == h[1] / 13 && h[1] / 13 == h[2] / 13;
    bool straight = (r[0] - r[1] == 1 && r[1] - r[2] == 1) || (r[0] == 14 && r[1] == 3 && r[2] == 2);
    if (r[0] == 14 && r[1] == 3 && r[2] == 2) r = {3, 2, 1};
    int high = r[0] * 225 + r[1] * 15 + r[2];
    if (straight && flush) return 5000000 + high;
    if (r[0] == r[1] && r[1] == r[2]) return 4000000 + high;
    if (straight) return 3000000 + high;
    if (flush) return 2000000 + high;
    if (r[0] == r[1] || r[1] == r[2]) { int pr = r[1], k = r[0] == r[1] ? r[2] : r[0]; return 1000000 + pr * 15 + k; }
    return high;
}
static const char *PokerName(int s) { static const char *n[] = {"High card", "Pair", "Flush", "Straight", "Three of a kind", "Straight flush"}; return n[s / 1000000]; }
struct TCP { bool active = false; int ante = 0; std::vector<int> me, dealer; std::string last; } g_tcp;
static void TCPDeal(int ante) {
    if (!PayChips(ante)) return;
    Shuffle();
    g_tcp = TCP(); g_tcp.active = true; g_tcp.ante = ante;
    g_tcp.me = {Draw(), Draw(), Draw()}; g_tcp.dealer = {Draw(), Draw(), Draw()};
}
static void TCPResolve(bool play) {
    int ps = PokerScore(g_tcp.me), ds = PokerScore(g_tcp.dealer), win = 0;
    std::string res;
    if (!play) res = "Folded";
    else if (!PayChips(g_tcp.ante)) return;
    else {
        bool qualifies = ds >= 1000000 || ds >= 12 * 225;
        if (!qualifies) { win = g_tcp.ante * 2 + g_tcp.ante; res = "Dealer doesn't qualify"; }
        else if (ps > ds) { win = g_tcp.ante * 4; res = "You win"; }
        else if (ps == ds) { win = g_tcp.ante * 2; res = "Push"; }
        else res = "Dealer wins";
        int cat = ps / 1000000;
        int bonus = cat == 5 ? 5 : cat == 4 ? 4 : cat == 3 ? 1 : 0;
        if (bonus) { win += g_tcp.ante * bonus; res += " + ante bonus"; }
    }
    WinChips(win);
    g_tcp.last = res + " | You " + HandName(g_tcp.me) + " (" + PokerName(ps) + ")  Dealer " + HandName(g_tcp.dealer) + " (" + PokerName(ds) + ")";
    Notify("~b~Three Card Poker~s~: " + g_tcp.last);
    g_tcp.active = false;
    Save();
}

// ---- roulette (American: 0, 00 and 1-36)
static const int RED[] = {1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36};
static bool IsRed(int n) { for (int r : RED) if (r == n) return true; return false; }
static std::string g_rouLast;
static void RouletteSpin(int kind, int number) {    // kind: 0 straight, 1 red, 2 black, 3 odd, 4 even, 5 low, 6 high, 7-9 dozens, 10-12 columns
    if (!PayChips(g_casBet)) return;
    Sound("DLC_VW_RULES", "dlc_vw_table_games_frontend_sounds");
    int t0 = Now();
    while (Now() - t0 < 4500) {
        Text("No more bets...", 0.5f, 0.82f, 0.6f, 4, 255, 255, 255, 255, 0, 1.0f, true);
        Wait(0);
    }
    int n = Rand(38) - 1;                // -1 = 00
    bool win = false; int mult = 0;
    if (n > 0) {
        switch (kind) {
        case 1: win = IsRed(n); mult = 2; break;
        case 2: win = !IsRed(n); mult = 2; break;
        case 3: win = n % 2 == 1; mult = 2; break;
        case 4: win = n % 2 == 0; mult = 2; break;
        case 5: win = n <= 18; mult = 2; break;
        case 6: win = n >= 19; mult = 2; break;
        case 7: case 8: case 9: win = (n - 1) / 12 == kind - 7; mult = 3; break;
        case 10: case 11: case 12: win = (n - 1) % 3 == kind - 10; mult = 3; break;
        }
    }
    if (kind == 0) { win = n == number; mult = 36; }
    std::string ns = n == -1 ? "00" : std::to_string(n);
    std::string col = n <= 0 ? "~g~green" : IsRed(n) ? "~r~red" : "black";
    g_rouLast = "Last: " + ns + " (" + col + "~s~)";
    if (win) { WinChips(g_casBet * mult); Notify("~b~Roulette~s~: " + ns + " " + col + "~s~ - you win ~g~" + std::to_string(g_casBet * mult) + " chips"); Sound("WIN", "HUD_AWARDS"); }
    else Notify("~b~Roulette~s~: " + ns + " " + col + "~s~ - no luck this time.");
    Save();
}

// ---- slots: three reels, weighted symbols, two or three cherries pay
static const char *SYM[] = {"Cherry", "Plum", "Melon", "Bell", "Seven", "Diamond"};
static const int SYMW[] = {6, 5, 4, 3, 2, 1}, SYMPAY[] = {5, 8, 10, 15, 25, 50};
static int Reel() { int r = Rand(21); for (int i = 0; i < 6; ++i) { if (r < SYMW[i]) return i; r -= SYMW[i]; } return 0; }
static std::string g_slotLast;
static void SlotSpin(int bet) {
    if (!PayChips(bet)) return;
    Sound("SPIN", "dlc_vw_casino_slot_machine_ak_player_sounds");
    int a = Reel(), b = Reel(), c = Reel(), t0 = Now();
    while (Now() - t0 < 2500) {
        int k = (Now() - t0) / 120;
        std::string s = std::string(SYM[(Now() - t0) < 900 ? k % 6 : a]) + "  |  " + SYM[(Now() - t0) < 1600 ? (k + 2) % 6 : b] + "  |  " + SYM[(k + 4) % 6];
        Text(s, 0.5f, 0.80f, 0.7f, 4, 255, 230, 120, 255, 0, 1.0f, true);
        Wait(0);
    }
    int cherries = (a == 0) + (b == 0) + (c == 0), win = 0;
    if (a == b && b == c) win = bet * SYMPAY[a];
    else if (cherries == 2) win = bet * 3;
    g_slotLast = std::string(SYM[a]) + " | " + SYM[b] + " | " + SYM[c];
    WinChips(win);
    if (win) { Notify("~b~Slots~s~: " + g_slotLast + " - ~g~" + std::to_string(win) + " chips"); Sound("WIN", "HUD_AWARDS"); }
    else Notify("~b~Slots~s~: " + g_slotLast);
    Save();
}

// ---- Inside Track: six horses, odds per race, a short animated race
static std::string g_trackLast;
static int g_trackOdds[6] = {0};
static const char *HORSES[6] = {"Dancin' Poppy", "Black Rock Rooster", "Tax The Poor", "Sir Scrambled", "Wee Scotch", "Downtown Renown"};
static void TrackNewOdds() { static const int pool[] = {1, 2, 3, 4, 5, 7, 9, 12, 15, 20, 30}; for (int i = 0; i < 6; ++i) g_trackOdds[i] = pool[Rand(11)]; }
static void TrackRace(int horse) {
    if (!PayChips(g_casBet)) return;
    float w[6], tot = 0.f;
    for (int i = 0; i < 6; ++i) { w[i] = 1.f / (g_trackOdds[i] + 1); tot += w[i]; }
    float r = Rand(10000) / 10000.f * tot; int winner = 0;
    for (int i = 0; i < 6; ++i) { if (r < w[i]) { winner = i; break; } r -= w[i]; }
    float pos[6] = {0}, spd[6];
    for (int i = 0; i < 6; ++i) spd[i] = 0.9f + Rand(100) / 500.f;
    spd[winner] += 0.12f;
    Sound("Race_Start", "dlc_vw_casino_inside_track_sounds");
    int t0 = Now(), last = t0;
    for (;;) {
        int now = Now(); float dt = (now - last) / 1000.f; last = now;
        bool done = false;
        for (int i = 0; i < 6; ++i) { pos[i] += dt * 0.075f * (spd[i] + (Rand(100) - 50) / 120.f); if (i == winner && pos[i] >= 1.f) done = true; if (i != winner && pos[i] > 0.985f) pos[i] = 0.985f; }
        Rect(0.5f, 0.5f, 0.62f, 0.42f, 10, 40, 20, 220);
        for (int i = 0; i < 6; ++i) {
            float y = 0.33f + i * 0.06f;
            Rect(0.5f, y, 0.56f, 0.004f, 255, 255, 255, 60);
            Text(HORSES[i], 0.205f, y - 0.03f, 0.3f, 4, i == horse ? 255 : 220, i == horse ? 220 : 220, i == horse ? 60 : 220, 255, 1);
            Rect(0.22f + 0.54f * std::min(pos[i], 1.f), y, 0.012f, 0.03f, 60 + i * 30, 200 - i * 25, 80 + i * 20, 255);
        }
        if (done || now - t0 > 30000) break;
        Wait(0);
    }
    int pay = winner == horse ? g_casBet * (g_trackOdds[horse] + 1) : 0;
    WinChips(pay);
    g_trackLast = std::string("Winner: ") + HORSES[winner];
    Notify("~b~Inside Track~s~: " + g_trackLast + (pay ? " - ~g~" + std::to_string(pay) + " chips" : ""));
    if (pay) Sound("WIN", "HUD_AWARDS");
    TrackNewOdds();
    Save();
}

// ---- menus (opened with E at each spot)
enum { CM_CASHIER = 100, CM_ROULETTE, CM_RNUM, CM_BLACKJACK, CM_POKER, CM_SLOTS, CM_TRACK };
static void CycleBet(int lo, int hi) { g_casBet = g_casBet >= hi ? lo : std::min(hi, g_casBet < lo ? lo : g_casBet * (g_casBet < 1000 ? 5 : 2)); }
static std::string ChipsStr() { return "CHIPS: " + std::to_string(g_chips); }
static bool CasinoMenu(int id, std::string &title, std::string &sub, std::vector<Item> &items) {
    switch (id) {
    case CM_CASHIER: {
        title = "Cashier"; sub = ChipsStr();
        for (int n : {1000, 10000, 50000, 250000})
            items.push_back({"Buy " + std::to_string(n) + " chips", Money(n), "One chip costs $1.", [n] { if (Pay(n)) { WinChips(n); Save(); Sound("PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET"); } }});
        items.push_back({"Cash out all chips", Money(g_chips), "Turn every chip back into cash.", [] { if (g_chips > 0) { AddCash(g_chips); Notify("Cashed out ~g~" + Money(g_chips)); g_chips = 0; Save(); } }});
        return true;
    }
    case CM_ROULETTE: {
        const CasTable &t = CAS_TABLES[g_casTable < 0 ? 0 : g_casTable];
        int lo = t.high ? 100 : 10, hi = t.high ? 5000 : 500;
        if (g_casBet < lo || g_casBet > hi) g_casBet = lo;
        title = t.high ? "High Limit Roulette" : "Roulette"; sub = ChipsStr();
        items.push_back({"Bet", std::to_string(g_casBet), "Select to change the bet. " + g_rouLast, [lo, hi] { CycleBet(lo, hi); }});
        static const char *names[] = {"", "Red", "Black", "Odd", "Even", "1 to 18", "19 to 36", "1st 12", "2nd 12", "3rd 12", "Column 1", "Column 2", "Column 3"};
        for (int k = 1; k <= 12; ++k) items.push_back({names[k], k <= 6 ? "1:1" : "2:1", g_rouLast, [k] { RouletteSpin(k, 0); }});
        items.push_back({"Single number", "35:1 >", "Pick 0, 00 or 1-36.", [] { Open((MenuId)CM_RNUM); }});
        return true;
    }
    case CM_RNUM: {
        title = "Single Number"; sub = ChipsStr() + "  BET " + std::to_string(g_casBet);
        for (int n = -1; n <= 36; ++n) {
            std::string s = n == -1 ? "00" : std::to_string(n);
            items.push_back({s, n <= 0 ? "green" : IsRed(n) ? "red" : "black", g_rouLast, [n] { RouletteSpin(0, n); }});
        }
        return true;
    }
    case CM_BLACKJACK: {
        const CasTable &t = CAS_TABLES[g_casTable < 0 ? 6 : g_casTable];
        int lo = t.high ? 500 : 50, hi = t.high ? 10000 : 1000;
        title = t.high ? "High Limit Blackjack" : "Blackjack"; sub = ChipsStr();
        if (!g_bj.active) {
            if (g_casBet < lo || g_casBet > hi) g_casBet = lo;
            items.push_back({"Bet", std::to_string(g_casBet), "Select to change the bet. " + g_bj.last, [lo, hi] { CycleBet(lo, hi); }});
            items.push_back({"Deal", "", g_bj.last, [] { BJDeal(g_casBet); }});
        } else {
            std::string d = "You: " + HandName(g_bj.me) + " (" + std::to_string(BJValue(g_bj.me)) + ")   Dealer: " + HandName(g_bj.dealer, true);
            items.push_back({"Hit", "", d, [] { g_bj.me.push_back(Draw()); if (BJValue(g_bj.me) >= 21) BJFinish(); }});
            items.push_back({"Stand", "", d, [] { BJFinish(); }});
            if (g_bj.me.size() == 2) items.push_back({"Double down", std::to_string(g_bj.bet), d, [] { if (PayChips(g_bj.bet)) { g_bj.bet *= 2; g_bj.me.push_back(Draw()); BJFinish(); } }});
        }
        return true;
    }
    case CM_POKER: {
        const CasTable &t = CAS_TABLES[g_casTable < 0 ? 10 : g_casTable];
        int lo = t.high ? 500 : 50, hi = t.high ? 10000 : 1000;
        title = t.high ? "High Limit Three Card Poker" : "Three Card Poker"; sub = ChipsStr();
        if (!g_tcp.active) {
            if (g_casBet < lo || g_casBet > hi) g_casBet = lo;
            items.push_back({"Ante", std::to_string(g_casBet), "Select to change the ante. " + g_tcp.last, [lo, hi] { CycleBet(lo, hi); }});
            items.push_back({"Deal", "", g_tcp.last, [] { TCPDeal(g_casBet); }});
        } else {
            std::string d = "Your hand: " + HandName(g_tcp.me) + " (" + PokerName(PokerScore(g_tcp.me)) + ")";
            items.push_back({"Play", std::to_string(g_tcp.ante), d, [] { TCPResolve(true); }});
            items.push_back({"Fold", "", d, [] { TCPResolve(false); }});
        }
        return true;
    }
    case CM_SLOTS: {
        title = "Slot Machine"; sub = ChipsStr();
        for (int b : {5, 25, 100, 500, 2500}) items.push_back({"Spin", std::to_string(b), "3 Diamonds x50, Sevens x25, Bells x15, Melons x10, Plums x8, Cherries x5, two Cherries x3. " + g_slotLast, [b] { SlotSpin(b); }});
        return true;
    }
    case CM_TRACK: {
        title = "Inside Track"; sub = ChipsStr();
        if (!g_trackOdds[0]) TrackNewOdds();
        if (g_casBet < 100 || g_casBet > 10000) g_casBet = 100;
        items.push_back({"Bet", std::to_string(g_casBet), "Select to change the bet. " + g_trackLast, [] { CycleBet(100, 10000); }});
        for (int i = 0; i < 6; ++i) items.push_back({HORSES[i], std::to_string(g_trackOdds[i]) + "/1", g_trackLast, [i] { TrackRace(i); }});
        return true;
    }
    }
    return false;
}
static void OpenCasinoMenu(int id) { g_menuOpen = true; g_stack.clear(); Open((MenuId)id); }

// ---- per-frame logic
static void CasinoTick() {
    int me = Me(); Vec3 mp = Pos(me);
    if (!g_casBlip) g_casBlip = CoordBlip(CAS_DOOR, 679, 0, "Diamond Casino", true);
    bool onFoot = !InAnyVehicle();
    // front door: walk in
    float dd = Dist(mp, CAS_DOOR);
    if (dd < 40.f) Marker(CAS_DOOR, 240, 200, 80);
    if (dd < 1.5f && onFoot && !g_menuOpen) {
        Help("Press ~INPUT_CONTEXT~ to enter the ~y~Diamond Casino~s~.");
        if (JustPressed(51)) { const InteriorDef *d = FindInterior("casino"); if (d) TeleportInside(*d); return; }
    }
    bool inside = Dist(mp, CAS_CENTER) < 80.f && mp.z < -40.f;
    if (!inside) { if (g_casSpawned) CasinoDespawn(); return; }
    if (!g_casSpawned) CasinoSpawn();
    // the podium car turns slowly
    if (Exists(g_casCar)) N(N_SET_ENTITY_HEADING, g_casCar, std::fmod(Now() / 60.f, 360.f));
    if (g_menuOpen) return;
    const InteriorDef *lobby = FindInterior("casino");
    Vec3 exitPos = V3(lobby->x, lobby->y, lobby->z);
    struct Spot { Vec3 p; const char *help; int menu; };
    if (Dist(mp, exitPos) < 25.f) Marker(exitPos, 93, 182, 229);
    if (Dist(mp, exitPos) < 1.5f) { Help("Press ~INPUT_CONTEXT~ to leave the casino."); if (JustPressed(51)) { Teleport(CAS_DOOR.x, CAS_DOOR.y - 1.5f, CAS_DOOR.z, 180.f); return; } }
    if (Dist(mp, CAS_CASHIER) < 20.f) Marker(CAS_CASHIER, 120, 220, 120);
    if (Dist(mp, CAS_CASHIER) < 1.5f) { Help("Press ~INPUT_CONTEXT~ to use the ~g~cashier~s~ (" + std::to_string(g_chips) + " chips)."); if (JustPressed(51)) OpenCasinoMenu(CM_CASHIER); return; }
    if (Dist(mp, CAS_WHEELSPOT) < 12.f) Marker(CAS_WHEELSPOT, 240, 200, 80);
    if (Dist(mp, CAS_WHEELSPOT) < 1.2f) { Help("Press ~INPUT_CONTEXT~ to spin the ~y~Lucky Wheel~s~."); if (JustPressed(51)) SpinWheel(); return; }
    if (Dist(mp, CAS_TRACK) < 3.0f) { Help("Press ~INPUT_CONTEXT~ to bet on ~b~Inside Track~s~."); if (JustPressed(51)) OpenCasinoMenu(CM_TRACK); return; }
    for (int i = 0; i < NCASTABLES; ++i) {
        const CasTable &t = CAS_TABLES[i];
        if (Dist2(mp, V3(t.x, t.y, 0)) < 2.4f && std::fabs(mp.z - t.z) < 2.5f) {
            static const char *g[] = {"roulette", "blackjack", "three card poker"};
            Help(std::string("Press ~INPUT_CONTEXT~ to play ~b~") + g[t.game] + "~s~" + (t.high ? " (high limit)." : "."));
            if (JustPressed(51)) { g_casTable = i; OpenCasinoMenu(t.game == 0 ? CM_ROULETTE : t.game == 1 ? CM_BLACKJACK : CM_POKER); }
            return;
        }
    }
    static const char *slots[] = {"vw_prop_casino_slot_01a", "vw_prop_casino_slot_02a", "vw_prop_casino_slot_03a", "vw_prop_casino_slot_04a",
                                  "vw_prop_casino_slot_05a", "vw_prop_casino_slot_06a", "vw_prop_casino_slot_07a", "vw_prop_casino_slot_08a"};
    static int nextSlotScan = 0;
    if (Now() > nextSlotScan) {
        nextSlotScan = Now() + 250; g_casSlot = 0;
        for (const char *s : slots) { int o = N<int>(N_GET_CLOSEST_OBJECT_OF_TYPE, mp.x, mp.y, mp.z, 1.3f, Joaat(s), 0, 0, 0); if (o) { g_casSlot = o; break; } }
    }
    if (g_casSlot) { Help("Press ~INPUT_CONTEXT~ to play the ~b~slot machine~s~."); if (JustPressed(51)) OpenCasinoMenu(CM_SLOTS); }
}

// ---------------------------------------------------------------- Garage elevator menu (garages live in GoriOnline.cpp)
enum { GM_GARAGE = 140 };
static bool GarageMenu(int id, std::string &title, std::string &sub, std::vector<Item> &items) {
    if (id != GM_GARAGE || g_garProp < 0) return false;
    int pi = g_garProp;
    const GLayout *L; GInst I; int per, pages; std::string key;
    GarageOf(pi, g_garPage, L, I, per, pages, key);
    title = PROPS[pi].type == T_HANGAR ? "Hangar" : "Garage"; sub = PROPS[pi].name;
    items.push_back({"Leave to the street", "", "Walk out of the building.", [] { CloseMenu(); LeaveGarageOnFoot(false); }});
    if (PropInterior(pi) && PROPS[pi].type != T_KOSATKA && PROPS[pi].type != T_HANGAR)
        items.push_back({"Elevator to the property", "", "Go up to the property itself.", [] { CloseMenu(); LeaveGarageOnFoot(true); }});
    for (int f = 0; f < pages; ++f)
        if (f != g_garPage) items.push_back({"Floor " + std::to_string(f + 1), ">", "Vehicles " + std::to_string(f * per + 1) + "-" + std::to_string((f + 1) * per) + ".",
                                             [pi, f] { CloseMenu(); N(N_DO_SCREEN_FADE_OUT, 300); Wait(350); SpawnGarage(pi, f, true); N(N_DO_SCREEN_FADE_IN, 400); }});
    return true;
}
static void OpenGarageMenu() { g_menuOpen = true; g_stack.clear(); Open((MenuId)GM_GARAGE); }
