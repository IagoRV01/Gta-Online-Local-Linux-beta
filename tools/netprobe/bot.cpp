// gobot — a second player for tests: a fake client that behaves like a crew member.
//   gobot <server-ip> [port=4499] [name=Bot] [seconds=600]
// It logs in (registering on first run, token in gobot-<name>.token), makes a character if it has none, shows its rank
// and look to the others, and then, by itself:
//   - follows the first other player: 2.5 m behind on foot, or in a passenger seat of the car that player drives;
//   - joins every job another player opens (missions, heists, races, deathmatches, survivals) and leaves it when it ends;
//   - in a race passes a checkpoint every BOT_CPMS ms (default 7000) and finishes when it has passed them all (Gori's race setup
//     says how many: "R|file|laps|vehicle|tod|weather|traffic|catchup|slip|checkpoints"), then sends its results ("rres");
//   - fights: it has health and armour, takes the hits other players report (DamageApply), dies (Death, its killer
//     credited, so deathmatch scores count) and respawns 5 s later; in a deathmatch it circles the first other player
//     12 m away and fires at it every BOT_SHOOTMS ms (default 2000), a hit (Damage, 25) with chance BOT_ACC % (default 50);
//   - in a survival it fights each wave the host announces ("S|place|wave") for BOT_WAVEMS ms (default 20000), with a
//     kill every few seconds, then reports the wave cleared (RaceProgress: kills, waves), so the host moves everyone on;
//   - votes in the next-job screen (Gori's "njh" / "njv" messages) for the slot in BOT_VOTE (default 0), 4 s after the list;
//   - prints the job states, its payouts (MoneyResult with requestId 0) and the messages it gets.
// A file gobot-<name>.cmd next to it is read every second, one command a line, then emptied:
//   join <jobId> | leave | vote <slot> | pace <ms per checkpoint> | shoot on|off | down | stay x y z | follow | say <kind> <text> | quit
//   host <kind> <payout> <title...> (a job of its own where the player is, so the player can be a member: tests of the
//   member's side, e.g. "say hcut <scene>|0|-1|2|<bot name>" as a heist leader) | close (that job closed)
//   home <property index> <building> (it owns a home there, as far as Gori's buzzer is concerned: it answers "aptq" for that
//   building right away and lets a visitor in, "buzzok", 2 s after the "buzz")
//   drive <model> x y z [heading] (it sits at the wheel of its own car there: Arena War's modes between players) | walk
//   goto x y z <speed m/s> (its car, or the bot on foot, moves there in a straight line)
//   score <n> [finishMs] (a scored event's RaceProgress) | run <text...> (its own job starts) | pass (its own job passed)
// Build: ./build.sh probe (with netprobe). Use it against a test server, not the live one: it registers an account.
#include <enet/enet.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include "protocol/protocol.h"
#include "common/character.h"
#include "common/json.h"

using namespace proto;

static uint32_t Joaat(const char *s) {   // GTA's name hash
    uint32_t h = 0;
    for (; *s; ++s) { h += (uint8_t)tolower((unsigned char)*s); h += h << 10; h ^= h >> 6; }
    h += h << 3; h ^= h >> 11; h += h << 15;
    return h;
}
static int EnvInt(const char *n, int def) { const char *v = std::getenv(n); return v ? std::atoi(v) : def; }

