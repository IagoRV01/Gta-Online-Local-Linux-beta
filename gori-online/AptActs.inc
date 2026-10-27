// GTA Online's apartment activities in our own interiors (IslandBuilder CustomApts.cs exports the points as
// NEW_APT_ACTS in apartments_data.h): sit on the sofas and chairs, watch TV from the sofa facing it, sleep in the bed
// (health back, the game saved), take a shower (blood and dirt washed off), have drinks (tipsy, drunk, pass out), change
// clothes at the wardrobe (saved outfits; LocalOnline keeps the outfit as the character's), the heist planning board.
// Included after the appearance helpers (ApplyWalk) and before BuildMenu.

enum { AM_WARDROBE = 480 };
static const char *const ACT_PROMPT[] = {"sit down", "sit down", "sleep", "use the wardrobe", "take a shower", "have a drink", "view the heist planning board"};
static int ActKindIndex(char k) { return k == 'T' ? 0 : k == 'C' ? 1 : k == 'B' ? 2 : k == 'W' ? 3 : k == 'S' ? 4 : k == 'D' ? 5 : 6; }
static float ActRange(char k) { return k == 'B' ? 1.9f : k == 'S' ? 1.3f : k == 'T' || k == 'C' ? 0.85f : 1.2f; }

static int g_act = -1;              // NEW_APT_ACTS index of what we are doing
static char g_actKind = 0;
static Vec3 g_actFrom;              // where we stood before (back there when getting up)
static int g_actUntil = 0, g_actCam = 0, g_actProp = 0;
static bool IsFemale() { return (uint32_t)N<int>(N_GET_ENTITY_MODEL, Me()) == Joaat("mp_f_freemode_01"); }
static bool LoadAnimDict(const char *d) {
    N(N_REQUEST_ANIM_DICT, d);
    for (int i = 0; i < 60 && !NB(N<int>(N_HAS_ANIM_DICT_LOADED, d)); ++i) Wait(25);
    return NB(N<int>(N_HAS_ANIM_DICT_LOADED, d));
}
static const char *AptIdInside() {
    int k = g_inside >= 0 ? NaptIndexOfProp(g_inside) : -1;
    return k >= 0 ? NEW_APTS[k].id : nullptr;
}
// a camera at c looking at t (the wardrobe, the planning board)
static void ActCam(Vec3 c, Vec3 t, float fov) {
    float dx = t.x - c.x, dy = t.y - c.y, dz = t.z - c.z;
    float h = std::atan2(-dx, dy) * 57.29578f, p = std::atan2(dz, std::sqrt(dx * dx + dy * dy)) * 57.29578f;
    if (!g_actCam || !NB(N<int>(N_DOES_CAM_EXIST, g_actCam))) g_actCam = N<int>(N_CREATE_CAM, "DEFAULT_SCRIPTED_CAMERA", 0);
    N(N_SET_CAM_COORD, g_actCam, c.x, c.y, c.z);
    N(N_SET_CAM_ROT, g_actCam, p, 0.f, h, 2);
    N(N_SET_CAM_FOV, g_actCam, fov);
    N(N_SET_CAM_ACTIVE, g_actCam, 1);
    N(N_RENDER_SCRIPT_CAMS, 1, 1, 600, 1, 0, 0);
}
static void ActCamEnd() {
    if (!g_actCam) return;
    if (NB(N<int>(N_IS_CAM_RENDERING, g_actCam))) N(N_RENDER_SCRIPT_CAMS, 0, 1, 600, 1, 0, 0);   // not when a heist corona took over
    N(N_DESTROY_CAM, g_actCam, 0);
    g_actCam = 0;
}
static Vec3 ActAhead(Vec3 p, float h, float d, float up = 0.f) {
    float r = h * 0.0174533f;
    return V3(p.x - std::sin(r) * d, p.y + std::cos(r) * d, p.z + up);
}

// ---------------------------------------------------------------- TV (the sofa facing it): Rockstar's TV playlists on
// the screen's render target, sound from the TV
static const NamedSet TV_CHANNELS[] = {{"CNT", "PL_STD_CNT"}, {"Weazel News", "PL_STD_WZL"}, {"Kung Fu Rainbow Lazerforce", "PL_WEB_KFLF"},
                                       {"Republican Space Rangers", "PL_WEB_RANGERS"}, {"Prairie Bob", "PL_WEB_PRB2"}, {"Workout", "PL_SP_WORKOUT"}};
