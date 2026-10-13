#include "game/sync.h"
#include <windows.h>
#include "common/json.h"
#include "common/log.h"
#include "engine/gta.h"
#include "game/net_client.h"
#include "game/profile_store.h"
#include "game/economy.h"
#include "game/jobs.h"

namespace sync {

static Settings g_set;
static profile_store::SyncState g_state;
static bool g_inFlight = false;
static unsigned long long g_lastReconnect = 0;
static const char *Who(const character::Character &c) { return c.name.empty() ? "-" : c.name.c_str(); }

void Init(const Settings &s) {
    g_set = s;
    g_state = profile_store::LoadSync();
    net::OnProfileSaved([](const proto::ProfileSavedMsg &m) {
        g_inFlight = false;
        if (m.ok) {
            g_state.serverRevision = m.revision;
            g_state.dirty = false;
            profile_store::SaveSync(g_state);
            LOGD("sync", "-", "server acknowledged rev %u", m.revision);
        } else if (m.reason.rfind("stale revision", 0) == 0) {
            // Only one client per account can be logged in, so our copy is the newest: rebase onto
            // the server's revision and the next save goes through.
            LOGW("sync", "-", "conflict (%s): rebasing on server rev %u", m.reason.c_str(), m.revision);
            g_state.serverRevision = m.revision;
        } else {
            LOGW("sync", "-", "server refused the save: %s", m.reason.c_str());
        }
    });
}

static std::string ToJsonText(const character::Character &c) { return json::Write(character::ToJson(c), 0); }

static bool Connect(std::string &status) {
    if (!g_set.enabled) { status = "server disabled in client.toml"; return false; }
    std::string token = profile_store::LoadToken();
    net::LoginResult r = net::Login(g_set.host, g_set.port, g_set.playerName, token, 3500);
    if (!r.ok) { status = r.error; LOGW("sync", g_set.playerName.c_str(), "offline: %s", r.error.c_str()); return false; }
    if (!r.newToken.empty()) { profile_store::SaveToken(r.newToken); LOGI("sync", g_set.playerName.c_str(), "account created on the server; token stored"); }
    status = "online";
    return true;
}

static void Connected() { economy::OnConnected(); jobs::OnDisconnected(); }   // a fresh session: no job until the server says so

// Uploads `c` on top of server revision `base`; on success c.revision/g_state follow the server.
static bool Upload(character::Character &c, uint32_t base, const char *why) {
    c.revision = base;
    proto::ProfileSavedMsg res;
    if (!net::SaveProfileAndWait(base, ToJsonText(c), res, 3000) || !res.ok) {
        LOGW("sync", Who(c), "upload (%s) failed: %s", why, res.reason.c_str());
        return false;
    }
    c.revision = res.revision;
    g_state = {res.revision, false, true};
    profile_store::Save(c);
    profile_store::SaveSync(g_state);
    LOGI("sync", Who(c), "uploaded (%s): server rev %u", why, res.revision);
    return true;
}

Begin Start(character::Character &c, std::string &status) {
    character::Character local;
    std::string err;
    bool haveLocal = profile_store::Exists() && profile_store::Load(local, err);
    g_state = profile_store::LoadSync();

    if (!Connect(status)) {
        if (haveLocal) { c = local; status = "offline - local save (" + status + ")"; return Begin::Existing; }
        status = "offline - new character (" + status + ")";
        return Begin::NeedsCreator;
    }
    proto::ProfileMsg prof;
    if (!net::RequestProfile(prof, 3000)) { net::Disconnect(); status = "offline - no profile answer"; return haveLocal ? (c = local, Begin::Existing) : Begin::NeedsCreator; }

    if (prof.exists) {
        json::Value v;
        character::Character srv;
        if (!json::Parse(prof.json, v, err) || !character::FromJson(v, srv, err)) {
            LOGE("sync", g_set.playerName.c_str(), "server profile unreadable: %s", err.c_str());
            status = "server profile unreadable";
            if (haveLocal) { c = local; return Begin::Existing; }
            return Begin::Failed;
        }
        srv.revision = prof.revision;
        bool offlineChanges = haveLocal && local.uuid == srv.uuid && g_state.dirty && g_state.known && g_state.serverRevision == prof.revision;
        if (offlineChanges && Upload(local, prof.revision, "offline changes")) {
            c = local;
            c.money = srv.money;   // the server's balances; offline money changes are replayed by economy
            c.bank = srv.bank;
            status = "online - offline changes uploaded";
            Connected();
            return Begin::Existing;
        }
        c = srv;
        g_state = {prof.revision, false, true};
        profile_store::Save(c);
        profile_store::SaveSync(g_state);
        LOGI("sync", Who(c), "profile loaded from the server (rev %u)%s", prof.revision, haveLocal ? ", cache refreshed" : ", cache created");
        status = "online";
        Connected();
        return Begin::Existing;
    }
    if (haveLocal) {   // first time on this server: migrate the local (Phase 1) character
        if (Upload(local, 0, "first upload of the local character")) { c = local; status = "online - character uploaded"; Connected(); return Begin::Existing; }
        c = local;
        status = "online - upload failed, playing from the cache";
        return Begin::Existing;
    }
    status = "online - new character";
    Connected();
    return Begin::NeedsCreator;
}

void Save(character::Character &c, const char *why) {
    c.revision = g_state.serverRevision;
    profile_store::Save(c);
    g_state.dirty = true;
    profile_store::SaveSync(g_state);
    if (net::Connected() && !g_inFlight) {
        if (net::SendProfileSave(g_state.serverRevision, ToJsonText(c))) g_inFlight = true;
    }
    LOGD("sync", Who(c), "save (%s) %s", why, net::Connected() ? "sent" : "cached offline");
}

void Finish(character::Character &c) {
    if (net::Connected()) {
        g_inFlight = false;
        Upload(c, g_state.serverRevision, "exit");
        net::Disconnect();
    }
}

void Tick(character::Character &c, bool active) {
    net::Service();
    if (!active || !g_set.enabled || net::Connected()) return;
    unsigned long long now = GetTickCount64();
    if (now - g_lastReconnect < 30000) return;
    g_lastReconnect = now;
    std::string status;
    if (!Connect(status)) return;
    proto::ProfileMsg prof;
    if (!net::RequestProfile(prof, 3000)) return;
    // We have been playing offline: our copy is the newest. Upload on top of whatever the server has.
    if (prof.exists) {   // the balance comes from the server; offline money changes are replayed
        json::Value v;
        std::string err;
        character::Character srv;
        if (json::Parse(prof.json, v, err) && character::FromJson(v, srv, err)) { c.money = srv.money; c.bank = srv.bank; }
    }
    if (Upload(c, prof.exists ? prof.revision : 0, "reconnect")) gta::Notify("~b~Local Online~s~  Back online, progress synced.");
    Connected();
}

bool Online() { return net::Connected(); }

std::string StatusLine() {
    if (!g_set.enabled) return "server disabled";
    if (!net::Connected()) return "offline (local save)";
    return "online " + g_set.host + " (" + std::to_string(net::RttMs()) + " ms)";
}

}  // namespace sync
