// GodEye admin API (Phase 9). A loopback-only TCP port where the GodEye backend reads a telemetry view of the
// authoritative state and sends admin commands. Never reachable from the LAN: browsers talk to GodEye, GodEye
// talks to this port, and GTA clients only ever talk to the ENet port.
//
// Wire format: one JSON object per line, both ways.
//   GodEye -> server   {"t":"auth","secret":"..."}            first line, within 5 s, or the connection is closed
//                      {"t":"full"}                            resend everything (after a page reload, say)
//                      {"t":"cmd","id":7,"line":"weather RAIN"}   the server.cmd commands (AdminCommand)
//   server -> GodEye   {"t":"hello","server":"...","protocol":11,"tick":20,"telemetry":10}
//                      {"t":"tel","ms":123,"world":{...},"players":[...],"gone":[3]}   at telemetry_hz, changes only
//                      {"t":"cmdr","id":7,"ok":true}
#include "server.h"
#include <enet/enet.h>   // winsock2.h on Windows
#include <cmath>
#include <cstdio>
#include "common/json.h"
#include "common/log.h"

using namespace proto;

static constexpr size_t kMaxLine = 4096, kMaxOut = 1 << 20;
static constexpr int kMaxAdmins = 4;

static std::string Esc(const std::string &s) {
    std::string o;
    for (unsigned char ch : s) {
        if (ch == '"' || ch == '\\') { o += '\\'; o += (char)ch; }
        else if (ch < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", ch); o += b; }
        else o += (char)ch;
    }
    return o;
}

static bool SameSecret(const std::string &a, const std::string &b) {   // constant time for equal lengths
    if (a.size() != b.size() || a.empty()) return false;
    unsigned char d = 0;
    for (size_t i = 0; i < a.size(); ++i) d |= (unsigned char)(a[i] ^ b[i]);
    return d == 0;
}

bool Server::AdminApiStart() {
    if (cfg_.adminApiPort <= 0) { LOGI("godeye", "-", "admin API off (godeye.admin_port = 0)"); return false; }
    if (cfg_.adminSecret.size() < 16) { LOGE("godeye", "-", "admin API off: the shared secret is missing or shorter than 16 characters"); return false; }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { LOGE("godeye", "-", "admin API: socket() failed"); return false; }
    BOOL excl = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&excl, sizeof excl);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // never the LAN
    a.sin_port = htons((u_short)cfg_.adminApiPort);
    u_long nb = 1;
    if (bind(s, (sockaddr *)&a, sizeof a) != 0 || listen(s, 4) != 0 || ioctlsocket(s, FIONBIO, &nb) != 0) {
        LOGE("godeye", "-", "admin API: cannot listen on 127.0.0.1:%d (port in use?)", cfg_.adminApiPort);
        closesocket(s);
        return false;
    }
    adminListen_ = (uintptr_t)s;
    LOGI("godeye", "-", "admin API on 127.0.0.1:%d (TCP, loopback only), telemetry %d Hz", cfg_.adminApiPort, cfg_.telemetryHz);
    return true;
}

void Server::AdminApiStop() {
    for (auto &a : admins_) closesocket((SOCKET)a.sock);
    admins_.clear();
    if (adminListen_ != ~(uintptr_t)0) closesocket((SOCKET)adminListen_);
    adminListen_ = ~(uintptr_t)0;
}

std::string Server::WorldTelemetry() const {
    char b[256];
    std::snprintf(b, sizeof b, "{\"hour\":%d,\"minute\":%d,\"frozen\":%s,\"weather\":\"%s\",\"next\":\"%s\",\"auto\":%s,\"blackout\":%s}",
                  clock_.Hour(), clock_.Minute(), clock_.frozen ? "true" : "false", Esc(weather_.Current()).c_str(),
                  Esc(weather_.Next()).c_str(), weather_.automatic ? "true" : "false", blackout_ ? "true" : "false");
    return b;
}

// One player, rounded so that standing still sends nothing: 0.1 m, 1 degree, 0.1 m/s.
std::string Server::PlayerTelemetry(const Client &c) const {
    char b[640];
    int n = std::snprintf(b, sizeof b, "{\"id\":%u,\"name\":\"%s\",\"rank\":%u,\"session\":%u,\"passive\":%s,\"activity\":%s,\"kills\":%d,\"deaths\":%d",
                          c.netId, Esc(c.name).c_str(), c.rank, c.session, (c.infoFlags & kInfoPassive) ? "true" : "false",
                          (c.infoFlags & kInfoActivity) ? "true" : "false", c.kills, c.deaths);
    if (!c.haveState) {   // connected, in story mode or loading: no position yet
        std::snprintf(b + n, sizeof b - n, ",\"playing\":false}");
        return b;
    }
    const PlayerStateMsg &s = c.state;
    float spd = std::sqrt(s.vel.x * s.vel.x + s.vel.y * s.vel.y + s.vel.z * s.vel.z);
    n += std::snprintf(b + n, sizeof b - n,
                       ",\"playing\":true,\"x\":%.1f,\"y\":%.1f,\"z\":%.1f,\"h\":%d,\"spd\":%.1f,\"hp\":%u,\"ar\":%u,\"wanted\":%u,\"weapon\":%u,\"dead\":%s,\"move\":%u",
                       s.pos.x, s.pos.y, s.pos.z, (int)std::lround(s.heading) % 360, spd, s.health, s.armour, s.wanted, s.weapon,
                       (s.flags & kFlagDead) ? "true" : "false", (unsigned)s.move);
    if (s.flags & kFlagInVehicle)
        std::snprintf(b + n, sizeof b - n, ",\"veh\":{\"model\":%u,\"seat\":%d,\"driver\":%u}}", s.veh.model, s.veh.seat, s.veh.driverNetId);
    else
        std::snprintf(b + n, sizeof b - n, ",\"veh\":null}");
    return b;
}