static const int NTV_CHANNELS = (int)(sizeof TV_CHANNELS / sizeof *TV_CHANNELS);
static bool g_tvOn = false;
static int g_tvChannel = 0, g_tvRt = -1, g_tvEnt = 0;
static const char *g_tvRtName = nullptr;
static void TvOff() {
    if (!g_tvOn) return;
    g_tvOn = false;
    N(N_SET_TV_CHANNEL, -1);
    if (g_tvRtName && NB(N<int>(N_IS_NAMED_RENDERTARGET_REGISTERED, g_tvRtName))) N(N_RELEASE_NAMED_RENDERTARGET, g_tvRtName);
    g_tvRt = -1; g_tvRtName = nullptr; g_tvEnt = 0;
}
static void TvChannel(int c) {
    g_tvChannel = (c + NTV_CHANNELS) % NTV_CHANNELS;
    N(N_SET_TV_CHANNEL_PLAYLIST, 0, TV_CHANNELS[g_tvChannel].set, 1);
    N(N_SET_TV_CHANNEL, 0);
    Subtitle(std::string("~b~") + TV_CHANNELS[g_tvChannel].name, 2000);
}
static void TvOn(Vec3 at) {
    uint32_t m = Joaat("ex_prop_ex_tv_flat_01");
    g_tvEnt = N<int>(N_GET_CLOSEST_OBJECT_OF_TYPE, at.x, at.y, at.z, 12.f, m, 0, 0, 0);
    if (!Exists(g_tvEnt)) { Notify("There is no TV here."); return; }
    for (const char *rt : {"ex_tvscreen", "tvscreen"}) {
        if (!NB(N<int>(N_IS_NAMED_RENDERTARGET_REGISTERED, rt))) N(N_REGISTER_NAMED_RENDERTARGET, rt, 0);
        if (!NB(N<int>(N_IS_NAMED_RENDERTARGET_LINKED, m))) N(N_LINK_NAMED_RENDERTARGET, m);
        if (NB(N<int>(N_IS_NAMED_RENDERTARGET_LINKED, m))) { g_tvRtName = rt; break; }
        N(N_RELEASE_NAMED_RENDERTARGET, rt);
    }
    if (!g_tvRtName) { Log("tv: no render target on ex_prop_ex_tv_flat_01"); Notify("The TV does not turn on."); return; }
    g_tvRt = N<int>(N_GET_NAMED_RENDERTARGET_RENDER_ID, g_tvRtName);
    g_tvOn = true;
    N(N_SET_TV_AUDIO_FRONTEND, 0);
    N(N_ATTACH_TV_AUDIO_TO_ENTITY, g_tvEnt);
    N(N_SET_TV_VOLUME, 0.f);
    N(N_ENABLE_MOVIE_SUBTITLES, 1);
    TvChannel(g_tvChannel);
    Log("tv: on (%s, rt %d)", g_tvRtName, g_tvRt);
}
static void TvDraw() {
    if (!g_tvOn) return;
    N(N_SET_TEXT_RENDER_ID, g_tvRt);
    N(N_SET_SCRIPT_GFX_DRAW_ORDER, 4);
    N(N_SET_SCRIPT_GFX_DRAW_BEHIND_PAUSEMENU, 1);
    N(N_DRAW_TV_CHANNEL, 0.5f, 0.5f, 1.0f, 1.0f, 0.0f, 255, 255, 255, 255);
    N(N_SET_TEXT_RENDER_ID, N<int>(N_GET_DEFAULT_SCRIPT_RENDERTARGET_RENDER_ID));
}

