// Hawick Gardens' residents (IslandBuilder Hawick.cs, hawick_data.h): while you are near the complex, people live in it
// the way GTA's own scenario points fill a park: they sit on the benches and in the gazebo, sunbathe on the spa's loungers,
// swim in the pool, soak in the hot tubs, run on the treadmills, do yoga, stroll the promenade and walk their dogs; a
// valet waits at Tower A's door. One new person every second and a half, up to 24, never in front of you; when you leave
// (or go into a property, or start a job) they are handed back to the game.
static std::vector<int> g_hgPeds;
static int g_hgBlock = 0;
static void GardensTick() {
    static int next = 0;
    if (Now() < next) return;
    next = Now() + 1500;
    Vec3 mp = Pos(Me());
    float d = std::hypot(mp.x - HG_CENTER[0], mp.y - HG_CENTER[1]);
    if (d < 400.f && !g_hgBlock) {   // no other ambient scenarios on the block, ped paths on
        g_hgBlock = N<int>(N_ADD_SCENARIO_BLOCKING_AREA, HG_BOX[0], HG_BOX[1], HG_BOX[2], HG_BOX[3], HG_BOX[4], HG_BOX[5], 0, 1, 1, 1) + 1;
        N(N_SET_PED_PATHS_IN_AREA, HG_BOX[0], HG_BOX[1], HG_BOX[2], HG_BOX[3], HG_BOX[4], HG_BOX[5], 1, 0);
        Log("gardens: scenario blocking on");
    }
    for (size_t k = 0; k < g_hgPeds.size();) {
        int pd = g_hgPeds[k];
        if (!Exists(pd) || N<int>(N_IS_ENTITY_DEAD, pd, 0)) { g_hgPeds.erase(g_hgPeds.begin() + k); continue; }
        ++k;
    }
    if (d > 230.f || g_inside >= 0 || g_mActive) {
        for (int pd : g_hgPeds) { int q = pd; N(N_SET_PED_AS_NO_LONGER_NEEDED, &q); }
        g_hgPeds.clear();
        return;
    }
    if ((int)g_hgPeds.size() >= 24) return;
    static const char *CASUAL[] = {"a_m_y_bevhills_01", "a_m_y_bevhills_02", "a_f_y_bevhills_01", "a_f_y_bevhills_02", "a_f_y_bevhills_03", "a_f_y_bevhills_04",
                                   "a_m_m_bevhills_01", "a_m_m_bevhills_02", "a_f_m_bevhills_01", "a_f_m_bevhills_02", "a_m_y_hipster_01", "a_f_y_hipster_02"};
    static const char *SWIM[] = {"a_f_y_beach_01", "a_m_y_beach_01", "a_m_y_beach_02", "a_f_m_beach_01", "a_m_y_musclbeac_01"};
    static const char *SPORT[] = {"a_f_y_fitness_01", "a_f_y_fitness_02", "a_m_y_runner_01", "a_m_y_runner_02", "a_f_y_runner_01", "a_m_y_musclbeac_02"};
    static const char *YOGA[] = {"a_f_y_yoga_01", "a_m_y_yoga_01", "a_f_y_fitness_01"};
    static const char *IDLE[] = {"WORLD_HUMAN_STAND_MOBILE", "WORLD_HUMAN_AA_COFFEE", "WORLD_HUMAN_HANG_OUT_STREET", "WORLD_HUMAN_TOURIST_MAP", "WORLD_HUMAN_STAND_IMPATIENT"};
#define HG_N(a) ((int)(sizeof a / sizeof *a))
    // what this one does: weights per kind
    int r = Rand(100), kind;
    if (r < 22) kind = 0;        // bench
    else if (r < 36) kind = 1;   // lounger
    else if (r < 48) kind = 2;   // swim
    else if (r < 54) kind = 3;   // hot tub
    else if (r < 62) kind = 4;   // treadmill
    else if (r < 68) kind = 5;   // yoga
    else if (r < 92) kind = 6;   // stroll / stand
    else kind = 7;               // walking a dog
    const char *model = kind == 1 || kind == 2 || kind == 3 ? SWIM[Rand(HG_N(SWIM))] : kind == 4 ? SPORT[Rand(HG_N(SPORT))] : kind == 5 ? YOGA[Rand(HG_N(YOGA))] : CASUAL[Rand(HG_N(CASUAL))];
    float x, y, z, h = (float)Rand(360);
    const float *p = nullptr;
    switch (kind) {
    case 0: p = HG_BENCHES[Rand(HG_N(HG_BENCHES))]; break;
    case 1: p = HG_LOUNGERS[Rand(HG_N(HG_LOUNGERS))]; break;
    case 2: p = HG_SWIM[Rand(HG_N(HG_SWIM))]; break;
    case 3: p = HG_TUBS[Rand(HG_N(HG_TUBS))]; break;
    case 4: p = HG_GYM[Rand(HG_N(HG_GYM))]; break;
    case 5: p = HG_YOGA[Rand(HG_N(HG_YOGA))]; break;
    default: p = HG_STROLL[Rand(HG_N(HG_STROLL))]; break;
    }
    x = p[0]; y = p[1]; z = p[2];
    bool fixedSpot = kind <= 5 && kind != 2;
    if (fixedSpot) h = p[3];
    else { x += (float)(Rand(60) - 30) / 10.f; y += (float)(Rand(60) - 30) / 10.f; }
    if (std::hypot(mp.x - x, mp.y - y) < 10.f) return;   // not right in front of you
    if (fixedSpot) for (int pd : g_hgPeds) { Vec3 q = Pos(pd); if (std::hypot(q.x - x, q.y - y) < 0.8f) return; }   // taken
    uint32_t m = Joaat(model);
    if (!LoadModel(m)) return;
    int ped = N<int>(N_CREATE_PED, 4, m, x, y, z + (kind == 2 ? 0.f : 0.5f), h, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
    if (!Exists(ped)) return;
    N(N_SET_PED_RANDOM_COMPONENT_VARIATION, ped, 0);
    N(N_SET_BLOCKING_OF_NON_TEMPORARY_EVENTS, ped, 0);
    switch (kind) {
    case 0: N(N_TASK_START_SCENARIO_AT_POSITION, ped, "PROP_HUMAN_SEAT_BENCH", x, y, z + 0.5f, h, 0, 1, 1); break;
    case 1: N(N_TASK_START_SCENARIO_AT_POSITION, ped, Rand(2) ? "WORLD_HUMAN_SUNBATHE_BACK" : "PROP_HUMAN_SEAT_SUNLOUNGER", x, y, z + 0.45f, h, 0, 1, 1); break;
    case 2: N(N_TASK_WANDER_IN_AREA, ped, x, y, z, 5.f, 2.f, 3.f); break;              // in the water: swims about
    case 3: N(N_TASK_START_SCENARIO_AT_POSITION, ped, "PROP_HUMAN_SEAT_BENCH", x, y, z + 0.5f, h, 0, 1, 1); break;
    case 4: N(N_TASK_START_SCENARIO_AT_POSITION, ped, "WORLD_HUMAN_JOG_STANDING", x, y, z + 0.5f, h, 0, 0, 1); break;
    case 5: N(N_TASK_START_SCENARIO_AT_POSITION, ped, "WORLD_HUMAN_YOGA", x, y, z + 0.5f, h, 0, 0, 1); break;
    case 6:
        if (Rand(2)) N(N_TASK_WANDER_IN_AREA, ped, x, y, z, 14.f, 4.f, 8.f);
        else N(N_TASK_START_SCENARIO_IN_PLACE, ped, IDLE[Rand(HG_N(IDLE))], 0, 1);
        break;
    default: {   // a dog walker: the person strolls, the dog follows
        N(N_TASK_WANDER_IN_AREA, ped, x, y, z, 16.f, 3.f, 6.f);
        uint32_t dm = Joaat(Rand(2) ? "a_c_poodle" : "a_c_westy");
        if (LoadModel(dm)) {
            int dog = N<int>(N_CREATE_PED, 28, dm, x + 1.f, y, z + 0.5f, h, 0, 0);
            N(N_SET_MODEL_AS_NO_LONGER_NEEDED, dm);
            if (Exists(dog)) { N(N_TASK_FOLLOW_TO_OFFSET_OF_ENTITY, dog, ped, 0.8f, -0.6f, 0.f, 1.5f, -1, 1.f, 1); g_hgPeds.push_back(dog); }
        }
        break;
    }
    }
#undef HG_N
    g_hgPeds.push_back(ped);
}
// the valet at Tower A's door: one, while you are near
static int g_hgValet = 0;
static void GardensValetTick() {
    static int next = 0;
    if (Now() < next) return;
    next = Now() + 3000;
    Vec3 mp = Pos(Me());
    float d = std::hypot(mp.x - HG_DOOR_A_WALK[0], mp.y - HG_DOOR_A_WALK[1]);
    if (d > 150.f || g_inside >= 0) {
        if (Exists(g_hgValet)) { N(N_SET_PED_AS_NO_LONGER_NEEDED, &g_hgValet); }
        g_hgValet = 0;
        return;
    }
    if (Exists(g_hgValet) || d < 25.f) return;
    uint32_t m = Joaat("s_m_y_valet_01");
    if (!LoadModel(m)) return;
    float h = HG_DOOR_A_WALK[3] + 180.f, r = h * 0.0174533f;
    float x = HG_DOOR_A_WALK[0] - std::sin(r) * 2.5f + std::cos(r) * 4.f, y = HG_DOOR_A_WALK[1] + std::cos(r) * 2.5f + std::sin(r) * 4.f;
    g_hgValet = N<int>(N_CREATE_PED, 4, m, x, y, HG_DOOR_A_WALK[2] + 0.5f, h, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
    if (Exists(g_hgValet)) N(N_TASK_START_SCENARIO_IN_PLACE, g_hgValet, "WORLD_HUMAN_STAND_IMPATIENT", 0, 1);
}
