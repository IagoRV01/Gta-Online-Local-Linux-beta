// GTA Local Online — client plugin (ScriptHookV .asi) for GTA V Legacy, runs under Proton on the BC-250.
// Phase 1: the custom freemode "YOU" character with local persistence.
// Files: <GTA folder>\LocalOnline\ (client.toml, LocalOnline.log, LocalOnline.cmd, cache\, backups\).
#include <windows.h>
#include <cstring>
#include <string>
#include "common/log.h"
#include "common/toml.h"
#include "engine/crash.h"
#include "engine/gta.h"
#include "engine/shv.h"
#include "game/jobs.h"
#include "game/remote.h"
#include "game/devcmd.h"
#include "game/economy.h"
#include "game/net_client.h"
#include "game/session.h"
#include "ui/startup.h"

#define LOCALONLINE_VERSION "0.1.0"

static std::string g_dir;
static session::Config g_cfg;

static int ParseKey(const std::string &s, int def) {
    if (s.size() >= 2 && (s[0] == 'F' || s[0] == 'f')) {
        int n = std::atoi(s.c_str() + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    if (s.size() == 1 && isalnum((unsigned char)s[0])) return toupper((unsigned char)s[0]);
    return def;
}

static void ScriptMain() {
    logx::Open(g_dir + "LocalOnline.log");
    toml::Doc cfg;
    std::string err;
    bool haveCfg = toml::LoadFile(g_dir + "client.toml", cfg, err);
    logx::SetMinLevel(cfg.Bool("debug.debug", false) ? logx::Level::Debug : logx::Level::Info);
    LOGI("client", "-", "GTA Local Online %s starting (dir %s)", LOCALONLINE_VERSION, g_dir.c_str());
    if (!haveCfg) LOGW("config", "-", "client.toml not loaded (%s); using defaults", err.c_str());

    g_cfg.menuKey = ParseKey(cfg.Str("client.menu_key", "F10"), VK_F10);
    g_cfg.autosaveSeconds = (int)cfg.Int("client.autosave_seconds", 15);
    if (g_cfg.autosaveSeconds < 5) g_cfg.autosaveSeconds = 5;
    g_cfg.cacheDir = g_dir + "cache\\";
    g_cfg.server.enabled = cfg.Bool("server.enabled", true);
    g_cfg.server.host = cfg.Str("server.address", "127.0.0.1");
    g_cfg.server.port = (int)cfg.Int("server.port", 4499);
    g_cfg.server.playerName = cfg.Str("player.name", "Player");
    g_cfg.backupDir = g_dir + "backups\\";
    devcmd::Init(g_dir + "LocalOnline.cmd", cfg.Bool("debug.remote_commands", false));
    gta::SetWaitHook([] {
        // Long game freezes (seen on the BC-250 while it swaps) explain network timeouts: log them.
        static unsigned long long last = 0;
        unsigned long long now = GetTickCount64();
        if (last && now - last > 2500) LOGW("client", "-", "game froze for %llu ms", now - last);
        last = now;
        net::Service();
    });

    while (!gta::PlayerPlaying()) gta::Wait(500);
    session::Init(g_cfg);
    LOGI("client", "-", "player ready");
    if (cfg.Bool("client.startup_menu", true)) {
        startup::Info info;
        info.characterName = session::CharacterName();
        info.serverText = "Server " + cfg.Str("server.address", "?") + ":" + std::to_string(cfg.Int("server.port", 4499)) +
                          "  -  profile saved on the server";
        if (startup::Run(info) == startup::Choice::LocalOnline) session::EnterYou();
    }
    gta::Notify("~b~GTA Local Online " LOCALONLINE_VERSION "~s~  ~y~" + cfg.Str("client.menu_key", "F10") + "~s~ menu.");

    for (;;) {
        session::Tick();
        gta::Wait(0);
    }
}

// ---- exports for other ScriptHookV mods (e.g. Gori Online) --------------------------------------
// Resolve with GetProcAddress(GetModuleHandleA("LocalOnline.asi"), "..."). Call only from a script
// fiber (ScriptHookV runs all scripts on the game thread, so no locking is needed).
// v2: wallet + Maze Bank. GetMoney/AddMoney keep their v1 meaning (total, earnings to the bank).
// v3: GetData/SetData, per-character data for other mods (Gori Online's properties, businesses...).
// v4: the Maze Bank transaction log. RequestLedger asks the server; GetLedger copies the last answer
//     as lines "delta<TAB>account<TAB>reason" (account 0 wallet, 1 bank), newest first, and returns
//     its length (call with cap 0 for the size); LedgerRevision changes when a new answer arrives.
static int CopyOut(const std::string &text, char *buf, int cap) {
    if (buf && cap > 0) {
        size_t n = text.size() < (size_t)cap - 1 ? text.size() : (size_t)cap - 1;
        memcpy(buf, text.data(), n);
        buf[n] = 0;
    }
    return (int)text.size();
}
// API v5: co-op jobs (see game/jobs.h)
extern "C" __declspec(dllexport) bool LocalOnline_JobOpen(const char *title, float x, float y, float z, int payout) {
    return jobs::Open(title ? title : "Job", proto::Vec3{x, y, z}, payout > 0 ? (uint32_t)payout : 0);
}
extern "C" __declspec(dllexport) void LocalOnline_JobUpdate(int status, int step, float x, float y, float z, const char *text) {
    jobs::Update((uint8_t)status, (uint16_t)step, proto::Vec3{x, y, z}, text ? text : "");
}
extern "C" __declspec(dllexport) void LocalOnline_JobJoin(unsigned id) { jobs::Join(id); }
extern "C" __declspec(dllexport) int LocalOnline_JobRevision() { return jobs::Revision(); }
extern "C" __declspec(dllexport) int LocalOnline_GetJobs(char *buf, int cap) { return CopyOut(jobs::ListText(), buf, cap); }
extern "C" __declspec(dllexport) int LocalOnline_GetJobState(char *buf, int cap) { return CopyOut(jobs::StateText(), buf, cap); }
// API v6: races (kind 1 jobs: synchronized start, progress, standings)
extern "C" __declspec(dllexport) bool LocalOnline_JobOpenKind(const char *title, float x, float y, float z, int payout, int kind) {
    uint8_t k = kind == 1 ? proto::kJobRace : kind == 2 ? proto::kJobDeathmatch : kind == 3 ? proto::kJobSurvival : proto::kJobMission;   // 2: deathmatch (scores from deaths), 3: survival (kills + waves via RaceProgress)
    return jobs::Open(title ? title : "Job", proto::Vec3{x, y, z}, payout > 0 ? (uint32_t)payout : 0, k);
}
extern "C" __declspec(dllexport) void LocalOnline_RaceProgress(int checkpoints, int finishMs) {
    jobs::RaceProgress((uint16_t)(checkpoints < 0 ? 0 : checkpoints), finishMs > 0 ? (uint32_t)finishMs : 0);
}
extern "C" __declspec(dllexport) int LocalOnline_GetRaceStandings(char *buf, int cap) { return CopyOut(jobs::StandingsText(), buf, cap); }
extern "C" __declspec(dllexport) int LocalOnline_ApiVersion() { return 8; }
// API v7: Gori's rank and flags (passive mode, in a solo activity) for the player lists and the sessions,
// and every known player for Gori's menus ("netId|name|rank|flags|session|sameSession" lines).
extern "C" __declspec(dllexport) void LocalOnline_SetPlayerInfo(int rank, int flags) { remote::SetMyInfo(rank, flags); }
extern "C" __declspec(dllexport) int LocalOnline_GetPlayers(char *buf, int cap) { return CopyOut(remote::PlayersText(), buf, cap); }
extern "C" __declspec(dllexport) unsigned LocalOnline_GetSession() { return remote::MySession(); }
extern "C" __declspec(dllexport) int LocalOnline_GetPlayerName(char *buf, int cap) { return CopyOut(session::DisplayName(), buf, cap); }
extern "C" __declspec(dllexport) void LocalOnline_HighlightPlayer(unsigned netId, int ms) { remote::Highlight((uint16_t)netId, ms); }
extern "C" __declspec(dllexport) bool LocalOnline_IsYouActive() { return session::InYou(); }
extern "C" __declspec(dllexport) long long LocalOnline_GetMoney() { return session::Money(); }
extern "C" __declspec(dllexport) bool LocalOnline_AddMoney(long long delta, const char *reason) { return session::AddMoney(delta, reason); }
extern "C" __declspec(dllexport) long long LocalOnline_GetWallet() { return session::Wallet(); }
extern "C" __declspec(dllexport) long long LocalOnline_GetBank() { return session::Bank(); }
extern "C" __declspec(dllexport) bool LocalOnline_Spend(long long amount, const char *reason, bool preferBank) { return session::Spend(amount, reason, preferBank); }
extern "C" __declspec(dllexport) int LocalOnline_GetData(const char *key, char *buf, int cap) { return session::GetData(key, buf, cap); }
extern "C" __declspec(dllexport) bool LocalOnline_SetData(const char *key, const char *value) { return session::SetData(key, value); }
extern "C" __declspec(dllexport) bool LocalOnline_ChangeAccount(int account, long long delta, const char *reason) { return session::ChangeAccount(account, delta, reason); }
// API v8: the outfit worn now becomes the character's (Gori's apartment wardrobe)
extern "C" __declspec(dllexport) bool LocalOnline_AdoptOutfit() { return session::AdoptOutfit(); }
extern "C" __declspec(dllexport) void LocalOnline_RequestLedger() { if (session::InYou()) economy::RequestLedger(); }
extern "C" __declspec(dllexport) int LocalOnline_LedgerRevision() { return economy::LedgerRevision(); }
extern "C" __declspec(dllexport) int LocalOnline_GetLedger(char *buf, int cap) {
    std::string text;
    for (const auto &row : economy::Ledger()) text += std::to_string(row.delta) + "\t" + std::to_string(row.account) + "\t" + row.reason + "\n";
    if (buf && cap > 0) {
        size_t n = text.size() < (size_t)cap - 1 ? text.size() : (size_t)cap - 1;
        memcpy(buf, text.data(), n);
        buf[n] = 0;
    }
    return (int)text.size();
}

static void OnKey(DWORD key, WORD, BYTE, BOOL, BOOL, BOOL wasDownBefore, BOOL isUpNow) {
    if (isUpNow || wasDownBefore) return;
    session::OnKey(key);
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        char path[MAX_PATH];
        GetModuleFileNameA(mod, path, MAX_PATH);
        std::string p(path);
        size_t s = p.find_last_of("\\/");
        g_dir = (s == std::string::npos ? std::string() : p.substr(0, s + 1)) + "LocalOnline\\";
        CreateDirectoryA(g_dir.c_str(), nullptr);
        if (!shv::Bind()) return TRUE;
        crashlog::InstallCrashLogger(mod);
        shv::scriptRegister(mod, ScriptMain);
        shv::keyboardRegister(OnKey);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (shv::scriptUnregister) shv::scriptUnregister(mod);
        if (shv::keyboardUnregister) shv::keyboardUnregister(OnKey);
    }
    return TRUE;
}