// ---------------------------------------------------------------- drinks: each one lasts 2 minutes; 2 tipsy, 3 drunk,
// 4 very drunk, 5 you pass out (and wake up in bed)
static int g_drinks = 0, g_drinkFade = 0;
static void DrunkApply() {
    int me = Me();
    if (g_drinks <= 1) {
        N(N_STOP_GAMEPLAY_CAM_SHAKING, 1);
        N(N_CLEAR_TIMECYCLE_MODIFIER);
        N(N_SET_PED_IS_DRUNK, me, 0);
        ApplyWalk();
        return;
    }
    N(N_SHAKE_GAMEPLAY_CAM, "DRUNK_SHAKE", g_drinks == 2 ? 0.4f : g_drinks == 3 ? 1.0f : 1.8f);
    N(N_SET_TIMECYCLE_MODIFIER, "Drunk");
    N(N_SET_TIMECYCLE_MODIFIER_STRENGTH, g_drinks == 2 ? 0.25f : g_drinks == 3 ? 0.6f : 1.0f);
    if (g_drinks >= 3) {
        const char *set = g_drinks == 3 ? "move_m@drunk@moderatedrunk" : "move_m@drunk@verydrunk";
        N(N_REQUEST_CLIP_SET, set);
        for (int i = 0; i < 40 && !NB(N<int>(N_HAS_CLIP_SET_LOADED, set)); ++i) Wait(25);
        N(N_SET_PED_MOVEMENT_CLIPSET, me, set, 0.5f);
        N(N_SET_PED_IS_DRUNK, me, 1);
    }
}
static void DrunkTick() {
    if (g_drinks <= 0) return;
    if (Dead(Me())) { g_drinks = 0; DrunkApply(); return; }
    if (Now() > g_drinkFade) { --g_drinks; g_drinkFade = Now() + 120000; DrunkApply(); if (!g_drinks) Notify("You have sobered up."); }
}

