// ---------------------------------------------------------------- GTA Online's special vehicles
// Mobile Operations Center (needs a Bunker), Avenger (a Facility) and Terrorbyte (a Nightclub), plus the
// Kosatka's options. They are bought on Warstock's own configurators (Web.inc, the choices packed like
// GameGlobals TRUCK / CHOPPER), requested from Services, and walked into: GTA Online's interiors
// (am_mp_armory_truck / am_mp_armory_aircraft / am_mp_hacker_truck) with the modules you bought.
//
// Saved in g_cust: "@MOC|config", "@Avenger|config", "@Terrorbyte|config", "@Kosatka|config" = the
// website's flag value for the configuration you own.
enum { SM_WWS = 160, SM_WWEAP, SM_VWS, SM_TUNE, SM_TUNECAT, SM_RESPRAY, SM_YMOVE };   // the workshop menus (SpvMenu)
enum SpvKind { SPV_MOC, SPV_AVENGER, SPV_TERRORBYTE, SPV_KOSATKA, SPV_COUNT };
struct SpvDef { const char *name, *key, *model; int needType; const char *needName, *interior; int blip, blipColour; };
static const SpvDef SPV[SPV_COUNT] = {
    {"Mobile Operations Center", "MOC", "trailerlarge", T_BUNKER, "Bunker", "moc", 564, 0},
    {"Avenger", "Avenger", "avenger", T_FACILITY, "Facility", "avenger", 589, 0},
    {"Terrorbyte", "Terrorbyte", "terbyte", T_NIGHTCLUB, "Nightclub", "terrorbyte", 632, 0},
    {"Kosatka", "Kosatka", "kosatka", -1, "", "kosatka", 760, 0},
};
static void OpenMenuAt(MenuId menu);   // Phone.inc
static std::string SpvKey(int k) { return std::string("@") + SPV[k].key + "|config"; }
static int SpvConfig(int k) { auto it = g_cust.find(SpvKey(k)); return it == g_cust.end() ? -1 : it->second; }
static bool SpvOwned(int k) { return k == SPV_KOSATKA ? Owns("Kosatka") : g_cust.count(SpvKey(k)) > 0; }
static bool SpvHasBase(int k) {
    if (SPV[k].needType < 0) return true;
    for (auto &o : g_props) { int i = FindProp(o); if (i >= 0 && PROPS[i].type == SPV[k].needType) return true; }
    return false;
}
// The configuration in the website's order (Warstock's purchasedTruck*/purchasedChopper*/...), -1s when not owned
static int SpvChoices(int k, int cfg, int out[8]) {
    for (int i = 0; i < 8; ++i) out[i] = -1;
    int n = k == SPV_MOC ? 5 : k == SPV_AVENGER ? 5 : k == SPV_TERRORBYTE ? 6 : 7;
    if (cfg < 0) return n;
    auto b = [cfg](int at, int bits) { return (cfg >> at) & ((1 << bits) - 1); };
    switch (k) {
    case SPV_MOC: out[0] = b(0, 4); out[1] = b(4, 4); out[2] = b(8, 4); out[3] = b(12, 4); out[4] = b(16, 4); break;            // cab, module 1-3, colour
    case SPV_AVENGER: out[0] = b(3, 4); out[1] = b(0, 1); out[2] = b(1, 1); out[3] = b(2, 1); out[4] = b(7, 1); break;           // interior, turret, vehicle wks, weapon wks, terminal
    case SPV_TERRORBYTE: out[0] = b(0, 4); out[1] = b(4, 5); out[2] = b(9, 1); out[3] = b(10, 1); out[4] = b(11, 1); out[5] = b(12, 1); break;   // tint, decal, missile, drone, weapon wks, vehicle wks
    default: out[0] = b(0, 5); out[1] = b(5, 6); out[2] = b(11, 1); out[3] = b(12, 1); out[4] = b(13, 1); out[5] = b(14, 1); out[6] = b(15, 1); break;   // colour, flag, sonar, missiles, workshop, heli, mini sub
    }
    return n;
}
// MOC module types (Warstock TruckPage.MODULE_DATA): 1 empty, 2 living quarters, 3 command center, 4 weapon workshop,
// 5 vehicle workshop (modules 2+3), 6 vehicle storage; the interior's section models (am_mp_armory_truck func_7295)
static const char *MocSectionModel(int type, int slot) {
    switch (type) {
    case 2: return "gr_prop_inttruck_living_01";
    case 3: return "gr_prop_inttruck_command_01";
    case 4: return "gr_prop_inttruck_gunmod_01";
    case 5: return "gr_prop_inttruck_carmod_01";
    case 6: return "gr_prop_inttruck_vehicle_01";
    default: return slot == 1 ? "gr_prop_inttruck_empty_02" : slot == 2 ? "gr_prop_inttruck_empty_03" : "gr_prop_inttruck_empty_01";
    }
}
static const float MOC_SLOT_Y[3] = {-3014.f, -3006.f, -2998.f};   // func_374: front (by the cab) to the rear door

// ---- in the world
static int g_spvVeh[SPV_COUNT] = {}, g_spvCab = 0, g_spvBlip[SPV_COUNT] = {};
static int g_spvIn = -1;
static int g_avPilot = 0;   // the Avenger's autopilot                 // the special vehicle we are inside of
static Vec3 g_spvBack{};                  // where we got in (used when the vehicle is gone)
static float g_spvBackH = 0.f;
static std::vector<int> g_spvSwapped;    // MOC sections swapped in
static int g_spvEnterAt = 0;

// where the stations are inside (calibrated in game); kind 0 exit, 1 computer, 2 weapon workshop, 3 vehicle workshop
struct SpvStation { int k, kind; float x, y, z; const char *help; };
static const SpvStation SPV_STATIONS[] = {
    {SPV_MOC, 0, 1103.56f, -2993.4f, -40.0f, "Press ~INPUT_CONTEXT~ to leave the Mobile Operations Center."},
    {SPV_MOC, 1, 1103.56f, -3011.0f, -40.0f, "Press ~INPUT_CONTEXT~ to use the Command Center."},
    {SPV_MOC, 2, 1105.2f, 0.f, -40.0f, "Press ~INPUT_CONTEXT~ to use the Weapon Workshop."},
    {SPV_MOC, 3, 1102.0f, -2999.0f, -40.0f, "Press ~INPUT_CONTEXT~ to use the Vehicle Workshop."},
    {SPV_AVENGER, 0, 529.5f, 4750.6f, -70.0f, "Press ~INPUT_CONTEXT~ to leave the Avenger."},
    {SPV_AVENGER, 2, 517.0f, 4752.0f, -70.0f, "Press ~INPUT_CONTEXT~ to use the Weapon Workshop."},
    {SPV_AVENGER, 3, 523.5f, 4752.6f, -70.0f, "Press ~INPUT_CONTEXT~ to use the Vehicle Workshop."},
    {SPV_AVENGER, 4, 515.96f, 4751.04f, -70.0f, "Press ~INPUT_CONTEXT~ to use the turrets."},          // the turret seats (xm_prop_x17_avengerchair)
    {SPV_AVENGER, 5, 509.6f, 4750.5f, -70.0f, "Press ~INPUT_CONTEXT~ to take the controls."},          // the cockpit
    {SPV_AVENGER, 1, 513.3f, 4750.4f, -70.0f, "Press ~INPUT_CONTEXT~ to use the Avenger's operations."},
    {SPV_TERRORBYTE, 0, -1421.0f, -3016.2f, -80.0f, "Press ~INPUT_CONTEXT~ to leave the Terrorbyte."},
    {SPV_TERRORBYTE, 1, -1421.6f, -3012.6f, -80.0f, "Press ~INPUT_CONTEXT~ to use the Nerve Center."},
    {SPV_TERRORBYTE, 2, -1422.0f, -3015.2f, -80.0f, "Press ~INPUT_CONTEXT~ to use the Weapon Workshop."},
    {SPV_TERRORBYTE, 3, -1420.2f, -3014.6f, -80.0f, "Press ~INPUT_CONTEXT~ to use the Specialized Vehicle Workshop."},
    {SPV_TERRORBYTE, 4, -1419.6f, -3010.8f, -80.0f, "Press ~INPUT_CONTEXT~ to use the Turret Station."},
    {SPV_TERRORBYTE, 6, -1419.6f, -3013.0f, -80.0f, "Press ~INPUT_CONTEXT~ to use the Drone Station."},
};

