// Host unit tests for everything that does not need GTA: config, character model, protocol.
// Built and run by build.sh before anything is deployed.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "common/character.h"
#include "common/json.h"
#include "common/sha256.h"
#include "common/interp.h"
#include "common/world.h"
#include "common/vehicle.h"
#include "common/toml.h"
#include "protocol/protocol.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define NEAR(a, b, eps) CHECK(std::fabs((double)(a) - (double)(b)) <= (eps))

static void TestToml() {
    toml::Doc d;
    std::string err;
    const char *text =
        "# server\n"
        "name = \"Gori LAN\"  # trailing comment\n"
        "[network]\n"
        "bind = \"0.0.0.0\"\n"
        "game_port = 4499\n"
        "tick_rate = 20.5\n"
        "lan_only = true\n"
        "[database]\n"
        "path = 'C:\\data\\lo.db'\n";
    CHECK(toml::Parse(text, d, err));
    CHECK(d.Str("name", "") == "Gori LAN");
    CHECK(d.Str("network.bind", "") == "0.0.0.0");
    CHECK(d.Int("network.game_port", 0) == 4499);
    NEAR(d.Num("network.tick_rate", 0), 20.5, 1e-9);
    CHECK(d.Bool("network.lan_only", false));
    CHECK(d.Str("database.path", "") == "C:\\data\\lo.db");   // literal string keeps backslashes
    CHECK(d.Int("missing", 7) == 7);
    toml::Doc bad;
    CHECK(!toml::Parse("port = 44 99\n", bad, err));
    CHECK(!toml::Parse("name = unquoted\n", bad, err));
    CHECK(!toml::Parse("a = 1\na = 2\n", bad, err));
    CHECK(!toml::Parse("list = [1, 2]\n", bad, err));
    CHECK(!toml::Parse("s = \"open\n", bad, err));
    CHECK(err.find("line 1") == 0);
}

static void TestCharacter() {
    using namespace character;
    CHECK(Joaat("mp_m_freemode_01") == 0x705E61F2u);
    CHECK(Joaat("MP_F_FREEMODE_01") == 0x9C9EFFD8u);
    CHECK(Joaat("WEAPON_PISTOL") == 0x1B06D571u);

    std::string u = NewUuid();
    CHECK(ValidUuid(u));
    CHECK(u[14] == '4');
    CHECK(NewUuid() != u);
    CHECK(!ValidUuid("not-a-uuid"));

    CHECK(ValidName("Alex"));
    CHECK(ValidName("Mr. Gori_2"));
    CHECK(!ValidName("x"));
    CHECK(!ValidName(" Alex"));
    CHECK(!ValidName("Alex<script>"));
    CHECK(!ValidName(std::string(30, 'a')));

    Character c = MakeDefault(true);
    c.uuid = NewUuid();
    c.name = "Alex";
    c.face[3] = 0.5f;
    c.overlays[Eyebrows] = {4, 0.8f, 3, 3};
    c.components[Tops] = {15, 2, 0};
    c.props[0] = {12, 1};
    c.tattoos.push_back({0x1234, 0x5678});
    c.pos = {100.5f, -200.25f, 30.f, 725.f};   // heading gets normalised
    c.money = 1500;
    c.bank = 25000;
    c.modData["gorionline"] = "rp=1200\nprop=Eclipse Towers, Apt 3\n";
    c.modData["Bad Key!"] = "x";
    std::vector<std::string> probs;
    CHECK(Validate(c, probs));
    NEAR(c.pos.heading, 5.f, 1e-3);
    CHECK(c.modData.count("Bad Key!") == 0 && c.modData.size() == 1);   // invalid key dropped

    json::Value v = ToJson(c);
    std::string text = json::Write(v);
    json::Value back;
    std::string err;
    CHECK(json::Parse(text, back, err));
    Character d;
    CHECK(FromJson(back, d, err));
    CHECK(d.uuid == c.uuid && d.name == "Alex" && d.female);
    NEAR(d.face[3], 0.5f, 1e-6);
    CHECK(d.overlays[Eyebrows].index == 4 && d.overlays[Eyebrows].color == 3);
    CHECK(d.components[Tops].drawable == 15 && d.components[Tops].texture == 2);
    CHECK(d.props[0].drawable == 12 && d.props[1].drawable == -1);
    CHECK(d.tattoos.size() == 1 && d.tattoos[0].overlay == 0x5678);
    CHECK(d.weapons.size() == 1 && d.weapons[0].hash == Joaat("WEAPON_PISTOL"));
    NEAR(d.pos.x, 100.5f, 0.01);
    CHECK(d.money == 1500 && d.bank == 25000);
    CHECK(d.modData.size() == 1 && d.modData["gorionline"] == "rp=1200\nprop=Eclipse Towers, Apt 3\n");
    CHECK(character::AppearanceOnly(d).modData.empty());
    CHECK(character::ValidModKey("gori.online-2") && !character::ValidModKey("") && !character::ValidModKey("A"));
    CHECK(ModelHash(d) == 0x9C9EFFD8u);

    // Hostile/corrupt values are clamped, not trusted.
    Character h = c;
    h.blend.shapeMother = 999;
    h.face[0] = 7.f;
    h.armour = 5000;
    h.pos.z = 99999.f;
    h.money = -5;
    probs.clear();
    CHECK(Validate(h, probs));
    CHECK(h.blend.shapeMother == 45 && h.face[0] == 1.f && h.armour == 100 && h.pos.z == 2700.f && h.money == 0);
    CHECK(probs.size() == 5);
    // Unusable profiles are rejected.
    Character bad = c;
    bad.name = "";
    probs.clear();
    CHECK(!Validate(bad, probs));
    Character nan = c;
    nan.pos.x = NAN;
    probs.clear();
    CHECK(!Validate(nan, probs));
    json::Value junk;
    CHECK(json::Parse("{\"schema\": 1, \"uuid\": \"x\", \"name\": \"Alex\"}", junk, err));
    CHECK(!FromJson(junk, d, err));
}