// ---------------------------------------------------------------- the activities
// the running shower: GTA Online's own effects (ob_mp_shower_med: ent_amb_shower from the head, 45 degrees down, and
// ent_amb_shower_steam at the feet), from the core particle asset
static int g_showerFx[2] = {0, 0};
static void ShowerFx(const NewAptAct &a, bool on) {
    for (int &f : g_showerFx) if (f) { N(N_STOP_PARTICLE_FX_LOOPED, f, 0); f = 0; }
    if (!on) return;
    float r = a.h * 0.0174533f, fx = -std::sin(r), fy = std::cos(r);   // the way the ped faces
    for (const char *asset : {"core", "scr_mp_house"}) {
        N(N_REQUEST_NAMED_PTFX_ASSET, asset);
        for (int k = 0; k < 100 && !NB(N<int>(N_HAS_NAMED_PTFX_ASSET_LOADED, asset)); ++k) Wait(20);
        if (!g_showerFx[0]) { N(N_USE_PARTICLE_FX_ASSET, asset); g_showerFx[0] = N<int>(N_START_PARTICLE_FX_LOOPED_AT_COORD, "ent_amb_shower", a.x + fx * 0.3f, a.y + fy * 0.3f, a.z + 2.05f, -45.f, 0.f, a.h + 180.f, 1.f, 0, 0, 0, 1); }
        if (!g_showerFx[1]) { N(N_USE_PARTICLE_FX_ASSET, asset); g_showerFx[1] = N<int>(N_START_PARTICLE_FX_LOOPED_AT_COORD, "ent_amb_shower_steam", a.x, a.y, a.z + 0.05f, 0.f, 0.f, a.h, 1.f, 0, 0, 0, 1); }
        Log("shower fx (%s): water %d steam %d", asset, g_showerFx[0], g_showerFx[1]);
        if (g_showerFx[0] && g_showerFx[1]) break;
    }
}
static void ActEnd(bool snapBack) {
    int me = Me();
    if (g_actKind == 'S') ShowerFx(NEW_APT_ACTS[g_act >= 0 ? g_act : 0], false);
    if (g_actKind == 'B' || g_actKind == 'S') {
        N(N_CLEAR_PED_TASKS_IMMEDIATELY, me);
        N(N_FREEZE_ENTITY_POSITION, me, 0);
        N(N_SET_ENTITY_COLLISION, me, 1, 1);
        if (snapBack) N(N_SET_ENTITY_COORDS, me, g_actFrom.x, g_actFrom.y, g_actFrom.z - 1.0f, 0, 0, 0, 0);   // takes the feet
    } else if (g_actKind == 'T' || g_actKind == 'C') N(N_CLEAR_PED_TASKS, me);
    else if (g_actKind == 'W') { N(N_FREEZE_ENTITY_POSITION, me, 0); N(N_CLEAR_PED_TASKS, me); }
    if (g_actProp) DeleteEntity(g_actProp);
    ActCamEnd();
    g_act = -1; g_actKind = 0;
}
static float g_bedAlong = 0.15f, g_bedUp = 1.2f, g_bedTurn = 0.f;   // the lying pose against the mattress centre (`bedpose`)
static void LieInBed(int i) {
    const NewAptAct &a = NEW_APT_ACTS[i];
    int me = Me();
    const char *dict = IsFemale() ? "amb@world_human_sunbathe@female@back@base" : "amb@world_human_sunbathe@male@back@base";
    LoadAnimDict(dict);
    N(N_CLEAR_PED_TASKS_IMMEDIATELY, me);
    N(N_SET_ENTITY_COLLISION, me, 0, 0);
    // the sunbathe pose lies along the ped's facing direction from its root: the root at the pillow end, facing the feet
    Vec3 head = ActAhead(V3(a.x, a.y, a.z), a.h, g_bedAlong);
    N(N_SET_ENTITY_COORDS_NO_OFFSET, me, head.x, head.y, a.z + g_bedUp, 0, 0, 0);
    N(N_SET_ENTITY_HEADING, me, std::fmod(a.h + g_bedTurn, 360.f));
    N(N_FREEZE_ENTITY_POSITION, me, 1);
    N(N_TASK_PLAY_ANIM, me, dict, "base", 8.f, -8.f, -1, 1, 0.f, 0, 0, 0);
}
static float g_seatUp = 0.f, g_seatFwd = -0.15f;   // the bench sit perches on the front edge: a little back (`seatpose fwd up [scenario]` tunes it)
static std::string g_seatScen = "PROP_HUMAN_SEAT_BENCH";
static void ActStart(int i) {
    const NewAptAct &a = NEW_APT_ACTS[i];
    int me = Me();
    g_actFrom = Pos(me);
    g_act = i; g_actKind = a.kind;
    switch (a.kind) {
    case 'T': case 'C':
    {
        Vec3 s = ActAhead(V3(a.x, a.y, a.z + g_seatUp), a.h, g_seatFwd);
        N(N_TASK_START_SCENARIO_AT_POSITION, me, g_seatScen.c_str(), s.x, s.y, s.z, a.h, 0, 1, 1);
    }
        break;
    case 'B': {
        N(N_DO_SCREEN_FADE_OUT, 500); Wait(600);
        LieInBed(i);
        N(N_SET_ENTITY_HEALTH, me, N<int>(N_GET_ENTITY_MAX_HEALTH, me), 0);
        if (g_drinks) { g_drinks = 0; DrunkApply(); }
        Save();
        Wait(1500);
        N(N_DO_SCREEN_FADE_IN, 800);
        Notify("You had a rest: health restored. ~g~Game saved.");
        break;
    }
    case 'S': {
        N(N_DO_SCREEN_FADE_OUT, 400); Wait(500);
        N(N_CLEAR_PED_TASKS_IMMEDIATELY, me);
        N(N_SET_ENTITY_COORDS_NO_OFFSET, me, a.x, a.y, a.z + 1.0f, 0, 0, 0);
        N(N_SET_ENTITY_HEADING, me, a.h);
        N(N_FREEZE_ENTITY_POSITION, me, 1);
        bool f = IsFemale();
        const char *dict = f ? "mp_safehouseshower@female@" : "mp_safehouseshower@male@";
        if (LoadAnimDict(dict)) N(N_TASK_PLAY_ANIM, me, dict, f ? "shower_idle_a" : "male_shower_idle_a", 8.f, -8.f, -1, 1, 0.f, 0, 0, 0);
        N(N_SET_PED_WETNESS_HEIGHT, me, 2.0f);
        ShowerFx(a, true);
        N(N_DO_SCREEN_FADE_IN, 500);
        g_actUntil = Now() + 9000;
        break;
    }
    case 'D': {
        const char *dict = "mp_player_intdrink";
        N(N_SET_ENTITY_HEADING, me, a.h);
        if (LoadAnimDict(dict)) {
            uint32_t pm = Joaat("prop_cs_whiskey_bottle");
            bool ok = LoadModel(pm);
            if (ok) {
                Vec3 p = Pos(me);
                g_actProp = N<int>(N_CREATE_OBJECT, pm, p.x, p.y, p.z, 0, 0, 0);
                N(N_ATTACH_ENTITY_TO_ENTITY, g_actProp, me, N<int>(N_GET_PED_BONE_INDEX, me, 28422), 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1, 1, 0, 1, 0, 1, 0);
                N(N_SET_MODEL_AS_NO_LONGER_NEEDED, pm);
            }
            N(N_TASK_PLAY_ANIM, me, dict, "loop_bottle", 4.f, -4.f, 4500, 48, 0.f, 0, 0, 0);
        }
        g_actUntil = Now() + 4500;
        break;
    }
    case 'W': {
        N(N_CLEAR_PED_TASKS_IMMEDIATELY, me);
        N(N_SET_ENTITY_COORDS, me, a.x, a.y, a.z, 0, 0, 0, 0);   // a.z = the floor (SET_ENTITY_COORDS takes the feet)
        float h = std::fmod(a.h + 180.f, 360.f);   // turned to the room, the camera in front
        N(N_SET_ENTITY_HEADING, me, h);
        N(N_FREEZE_ENTITY_POSITION, me, 1);
        Vec3 p = V3(a.x, a.y, a.z);
        ActCam(ActAhead(p, h, 2.4f, 1.45f), V3(p.x, p.y, p.z + 1.0f), 45.f);
        g_menuOpen = true; g_stack.clear(); Open((MenuId)AM_WARDROBE);
        break;
    }
    case 'P': {
        N(N_SET_ENTITY_HEADING, me, a.h);
        HbOpen();   // Online's planning board on the wall, its camera (HeistBoard.inc)
        break;
    }
    }
}
static void Drink() {
    ++g_drinks;
    g_drinkFade = Now() + 120000;
    if (g_drinks >= 5) {   // too many: you pass out and wake up in bed (or on the floor)
        Notify("You drank too much...");
        int me = Me();
        N(N_SET_PED_TO_RAGDOLL, me, 4000, 4000, 0, 0, 0, 0);
        Wait(1500);
        N(N_DO_SCREEN_FADE_OUT, 1500); Wait(4000);
        g_drinks = 3; g_drinkFade = Now() + 120000;
        const char *id = AptIdInside();
        for (int i = 0; id && i < NNEW_APT_ACTS; ++i)
            if (NEW_APT_ACTS[i].kind == 'B' && !std::strcmp(NEW_APT_ACTS[i].id, id)) { g_actFrom = ActAhead(V3(NEW_APT_ACTS[i].x, NEW_APT_ACTS[i].y, NEW_APT_ACTS[i].z + 1.f), NEW_APT_ACTS[i].h + 90.f, 1.6f); g_act = i; g_actKind = 'B'; LieInBed(i); break; }
        DrunkApply();
        Wait(1000);
        N(N_DO_SCREEN_FADE_IN, 1500);
        Notify("You woke up with a headache.");
        return;
    }
    DrunkApply();
    if (g_drinks == 2) Notify("You feel tipsy.");
    else if (g_drinks == 3) Notify("You are drunk.");
    else if (g_drinks == 4) Notify("You are very drunk. One more and you'll pass out.");
}