static Vec3 SpvEntry(int k) {   // behind the vehicle: the MOC's trailer doors, the Terrorbyte's back doors, the Avenger's ramp
    int v = g_spvVeh[k];
    Vec3 mn{}, mx{};
    N(N_GET_MODEL_DIMENSIONS, (uint32_t)N<int>(N_GET_ENTITY_MODEL, v), &mn, &mx);
    return N<Vec3>(N_GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS, v, 0.f, mn.y - 1.2f, 0.f);
}
static void SpvForget(int k) {
    RemoveBlip(g_spvBlip[k]);
    if (Exists(g_spvVeh[k])) { N(N_SET_ENTITY_AS_MISSION_ENTITY, g_spvVeh[k], 1, 1); N(N_DELETE_VEHICLE, &g_spvVeh[k]); }
    if (k == SPV_AVENGER && Exists(g_avPilot)) N(N_DELETE_PED, &g_avPilot);
    if (k == SPV_MOC && Exists(g_spvCab)) { N(N_SET_ENTITY_AS_MISSION_ENTITY, g_spvCab, 1, 1); N(N_DELETE_VEHICLE, &g_spvCab); }
    g_spvVeh[k] = 0;
    if (k == SPV_MOC) g_spvCab = 0;
}
static int SpvSpawnModel(const char *model, const Vec3 &p, float h) {
    uint32_t m = Joaat(model);
    if (!LoadModel(m)) return 0;
    int v = N<int>(N_CREATE_VEHICLE, m, p.x, p.y, p.z, h, 0, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
    if (!v) return 0;
    N(N_SET_ENTITY_AS_MISSION_ENTITY, v, 1, 1);
    N(N_SET_VEHICLE_HAS_BEEN_OWNED_BY_PLAYER, v, 1);
    N(N_SET_VEHICLE_ON_GROUND_PROPERLY, v, 5.0f);
    N(N_SET_VEHICLE_DIRT_LEVEL, v, 0.f);
    return v;
}
static void SpvRequest(int k) {
    if (Exists(g_spvVeh[k])) { Notify(std::string("Your ~b~") + SPV[k].name + "~s~ is already nearby (see the map)."); return; }
    if (!SpvHasBase(k)) { Notify(std::string("You do not own a ") + SPV[k].needName + ". Purchase one from foreclosures.maze-bank.com"); return; }
    int me = Me();
    Vec3 mp = Pos(me), fwd = N<Vec3>(N_GET_ENTITY_FORWARD_VECTOR, me);
    float dist = k == SPV_AVENGER ? 70.f : 40.f, h = N<float>(N_GET_ENTITY_HEADING, me);
    Vec3 pos = V3(mp.x + fwd.x * dist, mp.y + fwd.y * dist, mp.z), out{};
    float oh = h;
    if (NB(N<int>(N_GET_CLOSEST_VEHICLE_NODE_WITH_HEADING, pos.x, pos.y, pos.z, &out, &oh, 1, 3.0f, 0.0f))) { pos = out; h = oh; }
    int ch[8]; SpvChoices(k, SpvConfig(k), ch);
    if (k == SPV_MOC) {
        g_spvCab = SpvSpawnModel(ch[0] == 1 ? "hauler2" : "phantom3", pos, h);
        Vec3 back = N<Vec3>(N_GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS, g_spvCab, 0.f, -9.0f, 0.f);
        g_spvVeh[k] = SpvSpawnModel("trailerlarge", back, h);
        if (g_spvCab && g_spvVeh[k]) N(N_ATTACH_VEHICLE_TO_TRAILER, g_spvCab, g_spvVeh[k], 1.0f);
    } else g_spvVeh[k] = SpvSpawnModel(SPV[k].model, pos, h);
    if (!g_spvVeh[k]) { SpvForget(k); Notify("~r~Delivery failed, try again."); return; }
    g_spvBlip[k] = AddBlip(k == SPV_MOC && g_spvCab ? g_spvCab : g_spvVeh[k], SPV[k].blip, SPV[k].blipColour, SPV[k].name);
    Notify(std::string("Your ~b~") + SPV[k].name + "~s~ has been delivered nearby.");
    Log("spv: %s requested at %.1f %.1f %.1f", SPV[k].name, pos.x, pos.y, pos.z);
}
static void SpvReturn(int k) {
    SpvForget(k);
    Notify(std::string("Your ~b~") + SPV[k].name + "~s~ has been returned to your " + SPV[k].needName + ".");
}

// The interior with what you bought: entity sets (Avenger, Terrorbyte) or section models (MOC)
static void SpvEnter(int k) {
    const InteriorDef *d = FindInterior(SPV[k].interior);
    if (!d) return;
    int ch[8]; SpvChoices(k, SpvConfig(k), ch);
    std::string sets;
    auto add = [&](const std::string &s) { if (!sets.empty()) sets += ";"; sets += s; };
    g_unsets.clear();
    if (k == SPV_AVENGER) {
        add("SHELL_TINT:" + std::to_string(std::max(0, ch[0]) + 1)); add("CONTROL_1"); add("CONTROL_2"); add("CONTROL_3");
        if (ch[2] == 1) { add("VEHICLE_MOD"); add("thruster_dock"); } else { g_unsets.push_back("VEHICLE_MOD"); g_unsets.push_back("thruster_dock"); add("seat_no_thruster"); }
        if (ch[3] == 1) add("WEAPONS_MOD"); else g_unsets.push_back("WEAPONS_MOD");
        if (ch[4] == 1) { add("touch_screen"); g_unsets.push_back("seat_no_screen"); } else { add("seat_no_screen"); g_unsets.push_back("touch_screen"); }
    } else if (k == SPV_TERRORBYTE) {
        char b[48];
        for (int i = 1; i <= 25; ++i) { std::snprintf(b, sizeof b, "Int_03_ba_Design_%02d", i); if (i != std::max(0, ch[1]) + 1) g_unsets.push_back(b); }
        std::snprintf(b, sizeof b, "Int_03_ba_Design_%02d", std::max(0, ch[1]) + 1); add(b);
        add("Int_03_ba_Tint:" + std::to_string(std::max(0, ch[0]) + 1));   // tint 0 is the debug palette add("Int_03_ba_Light_Rig1");
        if (ch[3] == 1) add("Int_03_ba_drone"); else g_unsets.push_back("Int_03_ba_drone");
        if (ch[4] == 1) add("Int_03_ba_weapons_mod"); else g_unsets.push_back("Int_03_ba_weapons_mod");
        if (ch[5] == 1) add("Int_03_ba_bikemod"); else g_unsets.push_back("Int_03_ba_bikemod");
    }
    InteriorDef dd = *d;
    dd.sets = sets.empty() ? d->sets : sets.c_str();
    if (k == SPV_MOC) {   // swap the trailer's anchors for the sections you bought (am_mp_armory_truck func_7307/func_7313)
        uint32_t anchor = Joaat("gr_prop_inttruck_anchor");
        g_spvSwapped.clear();
        for (int s = 0; s < 3; ++s) {
            int type = ch[1 + s] < 0 ? (s == 0 ? 3 : 1) : ch[1 + s];
            if (s == 2 && ch[2] == 5) continue;   // the vehicle workshop takes modules 2 and 3
            uint32_t m = Joaat(MocSectionModel(type, s));
            LoadModel(m);
            N(N_CREATE_MODEL_SWAP, 1103.562f, MOC_SLOT_Y[s], -40.f, 1.0f, anchor, m, 1);
            g_spvSwapped.push_back(s);
        }
    }
    g_spvBack = Exists(g_spvVeh[k]) ? SpvEntry(k) : Pos(Me());
    g_spvBackH = Exists(g_spvVeh[k]) ? N<float>(N_GET_ENTITY_HEADING, g_spvVeh[k]) + 180.f : N<float>(N_GET_ENTITY_HEADING, Me());
    TeleportInside(dd);
    if (k == SPV_MOC) {   // the colour: the sections' tint (func_7294)
        int tint = std::max(0, ch[4]) + 1;   // func_7280: white is tint 1, the others follow (tint 0 is the sections' debug palette)
        for (int s = 0; s < 3; ++s) {
            int type = ch[1 + s] < 0 ? (s == 0 ? 3 : 1) : ch[1 + s];
            N(N_SET_TINT_INDEX_CLOSEST_BUILDING_OF_TYPE, 1103.562f, MOC_SLOT_Y[s], -40.f, 10.0f, Joaat(MocSectionModel(type, s)), tint);
        }
    }
    g_spvIn = k;
    g_spvEnterAt = Now();
    Log("spv: inside the %s (sets %s)", SPV[k].name, dd.sets);
}
static bool AvengerFlying();
static void SpvExit() {
    int k = g_spvIn;
    if (k < 0) return;
    g_spvIn = -1;
    Vec3 p = Exists(g_spvVeh[k]) ? SpvEntry(k) : g_spvBack;
    float h = Exists(g_spvVeh[k]) ? N<float>(N_GET_ENTITY_HEADING, g_spvVeh[k]) + 180.f : g_spvBackH;
    bool air = k == SPV_AVENGER && AvengerFlying();
    if (air) {   // in flight: out of the back with a parachute
        p = N<Vec3>(N_GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS, g_spvVeh[k], 0.f, -30.f, -8.f);
        N(N_GIVE_WEAPON_TO_PED, Me(), Joaat("GADGET_PARACHUTE"), 1, 0, 1);
    }
    N(N_DO_SCREEN_FADE_OUT, 400);
    Wait(450);
    if (air) {   // exactly there: Teleport() would snap to the ground below
        N(N_SET_ENTITY_COORDS, Me(), p.x, p.y, p.z, 0, 0, 0, 0);
        N(N_SET_ENTITY_HEADING, Me(), h);
    } else Teleport(p.x, p.y, p.z + 0.5f, h);
    if (k == SPV_MOC) {
        uint32_t anchor = Joaat("gr_prop_inttruck_anchor");
        for (int s : g_spvSwapped) N(N_REMOVE_MODEL_SWAP, 1103.562f, MOC_SLOT_Y[s], -40.f, 1.0f, anchor, 0u, 0);
        g_spvSwapped.clear();
    }
    N(N_DO_SCREEN_FADE_IN, 500);
    Log("spv: left the %s", SPV[k].name);
}

// ---------------------------------------------------------------- Turrets, autopilot and the drone (used from inside)
// GTA Online runs these as turret_cam_script / am_mp_drone: a scripted camera on the vehicle (the streaming
// focus follows it), the vehicle's weapon fired from the turret bone. The turrets are the models' own
// (avenger turret_1/2/3base + weapon_Na, terbyte turret_1base + weapon_1a/1b).
struct TurretDef { int spv; const char *bone, *muzzle; const char *weapon; int damage, cooldown; float up; };
static const TurretDef TURRETS[] = {
    {SPV_AVENGER, "turret_1base", "weapon_1a", "VEHICLE_WEAPON_AVENGER_CANNON", 250, 120, -1.2f},   // under the nose
    {SPV_AVENGER, "turret_2base", "weapon_2a", "VEHICLE_WEAPON_AVENGER_CANNON", 250, 120, 1.2f},    // on top
    {SPV_AVENGER, "turret_3base", "weapon_3a", "VEHICLE_WEAPON_AVENGER_CANNON", 250, 120, 1.0f},    // the tail
    {SPV_TERRORBYTE, "turret_1base", "weapon_1a", "VEHICLE_WEAPON_HACKER_MISSILE", 600, 900, 1.4f}, // the missile battery
};
static const int NTURRETS = (int)(sizeof TURRETS / sizeof *TURRETS);
static int g_turCam = 0, g_turVeh = 0, g_turIdx = -1, g_turNext = 0, g_turSide = 0;
static float g_turYaw = 0.f, g_turPitch = 0.f;
static Vec3 CamDir(float pitch, float yaw) {
    float p = pitch * 0.0174533f, y = yaw * 0.0174533f;
    return V3(-std::sin(y) * std::cos(p), std::cos(y) * std::cos(p), std::sin(p));
}
static void RemoteCamOn(int ent) {
    g_turCam = N<int>(N_CREATE_CAM, "DEFAULT_SCRIPTED_CAMERA", 1);
    N(N_SET_CAM_FOV, g_turCam, 60.f);
    N(N_RENDER_SCRIPT_CAMS, 1, 0, 0, 1, 0, 0);
    N(N_SET_FOCUS_ENTITY, ent);
}
static void RemoteCamOff() {
    if (g_turCam) { N(N_RENDER_SCRIPT_CAMS, 0, 0, 0, 1, 0, 0); N(N_DESTROY_CAM, g_turCam, 0); }
    g_turCam = 0;
    N(N_CLEAR_FOCUS);
    N(N_CLEAR_TIMECYCLE_MODIFIER);
}
static void TurretStart(int spv) {
    int v = g_spvVeh[spv];
    if (!Exists(v)) { Notify(std::string("Request your ~b~") + SPV[spv].name + "~s~ first (Services)."); return; }
    g_turIdx = -1;
    for (int i = 0; i < NTURRETS; ++i) if (TURRETS[i].spv == spv) { g_turIdx = i; break; }
    if (g_turIdx < 0) return;
    g_turVeh = v;
    g_turYaw = N<float>(N_GET_ENTITY_HEADING, v); g_turPitch = -10.f;
    RemoteCamOn(v);
    Log("turret: %s %s", SPV[spv].name, TURRETS[g_turIdx].bone);
}
static void TurretStop() { RemoteCamOff(); g_turVeh = 0; g_turIdx = -1; }
static void Crosshair() {
    N(N_DRAW_RECT, 0.5f, 0.5f, 0.012f, 0.0025f, 255, 255, 255, 200, 0);
    N(N_DRAW_RECT, 0.5f, 0.5f, 0.0014f, 0.022f, 255, 255, 255, 200, 0);
}
static void TurretTick() {
    if (g_turIdx < 0) return;
    const TurretDef &t = TURRETS[g_turIdx];
    if (!Exists(g_turVeh) || Dead(g_turVeh)) { TurretStop(); return; }
    N(N_DISABLE_ALL_CONTROL_ACTIONS, 0);
    g_turYaw -= N<float>(N_GET_DISABLED_CONTROL_NORMAL, 0, 1) * 5.f;
    g_turPitch -= N<float>(N_GET_DISABLED_CONTROL_NORMAL, 0, 2) * 4.f;
    g_turPitch = std::max(-85.f, std::min(60.f, g_turPitch));
    int b = N<int>(N_GET_ENTITY_BONE_INDEX_BY_NAME, g_turVeh, t.bone);
    Vec3 base = b >= 0 ? N<Vec3>(N_GET_WORLD_POSITION_OF_ENTITY_BONE, g_turVeh, b) : Pos(g_turVeh);
    base.z += t.up;
    N(N_SET_CAM_COORD, g_turCam, base.x, base.y, base.z);
    N(N_SET_CAM_ROT, g_turCam, g_turPitch, 0.f, g_turYaw, 2);
    Crosshair();
    if (NB(N<int>(N_IS_DISABLED_CONTROL_PRESSED, 0, 24)) && Now() >= g_turNext) {
        Vec3 d = CamDir(g_turPitch, g_turYaw);
        int mb = N<int>(N_GET_ENTITY_BONE_INDEX_BY_NAME, g_turVeh, t.spv == SPV_TERRORBYTE && (g_turSide++ & 1) ? "weapon_1b" : t.muzzle);
        Vec3 from = mb >= 0 ? N<Vec3>(N_GET_WORLD_POSITION_OF_ENTITY_BONE, g_turVeh, mb) : base;
        from = V3(from.x + d.x * 2.f, from.y + d.y * 2.f, from.z + d.z * 2.f);
        Vec3 to = V3(base.x + d.x * 800.f, base.y + d.y * 800.f, base.z + d.z * 800.f);
        N(N_SHOOT_SINGLE_BULLET_BETWEEN_COORDS_IGNORE_ENTITY, from.x, from.y, from.z, to.x, to.y, to.z, t.damage, 1, Joaat(t.weapon), Me(), 1, 0, -1.f, g_turVeh, 0);
        g_turNext = Now() + t.cooldown;
    }
    // the other turrets (Avenger): LB / RB or the mouse wheel
    bool next = NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 2, 206)) || NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 0, 14));
    bool prev = NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 2, 205)) || NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 0, 15));
    if (next || prev) {
        int k = g_turIdx;
        for (int n = 0; n < NTURRETS; ++n) { k = (k + (next ? 1 : NTURRETS - 1)) % NTURRETS; if (TURRETS[k].spv == t.spv) break; }
        g_turIdx = k;
    }
    Help(t.spv == SPV_AVENGER ? "~INPUT_ATTACK~ Fire~n~~INPUT_FRONTEND_LB~ ~INPUT_FRONTEND_RB~ Change turret~n~~INPUT_FRONTEND_CANCEL~ Exit"
                              : "~INPUT_ATTACK~ Fire missiles~n~~INPUT_FRONTEND_CANCEL~ Exit");
    if (NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 2, 202))) TurretStop();
}

