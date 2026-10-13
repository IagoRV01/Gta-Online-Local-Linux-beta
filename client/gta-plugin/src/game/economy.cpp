#include "game/economy.h"
#include <windows.h>
#include <deque>
#include "common/json.h"
#include "common/log.h"
#include "engine/gta.h"
#include "game/net_client.h"

namespace economy {

namespace {

struct Entry { uint32_t id = 0; uint8_t account = proto::kWallet; int64_t delta = 0; std::string reason; bool sent = false; };

std::string g_path;
Accounts g_acc;
std::function<void(int64_t, int64_t)> g_listener;
std::deque<Entry> g_pending;     // offline (not yet sent) and in-flight changes, in order
uint32_t g_nextId = 1;

const char *AccountName(uint8_t a) { return a == proto::kWallet ? "wallet" : a == proto::kBank ? "bank" : "deposit"; }

// The effect of one entry on (wallet, bank).
void Effect(const Entry &e, int64_t &w, int64_t &b) {
    if (e.account == proto::kWallet) w += e.delta;
    else if (e.account == proto::kBank) b += e.delta;
    else { w -= e.delta; b += e.delta; }
}

void Apply(int64_t w, int64_t b) {
    if (w < 0) w = 0;
    if (b < 0) b = 0;
    int64_t dw = w - g_acc.wallet(), db = b - g_acc.bank();
    g_acc.setWallet(w);
    g_acc.setBank(b);
    if ((dw || db) && g_listener) g_listener(dw, db);
}

void Persist() {
    json::Value arr;
    arr.type = json::Value::Array;
    for (const auto &e : g_pending) {
        json::Value o = json::Value::MakeObject();
        o.Set("account", json::Value::MakeNumber(e.account));
        o.Set("delta", json::Value::MakeNumber((double)e.delta));
        o.Set("reason", json::Value::MakeString(e.reason));
        arr.arr.push_back(o);
    }
    std::string tmp = g_path + ".tmp";
    if (json::SaveFile(tmp, arr)) MoveFileExA(tmp.c_str(), g_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

void Send(Entry &e) {
    wire::Writer w;
    proto::MoneyChangeMsg m;
    m.requestId = e.id;
    m.account = e.account;
    m.delta = e.delta;
    m.reason = e.reason.substr(0, proto::kMaxMoneyReason);
    proto::Put(w, m);
    net::SendEvent(w);
    e.sent = true;
}

}  // namespace

void Init(const std::string &dir, Accounts acc) {
    g_path = dir + "economy_pending.json";
    g_acc = std::move(acc);
    json::Value v;
    std::string err;
    if (json::LoadFile(g_path, v, err) && v.type == json::Value::Array) {
        for (const auto &o : v.arr) {
            int a = (int)o.Num("account", proto::kWallet);   // files from before the bank: wallet
            if (a < proto::kWallet || a > proto::kDeposit) continue;
            g_pending.push_back({g_nextId++, (uint8_t)a, (int64_t)o.Num("delta", 0), o.Str("reason", "offline"), false});
        }
        if (!g_pending.empty()) LOGI("economy", "-", "%d offline money change(s) waiting for the server", (int)g_pending.size());
    }
}

void SetListener(std::function<void(int64_t, int64_t)> fn) { g_listener = std::move(fn); }

int64_t Total() { return g_acc.wallet() + g_acc.bank(); }
int64_t Wallet() { return g_acc.wallet(); }
int64_t Bank() { return g_acc.bank(); }

bool Change(uint8_t account, int64_t delta, const std::string &reason) {
    if (delta == 0) return true;
    if (account > proto::kDeposit) return false;
    Entry e{g_nextId++, account, delta, reason, false};
    int64_t w = g_acc.wallet(), b = g_acc.bank();
    Effect(e, w, b);
    if (w < 0 || b < 0) return false;
    Apply(w, b);   // optimistic; corrected by the server's answer
    g_pending.push_back(e);
    if (net::Connected()) Send(g_pending.back());
    Persist();
    LOGI("economy", "-", "%+lld %s (%s) -> wallet %lld, bank %lld%s", (long long)delta, AccountName(account), reason.c_str(), (long long)w, (long long)b,
         net::Connected() ? "" : " [offline, queued]");
    return true;
}

bool Change(int64_t delta, const std::string &reason) { return Change(proto::kWallet, delta, reason); }

bool Spend(int64_t amount, const std::string &reason, bool preferBank) {
    if (amount <= 0) return amount == 0;
    int64_t w = g_acc.wallet(), b = g_acc.bank();
    if (w + b < amount) return false;
    if (preferBank && b >= amount) return Change(proto::kBank, -amount, reason);
    if (w >= amount) return Change(proto::kWallet, -amount, reason);
    if (b >= amount) return Change(proto::kBank, -amount, reason);
    return Change(proto::kWallet, -w, reason) && Change(proto::kBank, -(amount - w), reason);
}

void OnResult(const proto::MoneyResultMsg &m) {
    for (auto it = g_pending.begin(); it != g_pending.end(); ++it) {
        if (it->id != m.requestId) continue;
        if (!m.ok) {
            LOGW("economy", "-", "server refused %+lld %s (%s): %s", (long long)it->delta, AccountName(it->account), it->reason.c_str(), m.reason.c_str());
            gta::Notify("~r~Transaction refused:~s~ " + m.reason);
        }
        g_pending.erase(it);
        break;
    }
    // The server's balances are the truth, plus whatever we still have in flight/queued.
    int64_t w = m.wallet, b = m.bank;
    for (const auto &e : g_pending) Effect(e, w, b);
    Apply(w, b);
    Persist();
}

void OnConnected() {
    // Changes sent before a disconnect but never answered: the profile we just got from the server
    // already contains them if they were applied. Dropping them avoids paying twice.
    int dropped = 0;
    for (auto it = g_pending.begin(); it != g_pending.end();)
        if (it->sent) { ++dropped; it = g_pending.erase(it); } else ++it;
    if (dropped) LOGW("economy", "-", "%d unconfirmed money change(s) dropped after reconnecting", dropped);
    int n = 0;
    for (auto &e : g_pending)
        if (!e.sent) { Send(e); ++n; }
    if (n) LOGI("economy", "-", "replaying %d offline money change(s)", n);
}

int PendingCount() { return (int)g_pending.size(); }

namespace {
std::vector<proto::LedgerRow> g_ledger;
int g_ledgerRev = 0;
uint32_t g_ledgerReq = 0;
}
void RequestLedger() {
    if (!net::Connected()) return;
    wire::Writer w;
    proto::Put(w, proto::LedgerRequestMsg{++g_ledgerReq});
    net::SendEvent(w);
}
const std::vector<proto::LedgerRow> &Ledger() { return g_ledger; }
int LedgerRevision() { return g_ledgerRev; }
void OnLedger(const proto::LedgerMsg &m) {
    g_ledger = m.rows;
    ++g_ledgerRev;
    LOGI("economy", "-", "ledger: %d rows", (int)g_ledger.size());
}

}  // namespace economy
