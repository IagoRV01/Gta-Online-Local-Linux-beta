// Windows game server core: one ENet host, a fixed tick loop, and the authoritative session table.
// Handshake (version check), account auth (per-player token, stored hashed), the character profile
// (load/save with revision checks, validated like the client), and player-state relay as snapshots.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include "protocol/protocol.h"
#include "storage.h"
#include "common/world.h"

struct _ENetHost;
struct _ENetPeer;
struct _ENetPacket;

struct ServerConfig {
    std::string name = "Local Online";
    std::string bind = "0.0.0.0";
    int port = 4499;
    int maxPlayers = 8;
    int tickRate = 20;            // simulation + snapshot rate (Hz)
    int timeoutMs = 10000;        // peer considered gone after this long without acks
    int maxPacketsPerSecond = 120;   // per peer; extra packets are dropped and counted
    bool allowRegistration = true;   // unknown names get an account + token on first login (LAN)
    int minProfileSaveIntervalMs = 500;
    // combat (Phase 6)
    bool pvp = true;
    float maxHitDistance = 400.f;     // attacker-victim distance beyond which a hit report is refused
    int maxShotsPerSecond = 25;
    int maxDamagePerSecond = 800;     // per attacker (miniguns are ~600/s on a 200 hp ped)
    // world (Phase 7)
    bool weatherAutomatic = true;
    std::string adminCommandFile;     // lines like "time 22 0", "weather RAIN", "freeze on", "blackout on"
    // economy (Phase 8)
    long long maxTransaction = 10000000;
    long long maxIncomePerMinute = 5000000;
    long long startingMoneyCap = 1000000;   // money a brand-new (or migrated) character may bring along
    long long insuranceCost = 1000;         // claim a destroyed personal vehicle
    long long impoundCost = 2500;           // release an impounded one
    // co-op jobs (v9)
    long long maxJobPayout = 2000000;       // what one member can be paid for one job
    int minJobMs = 60000;                   // a job "passed" sooner than this after it started pays nothing
    // GodEye admin API (Phase 9): loopback TCP, newline-delimited JSON, shared secret
    int adminApiPort = 4500;                // 0 = off
    std::string adminSecret;                // required; main.cpp creates one in admin_secret_file if empty
    int telemetryHz = 10;
    // camera clients (Phase 12): the CCTV cameras GodEye can switch them to (cameras.json)
    struct Cctv { std::string id, name, zone; proto::Vec3 pos, rot; float fov = 50.f; };
    std::vector<Cctv> cctv;
    int maxCameras = 4;
};

class Server {
public:
    bool Start(const ServerConfig &cfg, Storage *storage);
    void Run(volatile bool &stop);   // blocks until stop is set
    void Shutdown();

private:
    struct Client {
        _ENetPeer *peer = nullptr;
        uint16_t netId = 0;
        bool hello = false, authed = false;
        std::string name, address, playerUuid;
        long long sessionId = 0;
        int lastProfileSaveMs = -100000;
        std::string appearanceJson;   // validated look, relayed to the other players
        uint16_t rank = 0;            // v11: from the client (Gori's RP), shown in the player lists
        uint8_t infoFlags = 0;        // proto::InfoFlags
        uint32_t session = 0;         // the server's: 0 freemode, a running job's id, or a private solo session
        long long msgWindowAt = 0; int msgsInWindow = 0;   // player messages (v14) rate limit
        int combatWindowMs = 0, shotsInWindow = 0, damageInWindow = 0;
        int kills = 0, deaths = 0;
        std::string characterUuid;        // known after ProfileRequest/ProfileSave
        long long incomeWindowMs = 0, incomeInWindow = 0;
        uint32_t lastSeq = 0;
        bool camera = false;              // v13: a camera client (no character, unseen by the players)
        proto::CameraSetMsg cam;          // what it shows now
        bool haveState = false;
        proto::PlayerStateMsg state;
        int windowStartMs = 0, windowPackets = 0, dropped = 0, malformed = 0;
    };