// the Avenger's autopilot: a hidden pilot keeps it circling where it is while you are in the hold
static bool AvengerFlying() {
    int v = g_spvVeh[SPV_AVENGER];
    return Exists(v) && NB(N<int>(N_IS_ENTITY_IN_AIR, v)) && N<float>(N_GET_ENTITY_HEIGHT_ABOVE_GROUND, v) > 15.f;
}
static void AvengerAutopilot() {
    int v = g_spvVeh[SPV_AVENGER], me = Me();
    if (!Exists(v) || MyVehicle() != v) { Notify("Fly your ~b~Avenger~s~ to use the autopilot."); return; }
    if (!AvengerFlying()) { Notify("The autopilot only works in the air."); return; }
    uint32_t m = Joaat("s_m_y_pilot_01");
    if (!LoadModel(m)) return;
    Vec3 p = Pos(v);
    N(N_CLEAR_PED_TASKS_IMMEDIATELY, me);
    g_avPilot = N<int>(N_CREATE_PED_INSIDE_VEHICLE, v, 26, m, -1, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
    if (!g_avPilot) { N(N_SET_PED_INTO_VEHICLE, me, v, -1); return; }
    N(N_SET_ENTITY_VISIBLE, g_avPilot, 0, 0);
    N(N_SET_BLOCKING_OF_NON_TEMPORARY_EVENTS, g_avPilot, 1);
    N(N_SET_ENTITY_INVINCIBLE, g_avPilot, 1);
    N(N_TASK_PLANE_MISSION, g_avPilot, v, 0, 0, p.x, p.y, p.z, 9, 35.f, 0.f, N<float>(N_GET_ENTITY_HEADING, v), p.z + 10.f, p.z - 10.f, 1);
    Notify("~b~Autopilot~s~ engaged. Use the cockpit to take the controls back.");
    SpvEnter(SPV_AVENGER);
}
static void AvengerTakeControls() {
    int v = g_spvVeh[SPV_AVENGER];
    if (!Exists(v)) return;
    g_spvIn = -1;
    N(N_DO_SCREEN_FADE_OUT, 300); Wait(350);
    if (Exists(g_avPilot)) { N(N_DELETE_PED, &g_avPilot); g_avPilot = 0; }
    N(N_SET_PED_INTO_VEHICLE, Me(), v, -1);
    N(N_DO_SCREEN_FADE_IN, 400);
}

// the Terrorbyte's drone (am_mp_drone): fly it around the truck, stun with it
static int g_drone = 0, g_droneNext = 0;
static float g_dYaw = 0.f, g_dPitch = 0.f;
static void DroneStart() {
    int t = g_spvVeh[SPV_TERRORBYTE];
    if (!Exists(t)) { Notify("Request your ~b~Terrorbyte~s~ first (Services)."); return; }
    uint32_t m = Joaat("ba_prop_battle_drone_quad");
    if (!LoadModel(m)) return;
    Vec3 p = N<Vec3>(N_GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS, t, 0.f, -2.f, 4.f);
    g_drone = N<int>(N_CREATE_OBJECT_NO_OFFSET, m, p.x, p.y, p.z, 0, 0, 0, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
    if (!g_drone) return;
    N(N_SET_ENTITY_COLLISION, g_drone, 0, 0);
    N(N_FREEZE_ENTITY_POSITION, g_drone, 1);
    g_dYaw = N<float>(N_GET_ENTITY_HEADING, t); g_dPitch = -15.f;
    RemoteCamOn(g_drone);
    N(N_SET_TIMECYCLE_MODIFIER, "eyeINtheSKY");
    Log("drone: launched");
}
static void DroneStop() {
    RemoteCamOff();
    if (Exists(g_drone)) N(N_DELETE_OBJECT, &g_drone);
    g_drone = 0;
}
static void DroneTick() {
    if (!g_drone) return;
    int t = g_spvVeh[SPV_TERRORBYTE];
    if (!Exists(g_drone) || !Exists(t)) { DroneStop(); return; }
    N(N_DISABLE_ALL_CONTROL_ACTIONS, 0);
    float dt = std::min(0.1f, N<float>(N_GET_FRAME_TIME));
    g_dYaw -= N<float>(N_GET_DISABLED_CONTROL_NORMAL, 0, 1) * 5.f;
    g_dPitch = std::max(-89.f, std::min(30.f, g_dPitch - N<float>(N_GET_DISABLED_CONTROL_NORMAL, 0, 2) * 4.f));
    float speed = NB(N<int>(N_IS_DISABLED_CONTROL_PRESSED, 0, 21)) ? 25.f : 10.f;   // Sprint: boost
    float fw = -N<float>(N_GET_DISABLED_CONTROL_NORMAL, 0, 31), side = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 0, 30);
    float up = (NB(N<int>(N_IS_DISABLED_CONTROL_PRESSED, 0, 22)) ? 1.f : 0.f) - (NB(N<int>(N_IS_DISABLED_CONTROL_PRESSED, 0, 36)) ? 1.f : 0.f);
    float y = g_dYaw * 0.0174533f;
    Vec3 p = Pos(g_drone);
    p.x += (-std::sin(y) * fw + std::cos(y) * side) * speed * dt;
    p.y += (std::cos(y) * fw + std::sin(y) * side) * speed * dt;
    p.z += up * speed * dt;
    float g = GroundAt(p.x, p.y, p.z + 2.f, p.z - 50.f);
    if (p.z < g + 0.6f) p.z = g + 0.6f;
    N(N_SET_ENTITY_COORDS_NO_OFFSET, g_drone, p.x, p.y, p.z, 0, 0, 0);
    N(N_SET_ENTITY_HEADING, g_drone, g_dYaw);
    N(N_SET_CAM_COORD, g_turCam, p.x, p.y, p.z - 0.15f);
    N(N_SET_CAM_ROT, g_turCam, g_dPitch, 0.f, g_dYaw, 2);
    Crosshair();
    float range = Dist(p, Pos(t));
    if (range > 600.f) { Notify("The Drone lost its connection with the Terrorbyte."); DroneStop(); return; }
    if (range > 500.f) Help("The Drone is losing connection. It must remain in proximity to the Terrorbyte in order to receive a signal.");
    else Help("~INPUT_ATTACK~ Stun~n~~INPUT_JUMP~ / ~INPUT_DUCK~ Up / Down~n~~INPUT_SPRINT~ Boost~n~~INPUT_FRONTEND_CANCEL~ Exit");
    if (NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 0, 24)) && Now() >= g_droneNext) {   // the shock: a stun-gun shot from the drone
        Vec3 d = CamDir(g_dPitch, g_dYaw);
        N(N_SHOOT_SINGLE_BULLET_BETWEEN_COORDS_IGNORE_ENTITY, p.x + d.x, p.y + d.y, p.z + d.z, p.x + d.x * 60.f, p.y + d.y * 60.f, p.z + d.z * 60.f, 1, 1, Joaat("WEAPON_STUNGUN"), Me(), 1, 0, -1.f, g_drone, 0);
        g_droneNext = Now() + 3000;
    }
    if (NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 2, 202))) DroneStop();
}
static void RemoteTick() { TurretTick(); DroneTick(); }