static void TestProtocol() {
    using namespace proto;
    PlayerStateMsg s;
    s.seq = 41;
    s.clientTimeMs = 123456;
    s.pos = {-1037.8f, -2737.6f, 20.2f};
    s.vel = {1.f, 2.f, 0.f};
    s.heading = 271.3f;
    s.move = Move::Sprint;
    s.flags = kFlagAiming | kFlagJumping;
    s.health = 87;
    s.armour = 50;
    s.weapon = 0x1B06D571u;
    wire::Writer w;
    PutPlayerState(w, s);
    CHECK(w.Size() == 4 + 4 + 4 + 12 + 12 + 2 + 1 + 2 + 1 + 1 + 1 + 4 + 12);   // 48 bytes + aim point (aiming flag set)

    wire::Reader r(w.Bytes().data(), w.Size());
    Header h;
    CHECK(GetHeader(r, h) && h.type == MsgType::PlayerState);
    PlayerStateMsg o;
    CHECK(GetPlayerState(r, o));
    CHECK(o.seq == 41 && o.move == Move::Sprint && o.flags == (kFlagAiming | kFlagJumping) && o.weapon == s.weapon);
    NEAR(o.heading, 271.3f, 0.01);
    NEAR(o.pos.y, -2737.6f, 1e-3);

    // truncated packet: every prefix must be rejected, never read out of bounds
    int rejected = 0;
    for (size_t n = 0; n < w.Size(); ++n) {
        wire::Reader t(w.Bytes().data(), n);
        Header th;
        PlayerStateMsg ts;
        if (!GetHeader(t, th) || !GetPlayerState(t, ts)) ++rejected;
    }
    CHECK(rejected == (int)w.Size());
    // trailing garbage rejected
    std::vector<uint8_t> extra = w.Bytes();
    extra.push_back(0xAA);
    wire::Reader te(extra.data(), extra.size());
    CHECK(GetHeader(te, h) && !GetPlayerState(te, o));
    // wrong version / magic rejected
    std::vector<uint8_t> wrong = w.Bytes();
    wrong[1] = kProtocolVersion + 1;
    wire::Reader tv(wrong.data(), wrong.size());
    CHECK(!GetHeader(tv, h));
    wrong = w.Bytes();
    wrong[0] = 0;
    wire::Reader tm(wrong.data(), wrong.size());
    CHECK(!GetHeader(tm, h));
    // NaN position rejected
    std::vector<uint8_t> nanpkt = w.Bytes();
    float qnan = NAN;
    std::memcpy(&nanpkt[12], &qnan, 4);
    wire::Reader tn(nanpkt.data(), nanpkt.size());
    CHECK(GetHeader(tn, h) && !GetPlayerState(tn, o));
    // bad enum rejected
    std::vector<uint8_t> badmove = w.Bytes();
    badmove[4 + 4 + 4 + 12 + 12 + 2] = 99;
    wire::Reader tb(badmove.data(), badmove.size());
    CHECK(GetHeader(tb, h) && !GetPlayerState(tb, o));

    // snapshot round trip + oversize count rejected
    std::vector<RemoteState> players(3);
    for (int i = 0; i < 3; ++i) { players[i].netId = (uint16_t)(i + 1); players[i].pos = {(float)i, 0, 0}; }
    wire::Writer ws;
    PutSnapshot(ws, 900, 45000, players);
    wire::Reader rs(ws.Bytes().data(), ws.Size());
    uint32_t tick = 0, st = 0;
    std::vector<RemoteState> got;
    CHECK(GetHeader(rs, h) && h.type == MsgType::Snapshot && GetSnapshot(rs, tick, st, got));
    CHECK(tick == 900 && st == 45000 && got.size() == 3 && got[2].netId == 3);
    std::vector<uint8_t> big = ws.Bytes();
    big[12] = 200;
    wire::Reader rb(big.data(), big.size());
    CHECK(GetHeader(rb, h) && !GetSnapshot(rb, tick, st, got));

    // strings: length-checked
    wire::Writer wstr;
    wstr.Str("hello");
    wire::Reader rstr(wstr.Bytes().data(), wstr.Size());
    CHECK(rstr.Str(3).empty() && !rstr.ok());   // longer than allowed
    wire::Reader rstr2(wstr.Bytes().data(), wstr.Size());
    CHECK(rstr2.Str() == "hello" && rstr2.Done());

    CHECK(SeqNewer(5, 4) && !SeqNewer(4, 5) && SeqNewer(2, 0xFFFFFFF0u));
    NEAR(UnpackHeading(PackHeading(-90.f)), 270.f, 0.01);
    NEAR(UnpackHeading(PackHeading(719.f)), 359.f, 0.01);
}