// the wardrobe: Online's "Saved Outfits" and every clothing slot, previewed on you
// g_outfit (before Serialize): "d.t.p" for components 1, 3..11 then "d.t" for props 0..7, ',' separated
static std::string OutfitNow() {
    int me = Me();
    std::string s;
    char b[40];
    for (int c = 1; c < 12; ++c) {
        if (c == 2) continue;
        std::snprintf(b, sizeof b, "%d.%d.%d,", N<int>(N_GET_PED_DRAWABLE_VARIATION, me, c), N<int>(N_GET_PED_TEXTURE_VARIATION, me, c), N<int>(N_GET_PED_PALETTE_VARIATION, me, c));
        s += b;
    }
    for (int a = 0; a < 8; ++a) {
        int d = N<int>(N_GET_PED_PROP_INDEX, me, a, 0);
        std::snprintf(b, sizeof b, "%d.%d,", d, d < 0 ? 0 : N<int>(N_GET_PED_PROP_TEXTURE_INDEX, me, a));
        s += b;
    }
    return s;
}
static void OutfitWear(const std::string &s) {
    int me = Me();
    auto p = Split(s, ',');
    int k = 0;
    for (int c = 1; c < 12; ++c) {
        if (c == 2) continue;
        if (k >= (int)p.size()) return;
        auto v = Split(p[k++], '.');
        if (v.size() == 3) N(N_SET_PED_COMPONENT_VARIATION, me, c, std::atoi(v[0].c_str()), std::atoi(v[1].c_str()), std::atoi(v[2].c_str()));
    }
    for (int a = 0; a < 8; ++a) {
        if (k >= (int)p.size()) return;
        auto v = Split(p[k++], '.');
        if (v.size() != 2) continue;
        int d = std::atoi(v[0].c_str());
        if (d < 0) N(N_CLEAR_PED_PROP, me, a, 0); else N(N_SET_PED_PROP_INDEX, me, a, d, std::atoi(v[1].c_str()), 1, 0);
    }
}
struct ClothSlot { const char *name; int id; bool prop; };
static const ClothSlot CLOTH_SLOTS[] = {{"Tops", 11, false}, {"Undershirts", 8, false}, {"Arms / Gloves", 3, false}, {"Legs", 4, false}, {"Shoes", 6, false},
                                        {"Hats", 0, true}, {"Glasses", 1, true}, {"Earrings", 2, true}, {"Watches", 6, true}, {"Bracelets", 7, true},
                                        {"Masks", 1, false}, {"Chains / Scarves", 7, false}, {"Bags / Parachutes", 5, false}, {"Body Armor", 9, false}};