static int MocSlotOf(const int *ch, int type) { for (int m = 0; m < 3; ++m) if (ch[1 + m] == type) return m; return -1; }
static bool SpvStationOn(const SpvStation &st) {
    int ch[8]; SpvChoices(st.k, SpvConfig(st.k), ch);
    switch (st.kind) {
    case 1: return st.k == SPV_MOC ? ch[1] == 3 : true;   // the MOC's Command Center is a module
    case 2: return st.k == SPV_MOC ? MocSlotOf(ch, 4) >= 0 || ch[2] == 5 : st.k == SPV_AVENGER ? ch[3] == 1 : ch[4] == 1;   // the MOC's double bay is "Weapon & Vehicle Workshop"
    case 3: return st.k == SPV_MOC ? ch[2] == 5 : st.k == SPV_AVENGER ? ch[2] == 1 : ch[5] == 1;
    case 4: return st.k == SPV_AVENGER ? ch[1] == 1 : ch[2] == 1;   // the turrets / the missile battery
    case 5: return Exists(g_spvVeh[SPV_AVENGER]);
    case 6: return ch[3] == 1;                                       // the drone station
    default: return true;
    }
}
static Vec3 SpvStationPos(const SpvStation &st) {
    if (st.k == SPV_MOC && st.kind == 2) {
        int ch[8]; SpvChoices(st.k, SpvConfig(st.k), ch);
        if (ch[2] == 5) return V3(st.x, -3001.0f, st.z);   // the bench of the double bay
        return V3(st.x, MOC_SLOT_Y[std::max(0, MocSlotOf(ch, 4))], st.z);
    }
    return V3(st.x, st.y, st.z);
}
static void SpvStationUse(const SpvStation &st) {
    switch (st.kind) {
    case 0: SpvExit(); break;
    case 1: OpenMenuAt(st.k == SPV_AVENGER ? M_HEISTS : M_BIZ); break;
    case 2: g_menuOpen = true; g_stack.clear(); Open((MenuId)SM_WWS); break;
    case 3: g_menuOpen = true; g_stack.clear(); Open((MenuId)SM_VWS); break;
    case 4: TurretStart(st.k); break;
    case 5: AvengerTakeControls(); break;
    case 6: DroneStart(); break;
    }
}
static void SpvWorkshopFinish();
static void WeaponModsTick();
static void BallisticTick();
static void YachtTick();
static void YachtServiceItems(std::vector<Item> &items);
static int g_wsVeh = 0;
static int g_lscAt = -1, g_wsValue = 0;   // Shops.inc: the vehicle is at LS Customs (a shop index), its catalogue value
static void LscFinish();
static void LscItems(std::vector<Item> &items);
static void SpecialVehiclesTick() {
    if (!YouActive()) return;
    WeaponModsTick();
    BallisticTick();
    YachtTick();
    if (g_turCam) { RemoteTick(); return; }
    if (g_wsVeh && !g_menuOpen) SpvWorkshopFinish();   // left the workshop menu with Back
    // the vehicles you requested
    for (int k = 0; k < SPV_COUNT; ++k) {
        if (!g_spvVeh[k]) continue;
        if (!Exists(g_spvVeh[k]) || Dead(g_spvVeh[k])) { SpvForget(k); Notify(std::string("Your ~b~") + SPV[k].name + "~s~ was destroyed. Request it again from Services."); continue; }
        if (g_spvIn >= 0 || g_menuOpen || InAnyVehicle() || k == SPV_KOSATKA) continue;
        Vec3 e = SpvEntry(k), me = Pos(Me());
        if (Dist(e, me) < 2.5f) {
            Help(std::string("Press ~INPUT_CONTEXT~ to enter the ") + SPV[k].name + ".");
            if (JustPressed(51)) { SpvEnter(k); return; }
        }
    }
    if (g_spvIn < 0) return;
    const InteriorDef *d = FindInterior(SPV[g_spvIn].interior);
    Vec3 me = Pos(Me());
    if (!d || Dist(me, V3(d->x, d->y, d->z)) > 60.f) { g_spvIn = -1; return; }   // teleported away / died
    if (g_menuOpen || Now() - g_spvEnterAt < 1000) return;
    const SpvStation *best = nullptr;   // the nearest one (the Terrorbyte's are close together)
    float bd = 1.4f;
    for (const SpvStation &st : SPV_STATIONS) {
        if (st.k != g_spvIn || !SpvStationOn(st)) continue;
        Vec3 sp = SpvStationPos(st);
        float d = Dist(me, V3(sp.x, sp.y, sp.z + 1.0f));
        if (d < bd) { bd = d; best = &st; }
    }
    if (best) {
        Help(best->help);
        if (JustPressed(51)) SpvStationUse(*best);
    }
}