void Server::AdminTelemetry(AdminConn &a, bool full) {
    if (full) { a.sent.clear(); a.sentWorld.clear(); }
    std::string players, gone;
    std::map<uint16_t, std::string> now;
    for (auto &kv : clients_) {
        const Client &c = kv.second;
        if (!c.authed) continue;
        std::string t = PlayerTelemetry(c);
        auto it = a.sent.find(c.netId);
        if (it == a.sent.end() || it->second != t) players += (players.empty() ? "" : ",") + t;
        now[c.netId] = std::move(t);
    }
    for (auto &kv : a.sent)
        if (!now.count(kv.first)) gone += (gone.empty() ? "" : ",") + std::to_string(kv.first);
    std::string world = WorldTelemetry();
    bool worldChanged = world != a.sentWorld;
    if (!full && players.empty() && gone.empty() && !worldChanged) return;   // nothing changed: nothing sent
    a.sent = std::move(now);
    a.sentWorld = world;
    a.out += "{\"t\":\"tel\",\"ms\":" + std::to_string(NowMs()) + (full ? ",\"full\":true" : "") +
             (worldChanged ? ",\"world\":" + world : "") + ",\"players\":[" + players + "],\"gone\":[" + gone + "]}\n";
}

void Server::AdminApiLine(AdminConn &a, const std::string &line) {
    json::Value v;
    std::string err;
    if (!json::Parse(line, v, err) || v.type != json::Value::Object) {
        if (!a.authed) a.in = "\x01";   // marks the connection for closing
        return;
    }
    std::string t = v.Str("t", "");
    if (!a.authed) {
        if (t == "auth" && SameSecret(v.Str("secret", ""), cfg_.adminSecret)) {
            a.authed = true;
            a.out += "{\"t\":\"hello\",\"server\":\"" + Esc(cfg_.name) + "\",\"protocol\":" + std::to_string(kProtocolVersion) +
                     ",\"tick\":" + std::to_string(cfg_.tickRate) + ",\"telemetry\":" + std::to_string(cfg_.telemetryHz) + "}\n";
            LOGI("godeye", "-", "GodEye backend connected");
            AdminTelemetry(a, true);
        } else {
            LOGW("godeye", "-", "admin API: bad auth, closing");
            a.in = "\x01";
        }
        return;
    }
    if (t == "full") AdminTelemetry(a, true);
    else if (t == "cmd") {
        std::string cmd = v.Str("line", "");
        long long id = (long long)v.Num("id", 0);
        bool ok = !cmd.empty() && cmd.size() < 120 && cmd.find('\n') == std::string::npos && AdminCommand(cmd);
        a.out += "{\"t\":\"cmdr\",\"id\":" + std::to_string(id) + ",\"ok\":" + (ok ? "true" : "false") + "}\n";
    }
}

void Server::AdminApiPoll(long long now) {
    if (adminListen_ == ~(uintptr_t)0) return;
    for (;;) {   // new connections
        SOCKET s = accept((SOCKET)adminListen_, nullptr, nullptr);
        if (s == INVALID_SOCKET) break;
        if ((int)admins_.size() >= kMaxAdmins) { closesocket(s); continue; }
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
        AdminConn a;
        a.sock = (uintptr_t)s;
        a.openedMs = now;
        admins_.push_back(std::move(a));
    }
    bool telemetry = cfg_.telemetryHz > 0 && now - lastTelemetryMs_ >= 1000 / cfg_.telemetryHz;
    if (telemetry) lastTelemetryMs_ = now;
    for (size_t i = 0; i < admins_.size();) {
        AdminConn &a = admins_[i];
        bool close = false;
        char buf[4096];
        for (;;) {
            int n = recv((SOCKET)a.sock, buf, sizeof buf, 0);
            if (n > 0) { a.in.append(buf, n); if (a.in.size() > kMaxLine * 4) { close = true; break; } continue; }
            if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK) close = true;
            break;
        }
        size_t nl;
        while (!close && (nl = a.in.find('\n')) != std::string::npos) {
            std::string line = a.in.substr(0, nl);
            a.in.erase(0, nl + 1);
            if (line.size() > kMaxLine) { close = true; break; }
            AdminApiLine(a, line);
            if (a.in == "\x01") close = true;
        }
        if (!a.authed && now - a.openedMs > 5000) close = true;
        if (!close && a.authed && telemetry) AdminTelemetry(a, false);
        if (!close && !a.out.empty()) {
            int n = send((SOCKET)a.sock, a.out.data(), (int)a.out.size(), 0);
            if (n > 0) a.out.erase(0, n);
            else if (WSAGetLastError() != WSAEWOULDBLOCK) close = true;
            if (a.out.size() > kMaxOut) close = true;   // GodEye stopped reading
        }
        if (close) {
            if (a.authed) LOGI("godeye", "-", "GodEye backend disconnected");
            closesocket((SOCKET)a.sock);
            admins_.erase(admins_.begin() + i);
            continue;
        }
        ++i;
    }
}