static void ClothCycle(const ClothSlot &c, bool texture, int d) {
    int me = Me();
    if (c.prop) {
        int cur = N<int>(N_GET_PED_PROP_INDEX, me, c.id, 0), n = N<int>(N_GET_NUMBER_OF_PED_PROP_DRAWABLE_VARIATIONS, me, c.id);
        if (texture) {
            if (cur < 0) return;
            int nt = N<int>(N_GET_NUMBER_OF_PED_PROP_TEXTURE_VARIATIONS, me, c.id, cur);
            int t = (N<int>(N_GET_PED_PROP_TEXTURE_INDEX, me, c.id) + d + nt) % (nt > 0 ? nt : 1);
            N(N_SET_PED_PROP_INDEX, me, c.id, cur, t, 1, 0);
            return;
        }
        int v = ((cur + 1 + d) % (n + 1) + n + 1) % (n + 1) - 1;   // -1 = none
        if (v < 0) N(N_CLEAR_PED_PROP, me, c.id, 0); else N(N_SET_PED_PROP_INDEX, me, c.id, v, 0, 1, 0);
        return;
    }
    int cur = N<int>(N_GET_PED_DRAWABLE_VARIATION, me, c.id);
    if (texture) {
        int nt = N<int>(N_GET_NUMBER_OF_PED_TEXTURE_VARIATIONS, me, c.id, cur);
        int t = (N<int>(N_GET_PED_TEXTURE_VARIATION, me, c.id) + d + nt) % (nt > 0 ? nt : 1);
        N(N_SET_PED_COMPONENT_VARIATION, me, c.id, cur, t, 0);
        return;
    }
    int n = N<int>(N_GET_NUMBER_OF_PED_DRAWABLE_VARIATIONS, me, c.id);
    if (n <= 0) return;
    N(N_SET_PED_COMPONENT_VARIATION, me, c.id, ((cur + d) % n + n) % n, 0, 0);
}
static std::string ClothValue(const ClothSlot &c, bool texture) {
    int me = Me();
    if (c.prop) {
        int cur = N<int>(N_GET_PED_PROP_INDEX, me, c.id, 0);
        if (cur < 0) return texture ? "-" : "None";
        if (texture) return std::to_string(N<int>(N_GET_PED_PROP_TEXTURE_INDEX, me, c.id) + 1) + "/" + std::to_string(N<int>(N_GET_NUMBER_OF_PED_PROP_TEXTURE_VARIATIONS, me, c.id, cur));
        return std::to_string(cur + 1) + "/" + std::to_string(N<int>(N_GET_NUMBER_OF_PED_PROP_DRAWABLE_VARIATIONS, me, c.id));
    }
    int cur = N<int>(N_GET_PED_DRAWABLE_VARIATION, me, c.id);
    if (texture) return std::to_string(N<int>(N_GET_PED_TEXTURE_VARIATION, me, c.id) + 1) + "/" + std::to_string(N<int>(N_GET_NUMBER_OF_PED_TEXTURE_VARIATIONS, me, c.id, cur));
    return std::to_string(cur + 1) + "/" + std::to_string(N<int>(N_GET_NUMBER_OF_PED_DRAWABLE_VARIATIONS, me, c.id));
}
static int g_outfitSlot = 0;
static bool AptMenu(const Frame &f, std::string &title, std::string &sub, std::vector<Item> &items) {
    if ((int)f.id != AM_WARDROBE) return false;
    title = "Wardrobe"; sub = "WARDROBE";
    for (int s = 0; s < 6; ++s) {
        bool has = !g_outfit[s].empty();
        items.push_back({"Outfit " + std::to_string(s + 1), has ? "" : "EMPTY", has ? "Wear this saved outfit." : "Save an outfit in this slot first.",
                         [s] { if (!g_outfit[s].empty()) { OutfitWear(g_outfit[s]); Sound("SELECT", "HUD_FRONTEND_DEFAULT_SOUNDSET"); } }});
    }
    items.push_back({"Save Outfit", "< Slot " + std::to_string(g_outfitSlot + 1) + " >", "Saves what you are wearing in the chosen slot.",
                     [] { g_outfit[g_outfitSlot] = OutfitNow(); Save(); Notify("Outfit saved in slot " + std::to_string(g_outfitSlot + 1) + "."); },
                     nullptr, [](int d) { g_outfitSlot = (g_outfitSlot + d + 6) % 6; }});
    for (const auto &c : CLOTH_SLOTS) {
        items.push_back({c.name, "< " + ClothValue(c, false) + " >", "", [] {}, nullptr, [&c](int d) { ClothCycle(c, false, d); }});
        items.push_back({std::string(c.name) + " Style", "< " + ClothValue(c, true) + " >", "", [] {}, nullptr, [&c](int d) { ClothCycle(c, true, d); }});
    }
    items.push_back({"Done", "", "Keep this outfit.", [] { CloseMenu(); }});
    return true;
}