// ---------------------------------------------------------------- Ballistic Equipment
// Bought on Warstock (catalogue item 324, $500,000; Gori Online asks for a Bunker where GTA Online asks
// for the research unlock), requested from Services: Merryweather drops a crate, you put it on.
// The suit is freemode's own (func_19799/func_19791/func_19792): the DLC_MP_GR_*_BERD_1_25 mask-slot armour,
// head prop 39 (38 female), 2600 health, no critical hits, no cover or vehicles, the Minigun (tint 7) and
// the ballistic movement. Taking it off (or dying) gives everything back.
static const int BALLISTIC_PRICE = 500000, BALLISTIC_HEALTH = 2600, BALLISTIC_COOLDOWN = 300000;
static const char *BAL_OWNED = "@Ballistic|owned", *BAL_WORN = "@Ballistic|worn";
static int g_balCrate = 0, g_balBlip = 0, g_balNextAt = 0;
static struct { bool on = false; int berdD = 0, berdT = 0, propD = -1, propT = 0, maxHealth = 200; } g_bal;
static bool BallisticOwned() { return g_cust.count(BAL_OWNED) > 0; }
static bool BallisticBuy(int price) {
    if (BallisticOwned()) { Notify("You already own the ~b~Ballistic Equipment~s~."); return false; }
    if (!SpvHasBase(SPV_MOC)) { Notify("You need a ~b~Bunker~s~ to buy the Ballistic Equipment."); return false; }
    PayWhat what("Ballistic Equipment");
    bool ok = Pay(price);
    if (ok) { g_cust[BAL_OWNED] = 1; Save(); Notify("~b~Ballistic Equipment~s~ purchased. Request it from Services."); }
    return ok;
}
static void BallisticOn() {
    int me = Me();
    if (InAnyVehicle()) { Notify("Get out of the vehicle to put on the Ballistic Equipment."); return; }
    bool female = (uint32_t)N<int>(N_GET_ENTITY_MODEL, me) == Joaat("mp_f_freemode_01");
    g_bal.berdD = N<int>(N_GET_PED_DRAWABLE_VARIATION, me, 1); g_bal.berdT = N<int>(N_GET_PED_TEXTURE_VARIATION, me, 1);
    g_bal.propD = N<int>(N_GET_PED_PROP_INDEX, me, 0, 0); g_bal.propT = N<int>(N_GET_PED_PROP_TEXTURE_INDEX, me, 0);
    g_bal.maxHealth = N<int>(N_GET_PED_MAX_HEALTH, me);
    struct { int64_t s[9]; char label[64]; char pad[64]; } comp{};
    uint32_t suit = Joaat(female ? "DLC_MP_GR_F_BERD_1_25" : "DLC_MP_GR_M_BERD_1_25");
    bool put = false;
    if (NB(N<int>(N_GET_SHOP_PED_COMPONENT, suit, &comp))) { N(N_SET_PED_COMPONENT_VARIATION, me, 1, (int)comp.s[3], (int)comp.s[4], 0); put = true; }
    else {   // story mode has no shop data: find the drawable by its hash name (like func_898 does for props)
        int nd = N<int>(N_GET_NUMBER_OF_PED_DRAWABLE_VARIATIONS, me, 1);
        for (int d = nd - 1; d >= 0 && !put; --d)
            for (int t = 0; t < N<int>(N_GET_NUMBER_OF_PED_TEXTURE_VARIATIONS, me, 1, d) && !put; ++t)
                if ((uint32_t)N<int>(N_GET_HASH_NAME_FOR_COMPONENT, me, 1, d, t) == suit) { N(N_SET_PED_COMPONENT_VARIATION, me, 1, d, t, 0); put = true; Log("ballistic: suit is berd %d/%d", d, t); }
    }
    if (!put) Log("ballistic: the suit's item was not found");
    N(N_SET_PED_PROP_INDEX, me, 0, female ? 38 : 39, 0, 1, 0);
    N(N_SET_PED_MAX_HEALTH, me, BALLISTIC_HEALTH);
    N(N_SET_ENTITY_HEALTH, me, BALLISTIC_HEALTH, 0, 0);
    N(N_SET_PED_SUFFERS_CRITICAL_HITS, me, 0);
    N(N_SET_PED_CONFIG_FLAG, me, 410, 1); N(N_SET_PED_CONFIG_FLAG, me, 108, 1);
    N(N_SET_PLAYER_MAX_EXPLOSIVE_DAMAGE, Player(), (float)(BALLISTIC_HEALTH / 5));
    N(N_SET_PLAYER_CAN_USE_COVER, Player(), 0);
    uint32_t mg = Joaat("WEAPON_MINIGUN");
    if (!NB(N<int>(N_HAS_PED_GOT_WEAPON, me, mg, 0))) g_cust[BAL_WORN] = 2; else g_cust[BAL_WORN] = 1;   // 2: the Minigun comes with the suit
    N(N_GIVE_WEAPON_TO_PED, me, mg, 9999, 0, 1);
    N(N_SET_PED_WEAPON_TINT_INDEX, me, mg, 7);
    N(N_REQUEST_CLIP_SET, "ANIM_GROUP_MOVE_BALLISTIC"); N(N_REQUEST_CLIP_SET, "MOVE_STRAFE_BALLISTIC");
    for (int i = 0; i < 40 && !(NB(N<int>(N_HAS_CLIP_SET_LOADED, "ANIM_GROUP_MOVE_BALLISTIC")) && NB(N<int>(N_HAS_CLIP_SET_LOADED, "MOVE_STRAFE_BALLISTIC"))); ++i) Wait(50);
    N(N_SET_PED_MOVEMENT_CLIPSET, me, "ANIM_GROUP_MOVE_BALLISTIC", 0.25f);
    N(N_SET_PED_STRAFE_CLIPSET, me, "MOVE_STRAFE_BALLISTIC");
    N(N_SET_WEAPON_ANIMATION_OVERRIDE, me, Joaat("Ballistic"));
    Sound("Armour_On", "DLC_GR_Steal_Miniguns_Sounds");
    g_bal.on = true;
    Save();
    Log("ballistic: on");
}
static void BallisticOff(bool dead) {
    int me = Me();
    if (!dead) {
        N(N_SET_PED_COMPONENT_VARIATION, me, 1, g_bal.berdD, g_bal.berdT, 0);
        if (g_bal.propD >= 0) N(N_SET_PED_PROP_INDEX, me, 0, g_bal.propD, g_bal.propT, 1, 0); else N(N_CLEAR_PED_PROP, me, 0, 0);
        N(N_SET_PED_MAX_HEALTH, me, g_bal.maxHealth);
        N(N_SET_ENTITY_HEALTH, me, g_bal.maxHealth, 0, 0);
        N(N_RESET_PED_MOVEMENT_CLIPSET, me, 0.25f);
        N(N_RESET_PED_STRAFE_CLIPSET, me);
        N(N_SET_WEAPON_ANIMATION_OVERRIDE, me, Joaat("DEFAULT"));
        N(N_SET_PED_SUFFERS_CRITICAL_HITS, me, 1);
        N(N_SET_PED_CONFIG_FLAG, me, 410, 0); N(N_SET_PED_CONFIG_FLAG, me, 108, 0);
    }
    N(N_SET_PLAYER_MAX_EXPLOSIVE_DAMAGE, Player(), -1.0f);
    N(N_SET_PLAYER_CAN_USE_COVER, Player(), 1);
    if (g_cust.count(BAL_WORN) && g_cust[BAL_WORN] == 2) N(N_REMOVE_WEAPON_FROM_PED, me, Joaat("WEAPON_MINIGUN"));
    g_cust.erase(BAL_WORN);
    g_bal.on = false;
    Save();
    Log("ballistic: off%s", dead ? " (died)" : "");
}
static void BallisticRequest() {
    if (g_bal.on) { Notify("You are already wearing the ~b~Ballistic Equipment~s~."); return; }
    if (Exists(g_balCrate)) { Notify("Your ~b~Ballistic Equipment~s~ is waiting nearby (see the map)."); return; }
    if (Now() < g_balNextAt) { Notify("Merryweather: \"The next drop will be ready in " + std::to_string((g_balNextAt - Now()) / 60000 + 1) + " minutes.\""); return; }
    int me = Me();
    Vec3 p = Pos(me), f = N<Vec3>(N_GET_ENTITY_FORWARD_VECTOR, me);
    Vec3 at = V3(p.x + f.x * 6.f, p.y + f.y * 6.f, p.z);
    at.z = GroundAt(at.x, at.y, p.z + 3.f, p.z - 1.f);
    uint32_t m = Joaat("prop_mb_crate_01a");
    if (!LoadModel(m)) { m = Joaat("prop_box_ammo04a"); LoadModel(m); }
    g_balCrate = N<int>(N_CREATE_OBJECT, m, at.x, at.y, at.z, 0, 1, 0);
    N(N_SET_MODEL_AS_NO_LONGER_NEEDED, m);
    if (!g_balCrate) { Notify("~r~The drop failed, try again."); return; }
    N(N_PLACE_OBJECT_ON_GROUND_PROPERLY, g_balCrate);
    N(N_SET_ENTITY_AS_MISSION_ENTITY, g_balCrate, 1, 1);
    g_balBlip = AddBlip(g_balCrate, 478, 2, "Ballistic Equipment");
    g_balNextAt = Now() + BALLISTIC_COOLDOWN;
    Notify("Merryweather: \"Your ~b~Ballistic Equipment~s~ has been dropped nearby.\"");
}
static void BallisticTick() {
    int me = Me();
    if (!g_bal.on && g_cust.count(BAL_WORN)) {   // reloaded while wearing it: the Minigun that came with it goes
        if (g_cust[BAL_WORN] == 2) N(N_REMOVE_WEAPON_FROM_PED, me, Joaat("WEAPON_MINIGUN"));
        g_cust.erase(BAL_WORN); Save();
    }
    if (Exists(g_balCrate)) {
        Vec3 c = Pos(g_balCrate);
        if (!InAnyVehicle() && !g_menuOpen && Dist(c, Pos(me)) < 2.2f) {
            Help("Press ~INPUT_CONTEXT~ to put on the Ballistic Equipment.");
            if (JustPressed(51)) {
                RemoveBlip(g_balBlip);
                N(N_DELETE_OBJECT, &g_balCrate); g_balCrate = 0;
                BallisticOn();
            }
        }
    }
    if (!g_bal.on) return;
    if (Dead(me)) { BallisticOff(true); return; }
    N(N_SET_PLAYER_MAY_NOT_ENTER_ANY_VEHICLE, Player());
    N(N_DISABLE_CONTROL_ACTION, 0, 37, 1);   // INPUT_SELECT_WEAPON: the Minigun only
    uint32_t mg = Joaat("WEAPON_MINIGUN"), cur = 0;
    N(N_GET_CURRENT_PED_WEAPON, me, &cur, 1);
    if (cur != mg) N(N_SET_CURRENT_PED_WEAPON, me, mg, 1);
    N(N_SET_PED_RESET_FLAG, me, 446, 1); N(N_SET_PED_RESET_FLAG, me, 417, 1); N(N_SET_PED_RESET_FLAG, me, 46, 1);
    if (N<float>(N_GET_ENTITY_SUBMERGED_LEVEL, me) >= 0.8f) N(N_SET_ENTITY_HEALTH, me, 0, 0, 0);   // it sinks, like in GTA Online
}