static long long Ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char **argv) {
    if (argc < 2) { std::printf("usage: gobot <server-ip> [port=4499] [name=Bot] [seconds=600]\n"); return 1; }
    const char *ip = argv[1];
    int port = argc > 2 ? std::atoi(argv[2]) : 4499;
    std::string name = argc > 3 ? argv[3] : "Bot";
    int seconds = argc > 4 ? std::atoi(argv[4]) : 600;
    int voteSlot = std::getenv("BOT_VOTE") ? std::atoi(std::getenv("BOT_VOTE")) : 0;
    int cpMs = std::getenv("BOT_CPMS") ? std::max(500, std::atoi(std::getenv("BOT_CPMS"))) : 7000;
    int shootMs = std::max(300, EnvInt("BOT_SHOOTMS", 2000)), accuracy = EnvInt("BOT_ACC", 50), waveMs = std::max(3000, EnvInt("BOT_WAVEMS", 20000));
    std::srand((unsigned)Ms());
    std::string tokenFile = "gobot-" + name + ".token", cmdFile = "gobot-" + name + ".cmd";
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    if (enet_initialize() != 0) { std::printf("enet init failed\n"); return 2; }
    ENetHost *host = enet_host_create(nullptr, 1, kNumChannels, 0, 0);
    ENetAddress addr{};
    enet_address_set_host(&addr, ip);
    addr.port = (enet_uint16)port;
    ENetPeer *peer = enet_host_connect(host, &addr, kNumChannels, 0);
    ENetEvent ev;
    if (enet_host_service(host, &ev, 3000) <= 0 || ev.type != ENET_EVENT_TYPE_CONNECT) { std::printf("FAIL: no answer from %s:%d\n", ip, port); return 3; }
    auto send = [&](const wire::Writer &w, uint8_t ch, bool rel) {
        enet_peer_send(peer, ch, enet_packet_create(w.Bytes().data(), w.Size(), rel ? ENET_PACKET_FLAG_RELIABLE : 0));
    };
    long long start = Ms(), end = start + seconds * 1000LL, nextState = start, nextCmd = start + 1000;
    auto T = [&] { return (Ms() - start) / 1000.0; };
    { wire::Writer w; PutHeader(w, MsgType::Hello); w.U32(3889); w.Str("gobot-0.1"); send(w, kChanControl, true); }

    bool authed = false, ready = false, following = true;
    uint16_t self = 0;
    std::map<uint16_t, std::string> names;
    // the player followed: its last state
    uint16_t lead = 0; RemoteState leadSt; bool haveLead = false;
    Vec3 stayAt{}; bool staying = false;
    uint32_t carModel = 0; float carHeading = 0.f; Vec3 goTo{}; float goSpeed = 0.f; Vec3 carVel{};   // its own car (drive / goto)
    // jobs
    uint32_t joined = 0, wantJoin = 0; uint8_t joinedKind = 0;
    std::set<uint32_t> done;   // jobs over: not joined again
    long long raceT0 = 0; int raceCps = 0;
    int raceTotal = 0, raceLaps = 1; std::string raceFile, raceVeh; bool raceDone = false;
    // combat: health / armour as GTA's (health = ped health - 100), death and respawn, the deathmatch's shots
    const uint32_t kRifle = Joaat("WEAPON_CARBINERIFLE");
    int hp = 100, armour = 50; long long deadUntil = 0, shotAt = 0, nextShot = 0; bool shooting = true;
    uint16_t lastAttacker = 0; uint32_t lastWeapon = 0;
    int kills = 0, deaths = 0;
    float orbit = 0.f; bool jobRunning = false; Vec3 jobAt{};
    // survival: the wave announced, the wave being fought (cleared at waveDoneAt), our kills, down until the next wave
    int svWave = 0, svCleared = 0, svKills = 0; long long svWaveDoneAt = 0, svNextKill = 0; bool svDown = false;
    auto progress = [&](uint16_t cps, uint32_t fin) { wire::Writer pw; Put(pw, RaceProgressMsg{cps, fin}); send(pw, kChanEvents, true); };
    // next-job vote
    int homeProp = -1, homeOb = -1; long long letInAt = 0; uint16_t visitorId = 0;   // the buzzer (Gori's Buzzer.inc)
    std::set<std::string> helloed; std::map<std::string, long long> voteAt; std::set<std::string> voted;
    uint32_t seq = 0;
    std::string myJson;

    auto sendMsg = [&](uint16_t to, const std::string &kind, const std::string &text) {
        wire::Writer w; Put(w, PlayerMessageMsg{to, kind, text}); send(w, kChanEvents, true);
    };
    auto join = [&](uint32_t id) {
        wire::Writer w; Put(w, JobJoinMsg{id}); send(w, kChanEvents, true);
        std::printf("t=%.1fs join job %u\n", T(), id);
    };

    while (Ms() < end) {
        while (enet_host_service(host, &ev, 5) > 0) {
            if (ev.type == ENET_EVENT_TYPE_DISCONNECT) { std::printf("t=%.1fs disconnected by the server\n", T()); return 4; }
            if (ev.type != ENET_EVENT_TYPE_RECEIVE) continue;
            wire::Reader r(ev.packet->data, ev.packet->dataLength);
            Header h;
            if (!GetHeader(r, h)) { enet_packet_destroy(ev.packet); continue; }
            switch (h.type) {
            case MsgType::Welcome: {
                std::string token; std::ifstream(tokenFile) >> token;
                wire::Writer a; Put(a, AuthMsg{name, token, kRolePlayer}); send(a, kChanControl, true);
                break;
            }
            case MsgType::AuthResult: {
                AuthResultMsg ar; Get(r, ar);
                authed = ar.ok; self = ar.netId;
                if (!ar.newToken.empty()) std::ofstream(tokenFile) << ar.newToken;
                std::printf("t=%.1fs auth %s, netId %u%s %s\n", T(), ar.ok ? "ok" : "REFUSED", ar.netId, ar.newToken.empty() ? "" : " (new account)", ar.reason.c_str());
                if (authed) { wire::Writer pr; PutProfileRequest(pr); send(pr, kChanControl, true); }
                break;
            }
            case MsgType::Reject: { uint8_t c = r.U8(); std::string why = r.Str(kMaxReason); std::printf("REJECTED (%u): %s\n", c, why.c_str()); break; }
            case MsgType::Profile: {
                ProfileMsg pm; Get(r, pm);
                character::Character ch; json::Value v; std::string err;
                bool have = pm.exists && json::Parse(pm.json, v, err) && character::FromJson(v, ch, err);
                if (!have) { ch = character::MakeDefault(false); ch.uuid = character::NewUuid(); ch.name = name; }
                myJson = json::Write(character::ToJson(ch), 0);
                if (!have) { wire::Writer ps; Put(ps, ProfileSaveMsg{0, myJson}); send(ps, kChanControl, true); std::printf("t=%.1fs no character: made one\n", T()); }
                else { ready = true; std::printf("t=%.1fs character rev %u, bank %lld\n", T(), pm.revision, (long long)ch.bank); }
                if (ready) {
                    wire::Writer aw; Put(aw, AppearanceUpdateMsg{myJson}); send(aw, kChanControl, true);
                    wire::Writer iw; Put(iw, PlayerInfoMsg{25, 0}); send(iw, kChanControl, true);
                }
                break;
            }
            case MsgType::ProfileSaved: {
                ProfileSavedMsg sv; Get(r, sv);
                std::printf("t=%.1fs character saved: %d rev %u %s\n", T(), sv.ok, sv.revision, sv.reason.c_str());
                if (sv.ok && !ready) {
                    ready = true;
                    wire::Writer aw; Put(aw, AppearanceUpdateMsg{myJson}); send(aw, kChanControl, true);
                    wire::Writer iw; Put(iw, PlayerInfoMsg{25, 0}); send(iw, kChanControl, true);
                }
                break;
            }
            case MsgType::PlayerJoined: {
                uint16_t id = r.U16(); std::string n = r.Str(kMaxName);
                names[id] = n;
                std::printf("t=%.1fs player %s (netId %u)\n", T(), n.c_str(), id);
                break;
            }
            case MsgType::PlayerLeft: { uint16_t id = r.U16(); std::printf("t=%.1fs %s left\n", T(), names[id].c_str()); if (id == lead) { lead = 0; haveLead = false; } break; }
            case MsgType::Snapshot: {
                uint32_t tick, st; std::vector<RemoteState> ps;
                if (GetSnapshot(r, tick, st, ps)) for (const auto &o : ps) {
                    if (o.netId == self) continue;
                    if (!lead) { lead = o.netId; std::printf("t=%.1fs following %s\n", T(), names[lead].c_str()); }
                    if (o.netId == lead) { leadSt = o; haveLead = true; }
                }
                break;
            }
            case MsgType::JobList: {
                JobListMsg jl;
                if (Get(r, jl)) {
                    std::printf("t=%.1fs jobs:", T());
                    for (auto &j : jl.jobs) std::printf(" [%u \"%s\" host %s members %u status %u kind %u]", j.id, j.title.c_str(), names[j.host].c_str(), j.members, j.status, j.kind);
                    std::printf("\n");
                    if (ready && !joined && !wantJoin)
                        for (auto &j : jl.jobs)
                            if (j.host != self && !done.count(j.id) && j.status <= kJobRunning) { wantJoin = j.id; joinedKind = j.kind; join(j.id); break; }
                }
                break;
            }
            case MsgType::JobState: {
                JobStateMsg js;
                if (Get(r, js)) {
                    std::printf("t=%.1fs job %u status %u step %u \"%s\" payout %u members %d kind %u\n", T(), js.jobId, js.status, js.step, js.text.c_str(), js.payout, (int)js.members.size(), js.kind);
                    bool mine = false;
                    for (auto m : js.members) if (m == self) mine = true;
                    if (mine) { joined = js.jobId; wantJoin = 0; joinedKind = js.kind; }
                    if (mine && StartsTogether(js.kind) && js.status == kJobRunning && js.startAt && !raceT0) {
                        raceT0 = Ms() + kRaceStartDelayMs; raceCps = 0; raceDone = false; raceTotal = 0;
                        if (!js.text.compare(0, 2, "R|")) {   // Gori's race setup
                            std::vector<std::string> f; std::string cur;
                            for (char ch : js.text + "|") { if (ch == '|') { f.push_back(cur); cur.clear(); } else cur += ch; }
                            if (f.size() > 3) { raceFile = f[1]; raceLaps = std::max(1, std::atoi(f[2].c_str())); raceVeh = f[3]; }
                            if (f.size() > 9) raceTotal = std::atoi(f[9].c_str());
                        }
                        std::printf("t=%.1fs race %s: %d checkpoints, one every %d ms\n", T(), raceFile.c_str(), raceTotal, cpMs);
                    }
                    if (mine) { jobRunning = js.status == kJobRunning; jobAt = js.objective; }
                    if (mine && js.kind == kJobDeathmatch && js.status == kJobRunning && hp > 0) armour = 100;   // the deathmatch's loadout
                    if (mine && js.kind == kJobSurvival && js.status == kJobRunning && !js.text.compare(0, 2, "S|")) {
                        int w = std::atoi(js.text.c_str() + js.text.rfind('|') + 1);
                        if (w > svWave) {
                            if (svWave == 0) { svCleared = 0; svKills = 0; }
                            svWave = w; svDown = false;
                            svWaveDoneAt = Ms() + (w == 1 ? kRaceStartDelayMs : 0) + waveMs; svNextKill = Ms() + 2500;
                            std::printf("t=%.1fs survival: wave %d\n", T(), w);
                            progress((uint16_t)svKills, (uint32_t)svCleared);
                        }
                    }
                    if (js.jobId == joined && js.status >= kJobPassed) {
                        std::printf("t=%.1fs job %u over (%s)\n", T(), js.jobId, js.status == kJobPassed ? "passed" : js.status == kJobFailed ? "failed" : "closed");
                        done.insert(js.jobId); joined = 0; raceT0 = 0; jobRunning = false; svWave = 0; armour = std::min(armour, 50);
                    }
                }
                break;
            }
            case MsgType::RaceStandings: {
                RaceStandingsMsg s;
                if (Get(r, s)) { std::printf("t=%.1fs standings:", T()); for (auto &e : s.entries) std::printf(" %s(cp %u%s)", names[e.netId].c_str(), e.checkpoints, e.finishMs ? " done" : ""); std::printf("\n"); }
                break;
            }
            case MsgType::MoneyResult: {
                MoneyResultMsg mr;
                if (Get(r, mr)) std::printf("t=%.1fs money #%u ok %d wallet %lld bank %lld %s\n", T(), mr.requestId, mr.ok, (long long)mr.wallet, (long long)mr.bank, mr.reason.c_str());
                break;
            }
            case MsgType::PlayerMessageRelay: {
                PlayerMessageRelayMsg m;
                if (Get(r, m)) {
                    std::printf("t=%.1fs message from %s: %s %s\n", T(), names[m.from].c_str(), m.kind.c_str(), m.text.substr(0, 120).c_str());
                    std::string tag = m.text.substr(0, m.text.find('|'));
                    if ((m.kind == "njh" || m.kind == "njl") && !helloed.count(tag)) {   // the next-job screen: say hello, vote soon
                        helloed.insert(tag);
                        sendMsg(0, "njh", tag + "|" + name);
                        voteAt[tag] = Ms() + 4000;
                    }
                    if (m.kind == "aptq" && homeProp >= 0 && std::atoi(m.text.c_str()) == homeOb) sendMsg(m.from, "apta", std::to_string(homeProp) + "|" + name);
                    if (m.kind == "buzz" && homeProp >= 0 && std::atoi(m.text.c_str()) == homeProp) { visitorId = m.from; letInAt = Ms() + 2000; }
                    if (m.kind == "njr") { voted.erase(tag); voteAt[tag] = Ms() + 4000; }
                    if (m.kind == "njd") std::printf("t=%.1fs the next job decided: %s\n", T(), m.text.c_str());
                }
                break;
            }
            case MsgType::KillFeed: {
                KillFeedMsg k;
                if (Get(r, k)) {
                    if (k.killer == self && k.victim != self) ++kills;
                    std::printf("t=%.1fs %s killed by %s (bot %d kills %d deaths)\n", T(), k.victim == self ? "the bot" : names[k.victim].c_str(),
                                k.killer == self ? "the bot" : k.killer ? names[k.killer].c_str() : "-", kills, deaths);
                }
                break;
            }
            case MsgType::DamageApply: {
                DamageApplyMsg d;
                if (!Get(r, d) || hp <= 0) break;
                int a = d.amount;
                int fromArmour = std::min(armour, a);
                armour -= fromArmour; a -= fromArmour; hp -= a;
                lastAttacker = d.attacker; lastWeapon = d.weapon;
                std::printf("t=%.1fs hit by %s for %u (weapon 0x%08X) -> health %d armour %d\n", T(), names[d.attacker].c_str(), d.amount, d.weapon, std::max(0, hp), armour);
                if (hp <= 0) {
                    hp = 0; ++deaths; deadUntil = Ms() + 5000;
                    wire::Writer dw; Put(dw, DeathMsg{lastAttacker, lastWeapon}); send(dw, kChanEvents, true);
                    std::printf("t=%.1fs the bot died, killed by %s\n", T(), names[lastAttacker].c_str());
                    if (joinedKind == kJobSurvival && svWave) { svDown = true; progress((uint16_t)svKills, (uint32_t)svCleared | kSurvivalDown); }
                }
                break;
            }
            default: break;
            }
            enet_packet_destroy(ev.packet);
        }
        long long now = Ms();
        // the votes
        for (auto &[tag, at] : voteAt)
            if (at && now >= at && !voted.count(tag)) {
                voted.insert(tag); at = 0;
                sendMsg(0, "njv", tag + "|" + std::to_string(voteSlot) + "|" + name);
                std::printf("t=%.1fs voted for slot %d\n", T(), voteSlot);
            }
        // a race: a checkpoint every cpMs, the finish at the last one
        if (raceT0 && now >= raceT0 && joinedKind == kJobRace && !raceDone) {
            int due = (int)((now - raceT0) / cpMs);
            if (raceTotal > 0 && due > raceTotal) due = raceTotal;
            if (due > raceCps) {
                raceCps = due;
                bool fin = raceTotal > 0 && raceCps >= raceTotal;
                uint32_t finMs = fin ? (uint32_t)(now - raceT0) : 0;
                wire::Writer pw; Put(pw, RaceProgressMsg{(uint16_t)raceCps, finMs}); send(pw, kChanEvents, true);
                if (fin) {
                    raceDone = true;
                    std::printf("t=%.1fs race finished in %.3f s\n", T(), finMs / 1000.0);
                    std::string veh = raceVeh.empty() ? std::string("-") : raceVeh;
                    int lap = raceLaps > 1 ? (int)(finMs / raceLaps) : 0;
                    sendMsg(0, "rres", raceFile + "|" + name + "|" + veh + "|" + std::to_string(lap) + "|" + std::to_string(600 + 10 * raceTotal));
                }
            }
        }
        // dead: back after 5 s, full health (a deathmatch's loadout again)
        if (hp <= 0 && now >= deadUntil) {
            hp = 100; armour = jobRunning && joinedKind == kJobDeathmatch ? 100 : 50; orbit += 2.5f;
            std::printf("t=%.1fs the bot respawned\n", T());
        }
        bool fighting = jobRunning && joinedKind == kJobDeathmatch && raceT0 && now >= raceT0;
        if (fighting && shooting && hp > 0 && haveLead && !(leadSt.flags & kFlagDead) && now >= nextShot) {
            nextShot = now + shootMs;
            Vec3 tgt = (leadSt.flags & kFlagInVehicle) ? leadSt.veh.pos : leadSt.pos;
            float o = orbit;
            Vec3 me{tgt.x + std::cos(o) * 12.f, tgt.y + std::sin(o) * 12.f, tgt.z};
            wire::Writer sw; Put(sw, ShotMsg{kRifle, {me.x, me.y, me.z + 0.5f}, {tgt.x, tgt.y, tgt.z + 0.3f}}); send(sw, kChanEvents, true);
            shotAt = now;
            if (std::rand() % 100 < accuracy) { wire::Writer dw; Put(dw, DamageMsg{lead, 25, kRifle, 0}); send(dw, kChanEvents, true); }
        }
        if (joinedKind == kJobSurvival && svWave && !svDown && hp > 0) {
            if (svWaveDoneAt && now >= svNextKill && now < svWaveDoneAt) { ++svKills; svNextKill = now + 2500 + std::rand() % 3000; progress((uint16_t)svKills, (uint32_t)svCleared); }
            if (svWaveDoneAt && now >= svWaveDoneAt) {
                svWaveDoneAt = 0; svCleared = svWave;
                progress((uint16_t)svKills, (uint32_t)svCleared);
                std::printf("t=%.1fs survival: wave %d cleared, %d kills\n", T(), svCleared, svKills);
            }
        }
        if (letInAt && now >= letInAt) { letInAt = 0; sendMsg(visitorId, "buzzok", std::to_string(homeProp)); std::printf("t=%.1fs let %s in\n", T(), names[visitorId].c_str()); }
        // the commands file
        if (now >= nextCmd) {
            nextCmd = now + 1000;
            std::ifstream f(cmdFile);
            std::string line; bool any = false;
            while (std::getline(f, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                any = true;
                std::istringstream is(line); std::string c; is >> c;
                std::printf("t=%.1fs command: %s\n", T(), line.c_str());
                if (c == "join") { uint32_t id = 0; is >> id; wantJoin = id; join(id); }
                else if (c == "leave") { join(0); if (joined) done.insert(joined); joined = 0; }
                else if (c == "host") {
                    int kind = 0, pay = 0; std::string title; is >> kind >> pay; std::getline(is, title);
                    if (!title.empty() && title[0] == ' ') title.erase(0, 1);
                    Vec3 at = haveLead ? leadSt.pos : Vec3{};
                    wire::Writer jw; Put(jw, JobOpenMsg{title, at, (uint32_t)pay, (uint8_t)kind}); send(jw, kChanEvents, true);
                    std::printf("t=%.1fs hosting \"%s\" (kind %d)\n", T(), title.c_str(), kind);
                }
                else if (c == "home") { is >> homeProp >> homeOb; }
                else if (c == "drive") {
                    std::string model; is >> model >> stayAt.x >> stayAt.y >> stayAt.z; if (!(is >> carHeading)) carHeading = 0.f;
                    carModel = Joaat(model.c_str()); staying = true; following = false; goSpeed = 0.f;
                }
                else if (c == "walk") { carModel = 0; }
                else if (c == "goto") { is >> goTo.x >> goTo.y >> goTo.z >> goSpeed; staying = true; following = false; }
                else if (c == "score") { int n = 0; unsigned fin = 0; is >> n; is >> fin; progress((uint16_t)n, fin); }
                else if (c == "run") { std::string text; std::getline(is, text); if (!text.empty() && text[0] == ' ') text.erase(0, 1); wire::Writer jw; Put(jw, JobUpdateMsg{kJobRunning, 0, {}, text}); send(jw, kChanEvents, true); }
                else if (c == "pass") { wire::Writer jw; Put(jw, JobUpdateMsg{kJobPassed, 0, {}, ""}); send(jw, kChanEvents, true); }
                else if (c == "close") { wire::Writer jw; Put(jw, JobUpdateMsg{kJobClosed, 0, {}, ""}); send(jw, kChanEvents, true); }
                else if (c == "shoot") { std::string v; is >> v; shooting = v != "off"; }
                else if (c == "down") { if (svWave) { svDown = true; progress((uint16_t)svKills, (uint32_t)svCleared | kSurvivalDown); } }
                else if (c == "pace") { is >> cpMs; if (cpMs < 500) cpMs = 500; }
                else if (c == "vote") { is >> voteSlot; voted.clear(); for (auto &[t, a] : voteAt) a = now; }
                else if (c == "stay") { is >> stayAt.x >> stayAt.y >> stayAt.z; staying = true; following = false; }
                else if (c == "follow") { staying = false; following = true; }
                else if (c == "say") { std::string k, rest; is >> k; std::getline(is, rest); if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1); sendMsg(0, k, rest); }
                else if (c == "quit") end = now;
            }
            f.close();
            if (any) std::ofstream(cmdFile, std::ios::trunc);
        }
        // the state, 20 Hz
        if (authed && ready && now >= nextState) {
            nextState += 50;
            PlayerStateMsg s;
            s.seq = ++seq; s.clientTimeMs = (uint32_t)(now - start); s.health = (uint8_t)std::max(0, hp); s.armour = (uint8_t)armour;
            if (hp <= 0) s.flags |= kFlagDead;
            carVel = {};
            if (staying && goSpeed > 0.f) {   // on its way to a point, 50 ms a step
                float dx = goTo.x - stayAt.x, dy = goTo.y - stayAt.y, dz = goTo.z - stayAt.z, d = std::sqrt(dx * dx + dy * dy + dz * dz), step = goSpeed * 0.05f;
                if (d <= step) { stayAt = goTo; goSpeed = 0.f; }
                else {
                    stayAt = {stayAt.x + dx / d * step, stayAt.y + dy / d * step, stayAt.z + dz / d * step};
                    carVel = {dx / d * goSpeed, dy / d * goSpeed, dz / d * goSpeed};
                    carHeading = std::fmod(std::atan2(-dx, dy) * 57.2958f + 360.f, 360.f);
                }
            }
            if (staying && carModel) {   // at the wheel of its own car
                s.flags |= kFlagInVehicle;
                s.veh.model = carModel; s.veh.seat = -1; s.veh.driverNetId = self;
                s.veh.pos = stayAt; s.veh.vel = carVel;
                float hr = carHeading / 57.2958f / 2.f;
                s.veh.rot = {0.f, 0.f, std::sin(hr), std::cos(hr)};
                s.veh.vflags = kVehEngine;
                s.pos = stayAt; s.vel = carVel; s.heading = carHeading; s.move = Move::Still;
            } else if (staying || !haveLead) { s.pos = staying ? stayAt : Vec3{-1037.8f, -2737.6f, 20.2f}; s.vel = carVel; s.heading = carHeading; s.move = goSpeed > 0.f ? Move::Walk : Move::Still; }
            else if (leadSt.flags & kFlagInVehicle) {   // ride along: a passenger of the car the player drives
                s.flags |= kFlagInVehicle;
                s.veh = leadSt.veh;
                s.veh.seat = 0;
                s.veh.driverNetId = leadSt.veh.driverNetId ? leadSt.veh.driverNetId : lead;
                s.pos = leadSt.veh.pos; s.vel = leadSt.veh.vel; s.heading = leadSt.heading; s.move = Move::Still;
            } else if (fighting && !(leadSt.flags & kFlagInVehicle)) {   // a deathmatch: around the player, 12 m away, facing it
                if (hp > 0) orbit += 0.05f * 0.35f;   // 0.35 rad/s
                Vec3 t = leadSt.pos;
                s.pos = {t.x + std::cos(orbit) * 12.f, t.y + std::sin(orbit) * 12.f, t.z};
                s.heading = std::fmod(std::atan2(-(t.x - s.pos.x), t.y - s.pos.y) * 57.2958f + 360.f, 360.f);
                s.vel = {-std::sin(orbit) * 4.2f, std::cos(orbit) * 4.2f, 0.f};
                s.move = hp > 0 ? Move::Walk : Move::Still;
                s.weapon = kRifle;
                if (hp > 0 && shooting) { s.flags |= kFlagAiming; s.aim = {t.x, t.y, t.z + 0.3f}; if (now - shotAt < 200) s.flags |= kFlagShooting; }
            } else {   // on foot, 2.5 m behind and to the side
                float hr = leadSt.heading / 57.2958f;
                float bx = std::sin(hr) * 2.5f + std::cos(hr) * 1.2f, by = -std::cos(hr) * 2.5f + std::sin(hr) * 1.2f;
                s.pos = {leadSt.pos.x + bx, leadSt.pos.y + by, leadSt.pos.z};
                s.vel = leadSt.vel; s.heading = leadSt.heading; s.move = leadSt.move;
            }
            wire::Writer w; PutPlayerState(w, s); send(w, kChanState, false);
        }
    }
    if (joined) join(0);
    enet_peer_disconnect(peer, 0);
    while (enet_host_service(host, &ev, 500) > 0) if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
    enet_host_destroy(host);
    enet_deinitialize();
    std::printf("t=%.1fs bye\n", T());
    return 0;
}