    void OnConnect(_ENetPeer *peer);
    void OnDisconnect(_ENetPeer *peer);
    void OnPacket(_ENetPeer *peer, const uint8_t *data, size_t size, uint8_t channel);
    void Send(_ENetPeer *peer, const wire::Writer &w, uint8_t channel, bool reliable);
    void Reject(Client &c, proto::RejectCode code, const std::string &reason);
    void BroadcastSnapshot();
    void HandleAuth(Client &c, const proto::AuthMsg &m);
    void HandleProfileRequest(Client &c);
    void HandleProfileSave(Client &c, const proto::ProfileSaveMsg &m);
    void HandleAppearance(Client &c, const proto::AppearanceUpdateMsg &m);
    void HandlePlayerInfo(Client &c, const proto::PlayerInfoMsg &m);
    uint32_t SessionOf(const Client &c) const;
    void RefreshSessions();                     // after any job or flag change: relays the players whose session changed
    void RelayInfo(const Client &c, _ENetPeer *only);   // nullptr = everyone authed
    void HandleShot(Client &c, const proto::ShotMsg &m);
    void HandleDamage(Client &c, const proto::DamageMsg &m);
    void HandleDeath(Client &c, const proto::DeathMsg &m);
    void HandleMoney(Client &c, const proto::MoneyChangeMsg &m);
    void HandleLedger(Client &c, const proto::LedgerRequestMsg &m);
    void HandleVehicleList(Client &c);
    void HandleVehicleRegister(Client &c, const proto::VehicleRegisterMsg &m);
    void HandleVehicleUpdate(Client &c, const proto::VehicleUpdateMsg &m);
    // co-op jobs (v9)
    struct Job {
        uint32_t id = 0;
        uint16_t host = 0;
        std::string title, text;
        proto::Vec3 start, objective;
        uint32_t payout = 0;
        uint8_t status = proto::kJobLobby;
        uint16_t step = 0;
        long long startedMs = 0;
        std::vector<uint16_t> members;   // not the host
        uint8_t kind = proto::kJobMission;
        long long startAt = 0;           // races: the server time of the start (0 = not started)
        struct Racer { uint16_t checkpoints = 0; uint32_t finishMs = 0; long long atMs = 0; };
        std::map<uint16_t, Racer> racers;   // races: host and members, by netId
    };
    std::vector<proto::RaceEntry> RaceOrder(const Job &j) const;
    void HandleRaceProgress(Client &c, const proto::RaceProgressMsg &m);
    void SendRaceStandings(const Job &j);
    void HandleJobOpen(Client &c, const proto::JobOpenMsg &m);
    void HandleJobJoin(Client &c, const proto::JobJoinMsg &m);
    void HandlePlayerMessage(Client &c, const proto::PlayerMessageMsg &m);   // v14: relayed within the sender's session
    void HandleJobUpdate(Client &c, const proto::JobUpdateMsg &m);
    void LeaveJobs(uint16_t netId);          // a member leaves / a host's job ends
    void CloseJob(uint32_t id, const char *why);
    void SendJobList(_ENetPeer *only);       // nullptr = everyone authed
    void SendJobState(const Job &j);
    void PayJob(const Job &j);
    std::map<uint32_t, Job> jobs_;
    uint32_t nextJobId_ = 1;
    Client *ByNetId(uint16_t id);
    void WorldTick(long long nowMs);
    void SendWorld(_ENetPeer *only);   // nullptr = everyone authed
    bool AdminCommand(const std::string &line);
    bool GodEyeCommand(const std::string &verb, const std::string &line);
    bool CameraCommand(const std::string &line);
    void SendCamera(Client &c);
    proto::CameraSetMsg DefaultCamera() const;
    std::string CamerasTelemetry() const;
    void SendGodEye(Client &c, const proto::GodEyeActionMsg &m);
    void SendTraffic(_ENetPeer *only);   // nullptr = everyone authed
    uint8_t trafficMode_ = 0;            // proto::TrafficMode, set by GodEye
    long long trafficUntilMs_ = 0;       // 0 = until changed
    // GodEye admin API (admin_api.cpp)
    struct AdminConn {
        uintptr_t sock = 0;
        bool authed = false;
        long long openedMs = 0;
        std::string in, out;
        std::map<uint16_t, std::string> sent;   // last telemetry line per player: only changes are pushed
        std::string sentWorld, sentCameras;
    };
    bool AdminApiStart();
    void AdminApiPoll(long long now);
    void AdminApiStop();
    void AdminApiLine(AdminConn &a, const std::string &line);
    void AdminTelemetry(AdminConn &a, bool full);
    std::string PlayerTelemetry(const Client &c) const;
    std::string WorldTelemetry() const;
    uintptr_t adminListen_ = ~(uintptr_t)0;
    std::vector<AdminConn> admins_;
    long long lastTelemetryMs_ = 0;
    void CombatWindow(Client &c);
    int NowMs() const;

    ServerConfig cfg_;
    Storage *db_ = nullptr;
    _ENetHost *host_ = nullptr;
    std::map<_ENetPeer *, Client> clients_;
    uint16_t nextNetId_ = 1;
    uint32_t tick_ = 0;
    long long startMs_ = 0;
    world::Clock clock_;
    world::Weather weather_{(uint32_t)std::random_device{}()};
    bool blackout_ = false;
    long long lastWorldMs_ = 0, lastWorldSentMs_ = 0, lastPersistMs_ = 0, lastCmdPollMs_ = 0;
    std::string lastSentWeather_;
};
