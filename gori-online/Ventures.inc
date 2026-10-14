// ---------------------------------------------------------------- GTA Online's newer businesses (2023-2025)
// The Chop Shop's Salvage Yards, Bottom Dollar Bounties' Bail Offices, Agents of Sabotage's Garment Factory and
// Money Fronts' small businesses (Hands On Car Wash, Smoke on the Water, Higgins Helitours). Entrances: freemode.ysc's
// simple interior locates (ids 162-176); interiors: mp2023_02 / mp2024_01 / mp2024_02 / mp2025_01 MLOs. Prices
// are close to Online's, not exact (their tunables are hashed). Like Gori's other businesses: a safe that fills
// every in-game day, plus the business's own jobs (co-op jobs online).

// Salvage Yard: steal a high-end car from a guarded garage, bring it to the yard and salvage it on the lift
static void SalvageRobbery(int pi) {
    Mission m;
    m.name = "SALVAGE YARD ROBBERY";
    const char *(&gang)[2] = GANGS[Rand(6)];
    const char *car = STEAL_CARS[Rand((int)(sizeof STEAL_CARS / sizeof *STEAL_CARS))];
    Step s = Steal(std::string("Steal the ~g~target vehicle~s~ from the guarded garage."), PickSpot(HOSTILE_SPOTS), 0.f, car);
    m.steps.push_back(Enemies(s, 7, gang[0], gang[1]));
    Step d = Deliver("Take it to your ~y~Salvage Yard~s~.", PropExt(pi));
    d.chasers = 2; d.wanted = 2;
    m.steps.push_back(d);
    int pay = 120000 + Rand(100000);
    m.coopPay = pay / 3;
    m.timeLimitMs = 25 * 60000;
    std::string name = car;
    m.onSuccess = [pay, name] {
        AddCash(pay); Save(); GiveRP(1500, "salvage yard robbery");
        Shard("~g~VEHICLE SALVAGED", "The " + name + " went up on the lift: " + Money(pay));
    };
    Notify("Salvage Yard: \"A collector wants this one in pieces. It's in a gang garage, so bring friends.\"");
    StartMission(m);
}

// Bail Office: bring a bail jumper in alive (full bounty) to the Mission Row police station
static const char *BOUNTY_TARGETS[][2] = {
    {"a_m_m_hillbilly_01", "Dwayne \"Meth Teeth\" Coombs"}, {"g_m_y_salvagoon_01", "Ramon Castillo"}, {"a_m_y_business_03", "Preston Hale"},
    {"g_m_y_lost_02", "Kyle \"Spark\" Mercer"}, {"a_m_m_eastsa_02", "Victor Duarte"}, {"a_f_y_vinewood_04", "Crystal Vane"},
};
static void BountyTarget(int pi) {
    (void)pi;
    int t = Rand((int)(sizeof BOUNTY_TARGETS / sizeof *BOUNTY_TARGETS));
    Mission m;
    m.name = "BOUNTY: " + std::string(BOUNTY_TARGETS[t][1]);
    for (auto &c : m.name) c = (char)toupper((unsigned char)c);
    const char *(&gang)[2] = GANGS[Rand(6)];
    Step a = Goto("Find ~r~" + std::string(BOUNTY_TARGETS[t][1]) + "~s~. His crew won't let him go quietly.", PickSpot(HOSTILE_SPOTS), 8.f);
    a.snapFoot = true;
    m.steps.push_back(Enemies(a, 5, gang[0], gang[1]));
    Step e = Escape("Get the bail jumper out and lose the ~r~cops~s~.", V3(0, 0, 0), 0.f);
    e.ally = BOUNTY_TARGETS[t][0]; e.wanted = 1;
    m.steps.push_back(e);
    m.steps.push_back(Goto("Hand them in at the ~y~Mission Row police station~s~.", V3(428.0f, -982.0f, 30.7f), 8.f));
    int pay = 30000 + Rand(30000);
    m.coopPay = pay / 2;
    m.timeLimitMs = 25 * 60000;
    std::string who = BOUNTY_TARGETS[t][1];
    m.onSuccess = [pay, who] {
        AddCash(pay); Save(); GiveRP(1200, "bounty");
        Shard("~g~BOUNTY COLLECTED", who + " is back in custody: " + Money(pay));
    };
    Notify("Bail Office: \"" + who + " skipped bail. Bring them in breathing.\"");
    StartMission(m);
}

// Garment Factory (Agents of Sabotage): the hacker's jobs against a private military contractor
static void SabotageJob(int pi) {
    (void)pi;
    Mission m;
    m.name = "SABOTAGE";
    Vec3 t = PickSpot(HOSTILE_SPOTS);
    m.steps.push_back(Enemies(Goto("Reach the contractor's ~y~server cache~s~.", t, 15.f), 7, "s_m_y_blackops_01", "weapon_carbinerifle"));
    Step h = Hold("Plant the ~g~charges~s~ on the servers.", t, 10);
    h.snapFoot = true;
    h.onDone = [t] { N(N_ADD_EXPLOSION, t.x, t.y, t.z, 2, 5.f, 1, 0, 1.f, 0); };
    m.steps.push_back(h);
    Step e = Escape("Get away and lose the ~r~cops~s~.");
    e.wanted = 3;
    m.steps.push_back(e);
    int pay = 45000 + Rand(35000);
    m.coopPay = pay / 2;
    m.timeLimitMs = 20 * 60000;
    m.onSuccess = [pay] {
        AddCash(pay); Save(); GiveRP(1300, "sabotage");
        Shard("~g~SABOTAGE COMPLETE", "The contractor's servers are scrap: " + Money(pay));
    };
    Notify("Garment Factory: \"Their servers are in a warehouse with too many guns. Make it loud.\"");
    StartMission(m);
}