// Interaction menu > Services
static void SpvServiceItems(std::vector<Item> &items) {
    for (int k = 0; k < SPV_KOSATKA; ++k) {
        if (!SpvOwned(k)) continue;
        if (!Exists(g_spvVeh[k]))
            items.push_back({std::string("Request ") + SPV[k].name, "", std::string("Request your ") + SPV[k].name + " to be delivered.", [k] { CloseMenu(); SpvRequest(k); }});
        else
            items.push_back({std::string("Return ") + SPV[k].name, "", std::string("Return your ") + SPV[k].name + " to the " + SPV[k].needName + ".", [k] { CloseMenu(); SpvReturn(k); }});
    }
    if (BallisticOwned()) {
        if (g_bal.on) items.push_back({"Remove Ballistic Equipment", "", "Take the Ballistic Equipment off.", [] { CloseMenu(); BallisticOff(false); }});
        else items.push_back({"Request Ballistic Equipment", "", "Merryweather drops your Ballistic Equipment nearby.", [] { CloseMenu(); BallisticRequest(); }});
    }
    if (Exists(g_spvVeh[SPV_AVENGER]) && MyVehicle() == g_spvVeh[SPV_AVENGER])
        items.push_back({"Avenger: Autopilot", "", "The Avenger keeps flying by itself while you go to the hold.", [] { CloseMenu(); AvengerAutopilot(); }});
    YachtServiceItems(items);
    if (g_spvIn >= 0) items.push_back({std::string("Leave the ") + SPV[g_spvIn].name, "", "", [] { CloseMenu(); SpvExit(); }});
}

// ---------------------------------------------------------------- Weapon Workshop (MOC / Avenger / Terrorbyte)
// GTA Online's: Mk II upgrades, tints, attachments. Base-game weapons are priced like gunclub_shop in
// multiplayer (weapon_mods.h); DLC weapons (the Mk II ones too) by the game's own DLC component data.
// What you buy is kept in g_cust ("@wt|<weapon>" tint, "@wc|<weapon>|<component>") and put back on the
// weapons whenever they are given again (load, death).
#include "weapon_mods.h"
static const char *WS_GUNS[] = {"WEAPON_PISTOL", "WEAPON_PISTOL_MK2", "WEAPON_COMBATPISTOL", "WEAPON_APPISTOL", "WEAPON_PISTOL50",
    "WEAPON_SNSPISTOL", "WEAPON_SNSPISTOL_MK2", "WEAPON_HEAVYPISTOL", "WEAPON_VINTAGEPISTOL", "WEAPON_REVOLVER", "WEAPON_REVOLVER_MK2",
    "WEAPON_CERAMICPISTOL", "WEAPON_MICROSMG", "WEAPON_SMG", "WEAPON_SMG_MK2", "WEAPON_ASSAULTSMG", "WEAPON_COMBATPDW", "WEAPON_MACHINEPISTOL",
    "WEAPON_MINISMG", "WEAPON_GUSENBERG", "WEAPON_PUMPSHOTGUN", "WEAPON_PUMPSHOTGUN_MK2", "WEAPON_SAWNOFFSHOTGUN", "WEAPON_ASSAULTSHOTGUN",
    "WEAPON_BULLPUPSHOTGUN", "WEAPON_HEAVYSHOTGUN", "WEAPON_COMBATSHOTGUN", "WEAPON_ASSAULTRIFLE", "WEAPON_ASSAULTRIFLE_MK2",
    "WEAPON_CARBINERIFLE", "WEAPON_CARBINERIFLE_MK2", "WEAPON_ADVANCEDRIFLE", "WEAPON_SPECIALCARBINE", "WEAPON_SPECIALCARBINE_MK2",
    "WEAPON_BULLPUPRIFLE", "WEAPON_BULLPUPRIFLE_MK2", "WEAPON_COMPACTRIFLE", "WEAPON_MILITARYRIFLE", "WEAPON_HEAVYRIFLE", "WEAPON_TACTICALRIFLE",
    "WEAPON_MG", "WEAPON_COMBATMG", "WEAPON_COMBATMG_MK2", "WEAPON_SNIPERRIFLE", "WEAPON_HEAVYSNIPER", "WEAPON_HEAVYSNIPER_MK2",
    "WEAPON_MARKSMANRIFLE", "WEAPON_MARKSMANRIFLE_MK2", "WEAPON_PRECISIONRIFLE", "WEAPON_GRENADELAUNCHER", "WEAPON_RPG", "WEAPON_MINIGUN",
    "WEAPON_KNUCKLE", "WEAPON_SWITCHBLADE"};
static const char *WS_MK2[][2] = {{"WEAPON_PISTOL", "WEAPON_PISTOL_MK2"}, {"WEAPON_SNSPISTOL", "WEAPON_SNSPISTOL_MK2"},
    {"WEAPON_REVOLVER", "WEAPON_REVOLVER_MK2"}, {"WEAPON_SMG", "WEAPON_SMG_MK2"}, {"WEAPON_PUMPSHOTGUN", "WEAPON_PUMPSHOTGUN_MK2"},
    {"WEAPON_ASSAULTRIFLE", "WEAPON_ASSAULTRIFLE_MK2"}, {"WEAPON_CARBINERIFLE", "WEAPON_CARBINERIFLE_MK2"},
    {"WEAPON_SPECIALCARBINE", "WEAPON_SPECIALCARBINE_MK2"}, {"WEAPON_BULLPUPRIFLE", "WEAPON_BULLPUPRIFLE_MK2"},
    {"WEAPON_COMBATMG", "WEAPON_COMBATMG_MK2"}, {"WEAPON_HEAVYSNIPER", "WEAPON_HEAVYSNIPER_MK2"}, {"WEAPON_MARKSMANRIFLE", "WEAPON_MARKSMANRIFLE_MK2"}};
static const int WS_TINT_PRICE[8] = {500, 5250, 10000, 7500, 5000, 5750, 5500, 12500};   // WM_TINT0-7 in multiplayer
struct WsMod { uint32_t comp; int price; std::string name; };
struct DlcWeapon { int64_t s[7]; char label[4][64]; };
struct DlcComponent { int64_t s[6]; char label[2][64]; };
static int DlcWeaponIndex(uint32_t w, DlcWeapon *out = nullptr) {
    int n = N<int>(N_GET_NUM_DLC_WEAPONS);
    for (int i = 0; i < n; ++i) {
        DlcWeapon d{};
        if (NB(N<int>(N_GET_DLC_WEAPON_DATA, i, &d)) && (uint32_t)d.s[1] == w) { if (out) *out = d; return i; }
    }
    return -1;
}
static std::string LabelOr(const char *lbl, const std::string &fallback) { std::string t = Label(lbl); return t.empty() ? fallback : t; }
static std::vector<WsMod> WeaponModsOf(uint32_t w, const char *name) {
    std::vector<WsMod> out;
    for (int i = 0; i < NWEAPON_MODS; ++i)
        if (!std::strcmp(WEAPON_MODS[i].weapon, name) && WEAPON_MODS[i].price >= 0)
            out.push_back({Joaat(WEAPON_MODS[i].component), WEAPON_MODS[i].price, LabelOr(WEAPON_MODS[i].label, WEAPON_MODS[i].component)});
    if (!out.empty()) return out;
    int di = DlcWeaponIndex(w);
    if (di < 0) return out;
    int n = N<int>(N_GET_NUM_DLC_WEAPON_COMPONENTS, di);
    for (int j = 0; j < n; ++j) {
        DlcComponent c{};
        if (!NB(N<int>(N_GET_DLC_WEAPON_COMPONENT_DATA, di, j, &c))) continue;
        int price = (int)c.s[5];
        if (price < 0 || c.s[1] != 0) continue;   // not sold / the default part
        out.push_back({(uint32_t)c.s[3], price, LabelOr(c.label[0], c.label[0])});
    }
    return out;
}
static std::string WeaponName(const char *name) {
    uint32_t w = Joaat(name);
    DlcWeapon d{};
    if (DlcWeaponIndex(w, &d) >= 0) { std::string t = Label(d.label[0]); if (!t.empty()) return t; }
    std::string s = std::string("WT_") + (name + 7);   // the base game's names: WT_PISTOL...
    static const std::map<std::string, const char *> fix = {{"WEAPON_ASSAULTRIFLE", "WT_RIFLE_ASL"}, {"WEAPON_CARBINERIFLE", "WT_RIFLE_CBN"},
        {"WEAPON_ADVANCEDRIFLE", "WT_RIFLE_ADV"}, {"WEAPON_SNIPERRIFLE", "WT_SNIP_RIF"}, {"WEAPON_HEAVYSNIPER", "WT_SNIP_HVY"},
        {"WEAPON_MICROSMG", "WT_SMG_MCR"}, {"WEAPON_ASSAULTSMG", "WT_SMG_ASL"}, {"WEAPON_PUMPSHOTGUN", "WT_SG_PMP"},
        {"WEAPON_SAWNOFFSHOTGUN", "WT_SG_SOF"}, {"WEAPON_ASSAULTSHOTGUN", "WT_SG_ASL"}, {"WEAPON_BULLPUPSHOTGUN", "WT_SG_BLP"},
        {"WEAPON_COMBATPISTOL", "WT_PIST_CBT"}, {"WEAPON_APPISTOL", "WT_PIST_AP"}, {"WEAPON_PISTOL50", "WT_PIST_50"}, {"WEAPON_COMBATMG", "WT_MG_CBT"},
        {"WEAPON_GRENADELAUNCHER", "WT_GL"}, {"WEAPON_MINIGUN", "WT_MINIGUN"}, {"WEAPON_RPG", "WT_RPG"}, {"WEAPON_PISTOL", "WT_PIST"}};
    auto it = fix.find(name);
    std::string t = Label(it != fix.end() ? it->second : s.c_str());
    if (!t.empty()) return t;
    std::string pretty = name + 7;
    for (auto &ch : pretty) ch = ch == '_' ? ' ' : (char)std::tolower(ch);
    return pretty;
}
static std::string WsKey(uint32_t w) { char b[32]; std::snprintf(b, sizeof b, "@wt|%08X", w); return b; }
static std::string WcKey(uint32_t w, uint32_t c) { char b[40]; std::snprintf(b, sizeof b, "@wc|%08X|%08X", w, c); return b; }
static int g_wmodNext = 0;
static void WeaponModsTick() {   // put the bought tints / attachments back on (weapons are given again at load and on respawn)
    if (!YouActive() || Now() < g_wmodNext) return;
    g_wmodNext = Now() + 2000;
    int me = Me();
    for (auto &c : g_cust) {
        if (c.first.size() < 12 || c.first[0] != '@' || c.first[1] != 'w') continue;
        uint32_t w = (uint32_t)std::strtoul(c.first.c_str() + 4, nullptr, 16);
        if (!NB(N<int>(N_HAS_PED_GOT_WEAPON, me, w, 0))) continue;
        if (c.first[2] == 't') { if (N<int>(N_GET_PED_WEAPON_TINT_INDEX, me, w) != c.second) N(N_SET_PED_WEAPON_TINT_INDEX, me, w, c.second); }
        else if (c.first[2] == 'c' && c.second) {
            uint32_t comp = (uint32_t)std::strtoul(c.first.c_str() + 13, nullptr, 16);
            if (!NB(N<int>(N_HAS_PED_GOT_WEAPON_COMPONENT, me, w, comp))) N(N_GIVE_WEAPON_COMPONENT_TO_PED, me, w, comp);
        }
    }
}

