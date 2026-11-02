// ---------------------------------------------------------------- the heists' minigames (GTA Online's, fm_mission_controller.c)
// The drill (DRILLING movie, The Fleeca Job's lockboxes): the left stick, W / S or the mouse push the bit in, the right
// trigger or the left button is its speed. It only bites while its tip is within reach of the hole's depth; pushed further than
// that it overheats, and when the temperature is full it jams until it has cooled. Four pins break at fixed depths, the
// last one finishes the job. Numbers as the script's.
// The hack (Hacking_PC movie): a desktop; My Computer, HackConnect finds the IP (select the right address as it scrolls,
// lives, 60 s), then BruteForce finds the password (stop each column on its letter). Losing starts that program again.
struct HeistDrill {
    bool on = false, jam = false, done = false;
    int mv = 0, pin = 0, sound = -1, prop = 0, doneAt = 0;
    float pos = 0.f, speed = 0.f, kick = 0.f, depth = 0.f, reach = 0.f, low = 0.f, high = 0.f, rate = 0.f, heat = 0.f, shown = 0.f, shake = 0.f, shakeTo = 0.f;
};
static HeistDrill g_drill;
static bool g_hgAuto = false;   // tests: the game plays itself
static void HgSnd(const char *name) { N(N_PLAY_SOUND_FRONTEND, -1, name, 0, 1); }   // the hack's sounds have no set
static float HgFrameTime() { return N<float>(N_GET_FRAME_TIME); }
static void HgCallF(int mv, const char *m, float a) { N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, m); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, a); N(N_END_SCALEFORM_MOVIE_METHOD); }
static void HgCallS(int mv, const char *m, float a, const char *s, float b = -1.f) {   // a number or two and a string (Online's -1: not passed)
    N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, m);
    N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, a);
    if (b != -1.f) N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, b);
    N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_TEXTURE_NAME_STRING, s);
    N(N_END_SCALEFORM_MOVIE_METHOD);
}
static void HgCall(int mv, const char *m) { N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, m); N(N_END_SCALEFORM_MOVIE_METHOD); }
static void HgHold() {   // the minigame has the screen and the controls
    N(N_HIDE_HUD_AND_RADAR_THIS_FRAME);
    N(N_THEFEED_HIDE_THIS_FRAME);
    N(N_DISABLE_ALL_CONTROL_ACTIONS, 0);
}

// a camera close to the work, for the drill
static int g_hgCam = 0;
static void HgCamOn(Vec3 c, Vec3 t, float fov) {
    float dx = t.x - c.x, dy = t.y - c.y, dz = t.z - c.z;
    if (!g_hgCam) g_hgCam = N<int>(N_CREATE_CAM, "DEFAULT_SCRIPTED_CAMERA", 0);
    N(N_SET_CAM_COORD, g_hgCam, c.x, c.y, c.z);
    N(N_SET_CAM_ROT, g_hgCam, std::atan2(dz, std::sqrt(dx * dx + dy * dy)) * 57.29578f, 0.f, std::atan2(-dx, dy) * 57.29578f, 2);
    N(N_SET_CAM_FOV, g_hgCam, fov);
    N(N_SET_CAM_ACTIVE, g_hgCam, 1);
    N(N_RENDER_SCRIPT_CAMS, 1, 1, 500, 1, 0, 0);
}
static void HgCamOff() {
    if (!g_hgCam) return;
    N(N_RENDER_SCRIPT_CAMS, 0, 1, 500, 1, 0, 0);
    N(N_DESTROY_CAM, g_hgCam, 0);
    g_hgCam = 0;
}
// a door of the map: locked or not (the bank's), and turned open by hand (the vault door)
static void HgDoorLock(const char *model, float x, float y, float z, bool locked) {
    N(N_SET_STATE_OF_CLOSEST_DOOR_OF_TYPE, Joaat(model), x, y, z, locked ? 1 : 0, 0.f, 0);
}
static int HgDoorTurn(const char *model, float x, float y, float z, float toHeading) {
    int o = N<int>(N_GET_CLOSEST_OBJECT_OF_TYPE, x, y, z, 3.f, Joaat(model), 0, 0, 0);
    if (!o) { Log("heist: no door %s", model); return 0; }
    N(N_FREEZE_ENTITY_POSITION, o, 1);
    N(N_SET_ENTITY_HEADING, o, toHeading);
    return o;
}

