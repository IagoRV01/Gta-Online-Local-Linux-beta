// ---------------------------------------------------------------- Knoway: self-driving cars, like GTA Online's
// The Knoway Ocelot E-Stride add-on (knowayest) drives around with nobody at the wheel: a hidden driver ped
// wanders the roads. Walk up to one and press E to ride in the back: it drives to your waypoint and charges a fare
// on arrival, like a taxi. A few of them stay around you; they never join the swapped traffic (Traffic.inc).
// Included from GoriOnline.cpp after Traffic.inc.
struct KnowayCar { int veh = 0, driver = 0, blip = 0, stillSince = 0; bool hired = false, arrived = false; Vec3 dest{}, from{}; };
static std::vector<KnowayCar> g_kw;
static int g_kwNext = 0, g_kwRide = -1;
static const int KNOWAY_MAX = 3;
static uint32_t KnowayModel() { static uint32_t h = Joaat("knowayest"); return h; }
static bool KnowayAvailable() {
    static int ok = -1;
    if (ok < 0) ok = NB(N<int>(N_IS_MODEL_IN_CDIMAGE, KnowayModel())) && NB(N<int>(N_IS_MODEL_A_VEHICLE, KnowayModel())) ? 1 : 0;
    return ok == 1;
}
static void KnowayWander(KnowayCar &k) {
    N(N_TASK_VEHICLE_DRIVE_WANDER, k.driver, k.veh, 14.f, 786603);   // gentle, law-abiding (Knoway's whole pitch)
    N(N_SET_PED_KEEP_TASK, k.driver, 1);
}
static void KnowayRemove(KnowayCar &k) {
    RemoveBlip(k.blip);
    if (Exists(k.driver)) DeleteEntity(k.driver);
    if (Exists(k.veh)) DeleteEntity(k.veh);
    k = KnowayCar();
}
static void KnowaySpawn() {
    uint32_t vm = KnowayModel(), pm = Joaat("a_m_y_business_01");
    if (!LoadModel(vm) || !LoadModel(pm)) return;
    Vec3 me = Pos(Me());
    for (int tries = 0; tries < 8; ++tries) {   // a road 120-260 m away, out of sight
        float a = (float)Rand(6283) / 1000.f, r = 120.f + (float)Rand(140);
        Vec3 out{}; float h = 0.f;
        if (!NB(N<int>(N_GET_CLOSEST_VEHICLE_NODE_WITH_HEADING, me.x + std::cos(a) * r, me.y + std::sin(a) * r, me.z, &out, &h, 0, 3.0f, 0.0f))) continue;   // main roads only: not golf or park paths
        if (Dist(out, me) < 100.f || NB(N<int>(N_IS_SPHERE_VISIBLE, out.x, out.y, out.z, 3.f))) continue;
        KnowayCar k;
        k.veh = N<int>(N_CREATE_VEHICLE, vm, out.x, out.y, out.z + 0.5f, h, 0, 0, 0);
        if (!k.veh) break;
        N(N_SET_ENTITY_AS_MISSION_ENTITY, k.veh, 1, 1);
        N(N_SET_VEHICLE_ON_GROUND_PROPERLY, k.veh, 5.f);
        N(N_SET_VEHICLE_DOORS_LOCKED, k.veh, 1);
        k.driver = N<int>(N_CREATE_PED_INSIDE_VEHICLE, k.veh, 26, pm, -1, 0, 0);
        if (!k.driver) { DeleteEntity(k.veh); break; }
        N(N_SET_ENTITY_AS_MISSION_ENTITY, k.driver, 1, 1);
        N(N_SET_ENTITY_VISIBLE, k.driver, 0, 0);                  // nobody at the wheel
        N(N_SET_ENTITY_INVINCIBLE, k.driver, 1);
        N(N_SET_BLOCKING_OF_NON_TEMPORARY_EVENTS, k.driver, 1);   // it doesn't panic, flee or fight
        N(N_SET_PED_CAN_BE_DRAGGED_OUT, k.driver, 0);
        N(N_SET_PED_CONFIG_FLAG, k.driver, 32, 0);                // can fly through the windscreen: no
        KnowayWander(k);
        g_kw.push_back(k);
        break;
    }
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, vm); N(N_SET_MODEL_AS_NO_LONGER_NEEDED, pm);
}
static void KnowayTick() {
    if (!YouActive() || !KnowayAvailable()) { for (auto &k : g_kw) KnowayRemove(k); g_kw.clear(); g_kwRide = -1; return; }
    int me = Me(); Vec3 mp = Pos(me);
    // the fleet: a few around you; far, wrecked or driverless ones go
    for (int i = (int)g_kw.size() - 1; i >= 0; --i) {
        KnowayCar &k = g_kw[i];
        bool riding = i == g_kwRide;
        bool still = !riding && N<float>(N_GET_ENTITY_SPEED, k.veh) < 0.5f;
        if (!still) k.stillSince = 0; else if (!k.stillSince) k.stillSince = Now();
        bool stuck = still && Now() - k.stillSince > 30000 && Dist(Pos(k.veh), mp) > 40.f;   // wedged somewhere: a fresh one comes
        if (!Exists(k.veh) || Dead(k.veh) || !Exists(k.driver) || stuck || (!riding && Dist(Pos(k.veh), mp) > 400.f)) {
            if (riding) g_kwRide = -1;
            KnowayRemove(k); g_kw.erase(g_kw.begin() + i);
            if (g_kwRide > i) --g_kwRide;
        }
    }
    if ((int)g_kw.size() < KNOWAY_MAX && Now() > g_kwNext && !g_mActive && !InActivity()) { g_kwNext = Now() + 8000; KnowaySpawn(); }
    // the ride
    int myVeh = InAnyVehicle() ? MyVehicle() : 0;
    if (g_kwRide >= 0) {
        KnowayCar &k = g_kw[g_kwRide];
        if (myVeh != k.veh) {   // got out: pay what was driven, back to wandering
            float km = Dist(k.from, Pos(k.veh)) / 1000.f;
            int fare = (int)(25 + 180 * km);
            if (k.hired) { PayWhat what("Knoway ride"); Pay(fare); Notify("~b~Knoway~s~: thanks for riding. " + Money(fare) + "."); }
            N(N_SET_VEHICLE_DOORS_LOCKED, k.veh, 1);
            k.hired = false; k.arrived = false;
            KnowayWander(k);
            g_kwRide = -1;
            return;
        }
        if (!k.hired) {   // no destination yet: the waypoint
            if (NB(N<int>(N_IS_WAYPOINT_ACTIVE))) {
                int wb = N<int>(N_GET_FIRST_BLIP_INFO_ID, 8);
                Vec3 w = N<Vec3>(N_GET_BLIP_INFO_ID_COORD, wb);
                Vec3 node{};
                if (NB(N<int>(N_GET_CLOSEST_VEHICLE_NODE, w.x, w.y, w.z, &node, 0, 3.0f, 0.0f))) w = node;
                k.dest = w; k.from = Pos(k.veh); k.hired = true; k.arrived = false;
                N(N_TASK_VEHICLE_DRIVE_TO_COORD_LONGRANGE, k.driver, k.veh, w.x, w.y, w.z, 22.f, 786603, 15.f);
                Notify("~b~Knoway~s~: on our way. Sit back and relax.");
            } else Help("~b~Knoway~s~: set a waypoint on the map and the car takes you there.");
        } else if (!k.arrived && (Dist(Pos(k.veh), k.dest) < 25.f ||   // there, or stopped for good near it (the drive task ends short of the node)
                   (Dist(Pos(k.veh), k.dest) < 80.f && N<float>(N_GET_ENTITY_SPEED, k.veh) < 0.5f && N<int>(N_GET_SCRIPT_TASK_STATUS, k.driver, 0x21D33957u) == 7))) {
            k.arrived = true;
            N(N_TASK_VEHICLE_TEMP_ACTION, k.driver, k.veh, 6, 4000);   // brake and stay
            Notify("~b~Knoway~s~: you have arrived. Press ~INPUT_VEH_EXIT~ to get out.");
        }
        return;
    }
    if (myVeh || g_menuOpen || Dead(me)) return;
    // hail one: walk up to it
    for (int i = 0; i < (int)g_kw.size(); ++i) {
        KnowayCar &k = g_kw[i];
        if (Dist(Pos(k.veh), mp) > 7.f) continue;
        Help("Press ~INPUT_CONTEXT~ to ride this ~b~Knoway~s~.");
        if (NB(N<int>(N_IS_CONTROL_JUST_PRESSED, 0, 51))) {
            N(N_TASK_VEHICLE_TEMP_ACTION, k.driver, k.veh, 6, 6000);
            N(N_SET_VEHICLE_DOORS_LOCKED, k.veh, 0);
            int seat = NB(N<int>(N_IS_VEHICLE_SEAT_FREE, k.veh, 1, 0)) ? 1 : 2;
            N(N_TASK_ENTER_VEHICLE, me, k.veh, 8000, seat, 1.f, 1, 0);
            g_kwRide = i;
            for (int t = 0; t < 160 && !(InAnyVehicle() && MyVehicle() == k.veh); ++t) Wait(50);
            if (!(InAnyVehicle() && MyVehicle() == k.veh)) { g_kwRide = -1; KnowayWander(k); }
        }
        break;
    }
    // nobody drives a Knoway from the front seat but its own computer
    for (auto &k : g_kw) if (N<int>(N_GET_PED_IN_VEHICLE_SEAT, k.veh, -1, 0) != k.driver && Exists(k.driver)) N(N_SET_PED_INTO_VEHICLE, k.driver, k.veh, -1);
}