// ---------------------------------------------------------------- Vehicle Workshop
// Your garage vehicle is brought into the workshop bay (MOC vehicle workshop, Avenger hold) or next to
// the Terrorbyte; the modifications are GTA Online's mod slots, saved into the garage record.
static const char *MOD_SLOT_NAMES[50] = {"Spoilers", "Front Bumper", "Rear Bumper", "Skirts", "Exhaust", "Chassis", "Grille", "Hood",
    "Left Fender", "Right Fender", "Roof", "Engine", "Brakes", "Transmission", "Horns", "Suspension", "Armor", "", "Turbo", "", "",
    "", "Xenon Lights", "Wheels", "Rear Wheel", "Plate Holder", "Vanity Plates", "Trim", "Ornaments", "Dashboard", "Dial", "Door Speaker",
    "Seats", "Steering Wheel", "Shift Lever", "Plaques", "Speakers", "Trunk", "Hydraulics", "Engine Block", "Air Filter", "Struts",
    "Arch Cover", "Aerials", "Trim Design", "Tank", "Windows", "", "Livery", ""};
struct SpvBay { int k; float x, y, z, h; };
static const SpvBay SPV_BAYS[] = {{SPV_MOC, 1103.56f, -2997.2f, -40.0f, 180.f}, {SPV_AVENGER, 525.0f, 4750.5f, -70.0f, 90.f}};   // the rear half of the trailer / the hold
static int g_wsIdx = -1;   // the vehicle in the workshop, its garage record
static int VehicleValue(int idx) {
    if (idx < 0 || idx >= (int)g_garage.size()) return g_wsValue > 0 ? g_wsValue : 100000;   // not a garage vehicle (LS Customs)
    const CatCar *c = FindCar(g_garage[idx].model);
    return c && c->price > 0 ? c->price : 100000;
}
static int ModPrice(int idx, int type, int level) {
    double v = VehicleValue(idx);
    double pct = (type == 11 || type == 12 || type == 13 || type == 15 || type == 16) ? 0.05 * (level + 1) : type == 18 ? 0.10 : 0.015 * (level + 1);
    return std::max(250, (int)(v * pct / 50.0) * 50);
}
static void SpvWorkshopFinish() {
    if (Exists(g_wsVeh) && g_wsIdx >= 0 && g_wsIdx < (int)g_garage.size()) { g_garage[g_wsIdx].data = CaptureVehicle(g_wsVeh); Save(); }
    if (g_lscAt >= 0) { LscFinish(); g_wsVeh = 0; g_wsIdx = -1; return; }   // LS Customs: you drive off
    if (Exists(g_wsVeh) && g_spvIn >= 0) {   // inside: the vehicle goes back to storage, you stay in the workshop
        N(N_TASK_LEAVE_VEHICLE, Me(), g_wsVeh, 16); Wait(900);
        N(N_SET_ENTITY_AS_MISSION_ENTITY, g_wsVeh, 1, 1); N(N_DELETE_VEHICLE, &g_wsVeh);
    } else if (Exists(g_wsVeh)) MakePersonal(g_wsVeh, g_wsIdx);   // next to the Terrorbyte: it stays as your personal vehicle
    g_wsVeh = 0; g_wsIdx = -1;
}
static void SpvWorkshopStart(int idx) {
    const SpvBay *bay = nullptr;
    for (const SpvBay &b : SPV_BAYS) if (g_spvIn == b.k) bay = &b;
    Vec3 p; float h;
    if (bay) { p = V3(bay->x, bay->y, bay->z + 0.5f); h = bay->h; }
    else {   // the Terrorbyte: by its side, outside
        int t = g_spvVeh[SPV_TERRORBYTE];
        if (!Exists(t)) { Notify("Request your ~b~Terrorbyte~s~ first."); return; }
        SpvExit();
        p = N<Vec3>(N_GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS, t, 4.5f, 0.f, 0.f); h = N<float>(N_GET_ENTITY_HEADING, t);
    }
    StorePersonal(true);
    int v = SpawnOwned(idx, p, h);
    if (!v) { Notify("~r~The vehicle could not be brought in."); return; }
    g_wsVeh = v; g_wsIdx = idx;
    N<int>(N_SET_VEHICLE_ON_GROUND_PROPERLY, v, 5.0f);
    if (bay) N(N_FREEZE_ENTITY_POSITION, v, 1);   // on the workshop floor, it stays put
    N(N_SET_PED_INTO_VEHICLE, Me(), v, -1);
    N(N_SET_VEHICLE_ENGINE_ON, v, 0, 1, 1);
    g_menuOpen = true; g_stack.clear(); Open((MenuId)SM_TUNE);
}