// every frame: the TV, the drinks, a menu closed under an activity
static void AptActsFrame() {
    DrunkTick();
    if (g_tvOn && (g_inside < 0 || !AptIdInside())) TvOff();
    TvDraw();
    HbFrame();
    if ((g_actKind == 'W' || (g_actKind == 'P' && !HbUsing())) && !g_menuOpen) {
        bool w = g_actKind == 'W';
        ActEnd(false);
        if (w) {
            if (LO().adoptOutfit && LO().adoptOutfit()) Notify("Outfit changed.");
            else if (LO().adoptOutfit) Notify("Outfit changed (not saved: Local Online is not active).");
        }
    }
    if (g_act >= 0 && (g_inside < 0 || Dead(Me()))) ActEnd(false);
}
// inside one of our apartments (PropertiesTick): the prompts and the activity in progress; true = busy, no other prompt
static bool AptActsTick(Vec3 mp, bool onFoot) {
    const char *id = AptIdInside();
    if (!id) return false;
    if (g_act >= 0) {
        const NewAptAct &a = NEW_APT_ACTS[g_act];
        switch (g_actKind) {
        case 'T': case 'C': {
            int me = Me();
            if (!NB(N<int>(N_IS_PED_USING_ANY_SCENARIO, me)) && Dist(mp, V3(a.x, a.y, a.z)) > 1.5f && Now() - g_actUntil > 0) { g_act = -1; g_actKind = 0; return false; }
            if (g_menuOpen) return true;
            if (g_actKind == 'T') {
                Help(std::string(g_tvOn ? "Press ~INPUT_CONTEXT_SECONDARY~ to turn off the TV.~n~~INPUT_CELLPHONE_LEFT~ ~INPUT_CELLPHONE_RIGHT~ change channel." : "Press ~INPUT_CONTEXT_SECONDARY~ to turn on the TV.") +
                     "~n~Press ~INPUT_CONTEXT~ to stand up.");
                if (JustPressed(52)) { if (g_tvOn) TvOff(); else TvOn(ActAhead(V3(a.x, a.y, a.z), a.h, 4.f)); }
                if (g_tvOn && JustPressed(174)) TvChannel(g_tvChannel - 1);
                if (g_tvOn && JustPressed(175)) TvChannel(g_tvChannel + 1);
            } else Help("Press ~INPUT_CONTEXT~ to stand up.");
            if (JustPressed(51)) ActEnd(false);
            return true;
        }
        case 'B':
            if (g_menuOpen) return true;
            Help("Press ~INPUT_CONTEXT~ to get up.");
            if (JustPressed(51)) {
                N(N_DO_SCREEN_FADE_OUT, 300); Wait(350);
                ActEnd(true);
                N(N_DO_SCREEN_FADE_IN, 500);
            }
            return true;
        case 'S':
            if (!g_menuOpen) Help("Press ~INPUT_CONTEXT~ to get out of the shower.");
            if (JustPressed(51) || Now() > g_actUntil) {
                int me = Me();
                N(N_DO_SCREEN_FADE_OUT, 300); Wait(350);
                N(N_CLEAR_PED_BLOOD_DAMAGE, me);
                N(N_RESET_PED_VISIBLE_DAMAGE, me);
                N(N_CLEAR_PED_ENV_DIRT, me);
                ActEnd(true);
                N(N_DO_SCREEN_FADE_IN, 500);
                Notify("You are clean.");
            }
            return true;
        case 'D':
            if (Now() > g_actUntil) { ActEnd(false); Drink(); }
            return true;
        default:
            return true;   // the wardrobe and the planning board run in the menu
        }
    }
    if (!onFoot || g_menuOpen || Dead(Me())) return false;
    int best = -1; float bd = 99.f;
    for (int i = 0; i < NNEW_APT_ACTS; ++i) {
        const NewAptAct &a = NEW_APT_ACTS[i];
        if (std::strcmp(a.id, id) || std::fabs(mp.z - 1.0f - (a.z - (a.kind == 'B' ? 0.62f : a.kind == 'T' || a.kind == 'C' ? 0.5f : 0.f))) > 1.4f) continue;
        float d = Dist2(mp, V3(a.x, a.y, a.z));
        if (d < ActRange(a.kind) && d < bd) { bd = d; best = i; }
    }
    if (best < 0) return false;
    Help(std::string("Press ~INPUT_CONTEXT~ to ") + ACT_PROMPT[ActKindIndex(NEW_APT_ACTS[best].kind)] + ".");
    if (JustPressed(51)) { g_actUntil = Now() + 1500; ActStart(best); }
    return true;
}
// `aptact <kind>`: walk to the nearest activity of that kind in this apartment and start it (tests)
static void AptActCommand(const std::string &kind) {
    const char *id = AptIdInside();
    if (!id || kind.empty()) { Log("aptact: not in one of our apartments"); return; }
    if (g_act >= 0) ActEnd(true);
    for (int i = 0; i < NNEW_APT_ACTS; ++i) {
        const NewAptAct &a = NEW_APT_ACTS[i];
        char want = kind[0] >= 'a' && kind[0] <= 'z' ? (char)(kind[0] - 32) : kind[0];
        if (std::strcmp(a.id, id) || a.kind != want) continue;
        Vec3 st = a.kind == 'B' ? ActAhead(V3(a.x, a.y, a.z), a.h + 90.f, 1.6f) : V3(a.x, a.y, a.z);
        float floorZ = a.z - (a.kind == 'B' ? 0.62f : a.kind == 'T' || a.kind == 'C' ? 0.5f : 0.f);
        if (a.kind != 'T' && a.kind != 'C') N(N_SET_ENTITY_COORDS, Me(), st.x, st.y, floorZ, 0, 0, 0, 0);
        Wait(200);
        Log("aptact: %c at %.2f %.2f %.2f h %.0f", a.kind, a.x, a.y, a.z, a.h);
        g_actUntil = Now() + 1500;
        ActStart(i);
        return;
    }
    Log("aptact: no '%s' here", kind.c_str());
}