static int HgDoorSlide(const char *model, float x, float y, float z, float dx, float dy) {   // a sliding gate: moved aside, or back with -dx -dy
    int o = N<int>(N_GET_CLOSEST_OBJECT_OF_TYPE, x, y, z, 15.f, Joaat(model), 0, 0, 0);
    if (!o) { Log("heist: no gate %s", model); return 0; }
    N(N_SET_ENTITY_AS_MISSION_ENTITY, o, 1, 1);
    N(N_FREEZE_ENTITY_POSITION, o, 1);
    N(N_SET_ENTITY_COORDS_NO_OFFSET, o, x + dx, y + dy, z, 0, 0, 0);
    return o;
}

// ---- the drill
static void DrillStart() {
    int me = Me();
    g_drill = HeistDrill();
    g_drill.on = true;
    g_drill.mv = N<int>(N_REQUEST_SCALEFORM_MOVIE, "DRILLING");
    N(N_REQUEST_ANIM_DICT, "anim@heists@fleeca_bank@drilling");
    N<int>(N_REQUEST_SCRIPT_AUDIO_BANK, "DLC_MPHEIST\\HEIST_FLEECA_DRILL", 0, -1);
    N<int>(N_REQUEST_SCRIPT_AUDIO_BANK, "DLC_MPHEIST\\HEIST_FLEECA_DRILL_2", 0, -1);
    uint32_t pm = Joaat("hei_prop_heist_drill");
    if (LoadModel(pm)) {
        Vec3 p = Pos(me);
        g_drill.prop = N<int>(N_CREATE_OBJECT, pm, p.x, p.y, p.z, 0, 0, 0);
        if (g_drill.prop) N(N_ATTACH_ENTITY_TO_ENTITY, g_drill.prop, me, N<int>(N_GET_PED_BONE_INDEX, me, 28422), 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0, 0, 0, 0, 2, 1);
        N(N_SET_MODEL_AS_NO_LONGER_NEEDED, pm);
    }
    g_drill.sound = N<int>(N_GET_SOUND_ID);
    N(N_PLAY_SOUND_FROM_ENTITY, g_drill.sound, "Drill", me, "DLC_HEIST_FLEECA_SOUNDSET", 1, 0);
    Log("drill: started");
}
static void DrillEnd() {
    if (g_drill.sound >= 0) { N(N_STOP_SOUND, g_drill.sound); N(N_RELEASE_SOUND_ID, g_drill.sound); }
    if (g_drill.prop) DeleteEntity(g_drill.prop);
    if (g_drill.mv) N(N_SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED, &g_drill.mv);
    N(N_RELEASE_NAMED_SCRIPT_AUDIO_BANK, "DLC_MPHEIST\\HEIST_FLEECA_DRILL");
    N(N_RELEASE_NAMED_SCRIPT_AUDIO_BANK, "DLC_MPHEIST\\HEIST_FLEECA_DRILL_2");
    N(N_CLEAR_PED_TASKS, Me());
    g_drill.on = false;
}
// every frame while drilling; 1 when it is through, -1 when you left it (Back), 0 otherwise
static int DrillTick() {
    HeistDrill &d = g_drill;
    int me = Me();
    float dt = HgFrameTime();
    N(N_HIDE_HUD_AND_RADAR_THIS_FRAME);
    N(N_THEFEED_HIDE_THIS_FRAME);
    // only the player's controls off: the drill reads the frontend ones
    for (int c : {1, 2, 21, 22, 23, 24, 25, 30, 31, 32, 33, 34, 35, 37, 44, 45, 47, 51, 52, 140, 141, 142, 143, 257, 263, 264}) N(N_DISABLE_CONTROL_ACTION, 0, c, 1);
    if (NB(N<int>(N_HAS_ANIM_DICT_LOADED, "anim@heists@fleeca_bank@drilling")) && !NB(N<int>(N_IS_ENTITY_PLAYING_ANIM, me, "anim@heists@fleeca_bank@drilling", "drill_straight_idle", 3)))
        N(N_TASK_PLAY_ANIM, me, "anim@heists@fleeca_bank@drilling", "drill_straight_idle", 8.f, -8.f, -1, 1, 0.f, 0, 0, 0);
    if (d.jam) { d.pos = 0.f; d.speed = 0.f; }
    else if (g_hgAuto) {   // tests: speed up, the tip just ahead of the hole's bottom
        d.speed = 0.7f;
        d.pos = std::min(d.high - 0.01f, d.depth + 0.05f);
    } else {
        bool mouse = NB(N<int>(N_IS_USING_KEYBOARD_AND_MOUSE, 2));
        // the frontend group, as the script's (only the player's controls are disabled). The script adds the axis times 0.25
        // every frame, meant for the mouse's small movements: W and S (the axis at full) push the bit in and out gradually
        if (mouse) {
            float y = N<float>(N_GET_CONTROL_NORMAL, 2, 219);                  // INPUT_SCRIPT_LEFT_AXIS_Y
            if (std::fabs(y) > 0.99f) d.pos -= y * 0.35f * dt;
            else d.pos -= (y ? y : N<float>(N_GET_DISABLED_CONTROL_NORMAL, 0, 2)) * 0.25f;   // or the mouse, as INPUT_LOOK_UD
        } else d.pos = -N<float>(N_GET_CONTROL_NORMAL, 2, 196) / 0.82f;        // INPUT_FRONTEND_AXIS_Y
        d.speed = N<float>(N_GET_CONTROL_NORMAL, 2, 229);                      // INPUT_SCRIPT_RT
        if (!mouse && d.speed < 0.03f) d.speed = 0.f;
        static int logAt = 0;
        if (Now() > logAt) { logAt = Now() + 3000; Log("drill: pos %.3f speed %.2f depth %.3f heat %.2f", d.pos, d.speed, d.depth, d.heat); }
    }
    d.pos = std::clamp(d.pos, 0.f, 1.f);
    d.kick = std::max(0.f, d.kick - (0.4f + 0.2f * (1.f - d.speed)) * dt);
    d.speed = std::min(1.f, d.speed * (1.f + d.kick * 0.5f));
    d.reach = 0.25f - (0.25f - 0.15f) * d.speed + (1.f - d.depth) * 0.25f;
    d.rate = 0.025f * d.speed;
    d.reach /= 1.f + d.kick * 6.f;
    d.low = d.depth;
    d.high = std::min(1.f, d.low + d.reach);
    if (!d.jam && d.speed > 0.f) {
        if (d.pos > d.high) d.heat += (0.35f + d.speed * 0.2f) * dt;              // pushed too far: it overheats
        else if (d.pos >= d.low) d.depth = std::min(1.f, d.depth + d.rate * dt);   // biting
    }
    d.heat -= (d.jam ? 0.08f * 2.f : d.speed > 0.f ? 0.08f : 0.08f * 1.25f) * dt;
    d.heat = std::clamp(d.heat, 0.f, 1.f);
    d.shown += (d.pos - d.shown) * 0.5f;
    if (d.shown > d.depth) d.shown = d.depth;
    if (d.pos > d.high) { if (std::fabs(d.shakeTo - d.shake) < 0.002f) d.shakeTo = -(float)Rand(40) / 1000.f; }
    else d.shakeTo = 0.f;
    d.shake += (d.shakeTo - d.shake) * std::min(1.f, dt * 20.f);
    if (d.heat >= 1.f && !d.jam) { d.jam = true; N(N_PLAY_SOUND_FROM_ENTITY, -1, "Drill_Jam", me, "DLC_HEIST_FLEECA_SOUNDSET", 1, 20); Log("drill: jammed at %.3f", d.depth); }
    if (d.jam && d.heat <= 0.f) d.jam = false;
    static const float PIN[4] = {0.325f, 0.475f, 0.625f, 0.775f};
    if (d.pin < 4 && d.depth >= PIN[d.pin] && (d.pin == 3 || d.depth > PIN[d.pin])) {   // a pin breaks: the bit jumps through
        Sound("Drill_Pin_Break", "DLC_HEIST_FLEECA_SOUNDSET");
        if (d.pin < 3 && d.depth < PIN[d.pin] + 0.005f) d.depth = PIN[d.pin] + 0.005f;
        d.depth += 0.01f;
        d.kick = 1.f;
        ++d.pin;
        Log("drill: pin %d at %.3f", d.pin, d.depth);
    }
    if (d.depth >= 0.775f) { if (!d.doneAt) d.doneAt = Now(); }
    else d.doneAt = 0;
    if (d.sound >= 0) N(N_SET_VARIABLE_ON_SOUND, d.sound, "DrillState", d.pos > d.high ? 1.f : d.speed > 0.f ? 0.5f : 0.f);
    if (NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, d.mv))) {
        HgCallF(d.mv, "SET_SPEED", d.speed * 0.79f + d.kick * 0.08f);
        HgCallF(d.mv, "SET_HOLE_DEPTH", d.depth);
        HgCallF(d.mv, "SET_DRILL_POSITION", d.shown + d.shake);
        HgCallF(d.mv, "SET_TEMPERATURE", d.heat);
        N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, d.mv, 255, 255, 255, 255, 0);
    }
    if (d.doneAt && Now() - d.doneAt > 500) return 1;
    return NB(N<int>(N_IS_CONTROL_JUST_PRESSED, 2, 202)) ? -1 : 0;
}