// ---------------------------------------------------------------- the workshop menus
static bool SpvMenu(const Frame &f, std::string &title, std::string &sub, std::vector<Item> &items) {
    int me = Me();
    switch ((int)f.id) {
    case SM_WWS: {
        title = "Weapon Workshop"; sub = "WEAPONS";
        for (int i = 0; i < (int)(sizeof WS_GUNS / sizeof *WS_GUNS); ++i) {
            uint32_t w = Joaat(WS_GUNS[i]);
            if (!NB(N<int>(N_HAS_PED_GOT_WEAPON, me, w, 0))) continue;
            items.push_back({WeaponName(WS_GUNS[i]), "", "Upgrades, tints and attachments.", [i] { Open((MenuId)SM_WWEAP, i); }});
        }
        if (items.empty()) items.push_back({"You have no weapons to modify", "", "Buy weapons at Ammu-Nation.", [] {}});
        return true;
    }
    case SM_WWEAP: {
        const char *name = WS_GUNS[f.param];
        uint32_t w = Joaat(name);
        title = WeaponName(name); sub = "WEAPON WORKSHOP";
        if (!NB(N<int>(N_HAS_PED_GOT_WEAPON, me, w, 0))) { g_stack.pop_back(); return true; }
        for (auto &mk : WS_MK2) if (!std::strcmp(mk[0], name)) {
            uint32_t w2 = Joaat(mk[1]);
            DlcWeapon d{}; int cost = DlcWeaponIndex(w2, &d) >= 0 ? (int)d.s[3] : 0;
            if (NB(N<int>(N_HAS_PED_GOT_WEAPON, me, w2, 0))) break;
            std::string mk2 = mk[1];
            items.push_back({"Upgrade to " + WeaponName(mk[1]), Money(cost), "The Mk II: more attachments, tints and special ammo.", [w, w2, cost, mk2] {
                PayWhat what("weapon upgrade");
                if (cost > 0 && !Pay(cost)) return;
                int me2 = Me(), ammo = N<int>(N_GET_AMMO_IN_PED_WEAPON, me2, w);
                N(N_REMOVE_WEAPON_FROM_PED, me2, w);
                N(N_GIVE_WEAPON_TO_PED, me2, w2, std::max(ammo, 60), 0, 1);
                Sound("PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET");
                Notify("~g~Upgraded~s~ to the " + WeaponName(mk2.c_str()) + ".");
                for (int j = 0; j < (int)(sizeof WS_GUNS / sizeof *WS_GUNS); ++j) if (mk2 == WS_GUNS[j]) { g_stack.back().param = j; g_stack.back().sel = 0; }
            }});
        }
        int tints = N<int>(N_GET_WEAPON_TINT_COUNT, w), cur = N<int>(N_GET_PED_WEAPON_TINT_INDEX, me, w);
        bool mk2 = std::strstr(name, "_MK2") != nullptr;
        for (int t = 0; t < tints; ++t) {
            char lbl[32]; std::snprintf(lbl, sizeof lbl, mk2 ? "WCT_C_TINT_%d" : "WM_TINT%d", t);
            int price = mk2 ? 5000 : (t < 8 ? WS_TINT_PRICE[t] : 5000);
            std::string label = LabelOr(lbl, "Tint " + std::to_string(t));
            items.push_back({label, t == cur ? "Owned" : Money(price), "A tint for this weapon.", [w, t, price] {
                if (N<int>(N_GET_PED_WEAPON_TINT_INDEX, Me(), w) == t) return;
                PayWhat what("weapon tint");
                if (!Pay(price)) return;
                N(N_SET_PED_WEAPON_TINT_INDEX, Me(), w, t);
                g_cust[WsKey(w)] = t; Save();
                Sound("PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET");
            }});
        }
        for (const WsMod &m : WeaponModsOf(w, name)) {
            if (!NB(N<int>(N_DOES_WEAPON_TAKE_WEAPON_COMPONENT, w, m.comp))) continue;
            bool has = NB(N<int>(N_HAS_PED_GOT_WEAPON_COMPONENT, me, w, m.comp));
            uint32_t comp = m.comp; int price = m.price;
            items.push_back({m.name, has ? "Equipped" : Money(price), has ? "Select to remove it." : "Attach it to this weapon.", [w, comp, price] {
                int me2 = Me();
                if (NB(N<int>(N_HAS_PED_GOT_WEAPON_COMPONENT, me2, w, comp))) { N(N_REMOVE_WEAPON_COMPONENT_FROM_PED, me2, w, comp); g_cust.erase(WcKey(w, comp)); Save(); return; }
                PayWhat what("weapon attachment");
                if (price > 0 && !Pay(price)) return;
                N(N_GIVE_WEAPON_COMPONENT_TO_PED, me2, w, comp);
                for (auto it = g_cust.begin(); it != g_cust.end();) {   // a part in the same slot was replaced
                    char pre[20]; std::snprintf(pre, sizeof pre, "@wc|%08X|", w);
                    if (!it->first.compare(0, std::strlen(pre), pre)) {
                        uint32_t c2 = (uint32_t)std::strtoul(it->first.c_str() + 13, nullptr, 16);
                        if (!NB(N<int>(N_HAS_PED_GOT_WEAPON_COMPONENT, me2, w, c2))) { it = g_cust.erase(it); continue; }
                    }
                    ++it;
                }
                g_cust[WcKey(w, comp)] = 1; Save();
                Sound("PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET");
            }});
        }
        return true;
    }
    case SM_VWS: {
        title = "Vehicle Workshop"; sub = "YOUR VEHICLES";
        for (int i = 0; i < (int)g_garage.size(); ++i) {
            const CatCar *c = FindCar(g_garage[i].model);
            if (c && AirOrSea(c->cls)) continue;
            items.push_back({c ? c->name : g_garage[i].model, GarageLabel(g_garage[i].prop), "Bring it into the workshop and modify it.", [i] { CloseMenu(); SpvWorkshopStart(i); }});
        }
        if (items.empty()) items.push_back({"No vehicles", "", "Buy vehicles on the vehicle websites.", [] {}});
        return true;
    }
    case SM_TUNE: {
        int v = g_wsVeh;
        title = "Vehicle Workshop"; sub = "MODIFY";
        if (!Exists(v)) { CloseMenu(); return true; }
        N(N_SET_VEHICLE_MOD_KIT, v, 0);
        for (int t = 0; t < 50; ++t) {
            if (t == 18 || t == 22) {
                bool on = NB(N<int>(N_IS_TOGGLE_MOD_ON, v, t));
                int price = ModPrice(g_wsIdx, t, 0);
                items.push_back({MOD_SLOT_NAMES[t], on ? "Installed" : Money(price), "", [t, price] {
                    int v2 = g_wsVeh; if (!Exists(v2)) return;
                    if (NB(N<int>(N_IS_TOGGLE_MOD_ON, v2, t))) { N(N_TOGGLE_VEHICLE_MOD, v2, t, 0); return; }
                    PayWhat what("vehicle modification");
                    if (Pay(price)) { N(N_TOGGLE_VEHICLE_MOD, v2, t, 1); Sound("PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET"); }
                }});
                continue;
            }
            if (!*MOD_SLOT_NAMES[t] || N<int>(N_GET_NUM_VEHICLE_MODS, v, t) <= 0) continue;
            std::string nm = MOD_SLOT_NAMES[t];
            const char *slot = N<const char *>(N_GET_MOD_SLOT_NAME, v, t);
            if (slot && *slot) { std::string l = Label(slot); if (!l.empty()) nm = l; }
            items.push_back({nm, std::to_string(N<int>(N_GET_NUM_VEHICLE_MODS, v, t)) + " options", "", [t] { Open((MenuId)SM_TUNECAT, t); }});
        }
        items.push_back({"Respray", "", "Primary and secondary colours.", [] { Open((MenuId)SM_RESPRAY, 0); }});
        items.push_back({"Window Tint", "", "", [] { Open((MenuId)SM_TUNECAT, 100); }});
        items.push_back({"Done", "", "Save the modifications.", [] { CloseMenu(); SpvWorkshopFinish(); }});
        if (g_lscAt >= 0) { title = "Los Santos Customs"; LscItems(items); }
        return true;
    }
    case SM_TUNECAT: {
        int v = g_wsVeh, t = f.param;
        if (!Exists(v)) { CloseMenu(); return true; }
        title = t == 100 ? "Window Tint" : MOD_SLOT_NAMES[t]; sub = "OPTIONS";
        if (t == 100) {
            static const char *tints[] = {"None", "Pure Black", "Dark Smoke", "Light Smoke", "Stock", "Limo", "Green"};
            int cur = N<int>(N_GET_VEHICLE_WINDOW_TINT, v);
            for (int i = 0; i < 7; ++i) {
                int price = i == 0 ? 0 : ModPrice(g_wsIdx, 46, 0);
                items.push_back({tints[i], cur == i ? "Installed" : Money(price), "", [i, price] {
                    PayWhat what("vehicle modification");
                    if (price > 0 && !Pay(price)) return;
                    N(N_SET_VEHICLE_WINDOW_TINT, g_wsVeh, i); Sound("PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET");
                }});
            }
            return true;
        }
        int n = N<int>(N_GET_NUM_VEHICLE_MODS, v, t), cur = N<int>(N_GET_VEHICLE_MOD, v, t);
        for (int i = -1; i < n; ++i) {
            std::string nm;
            if (i < 0) nm = "Stock";
            else {
                const char *l = N<const char *>(N_GET_MOD_TEXT_LABEL, v, t, i);
                nm = l && *l ? Label(l) : "";
                if (nm.empty()) nm = (t == 11 || t == 12 || t == 13 || t == 15 || t == 16 ? "Level " : "Option ") + std::to_string(i + 1);
            }
            int price = i < 0 ? 0 : ModPrice(g_wsIdx, t, i);
            items.push_back({nm, cur == i ? "Installed" : Money(price), "", [t, i, price] {
                int v2 = g_wsVeh; if (!Exists(v2) || N<int>(N_GET_VEHICLE_MOD, v2, t) == i) return;
                PayWhat what("vehicle modification");
                if (price > 0 && !Pay(price)) return;
                if (i < 0) N(N_REMOVE_VEHICLE_MOD, v2, t); else N(N_SET_VEHICLE_MOD, v2, t, i, 0);
                Sound("PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET");
            }});
        }
        return true;
    }
    case SM_RESPRAY: {
        int v = g_wsVeh;
        if (!Exists(v)) { CloseMenu(); return true; }
        title = f.param == 0 ? "Primary Colour" : "Secondary Colour"; sub = "RESPRAY";
        static const struct { const char *name; int c; } COLS[] = {{"Black", 0}, {"Graphite", 1}, {"Silver", 4}, {"Red", 27}, {"Orange", 38},
            {"Yellow", 88}, {"Green", 53}, {"Blue", 64}, {"Midnight Blue", 141}, {"Purple", 145}, {"Pink", 135}, {"White", 111}, {"Gold", 37},
            {"Brown", 96}, {"Matte Black", 12}, {"Chrome", 120}};
        int a = 0, b = 0; N(N_GET_VEHICLE_COLOURS, v, &a, &b);
        int curc = f.param == 0 ? a : b, price = ModPrice(g_wsIdx, 0, 0);
        for (auto &c : COLS) {
            int col = c.c, which = f.param;
            items.push_back({c.name, curc == col ? "Installed" : Money(price), "", [col, which, price] {
                int v2 = g_wsVeh; if (!Exists(v2)) return;
                PayWhat what("vehicle respray");
                if (!Pay(price)) return;
                int a2 = 0, b2 = 0; N(N_GET_VEHICLE_COLOURS, v2, &a2, &b2);
                N(N_CLEAR_VEHICLE_CUSTOM_PRIMARY_COLOUR, v2); N(N_CLEAR_VEHICLE_CUSTOM_SECONDARY_COLOUR, v2);
                N(N_SET_VEHICLE_COLOURS, v2, which == 0 ? col : a2, which == 1 ? col : b2);
                Sound("PURCHASE", "HUD_LIQUOR_STORE_SOUNDSET");
            }});
        }
        if (f.param == 0) items.push_back({"Secondary Colour", "", "", [] { Open((MenuId)SM_RESPRAY, 1); }});
        return true;
    }
    default: return false;
    }
}