static void TestSha256() {
    CHECK(sha256::Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256::Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256::Hex(std::string(1000, 'a')) == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
    std::string t = sha256::RandomToken();
    CHECK(t.size() == 48 && t != sha256::RandomToken());
}

static void TestControlMessages() {
    using namespace proto;
    AuthResultMsg a;
    a.ok = true; a.netId = 7; a.playerUuid = character::NewUuid(); a.newToken = "abc123"; a.reason = "";
    wire::Writer w;
    Put(w, a);
    wire::Reader r(w.Bytes().data(), w.Size());
    Header h;
    AuthResultMsg b;
    CHECK(GetHeader(r, h) && h.type == MsgType::AuthResult && Get(r, b));
    CHECK(b.ok && b.netId == 7 && b.playerUuid == a.playerUuid && b.newToken == "abc123");

    ProfileMsg p;
    p.exists = true; p.revision = 42; p.json = std::string(40000, 'x');
    wire::Writer wp;
    Put(wp, p);
    wire::Reader rp(wp.Bytes().data(), wp.Size());
    ProfileMsg q;
    CHECK(GetHeader(rp, h) && Get(rp, q) && q.revision == 42 && q.json.size() == 40000);

    ProfileSaveMsg s;
    s.baseRevision = 3; s.json = "{}";
    wire::Writer ws;
    Put(ws, s);
    std::vector<uint8_t> cut(ws.Bytes().begin(), ws.Bytes().end() - 1);   // truncated -> rejected
    wire::Reader rs(cut.data(), cut.size());
    ProfileSaveMsg t;
    CHECK(GetHeader(rs, h) && !Get(rs, t));
}

static void TestInterp() {
    using namespace interp;
    NEAR(LerpHeading(350, 10, 0.5f), 0.f, 1e-3);
    NEAR(LerpHeading(10, 350, 0.25f), 5.f, 1e-3);
    { float h = LerpHeading(90, 270, 0.5f); CHECK(std::fabs(h - 0.f) < 1e-3 || std::fabs(h - 180.f) < 1e-3); }   // half turn: either way
    Buffer b;
    Sample a; a.t = 1.0; a.pos = {0, 0, 0}; a.vel = {10, 0, 0}; a.heading = 350;
    Sample c; c.t = 1.1; c.pos = {1, 0, 0}; c.vel = {10, 0, 0}; c.heading = 10;
    b.Push(a); b.Push(c);
    b.Push(a);   // older sample ignored
    Result r = b.At(1.05);
    CHECK(r.valid && !r.extrapolated);
    NEAR(r.s.pos.x, 0.5f, 1e-4);
    NEAR(r.s.heading, 0.f, 1e-3);
    r = b.At(1.2);   // 0.1 s past the newest: dead reckoning
    CHECK(r.extrapolated && !r.stale);
    NEAR(r.s.pos.x, 2.f, 1e-3);
    r = b.At(2.0);   // far past: clamped to 0.25 s and flagged stale
    CHECK(r.stale);
    NEAR(r.s.pos.x, 3.5f, 1e-3);
    r = b.At(0.5);   // before the first sample: hold the first
    NEAR(r.s.pos.x, 0.f, 1e-6);
    Clock k;
    k.OnSnapshot(100.0, 10.0);
    NEAR(k.ServerNow(10.5), 100.5, 1e-9);
    k.OnSnapshot(101.0, 11.02);   // 20 ms jitter: smoothed
    CHECK(k.ServerNow(11.02) > 101.0 && k.ServerNow(11.02) < 101.02);   // moved 5% toward the new sample
}

static void TestVehicleState() {
    using namespace proto;
    PlayerStateMsg s;
    s.seq = 5; s.flags = kFlagInVehicle; s.move = Move::Still;
    s.veh.model = character::Joaat("sultan"); s.veh.seat = -1; s.veh.driverNetId = 3;
    s.veh.pos = {1, 2, 3}; s.veh.rot = {0, 0, 0.7071068f, 0.7071068f}; s.veh.vel = {20, 0, 0}; s.veh.angVel = {0, 0, 0.5f};
    s.veh.color1 = 12; s.veh.color2 = 34; s.veh.vflags = kVehEngine | kVehLights;
    wire::Writer w;
    PutPlayerState(w, s);
    CHECK(w.Size() == 48 + 62);
    wire::Reader r(w.Bytes().data(), w.Size());
    Header h;
    PlayerStateMsg o;
    CHECK(GetHeader(r, h) && GetPlayerState(r, o));
    CHECK(o.veh.model == s.veh.model && o.veh.seat == -1 && o.veh.driverNetId == 3 && o.veh.color2 == 34 && o.veh.vflags == 3);
    NEAR(o.veh.rot.z, 0.7071068f, 1e-6);
    // on foot: no vehicle block on the wire
    PlayerStateMsg foot;
    wire::Writer wf;
    PutPlayerState(wf, foot);
    CHECK(wf.Size() == 48);
    // bad quaternion / seat rejected
    PlayerStateMsg bad = s;
    bad.veh.rot = {0, 0, 0, 0};
    wire::Writer wb;
    PutPlayerState(wb, bad);
    wire::Reader rb(wb.Bytes().data(), wb.Size());
    CHECK(GetHeader(rb, h) && !GetPlayerState(rb, o));
    bad = s; bad.veh.seat = 40;
    wire::Writer ws;
    PutPlayerState(ws, bad);
    wire::Reader rs(ws.Bytes().data(), ws.Size());
    CHECK(GetHeader(rs, h) && !GetPlayerState(rs, o));
    // nlerp between yaw 0 and yaw 90
    interp::Quat q = interp::Nlerp({0, 0, 0, 1}, {0, 0, 0.7071068f, 0.7071068f}, 0.5f);
    NEAR(q.z, 0.3826834f, 1e-4);   // yaw 45
    NEAR(q.w, 0.9238795f, 1e-4);
    q = interp::Nlerp({0, 0, 0, 1}, {0, 0, 0, -1}, 0.5f);   // same rotation, opposite sign: no flip
    NEAR(std::fabs(q.w), 1.f, 1e-5);
}

static void TestCombatMessages() {
    using namespace proto;
    // aim point travels only while aiming/shooting
    PlayerStateMsg s;
    s.flags = kFlagAiming;
    s.aim = {10, 20, 30};
    wire::Writer w;
    PutPlayerState(w, s);
    CHECK(w.Size() == 48 + 12);
    wire::Reader r(w.Bytes().data(), w.Size());
    Header h;
    PlayerStateMsg o;
    CHECK(GetHeader(r, h) && GetPlayerState(r, o));
    NEAR(o.aim.y, 20.f, 1e-6);
    // aiming from a car: vehicle block then aim
    s.flags = kFlagAiming | kFlagInVehicle;
    s.veh.model = 1; s.veh.rot = {0, 0, 0, 1};
    wire::Writer wv;
    PutPlayerState(wv, s);
    CHECK(wv.Size() == 48 + 62 + 12);
    wire::Reader rv(wv.Bytes().data(), wv.Size());
    CHECK(GetHeader(rv, h) && GetPlayerState(rv, o) && o.veh.model == 1);
    NEAR(o.aim.z, 30.f, 1e-6);
    // damage: bounds enforced at parse time
    DamageMsg d{4, 35, 0x1B06D571u, 31086};
    wire::Writer wd;
    Put(wd, d);
    wire::Reader rd(wd.Bytes().data(), wd.Size());
    DamageMsg e;
    CHECK(GetHeader(rd, h) && h.type == MsgType::Damage && Get(rd, e) && e.target == 4 && e.amount == 35 && e.bone == 31086);
    CHECK(PvpDamage(26.f, 24817, 208) == 26);    // pistol to the chest: base damage, not the measured 208
    CHECK(PvpDamage(26.f, 31086, 208) == 65);    // headshot x2.5
    CHECK(PvpDamage(26.f, 58271, 30) == 21);     // thigh x0.8
    CHECK(PvpDamage(0.f, 0, 250) == 50);         // no weapon base (vehicle): measured, capped
    CHECK(PvpDamage(500.f, 31086, 0) == kMaxDamagePerHit);
    for (uint16_t bad : {(uint16_t)0, (uint16_t)(kMaxDamagePerHit + 1)}) {
        DamageMsg x{4, bad, 1, 0};
        wire::Writer wx;
        Put(wx, x);
        wire::Reader rx(wx.Bytes().data(), wx.Size());
        CHECK(GetHeader(rx, h) && !Get(rx, e));
    }
    KillFeedMsg k{2, 3, 0x1B06D571u};
    wire::Writer wk;
    Put(wk, k);
    wire::Reader rk(wk.Bytes().data(), wk.Size());
    KillFeedMsg k2;
    CHECK(GetHeader(rk, h) && Get(rk, k2) && k2.victim == 2 && k2.killer == 3);
    ShotRelayMsg sr{5, 7, {1, 2, 3}, {4, 5, 6}};
    wire::Writer ws;
    Put(ws, sr);
    wire::Reader rs(ws.Bytes().data(), ws.Size());
    ShotRelayMsg sr2;
    CHECK(GetHeader(rs, h) && Get(rs, sr2) && sr2.shooter == 5);
    NEAR(sr2.target.z, 6.f, 1e-6);
}

static void TestAppearanceMessages() {
    using namespace proto;
    character::Character c = character::MakeDefault(false);
    c.uuid = character::NewUuid(); c.name = "Gori"; c.money = 999; c.pos = {1, 2, 3, 4};
    character::Character a = character::AppearanceOnly(c);
    CHECK(a.money == 0 && a.weapons.empty() && a.pos.x != 1.f && a.name == "Gori" && a.components[11].drawable == c.components[11].drawable);
    wire::Writer w;
    Put(w, PlayerAppearanceMsg{9, json::Write(character::ToJson(a), 0)});
    wire::Reader r(w.Bytes().data(), w.Size());
    Header h;
    PlayerAppearanceMsg m;
    CHECK(GetHeader(r, h) && h.type == MsgType::PlayerAppearance && Get(r, m) && m.netId == 9);
    json::Value v;
    std::string err;
    character::Character back;
    CHECK(json::Parse(m.json, v, err) && character::FromJson(v, back, err) && back.name == "Gori");
    wire::Writer wj;
    Put(wj, PlayerJoinedMsg{4, "Alex"});
    wire::Reader rj(wj.Bytes().data(), wj.Size());
    PlayerJoinedMsg j;
    CHECK(GetHeader(rj, h) && Get(rj, j) && j.netId == 4 && j.name == "Alex");
}

static void TestWorld() {
    world::Clock c;
    c.Set(23, 59, 0);
    c.Advance(2000);                       // one game minute
    CHECK(c.Hour() == 0 && c.Minute() == 0);
    c.frozen = true;
    c.Advance(600000);
    CHECK(c.Hour() == 0 && c.Minute() == 0);
    c.frozen = false;
    c.Set(25, 99);                          // wraps hour, clamps minute
    CHECK(c.Hour() == 1 && c.Minute() == 59);
    c.SetMinutes(-30);
    CHECK(c.Hour() == 23 && c.Minute() == 30);
    CHECK(world::ValidWeather("RAIN") && !world::ValidWeather("rain") && !world::ValidWeather("LAVA"));

    world::Weather w(42);
    w.minHoldMs = w.maxHoldMs = 1000;
    w.transitionMs = 500;
    CHECK(w.Set("OVERCAST", 0) && !w.Set("LAVA", 0));
    CHECK(w.Current() == "OVERCAST");
    int changes = 0;
    std::string last = w.Current();
    for (int64_t t = 0; t < 200000; t += 100) {
        w.Advance(t);
        float tr = w.Transition(t);
        CHECK(tr >= 0.f && tr <= 1.f);
        if (w.Current() != last) { ++changes; CHECK(world::ValidWeather(w.Current())); last = w.Current(); }
    }
    CHECK(changes > 20);                    // it does cycle
    w.automatic = false;
    w.Set("THUNDER", 300000);
    for (int64_t t = 300000; t < 400000; t += 100) w.Advance(t);
    CHECK(w.Current() == "THUNDER");        // admin-forced weather holds when not automatic

    using namespace proto;
    WorldStateMsg m;
    m.hour = 22; m.minute = 15; m.flags = kWorldBlackout; m.weather = "RAIN"; m.nextWeather = "THUNDER"; m.transition = 0.5f;
    wire::Writer wr;
    Put(wr, m);
    wire::Reader rr(wr.Bytes().data(), wr.Size());
    Header h;
    WorldStateMsg o;
    CHECK(GetHeader(rr, h) && h.type == MsgType::WorldState && Get(rr, o) && o.hour == 22 && o.weather == "RAIN" && (o.flags & kWorldBlackout));
    m.hour = 30;
    wire::Writer wb;
    Put(wb, m);
    wire::Reader rb(wb.Bytes().data(), wb.Size());
    CHECK(GetHeader(rb, h) && !Get(rb, o));
    // Maze Bank transaction log
    {   // co-op jobs (v9)
        wire::Writer jw;
        Put(jw, JobOpenMsg{"Pacific Standard", {1.f, 2.f, 3.f}, 25000});
        wire::Reader jr(jw.Bytes().data(), jw.Size());
        Header jh; JobOpenMsg jo;
        CHECK(GetHeader(jr, jh) && jh.type == MsgType::JobOpen && Get(jr, jo) && jo.title == "Pacific Standard" && jo.start.z == 3.f && jo.payout == 25000);
        JobStateMsg st; st.jobId = 4; st.host = 2; st.status = kJobRunning; st.step = 3; st.objective = {5.f, 6.f, 7.f}; st.text = "Go to the bank"; st.payout = 9; st.members = {3, 5};
        wire::Writer sw; Put(sw, st);
        wire::Reader sr(sw.Bytes().data(), sw.Size());
        JobStateMsg so;
        CHECK(GetHeader(sr, jh) && jh.type == MsgType::JobState && Get(sr, so) && so.jobId == 4 && so.step == 3 && so.text == "Go to the bank" && so.members.size() == 2 && so.members[1] == 5);
        JobListMsg jl; jl.jobs.push_back({7, 2, "Fleeca Job", {1, 1, 1}, 2, kJobLobby});
        wire::Writer lw; Put(lw, jl);
        wire::Reader lr(lw.Bytes().data(), lw.Size());
        JobListMsg lo;
        CHECK(GetHeader(lr, jh) && jh.type == MsgType::JobList && Get(lr, lo) && lo.jobs.size() == 1 && lo.jobs[0].title == "Fleeca Job" && lo.jobs[0].members == 2);
        JobUpdateMsg up; up.status = 9;   // out of range
        wire::Writer uw; Put(uw, up);
        wire::Reader ur(uw.Bytes().data(), uw.Size());
        JobUpdateMsg uo;
        CHECK(GetHeader(ur, jh) && !Get(ur, uo));
        {   // races (v10)
            wire::Writer rw; Put(rw, JobOpenMsg{"Stunt - Double Loop", {1.f, 2.f, 3.f}, 30000, kJobRace});
            wire::Reader rr(rw.Bytes().data(), rw.Size());
            Header rh; JobOpenMsg ro;
            CHECK(GetHeader(rr, rh) && Get(rr, ro) && ro.kind == kJobRace && ro.payout == 30000);
            JobStateMsg rs; rs.jobId = 3; rs.kind = kJobRace; rs.startAt = 123456; rs.members = {4};
            wire::Writer sw2; Put(sw2, rs);
            wire::Reader sr2(sw2.Bytes().data(), sw2.Size());
            JobStateMsg rso;
            CHECK(GetHeader(sr2, rh) && Get(sr2, rso) && rso.kind == kJobRace && rso.startAt == 123456 && rso.members.size() == 1);
            wire::Writer pw; Put(pw, RaceProgressMsg{17, 95432});
            wire::Reader pr(pw.Bytes().data(), pw.Size());
            RaceProgressMsg po;
            CHECK(GetHeader(pr, rh) && rh.type == MsgType::RaceProgress && Get(pr, po) && po.checkpoints == 17 && po.finishMs == 95432);
            RaceStandingsMsg st2; st2.jobId = 3; st2.entries = {{2, 20, 90000}, {5, 18, 0}};
            wire::Writer tw; Put(tw, st2);
            wire::Reader tr(tw.Bytes().data(), tw.Size());
            RaceStandingsMsg to;
            CHECK(GetHeader(tr, rh) && rh.type == MsgType::RaceStandings && Get(tr, to) && to.entries.size() == 2 && to.entries[0].finishMs == 90000 && to.entries[1].netId == 5);
            wire::Writer vw; Put(vw, JobOpenMsg{"Survival - Stab City", {1.f, 2.f, 3.f}, 40000, kJobSurvival});
            wire::Reader vr(vw.Bytes().data(), vw.Size());
            CHECK(GetHeader(vr, rh) && Get(vr, ro) && ro.kind == kJobSurvival && StartsTogether(ro.kind) && !StartsTogether(kJobMission));
            wire::Writer bw; Put(bw, JobOpenMsg{"x", {}, 0, 7});   // unknown kind
            wire::Reader br(bw.Bytes().data(), bw.Size());
            CHECK(GetHeader(br, rh) && !Get(br, ro));
        }
        wire::Writer ew; Put(ew, JobOpenMsg{"", {}, 0});   // a job needs a title
        wire::Reader er(ew.Bytes().data(), ew.Size());
        CHECK(GetHeader(er, jh) && !Get(er, jo));
    }
    LedgerMsg lm;
    lm.requestId = 7;
    lm.rows = {{-100, kBank, "ATM withdrawal", "2026-10-10T15:00:00Z"}, {2500, kWallet, "job", "2026-10-10T14:00:00Z"}};
    wire::Writer wl;
    Put(wl, lm);
    wire::Reader rl(wl.Bytes().data(), wl.Size());
    LedgerMsg lo;
    CHECK(GetHeader(rl, h) && h.type == MsgType::Ledger && Get(rl, lo) && lo.requestId == 7 && lo.rows.size() == 2 && lo.rows[0].delta == -100 &&
          lo.rows[0].account == kBank && lo.rows[1].reason == "job");
    // wanted level travels and is bounded
    PlayerStateMsg s;
    s.wanted = 3;
    wire::Writer ws;
    PutPlayerState(ws, s);
    wire::Reader rs(ws.Bytes().data(), ws.Size());
    PlayerStateMsg so;
    CHECK(GetHeader(rs, h) && GetPlayerState(rs, so) && so.wanted == 3);
    s.wanted = 9;
    wire::Writer ws2;
    PutPlayerState(ws2, s);
    wire::Reader rs2(ws2.Bytes().data(), ws2.Size());
    CHECK(GetHeader(rs2, h) && !GetPlayerState(rs2, so));
}

static void TestVehicles() {
    using namespace vehicle;
    CHECK(ValidPlate("GORI 1") && ValidPlate("A") && !ValidPlate("") && !ValidPlate("TOOLONGPL") && !ValidPlate("gori") && !ValidPlate("   "));
    CHECK(CanTransition(State::Stored, State::Active) && CanTransition(State::Active, State::Active) && CanTransition(State::Active, State::Destroyed));
    CHECK(CanTransition(State::Destroyed, State::Stored) && !CanTransition(State::Destroyed, State::Active) && !CanTransition(State::Stored, State::Destroyed));
    Personal v;
    v.model = character::Joaat("sultan");
    v.plate = "GORI";
    v.mods[11] = 3;   // engine
    v.turbo = true;
    v.color1 = 28;
    v.x = 1; v.y = 2; v.z = 3; v.heading = 370;
    v.state = State::Active;
    std::string err;
    CHECK(Validate(v, err));
    NEAR(v.heading, 10.f, 1e-3);
    json::Value j = ToJson(v);
    Personal b;
    CHECK(FromJson(j, b, err) && b.model == v.model && b.plate == "GORI" && b.mods[11] == 3 && b.mods[0] == -1 && b.turbo && b.color1 == 28 && b.state == State::Active);
    Personal bad = v;
    bad.plate = "bad!";
    CHECK(!Validate(bad, err));
    bad = v;
    bad.model = 0;
    CHECK(!Validate(bad, err));
    bad = v;
    bad.mods[3] = 9999;
    CHECK(Validate(bad, err) && bad.mods[3] == 255);   // clamped
    using namespace proto;
    VehicleUpdateMsg u{7, "uuid", 2, {1, 2, 3}, 90, 500, 600};
    wire::Writer w;
    Put(w, u);
    wire::Reader r(w.Bytes().data(), w.Size());
    Header h;
    VehicleUpdateMsg u2;
    CHECK(GetHeader(r, h) && Get(r, u2) && u2.state == 2 && u2.uuid == "uuid");
    u.state = 9;
    wire::Writer wb;
    Put(wb, u);
    wire::Reader rb(wb.Bytes().data(), wb.Size());
    CHECK(GetHeader(rb, h) && !Get(rb, u2));
    VehicleListMsg l;
    l.vehicles = {"{}", "{\"a\":1}"};
    wire::Writer wl;
    Put(wl, l);
    wire::Reader rl(wl.Bytes().data(), wl.Size());
    VehicleListMsg l2;
    CHECK(GetHeader(rl, h) && Get(rl, l2) && l2.vehicles.size() == 2 && l2.vehicles[1] == "{\"a\":1}");
}

int main() {
    TestVehicles();
    TestWorld();
    TestInterp();
    TestAppearanceMessages();
    TestVehicleState();
    TestCombatMessages();
    TestSha256();
    TestControlMessages();
    TestToml();
    TestCharacter();
    TestProtocol();
    std::printf("tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
