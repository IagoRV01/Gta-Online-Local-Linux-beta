// ---------------------------------------------------------------- the end of a heist, as Online's
// HEIST_CELEBRATION (fm_mission_controller's case 5): potential take, actual take, your cut, the Elite Challenge's
// lines with their ticks, then the bonuses: first time, All In Order, Loyalty, Criminal Mastermind, and the total.
// The labels and the order of the lines are the script's. The limits of the Elite Challenge are in the cloud's job data
// and tunables, not in the script: these are the published ones. Death fails a job here, so a passed job has nobody wasted
// on the leader's side (a member's death isn't known to this game).
static const struct { int timeMs, vehPct, rashPct, extractMs, kills; bool noose; int elite, first; } ELITE[5] = {
    {320000, 6, -1, 0, 0, false, 50000, 50000},        // Fleeca: under 5:20, vehicle damage 6%
    {0, -1, 1, 280000, 0, false, 100000, 100000},      // Prison: Rashkovsky 1%, extraction under 4:40
    {660000, 2, -1, 0, 0, false, 100000, 100000},      // Humane: under 11:00, vehicle damage 2%
    {390000, -1, -1, 0, 75, false, 100000, 100000},    // Series A: under 6:30, 75 kills
    {615000, -1, -1, 0, 0, true, 100000, 100000},      // Pacific: under 10:15, NOOSE not called
};
static const int AWARD_ORDER = 1000000, AWARD_LOYALTY = 1000000, AWARD_MASTERMIND = 10000000;
static struct {
    bool on = false; int h = -1, startMs = 0;
    int veh = 0, vehHealth = 0; float vehDamage = 0.f;          // the damage to the vehicles you drove, % of their health
    int ally = 0, allyHealth = 0, allyStart = 0; float allyDamage = 0.f;   // Rashkovsky, from when he's with you
    int alarmAt = 0; bool noose = false;                        // Pacific Standard: too long after the vault's alarm
} g_elite;
static void EliteBegin(int h) { g_elite = {}; if (h >= 0 && h < 5) { g_elite.on = true; g_elite.h = h; g_elite.startMs = Now(); } }
static void EliteAlarm() { if (g_elite.on) g_elite.alarmAt = Now(); }
static void EliteTick() {
    if (!g_elite.on) return;
    int me = Me();
    int v = NB(N<int>(N_IS_PED_IN_ANY_VEHICLE, me, 0)) ? N<int>(N_GET_VEHICLE_PED_IS_IN, me, 0) : 0;
    if (v) {
        int hp = N<int>(N_GET_ENTITY_HEALTH, v), most = N<int>(N_GET_ENTITY_MAX_HEALTH, v);
        if (v == g_elite.veh && hp < g_elite.vehHealth && most > 0) g_elite.vehDamage += 100.f * (g_elite.vehHealth - hp) / most;
        g_elite.veh = v; g_elite.vehHealth = hp;
    }
    if (g_mAlly && Exists(g_mAlly)) {
        int hp = N<int>(N_GET_ENTITY_HEALTH, g_mAlly), most = N<int>(N_GET_ENTITY_MAX_HEALTH, g_mAlly);
        if (g_elite.ally != g_mAlly) { g_elite.ally = g_mAlly; g_elite.allyStart = Now(); }
        else if (hp < g_elite.allyHealth && most > 0) g_elite.allyDamage += 100.f * (g_elite.allyHealth - hp) / most;
        g_elite.allyHealth = hp;
    }
    if (ELITE[g_elite.h].noose && g_elite.alarmAt && !g_elite.noose && Now() - g_elite.alarmAt > 210000) {
        g_elite.noose = true;   // three and a half minutes in the vault: from here on the police is NOOSE
        for (size_t i = g_mStep; i < g_m.steps.size(); ++i) if (g_m.steps[i].wanted) g_m.steps[i].wanted = 5;
        N(N_SET_PLAYER_WANTED_LEVEL, Player(), 5, 0); N(N_SET_PLAYER_WANTED_LEVEL_NOW, Player(), 0);
        Notify("Lester: \"You took too long in there. NOOSE has been called.\"");
    }
}
static std::string EliteClock(int ms) { char b[16]; ms = std::max(ms, 0) / 1000; std::snprintf(b, sizeof b, "%02d:%02d", ms / 60, ms % 60); return b; }
static std::string ElitePct(float p) { return std::to_string((int)std::ceil(p)) + "%"; }
// the screen's lines for a finale: returns what the bonuses add up to. elite < 5 only for the original five.
static int EliteLines(int h, int cut, int pct, int timeMs, int extractMs, float vehDmg, float rashDmg, int kills, bool noose, bool first, bool order, bool loyal, bool master) {
    auto &c = g_celHeist;
    c = CelHeist(); c.on = true;
    int v = HeistTake(h);
    auto row = [&c](float a, float b, const char *t, const char *d = "", const std::string &sub = "", const std::string &right = "", int icon = 3, int snd = -1, int tn = -1) {
        c.rows.push_back({a, b, t, d, sub, right, icon, snd, tn});
    };
    row(0.f, (float)v, "CELEB_POTENTIAL_TAKE", "", "", "", 3, 3);
    row((float)v, (float)v, "CELEB_ACTUAL_TAKE", "", "", "", 3, 4);
    row((float)v, (float)cut, "PER_CELEB_CUT_TAKE", "", "", "", 3, 5, pct);
    int now = cut, add = 0;
    if (h < 5) {
        const auto &e = ELITE[h];
        bool all = true, firstLine = true;
        auto line = [&](const char *d, const std::string &sub, const std::string &right, bool ok) {
            row((float)now, (float)now, "CELEB_ELITE_CH", d, sub, right, ok ? 1 : 2, firstLine ? 6 : -1);
            firstLine = false;
            if (!ok) all = false;
        };
        if (e.timeMs) line("CELEB_EL_COMPT", EliteClock(e.timeMs), EliteClock(timeMs), timeMs <= e.timeMs);
        if (e.vehPct >= 0) line("CELEB_EL_VEHDM", std::to_string(e.vehPct) + "%", ElitePct(vehDmg), std::ceil(vehDmg) <= e.vehPct);
        if (e.rashPct >= 0) line("CELEB_EL_RCHDM", std::to_string(e.rashPct) + "%", ElitePct(rashDmg), std::ceil(rashDmg) <= e.rashPct);
        if (e.extractMs) line("CELEB_EL_EXTTM", EliteClock(e.extractMs), EliteClock(extractMs), extractMs <= e.extractMs);
        if (e.kills) line("CELEB_EL_KLLEN", std::to_string(e.kills), std::to_string(kills), kills >= e.kills);
        if (e.noose) line("CELEB_EL_NOOSE", "", "", !noose);
        line("CELEB_EL_NODIE", "", "", true);
        int bonus = all ? e.elite : 0;
        row((float)now, (float)(now + bonus), all ? "CELEB_EL_CH_P" : "CELEB_EL_CH_F", "", "", "", 3, all ? 6 : 2);
        now += bonus; add += bonus;
        auto award = [&](bool on, int cash, const char *t, const char *d) {
            if (!on) return;
            row((float)now, (float)(now + cash), t, d, "", "", 3, 3);
            now += cash; add += cash;
        };
        award(first, e.first, "CELEB_FB_1_TM", "CELEB_FB_1_TM2");
        award(order, AWARD_ORDER, "CELEB_FB_ORDER", "CELEB_FB_ORD2");
        award(loyal, AWARD_LOYALTY, "CELEB_FB_SMTM", "CELEB_FB_SMTM2");
        award(master, AWARD_MASTERMIND, "CELEB_FB_ULT", "CELEB_FB_ULT2");
    }
    row((float)now, (float)now, "CELEB_TOTAL_EARNED", "", "", "", 3, 7);
    return add;
}
// the five heists played in Online's order: All In Order, Loyalty and Criminal Mastermind (first time through only)
static void HeistAwardsStage(int h) {
    if (h < 0 || h >= 5 || g_ha.next >= 5 || h < g_ha.next) return;
    if (h > g_ha.next) g_ha.broken = true;
    std::string crew = g_coop.members;
    if (!g_ha.crewSet) { g_ha.crewSet = true; g_ha.crew = crew; }
    else if (crew != g_ha.crew) g_ha.crewBroken = true;
}
static void EliteDied() { if (g_elite.h >= 0 || g_heistRun >= 0) { int h = g_heistRun; if (h >= 0 && h < 5 && h >= g_ha.next) { g_ha.dead = true; Save(); } } }