// ---- the hack: the movie's desktop, as Online's (return values of SET_INPUT_EVENT_SELECT: 4 My Computer opened, 81 its
// window ready, 80 no access there, 82 HackConnect clicked, 83 BruteForce clicked, 84 the right IP, 85 a wrong IP, 86 the
// whole password, 87 a wrong letter, 92 a right letter). HackConnect first, then BruteForce.
enum { HK_DESK, HK_IP, HK_BRUTE, HK_WON };
static struct {
    bool on = false, setup = false, ipDone = false, stopped = false, popup = false;
    int mv = 0, phase = HK_DESK, lives = 5, ret = 0, startedAt = 0, closeAt = 0, sound = -1, stoppedAt = 0, popupAt = 0, inject = 0;
    float speed = 25.f, curX = -1.f, curY = -1.f, lookX = 0.f, lookY = 0.f, lookT = 0.f;
    int maxLives = 5, background = 0;   // Online's easy setting: 5 lives, speed 25 (others 4 / 50, 3 / 90); the desktop's picture
} g_hack;
static const char *const HACK_WORDS[] = {"PASSWORD", "FIREWALL", "PROTOCOL", "DECIPHER", "FORTRESS", "UNLOCKED", "LAUNCHER", "GENETICS"};
static void HackLives(float n) { N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, g_hack.mv, "SET_LIVES"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, n); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, (float)g_hack.maxLives); N(N_END_SCALEFORM_MOVIE_METHOD); }
static void HackStopSound() { if (g_hack.sound >= 0) { N(N_STOP_SOUND, g_hack.sound); N(N_RELEASE_SOUND_ID, g_hack.sound); g_hack.sound = -1; } }
static void HackApp(int phase) {   // a program's window, its countdown and its sound
    int mv = g_hack.mv;
    g_hack.phase = phase; g_hack.lives = g_hack.maxLives; g_hack.startedAt = Now(); g_hack.stopped = false;
    HackLives((float)g_hack.lives);
    HgCallF(mv, "SET_SPEED", g_hack.speed);
    if (phase == HK_BRUTE) for (int c = 0; c < 8; ++c) { N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, "SET_COLUMN_SPEED"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, (float)c); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, g_hack.speed); N(N_END_SCALEFORM_MOVIE_METHOD); }
    HgCallF(mv, "OPEN_APP", phase == HK_BRUTE ? 1.f : 0.f);
    if (phase == HK_BRUTE) {
        N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, "SET_ROULETTE_WORD");
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_TEXTURE_NAME_STRING, HACK_WORDS[Rand(8)]);
        N(N_END_SCALEFORM_MOVIE_METHOD);
    }
    HackStopSound();
    g_hack.sound = N<int>(N_GET_SOUND_ID);
    N(N_PLAY_SOUND_FRONTEND, g_hack.sound, phase == HK_BRUTE ? "HACKING_COUNTDOWN_CRACK_PASS" : "HACKING_COUNTDOWN_IP_FIND", 0, 1);
    Log("hack: %s", phase == HK_BRUTE ? "BruteForce" : "HackConnect");
}
static void HackPopup(const char *label) {   // "access denied" and the like, a moment on the screen
    HgSnd("HACKING_CLICK_BAD");
    HgCallS(g_hack.mv, "OPEN_ERROR_POPUP", 1.f, label);
    g_hack.popup = true; g_hack.popupAt = Now();
}
static void HackStart() {
    g_hack.on = true; g_hack.setup = false; g_hack.ipDone = false; g_hack.phase = HK_DESK; g_hack.ret = 0; g_hack.sound = -1; g_hack.closeAt = 0; g_hack.popup = false; g_hack.inject = 0;
    g_hack.curX = g_hack.curY = -1.f;
    N(N_REQUEST_ADDITIONAL_TEXT, "HACK", 3);   // the desktop's labels and messages
    for (int i = 0; i < 7; ++i) N(N_REQUEST_STREAMED_TEXTURE_DICT, ("HACKING_PC_desktop_" + std::to_string(i)).c_str(), 0);
    g_hack.mv = N<int>(N_REQUEST_SCALEFORM_MOVIE, "Hacking_PC");
}
static void HackEnd() {
    HackStopSound();
    if (g_hack.mv) N(N_SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED, &g_hack.mv);
    g_hack.on = false;
}
// a lost program (no lives or no time): its outcome a moment, then back to the desktop to start it again
static void HackLost() {
    HackStopSound();
    HgSnd("HACKING_FAILURE");
    bool brute = g_hack.phase == HK_BRUTE;
    HgCallS(g_hack.mv, brute ? "SET_ROULETTE_OUTCOME" : "SET_IP_OUTCOME", 0.f, brute ? "LOSEBRUTE" : "LOSTIP");
    g_hack.closeAt = Now() + 2500;
}
// every frame while hacking; 1 when the password is in, -1 when you left it (Back on the desktop), 0 otherwise
static int HackTick() {
    int mv = g_hack.mv;
    HgHold();
    if (!NB(N<int>(N_HAS_SCALEFORM_MOVIE_LOADED, mv))) return 0;
    if (!g_hack.setup) {
        g_hack.setup = true;
        HgCallF(mv, "SET_BACKGROUND", (float)g_hack.background);
        HgCallS(mv, "ADD_PROGRAM", 7.f, "MyComputer", 4.f);
        N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, "SET_LABELS");
        for (const char *l : {"H_ICON_1", g_hack.background == 0 ? "H_ICON_2a" : "H_ICON_2", "H_ICON_3", "H_ICON_4", "H_ICON_5", "H_ICON_6"}) { N(N_BEGIN_TEXT_COMMAND_SCALEFORM_STRING, l); N(N_END_TEXT_COMMAND_SCALEFORM_STRING); }
        N(N_END_SCALEFORM_MOVIE_METHOD);
        HackLives((float)g_hack.maxLives);
        HgCallF(mv, "SET_SPEED", g_hack.speed);
    }
    N(N_DRAW_SCALEFORM_MOVIE_FULLSCREEN, mv, 255, 255, 255, 255, 0);
    int now = Now();
    if (g_hack.closeAt) {   // the outcome on the screen a moment
        if (now < g_hack.closeAt) return 0;
        g_hack.closeAt = 0;
        if (g_hack.phase == HK_WON) { Log("hack: done"); return 1; }
        HgCall(mv, "CLOSE_APP");
        g_hack.phase = HK_DESK;
    }
    if (g_hack.popup && now - g_hack.popupAt > 2500) { HgCallS(mv, "OPEN_ERROR_POPUP", 0.f, ""); g_hack.popup = false; }
    auto pressed = [](int c) { return NB(N<int>(N_IS_DISABLED_CONTROL_JUST_PRESSED, 2, c)); };
    bool cursor = NB(N<int>(N_IS_USING_KEYBOARD_AND_MOUSE, 2));
    if (g_hack.phase == HK_DESK) {
        // the desktop: the cursor, with the mouse or the stick
        if (cursor) {
            float x = N<float>(N_GET_DISABLED_CONTROL_UNBOUND_NORMAL, 2, 239), y = N<float>(N_GET_DISABLED_CONTROL_UNBOUND_NORMAL, 2, 240);
            if (x != g_hack.curX || y != g_hack.curY) {
                N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, "SET_CURSOR"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, x); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, y); N(N_END_SCALEFORM_MOVIE_METHOD);
                g_hack.curX = x; g_hack.curY = y;
            }
        } else {
            float dt = HgFrameTime(), dx = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 195) * 127.f * 8.f * dt, dy = N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 196) * 127.f * 8.f * dt;
            if (std::fabs(dx) > 1.f || std::fabs(dy) > 1.f) { N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, "MOVE_CURSOR"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, dx); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, dy); N(N_END_SCALEFORM_MOVIE_METHOD); }
        }
        if (!g_hack.ret && !g_hack.popup && pressed(202)) return -1;
    } else if (g_hack.phase == HK_IP || g_hack.phase == HK_BRUTE) {
        // a program: its 60 s, the arrows, the mouse in steps
        int left = 60000 - (now - g_hack.startedAt);
        if (left <= 0) { HackLost(); return 0; }
        N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, "SET_COUNTDOWN");
        N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, (float)(left / 60000)); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, (float)(left / 1000 % 60)); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT, (float)(left % 1000));
        N(N_END_SCALEFORM_MOVIE_METHOD);
        if (g_hack.stopped && now - g_hack.stoppedAt > 500) { HgCall(mv, "RESET_ROULETTE"); g_hack.stopped = false; }
        auto move = [mv](int ev) {
            HgSnd("HACKING_MOVE_CURSOR");
            N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, "SET_INPUT_EVENT"); N(N_SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT, ev); N(N_END_SCALEFORM_MOVIE_METHOD);
        };
        if (pressed(188) || pressed(32)) move(8);
        if (pressed(187) || pressed(33)) move(9);
        if (pressed(189) || pressed(34)) move(10);
        if (pressed(190) || pressed(35)) move(11);
        if (cursor) {   // the mouse moves it too, a step every 0.15 s (Online's)
            g_hack.lookX += N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 1); g_hack.lookY += N<float>(N_GET_DISABLED_CONTROL_NORMAL, 2, 2); g_hack.lookT += HgFrameTime();
            if (g_hack.lookT > 0.15f) {
                if (g_hack.lookY < -0.3f) move(8); else if (g_hack.lookY > 0.3f) move(9);
                if (g_hack.lookX < -0.3f) move(10); else if (g_hack.lookX > 0.3f) move(11);
                g_hack.lookX = g_hack.lookY = g_hack.lookT = 0.f;
            }
        }
        if (!g_hack.ret && pressed(202)) { HackStopSound(); HgCall(mv, "CLOSE_APP"); g_hack.phase = HK_DESK; return 0; }
    }
    if (g_hack.phase != HK_WON && !g_hack.ret && !g_hack.popup && (pressed(201) || pressed(237))) {
        HgSnd("HACKING_CLICK");
        N<int>(N_BEGIN_SCALEFORM_MOVIE_METHOD, mv, "SET_INPUT_EVENT_SELECT");
        g_hack.ret = N<int>(N_END_SCALEFORM_MOVIE_METHOD_RETURN_VALUE);
    }
    int r = 0;
    if (g_hack.inject) { r = g_hack.inject; g_hack.inject = 0; }   // tests
    else if (g_hack.ret && NB(N<int>(N_IS_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_READY, g_hack.ret))) { r = N<int>(N_GET_SCALEFORM_MOVIE_METHOD_RETURN_VALUE_INT, g_hack.ret); g_hack.ret = 0; }
    if (!r) return 0;
    Log("hack: result %d (phase %d)", r, g_hack.phase);
    switch (r) {
    case 80: HackPopup("ACCESSD"); break;
    case 82:   // HackConnect
        if (g_hack.ipDone) HackPopup("TRYBRUTE"); else HackApp(HK_IP);
        break;
    case 83:   // BruteForce: once connected
        if (g_hack.ipDone) HackApp(HK_BRUTE); else HackPopup("TRYHACK");
        break;
    case 84:   // the right IP
        HackStopSound();
        HgSnd("HACKING_CLICK_GOOD");
        HgCallS(mv, "SET_IP_OUTCOME", 1.f, "WINIP");
        g_hack.ipDone = true; g_hack.closeAt = now + 2000;
        break;
    case 86:   // the whole password
        HackStopSound();
        HgSnd("HACKING_SUCCESS");
        HgCallS(mv, "SET_ROULETTE_OUTCOME", 1.f, "WINBRUTE");
        g_hack.phase = HK_WON; g_hack.closeAt = now + 2000;
        break;
    case 85: case 87:   // a wrong address / a wrong letter: a life
        if (--g_hack.lives > 0) {
            HgSnd("HACKING_CLICK_BAD");
            HackLives((float)g_hack.lives);
            if (r == 87) { HgCall(mv, "STOP_ROULETTE"); g_hack.stopped = true; g_hack.stoppedAt = now; }
        } else HackLost();
        break;
    case 92: HgSnd("HACKING_CLICK_GOOD"); break;
    default: break;
    }
    return 0;
}
