// netprobe — a fake GTA client for the game server. Speaks the real protocol (Hello, Auth,
// PlayerState at 20 Hz) and reports RTT, loss and the snapshots it gets back. Used to prove the
// LAN path (BC-250 -> Windows UDP) and to be the "second client" before a second GTA exists.
//   netprobe <server-ip> [port] [name] [seconds]
// Build on Linux:   g++ -std=c++17 -O2 -I../../shared -I../../third_party/enet/include netprobe.cpp ../../third_party/enet/{callbacks,compress,host,list,packet,peer,protocol,unix}.c -o netprobe
// Build on Windows: ./build.sh probe   (w64devkit)
#include <enet/enet.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include "protocol/protocol.h"
#ifdef NETPROBE_ECONOMY
#include "common/character.h"
#include "common/json.h"
#include "common/vehicle.h"
#endif

using namespace proto;

static long long Ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static uint32_t Joaat(const char *s) {
    uint32_t h = 0;
    for (; *s; ++s) { char c = *s; if (c >= 'A' && c <= 'Z') c += 32; h += (uint8_t)c; h += h << 10; h ^= h >> 6; }
    h += h << 3; h ^= h >> 11; h += h << 15;
    return h;
}

int main(int argc, char **argv) {
    if (argc < 2) { std::printf("usage: netprobe <server-ip> [port=4499] [name=Probe] [seconds=10] [cx cy cz radius speed [vehicle-model | combat]]\n"); return 1; }
    const char *ip = argv[1];
    int port = argc > 2 ? std::atoi(argv[2]) : 4499;
    std::string name = argc > 3 ? argv[3] : "Probe";
    int seconds = argc > 4 ? std::atoi(argv[4]) : 10;
    std::string tokenFile = "netprobe-" + name + ".token";
    // optional walk path: centre x y z (ped coords, i.e. ~1 m above ground), radius, speed m/s
    float cx = argc > 5 ? (float)std::atof(argv[5]) : -1037.8f, cy = argc > 6 ? (float)std::atof(argv[6]) : -2737.6f;
    float cz = argc > 7 ? (float)std::atof(argv[7]) : 20.2f, radius = argc > 8 ? (float)std::atof(argv[8]) : 20.f;
    float speed = argc > 9 ? (float)std::atof(argv[9]) : 5.f;
    float w = speed / radius;   // angular speed (rad/s)
    // vehicle mode: 6 s on foot, drive the circle, last 10 s on foot next to the parked car
    std::string vehModel = argc > 10 ? argv[10] : "";
    // combat mode: stand still with a pistol, take damage (100 hp, respawn after 5 s), shoot at
    // the other player every 3 s (visual) and hurt them for 30 every 4 s (tests DamageApply/kills)
    bool combat = vehModel == "combat";
    if (combat) vehModel.clear();
    // economy mode (Windows build only): profile + money round trips against the server
    // co-op job modes (v9): "jobhost" opens a job here and walks it through 3 steps to "passed" (needs >= 90 s);
    // "jobjoin" joins the first open job, follows its state and reports the payout
    bool jobHost = vehModel == "jobhost", jobJoin = vehModel == "jobjoin";
    // race modes (v10): "racehost" opens a race, starts it at 10 s and passes a checkpoint every 5 s (15 to finish),
    // "racejoin" joins it and passes one every 6 s; both print the standings (run >= 110 s)
    bool raceHost = vehModel == "racehost", raceJoin = vehModel == "racejoin";
    // deathmatch modes: "dmhost" opens one and starts it at 10 s, "dmjoin" joins; from the start the host is
    // "killed" by the other every 9 s and the joiner every 5 s; the host passes it 60 s after the start
    bool dmHost = vehModel == "dmhost", dmJoin = vehModel == "dmjoin";
    // survival modes: "svhost" opens one (payout 40000) and starts it at 10 s, "svjoin" joins; the host kills one
    // enemy every 4 s and clears a wave every 8 s, the joiner every 6 / 12 s; the host passes it 62 s after the start
    // (host 7 waves -> 28000, joiner 4 waves -> 16000)
    bool svHost = vehModel == "svhost", svJoin = vehModel == "svjoin";
    if (svHost) dmHost = true;
    if (svJoin) dmJoin = true;
    if (dmHost) raceHost = true;
    if (dmJoin) raceJoin = true;
    uint16_t otherId = 0;
    long long nextDeath = 0;
    if (raceJoin) jobJoin = true;
    if (jobHost || jobJoin || raceHost) vehModel = "economy";
    long long raceT0 = 0;   // local time of the race start
    int raceCps = 0, racePhase = 0;
    bool raceDone = false;
    uint32_t joined = 0;
    int jobPhase = 0;
    bool economyMode = vehModel == "economy" || vehModel == "garage";
    bool garageMode = vehModel == "garage";
    if (economyMode) vehModel.clear();
    int garStep = 0;
    std::string garUuid;
    int econStep = 0;
    uint32_t profileRev = 0;
    bool haveProfile = false;
    int hp = 100;
    long long deadUntil = 0, nextShot = 0, nextHurt = Ms() + 25000;   // let the other player shoot first
    uint16_t lastAttacker = 0, enemy = 0;
    Vec3 enemyPos;
    uint16_t selfNetId = 0;

    if (enet_initialize() != 0) { std::printf("enet init failed\n"); return 2; }
    ENetHost *host = enet_host_create(nullptr, 1, kNumChannels, 0, 0);
    ENetAddress addr{};
    enet_address_set_host(&addr, ip);
    addr.port = (enet_uint16)port;
    ENetPeer *peer = enet_host_connect(host, &addr, kNumChannels, 0);
    ENetEvent ev;
    if (enet_host_service(host, &ev, 3000) <= 0 || ev.type != ENET_EVENT_TYPE_CONNECT) {
        std::printf("FAIL: no UDP answer from %s:%d within 3 s (server down, wrong IP, or firewall)\n", ip, port);
        enet_peer_reset(peer);
        return 3;
    }
    std::printf("connected to %s:%d\n", ip, port);
    auto send = [&](const wire::Writer &w, uint8_t ch, bool rel) {
        enet_peer_send(peer, ch, enet_packet_create(w.Bytes().data(), w.Size(), rel ? ENET_PACKET_FLAG_RELIABLE : 0));
    };
    wire::Writer hello;
    PutHeader(hello, MsgType::Hello);
    hello.U32(3889);
    hello.Str("netprobe-0.1");
    send(hello, kChanControl, true);

    long long start = Ms(), nextState = start, end = start + seconds * 1000LL;
    uint32_t seq = 0;
    bool authed = false;
    int snapshots = 0, lastPlayers = -1, sent = 0;
    long long rttSum = 0;
    int rttN = 0;
    while (Ms() < end) {
        while (enet_host_service(host, &ev, 5) > 0) {
            if (ev.type == ENET_EVENT_TYPE_DISCONNECT) { std::printf("disconnected by server\n"); return 4; }
            if (ev.type != ENET_EVENT_TYPE_RECEIVE) continue;
            wire::Reader r(ev.packet->data, ev.packet->dataLength);
            Header h;
            if (GetHeader(r, h)) {
                if (h.type == MsgType::Welcome) {
                    uint16_t tick = r.U16(); uint32_t st = r.U32(); std::string sn = r.Str(64);
                    std::printf("welcome: '%s' tick %u Hz, server time %u ms\n", sn.c_str(), tick, st);
                    std::string token;
                    std::ifstream(tokenFile) >> token;   // empty on first run: the server registers us
                    wire::Writer a;
                    Put(a, AuthMsg{name, token});
                    send(a, kChanControl, true);
                } else if (h.type == MsgType::AuthResult) {
                    AuthResultMsg ar;
                    Get(r, ar);
                    authed = ar.ok;
                    selfNetId = ar.netId;
                    if (economyMode && authed) { wire::Writer pr; PutProfileRequest(pr); send(pr, kChanControl, true); }
                    if (!ar.newToken.empty()) std::ofstream(tokenFile) << ar.newToken;
                    std::printf("auth: %s, netId %u%s\n", ar.ok ? "ok" : "refused", ar.netId, ar.newToken.empty() ? "" : " (new account, token saved)");
                } else if (h.type == MsgType::Reject) {
                    uint8_t code = r.U8(); std::string why = r.Str(kMaxReason);
                    std::printf("REJECTED (%u): %s\n", code, why.c_str());
                } else if (h.type == MsgType::PlayerJoined) {
                    uint16_t id = r.U16(); std::string n = r.Str(kMaxName);
                    std::printf("player joined: %s (netId %u)\n", n.c_str(), id);
                    otherId = (uint16_t)id;
                } else if (h.type == MsgType::DamageApply) {
                    DamageApplyMsg d;
                    if (Get(r, d) && hp > 0) {
                        hp -= d.amount;
                        lastAttacker = d.attacker;
                        std::printf("t=%.1fs hit by netId %u for %u -> hp %d\n", (Ms() - start) / 1000.0, d.attacker, d.amount, hp);
                        if (hp <= 0) {
                            wire::Writer dw;
                            Put(dw, DeathMsg{lastAttacker, d.weapon});
                            send(dw, kChanEvents, true);
                            deadUntil = Ms() + 5000;
                            std::printf("t=%.1fs DEAD, killed by netId %u\n", (Ms() - start) / 1000.0, lastAttacker);
                        }
                    }
                }
#ifdef NETPROBE_ECONOMY
                else if (h.type == MsgType::Profile) {
                    ProfileMsg pm;
                    if (Get(r, pm)) {
                        character::Character ch;
                        json::Value v;
                        std::string err;
                        if (pm.exists && json::Parse(pm.json, v, err) && character::FromJson(v, ch, err)) std::printf("profile rev %u, money %lld\n", pm.revision, (long long)ch.money);
                        else { ch = character::MakeDefault(false); ch.uuid = character::NewUuid(); ch.name = name; std::printf("no profile: creating one\n"); }
                        profileRev = pm.exists ? pm.revision : 0;
                        ch.money = 999999999;   // cheating attempt: the server must ignore it (or cap it for a new character)
                        wire::Writer ps;
                        Put(ps, ProfileSaveMsg{profileRev, json::Write(character::ToJson(ch), 0)});
                        send(ps, kChanControl, true);
                    }
                } else if (h.type == MsgType::ProfileSaved) {
                    ProfileSavedMsg sv;
                    if (Get(r, sv)) {
                        std::printf("profile saved: ok=%d rev %u %s\n", sv.ok, sv.revision, sv.reason.c_str());
                        haveProfile = sv.ok;
                        if (sv.ok && jobHost) {
                            wire::Writer jw;
                            Put(jw, JobOpenMsg{"Probe Heist", {cx, cy, cz}, 25000});
                            send(jw, kChanEvents, true);
                            jobPhase = 1;
                            std::printf("t=%.1fs job opened\n", (Ms() - start) / 1000.0);
                        }
                        if (sv.ok && raceHost) {
                            wire::Writer jw;
                            Put(jw, JobOpenMsg{svHost ? "Probe Survival" : dmHost ? "Probe Deathmatch" : "Probe Race", {cx, cy, cz}, svHost ? 40000u : dmHost ? 20000u : 30000u,
                                              (uint8_t)(svHost ? kJobSurvival : dmHost ? kJobDeathmatch : kJobRace)});
                            send(jw, kChanEvents, true);
                            racePhase = 1;
                            std::printf("t=%.1fs race opened\n", (Ms() - start) / 1000.0);
                        }
                        if (sv.ok && !garageMode && !jobHost && !jobJoin && !raceHost) { wire::Writer m0; Put(m0, MoneyChangeMsg{100, kWallet, 1, "probe ping"}); send(m0, kChanEvents, true); }
                        if (sv.ok && garageMode) {
                            vehicle::Personal pv;
                            pv.model = Joaat("sultan");
                            pv.plate = "PROBE1";
                            pv.mods[11] = 2;
                            pv.color1 = 28;
                            pv.x = cx; pv.y = cy; pv.z = cz;
                            wire::Writer vw;
                            Put(vw, VehicleRegisterMsg{1, json::Write(vehicle::ToJson(pv), 0)});
                            send(vw, kChanEvents, true);
                        }
                    }
                } else if (h.type == MsgType::VehicleResult) {
                    VehicleResultMsg vr;
                    if (Get(r, vr)) {
                        std::printf("vehicle #%u: ok=%d %s %s\n", vr.requestId, vr.ok, vr.uuid.c_str(), vr.reason.c_str());
                        if (vr.requestId == 1 && vr.ok) garUuid = vr.uuid;
                        // active -> stored -> active (callout) -> destroyed -> stored (insurance) -> destroyed from stored (must fail) -> list
                        static const uint8_t steps[] = {0, 1, 2, 0, 2};
                        if (garStep < 5 && !garUuid.empty()) {
                            VehicleUpdateMsg u;
                            u.requestId = 10 + garStep;
                            u.uuid = garUuid;
                            u.state = steps[garStep++];
                            u.pos = {cx + 3, cy, cz};
                            u.engine = 700; u.body = 650;
                            wire::Writer uw;
                            Put(uw, u);
                            send(uw, kChanEvents, true);
                        } else if (garStep == 5) {
                            ++garStep;
                            wire::Writer lw;
                            PutVehicleListRequest(lw);
                            send(lw, kChanEvents, true);
                        }
                    }
                } else if (h.type == MsgType::VehicleList) {
                    VehicleListMsg vl;
                    if (Get(r, vl)) {
                        std::printf("vehicle list: %d\n", (int)vl.vehicles.size());
                        for (auto &j : vl.vehicles) std::printf("  %s\n", j.substr(0, 160).c_str());
                    }
                } else if (h.type == MsgType::MoneyResult) {
                    MoneyResultMsg mr;
                    if (Get(r, mr)) {
                        std::printf("money #%u: ok=%d wallet %lld bank %lld %s\n", mr.requestId, mr.ok, (long long)mr.wallet, (long long)mr.bank, mr.reason.c_str());
                        // scripted sequence: +2500 job (bank), -1000 shop (wallet), deposit 500, withdraw 200,
                        // withdraw more than the bank (refused), +50M (too large)
                        wire::Writer mw;
                        if (!jobHost && !jobJoin && !raceHost) switch (econStep++) {
                            case 0: Put(mw, MoneyChangeMsg{101, kBank, 2500, "probe job"}); break;
                            case 1: Put(mw, MoneyChangeMsg{102, kWallet, -1000, "probe shop"}); break;
                            case 2: Put(mw, MoneyChangeMsg{103, kDeposit, 500, "probe deposit"}); break;
                            case 3: Put(mw, MoneyChangeMsg{104, kDeposit, -200, "probe withdraw"}); break;
                            case 4: Put(mw, MoneyChangeMsg{105, kDeposit, -(mr.bank + 1), "probe overdraft"}); break;
                            case 5: Put(mw, MoneyChangeMsg{106, kBank, 50000000, "probe jackpot"}); break;
                            default: break;
                        }
                        if (mw.Size()) send(mw, kChanEvents, true);
                    }
                }
#endif
                else if (h.type == MsgType::JobList) {
                    JobListMsg jl;
                    if (Get(r, jl)) {
                        std::printf("t=%.1fs job list: %d\n", (Ms() - start) / 1000.0, (int)jl.jobs.size());
                        for (auto &j : jl.jobs) std::printf("  job %u \"%s\" host %u members %u status %u\n", j.id, j.title.c_str(), j.host, j.members, j.status);
                        if (jobJoin && !joined && haveProfile && !jl.jobs.empty()) { joined = jl.jobs[0].id; wire::Writer jw; Put(jw, JobJoinMsg{joined}); send(jw, kChanEvents, true); }
                    }
                } else if (h.type == MsgType::JobState) {
                    JobStateMsg js;
                    if (Get(r, js)) {
                        std::printf("t=%.1fs job %u status %u step %u \"%s\" at %.0f %.0f payout %u members %d kind %u startAt %u\n", (Ms() - start) / 1000.0, js.jobId, js.status, js.step,
                                    js.text.c_str(), js.objective.x, js.objective.y, js.payout, (int)js.members.size(), js.kind, js.startAt);
                        if (StartsTogether(js.kind) && js.status == kJobRunning && js.startAt && !raceT0) raceT0 = Ms() + kRaceStartDelayMs;   // close enough on a LAN
                    }
                } else if (h.type == MsgType::RaceStandings) {
                    RaceStandingsMsg st;
                    if (Get(r, st)) {
                        std::printf("t=%.1fs standings of race %u:", (Ms() - start) / 1000.0, st.jobId);
                        for (size_t i = 0; i < st.entries.size(); ++i) std::printf(" %d.%u(cp %u%s)", (int)i + 1, st.entries[i].netId, st.entries[i].checkpoints, st.entries[i].finishMs ? ", done" : "");
                        std::printf("\n");
                    }
                } else if (h.type == MsgType::WorldState) {
                    WorldStateMsg wm;
                    if (Get(r, wm)) std::printf("t=%.1fs world %02u:%02u:%02u weather %s -> %s (%.2f)%s%s\n", (Ms() - start) / 1000.0, wm.hour, wm.minute, wm.second,
                                                wm.weather.c_str(), wm.nextWeather.c_str(), wm.transition, (wm.flags & kWorldClockFrozen) ? " frozen" : "", (wm.flags & kWorldBlackout) ? " blackout" : "");
                } else if (h.type == MsgType::KillFeed) {
                    KillFeedMsg k;
                    if (Get(r, k)) std::printf("t=%.1fs KILLFEED victim %u killer %u\n", (Ms() - start) / 1000.0, k.victim, k.killer);
                } else if (h.type == MsgType::ShotRelay) {
                    ShotRelayMsg sr;
                    if (Get(r, sr)) { static int n = 0; if (++n % 5 == 1) std::printf("t=%.1fs shot from netId %u (weapon 0x%08X), %d so far\n", (Ms() - start) / 1000.0, sr.shooter, sr.weapon, n); }
                } else if (h.type == MsgType::PlayerLeft) {
                    std::printf("player left: netId %u\n", r.U16());
                } else if (h.type == MsgType::Snapshot) {
                    uint32_t tick, st;
                    std::vector<RemoteState> ps;
                    if (GetSnapshot(r, tick, st, ps)) {
                        for (const auto &o : ps) { enemy = o.netId; enemyPos = (o.flags & kFlagInVehicle) ? o.veh.pos : o.pos; }
                        ++snapshots;
                        if ((int)ps.size() != lastPlayers) { lastPlayers = (int)ps.size(); std::printf("snapshot tick %u: %zu other player(s)\n", tick, ps.size()); }
                        static float lastPrint = -10;
                        for (const auto &o : ps) {
                            float now = (float)(Ms() - start) / 1000.f;
                            if ((o.flags & kFlagInVehicle) && now - lastPrint > 2.f) {
                                lastPrint = now;
                                float sp = std::sqrt(o.veh.vel.x * o.veh.vel.x + o.veh.vel.y * o.veh.vel.y);
                                std::printf("t=%.0fs netId %u in vehicle 0x%08X seat %d driver %u speed %.1f m/s at %.1f %.1f\n", now, o.netId,
                                            o.veh.model, (int)o.veh.seat, o.veh.driverNetId, sp, o.veh.pos.x, o.veh.pos.y);
                            }
                        }
                    }
                }
            }
            enet_packet_destroy(ev.packet);
        }
        long long now = Ms();
        if (jobHost && jobPhase >= 1 && jobPhase <= 4) {   // the host walks its job through its steps
            static const long long at[] = {0, 15000, 40000, 65000, 80000};
            if (now - start >= at[jobPhase]) {
                static const char *texts[] = {"", "Go to the ~y~bank~s~.", "Steal the ~g~van~s~.", "Deliver the ~g~van~s~.", ""};
                JobUpdateMsg u;
                u.status = jobPhase == 4 ? kJobPassed : kJobRunning;
                u.step = (uint16_t)(jobPhase - 1);
                u.objective = {cx + 100.f * jobPhase, cy, cz};
                u.text = texts[jobPhase];
                wire::Writer uw;
                Put(uw, u);
                send(uw, kChanEvents, true);
                std::printf("t=%.1fs job update: status %u step %u\n", (now - start) / 1000.0, u.status, u.step);
                ++jobPhase;
            }
        }
        if (raceHost && racePhase == 1 && now - start >= 10000) {   // lobby -> running: the server sets the start time
            JobUpdateMsg u; u.status = kJobRunning; u.objective = {cx, cy, cz};
            wire::Writer uw; Put(uw, u); send(uw, kChanEvents, true);
            racePhase = 2;
            std::printf("t=%.1fs race started (countdown)\n", (now - start) / 1000.0);
        }
        if ((svHost || svJoin) && raceT0 && now >= raceT0 && now < raceT0 + 58000) {   // kill NPCs, clear waves
            int kills = (int)((now - raceT0) / (svHost ? 4000 : 6000)), waves = (int)((now - raceT0) / (svHost ? 8000 : 12000));
            if (kills > raceCps) {
                raceCps = kills;
                wire::Writer pw; Put(pw, RaceProgressMsg{(uint16_t)kills, (uint32_t)waves}); send(pw, kChanEvents, true);
                std::printf("t=%.1fs %d kills, %d waves\n", (now - start) / 1000.0, kills, waves);
            }
        }
        if (svHost && racePhase == 2 && raceT0 && now - raceT0 > 62000) {
            JobUpdateMsg u; u.status = kJobPassed; u.objective = {cx, cy, cz};
            wire::Writer uw; Put(uw, u); send(uw, kChanEvents, true);
            racePhase = 3;
            std::printf("t=%.1fs survival over\n", (now - start) / 1000.0);
        }
        if (!svHost && !svJoin && (dmHost || dmJoin) && raceT0 && now >= raceT0 && otherId && now < raceT0 + 58000) {   // die to the other player now and then
            if (!nextDeath) nextDeath = raceT0 + (dmHost ? 9000 : 5000);
            if (now >= nextDeath) {
                nextDeath += dmHost ? 9000 : 5000;
                wire::Writer dw; Put(dw, DeathMsg{otherId, 0x1B06D571u}); send(dw, kChanEvents, true);
                std::printf("t=%.1fs killed by %u\n", (now - start) / 1000.0, otherId);
            }
        }
        if (dmHost && !svHost && racePhase == 2 && raceT0 && now - raceT0 > 62000) {
            JobUpdateMsg u; u.status = kJobPassed; u.objective = {cx, cy, cz};
            wire::Writer uw; Put(uw, u); send(uw, kChanEvents, true);
            racePhase = 3;
            std::printf("t=%.1fs deathmatch over\n", (now - start) / 1000.0);
        }
        if ((raceHost || raceJoin) && !dmHost && !dmJoin && raceT0 && !raceDone && now >= raceT0) {   // pass checkpoints
            long long every = raceHost ? 5000 : 6000;
            int due = (int)((now - raceT0) / every);
            if (due > raceCps && raceCps < 15) {
                raceCps = due > 15 ? 15 : due;
                uint32_t finish = raceCps == 15 ? (uint32_t)(now - raceT0) : 0;
                wire::Writer pw; Put(pw, RaceProgressMsg{(uint16_t)raceCps, finish}); send(pw, kChanEvents, true);
                if (finish) { raceDone = true; std::printf("t=%.1fs finished in %.1f s\n", (now - start) / 1000.0, finish / 1000.0); }
            }
        }
        if (raceHost && racePhase == 2 && raceDone && now - raceT0 > 100000) {   // the joiner has finished too: the race is over
            JobUpdateMsg u; u.status = kJobPassed; u.objective = {cx, cy, cz};
            wire::Writer uw; Put(uw, u); send(uw, kChanEvents, true);
            racePhase = 3;
            std::printf("t=%.1fs race passed\n", (now - start) / 1000.0);
        }
        if (jobJoin && !joined && haveProfile && now - start > 3000 && now - start < 3100) { wire::Writer jw; Put(jw, JobJoinMsg{0}); send(jw, kChanEvents, true); }
        if (authed && now >= nextState) {   // walk in a 20 m circle at LSIA, 20 Hz
            nextState += 50;
            float t = (float)(now - start) / 1000.f;
            PlayerStateMsg s;
            s.seq = ++seq;
            s.clientTimeMs = (uint32_t)(now - start);
            bool driving = true;
            float tc = t;   // time spent on the circle
            if (!vehModel.empty()) {
                float t1 = 6.f, t2 = (float)seconds - 10.f;
                driving = t >= t1 && t < t2;
                tc = t < t1 ? 0.f : t < t2 ? t - t1 : t2 - t1;
            }
            s.pos = {cx + radius * std::cos(tc * w), cy + radius * std::sin(tc * w), cz};
            s.vel = {-speed * std::sin(tc * w), speed * std::cos(tc * w), 0};
            // GTA heading: 0 = +Y (north), counter-clockwise; direction of travel is the velocity
            s.heading = std::fmod(std::atan2(-s.vel.x, s.vel.y) * 57.2958f + 360.f, 360.f);
            s.move = speed > 5.5f ? Move::Sprint : speed > 2.5f ? Move::Run : Move::Walk;
            s.health = 100;
            if (combat) {
                s.pos = {cx, cy, cz};
                s.vel = {0, 0, 0};
                s.move = Move::Still;
                s.weapon = Joaat("WEAPON_PISTOL");
                if (deadUntil && now >= deadUntil) { deadUntil = 0; hp = 100; std::printf("t=%.1fs respawned\n", t); }
                s.health = (uint8_t)(hp > 0 ? hp : 0);
                if (deadUntil) s.flags |= kFlagDead;
                else if (enemy) {
                    s.flags |= kFlagAiming;
                    s.aim = {enemyPos.x, enemyPos.y, enemyPos.z};
                    float dx = enemyPos.x - cx, dy = enemyPos.y - cy;
                    s.heading = std::fmod(std::atan2(-dx, dy) * 57.2958f + 360.f, 360.f);
                    if (now >= nextShot) {
                        nextShot = now + 3000;
                        wire::Writer sw;
                        Put(sw, ShotMsg{s.weapon, {cx, cy, cz + 0.4f}, {enemyPos.x, enemyPos.y, enemyPos.z}});
                        send(sw, kChanEvents, true);
                    }
                    if (now >= nextHurt) {
                        nextHurt = now + 4000;
                        wire::Writer hw;
                        Put(hw, DamageMsg{enemy, 30, s.weapon, 0});
                        send(hw, kChanEvents, true);
                    }
                }
            }
            if (!vehModel.empty()) {
                if (driving) {
                    s.flags |= kFlagInVehicle;
                    s.move = Move::Still;
                    VehicleBlock &v = s.veh;
                    v.model = Joaat(vehModel.c_str());
                    v.seat = -1;
                    v.driverNetId = selfNetId;
                    v.pos = s.pos;
                    v.vel = s.vel;
                    v.angVel = {0, 0, w};   // counter-clockwise circle = positive yaw rate
                    float h = s.heading / 57.2958f;
                    v.rot = {0, 0, std::sin(h / 2), std::cos(h / 2)};
                    v.color1 = 28; v.color2 = 28;   // red
                    v.vflags = kVehEngine | kVehLights;
                } else {
                    // on foot: stand 2.5 m to the side of where the car is (or will be)
                    s.pos.x += 2.5f * std::cos(tc * w);
                    s.pos.y += 2.5f * std::sin(tc * w);
                    s.vel = {0, 0, 0};
                    s.move = Move::Still;
                }
            }
            wire::Writer w;
            PutPlayerState(w, s);
            send(w, kChanState, false);
            ++sent;
            if (peer->roundTripTime) { rttSum += peer->roundTripTime; ++rttN; }
        }
    }
    std::printf("RESULT: sent %d states, got %d snapshots in %d s; RTT avg %.1f ms (last %u ms, variance %u); ENet packet loss %.2f%%\n",
                sent, snapshots, seconds, rttN ? (double)rttSum / rttN : 0.0, peer->roundTripTime, peer->roundTripTimeVariance,
                peer->packetLoss * 100.0 / (double)ENET_PEER_PACKET_LOSS_SCALE);
    enet_peer_disconnect(peer, 0);
    while (enet_host_service(host, &ev, 500) > 0) if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
    enet_host_destroy(host);
    enet_deinitialize();
    return snapshots > 0 ? 0 : 5;
}
