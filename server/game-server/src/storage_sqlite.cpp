// SQLite driver for Storage. WAL journal, foreign keys on, every write in its own statement or
// transaction. All SQL uses bound parameters (no string concatenation of values).
#include <sqlite3.h>
#include <ctime>
#include "common/log.h"
#include "storage.h"

std::string NowIsoUtc() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return b;
}

namespace {

class Stmt {
public:
    Stmt(sqlite3 *db, const char *sql) { if (sqlite3_prepare_v2(db, sql, -1, &s_, nullptr) != SQLITE_OK) LOGE("database", "-", "prepare failed: %s (%s)", sqlite3_errmsg(db), sql); }
    ~Stmt() { sqlite3_finalize(s_); }
    Stmt &Bind(int i, const std::string &v) { sqlite3_bind_text(s_, i, v.c_str(), (int)v.size(), SQLITE_TRANSIENT); return *this; }
    Stmt &Bind(int i, long long v) { sqlite3_bind_int64(s_, i, v); return *this; }
    Stmt &Bind(int i, double v) { sqlite3_bind_double(s_, i, v); return *this; }
    int Step() { return s_ ? sqlite3_step(s_) : SQLITE_ERROR; }
    std::string Text(int c) { auto *t = sqlite3_column_text(s_, c); return t ? (const char *)t : ""; }
    long long Int(int c) { return sqlite3_column_int64(s_, c); }
    bool ok() const { return s_ != nullptr; }

private:
    sqlite3_stmt *s_ = nullptr;
};

class SqliteStorage : public Storage {
public:
    ~SqliteStorage() override { if (db_) sqlite3_close(db_); }

    bool Open(const std::string &path, const std::string &schemaSql, std::string &err) override {
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) { err = sqlite3_errmsg(db_); return false; }
        Exec("PRAGMA journal_mode=WAL;");
        Exec("PRAGMA foreign_keys=ON;");
        Exec("PRAGMA busy_timeout=2000;");
        bool fresh = true;
        {
            Stmt q(db_, "SELECT name FROM sqlite_master WHERE type='table' AND name='schema_version'");
            fresh = q.Step() != SQLITE_ROW;
        }
        if (fresh) {
            char *msg = nullptr;
            if (sqlite3_exec(db_, ("BEGIN;" + schemaSql + "COMMIT;").c_str(), nullptr, nullptr, &msg) != SQLITE_OK) {
                err = msg ? msg : "schema failed";
                sqlite3_free(msg);
                Exec("ROLLBACK;");
                return false;
            }
            LOGI("database", "-", "created new database %s (schema v2)", path.c_str());
        } else {
            long long v = 0;
            {
                Stmt q(db_, "SELECT version FROM schema_version");
                v = q.Step() == SQLITE_ROW ? q.Int(0) : 0;
            }
            if (v == 1) {   // v1 -> v2: Maze Bank accounts
                char *msg = nullptr;
                if (sqlite3_exec(db_, "BEGIN; ALTER TABLE characters ADD COLUMN bank BIGINT NOT NULL DEFAULT 0; UPDATE schema_version SET version = 2; COMMIT;",
                                 nullptr, nullptr, &msg) != SQLITE_OK) {
                    err = std::string("migration to v2 failed: ") + (msg ? msg : "?");
                    sqlite3_free(msg);
                    Exec("ROLLBACK;");
                    return false;
                }
                LOGI("database", "-", "migrated schema v1 -> v2 (bank column)");
                v = 2;
            }
            LOGI("database", "-", "opened %s (schema v%lld)", path.c_str(), v);
            if (v != 2) { err = "unsupported schema version " + std::to_string(v); return false; }
        }
        return true;
    }

    std::optional<PlayerRow> FindPlayer(const std::string &name) override {
        Stmt q(db_, "SELECT uuid, name, token_hash, role, banned FROM players WHERE name = ?1 COLLATE NOCASE");
        q.Bind(1, name);
        if (q.Step() != SQLITE_ROW) return std::nullopt;
        return PlayerRow{q.Text(0), q.Text(1), q.Text(2), q.Text(3), q.Int(4) != 0};
    }

    bool CreatePlayer(const PlayerRow &p) override {
        Stmt q(db_, "INSERT INTO players(uuid, name, token_hash, role, created_at) VALUES(?1, ?2, ?3, ?4, ?5)");
        q.Bind(1, p.uuid).Bind(2, p.name).Bind(3, p.tokenHash).Bind(4, p.role.empty() ? std::string("player") : p.role).Bind(5, NowIsoUtc());
        return q.Step() == SQLITE_DONE;
    }

    void TouchPlayer(const std::string &uuid) override {
        Stmt q(db_, "UPDATE players SET last_seen_at = ?1 WHERE uuid = ?2");
        q.Bind(1, NowIsoUtc()).Bind(2, uuid).Step();
    }

    std::optional<CharacterRow> LoadCharacter(const std::string &playerUuid) override {
        Stmt q(db_, "SELECT uuid, player_uuid, appearance, revision FROM characters WHERE player_uuid = ?1 ORDER BY updated_at DESC LIMIT 1");
        q.Bind(1, playerUuid);
        if (q.Step() != SQLITE_ROW) return std::nullopt;
        CharacterRow row;
        row.uuid = q.Text(0); row.playerUuid = q.Text(1); row.json = q.Text(2); row.revision = (uint32_t)q.Int(3);
        return row;
    }

    SaveResult SaveCharacter(const CharacterRow &row, uint32_t base, uint32_t &newRev, std::string &err) override {
        // The full profile JSON lives in `appearance` (schema v1 keeps state inside it too); the
        // other columns are denormalised for queries/GodEye. The JSON is validated by the caller.
        Exec("BEGIN IMMEDIATE;");
        bool exists = false;
        uint32_t stored = 0;
        std::string owner;
        {
            Stmt cur(db_, "SELECT revision, player_uuid FROM characters WHERE uuid = ?1");
            cur.Bind(1, row.uuid);
            if (cur.Step() == SQLITE_ROW) { exists = true; stored = (uint32_t)cur.Int(0); owner = cur.Text(1); }
        }
        if (exists && owner != row.playerUuid) { Exec("ROLLBACK;"); err = "character belongs to another player"; return SaveResult::Invalid; }
        if (stored != base) { Exec("ROLLBACK;"); err = "stale revision " + std::to_string(base) + " (server has " + std::to_string(stored) + ")"; newRev = stored; return SaveResult::Conflict; }
        newRev = base + 1;
        int rc;
        {
            Stmt w(db_, exists
                ? "UPDATE characters SET appearance = ?1, revision = ?2, updated_at = ?3, name = ?5, model = ?6, pos_x = ?8, pos_y = ?9, pos_z = ?10, heading = ?11 WHERE uuid = ?4 AND ?7 = ?7"
                : "INSERT INTO characters(appearance, revision, updated_at, uuid, name, model, money, pos_x, pos_y, pos_z, heading, player_uuid, state, bank) "
                  "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, '{}', ?13)");
            w.Bind(1, row.json).Bind(2, (long long)newRev).Bind(3, NowIsoUtc()).Bind(4, row.uuid).Bind(5, row.name).Bind(6, row.model)
             .Bind(7, row.money).Bind(8, row.x).Bind(9, row.y).Bind(10, row.z).Bind(11, row.heading);
            if (!exists) w.Bind(12, row.playerUuid).Bind(13, row.bank);
            rc = w.Step();
        }
        if (rc != SQLITE_DONE) { err = sqlite3_errmsg(db_); Exec("ROLLBACK;"); return SaveResult::Error; }
        Exec("COMMIT;");
        return SaveResult::Ok;
    }

    long long StartSession(const std::string &playerUuid, const std::string &address) override {
        Stmt q(db_, "INSERT INTO sessions(player_uuid, address, started_at) VALUES(?1, ?2, ?3)");
        q.Bind(1, playerUuid).Bind(2, address).Bind(3, NowIsoUtc()).Step();
        return sqlite3_last_insert_rowid(db_);
    }

    void EndSession(long long id, const std::string &reason) override {
        Stmt q(db_, "UPDATE sessions SET ended_at = ?1, end_reason = ?2 WHERE id = ?3");
        q.Bind(1, NowIsoUtc()).Bind(2, reason).Bind(3, id).Step();
    }

    void Event(const std::string &kind, const std::string &actor, const std::string &data) override {
        Stmt q(db_, "INSERT INTO events(at, kind, actor_uuid, data) VALUES(?1, ?2, ?3, ?4)");
        q.Bind(1, NowIsoUtc()).Bind(2, kind).Bind(3, actor).Bind(4, data.empty() ? std::string("{}") : data).Step();
    }

    bool AdjustMoney(const std::string &cu, long long delta, const std::string &reason, long long &balance, std::string &err) override {
        long long bank = 0;
        return AdjustAccounts(cu, delta, 0, reason, balance, bank, err);
    }

    bool AdjustAccounts(const std::string &cu, long long dWallet, long long dBank, const std::string &reason, long long &wallet, long long &bank, std::string &err) override {
        Exec("BEGIN IMMEDIATE;");
        long long cw = -1, cb = -1;
        {
            Stmt q(db_, "SELECT money, bank FROM characters WHERE uuid = ?1");
            q.Bind(1, cu);
            if (q.Step() == SQLITE_ROW) { cw = q.Int(0); cb = q.Int(1); }
        }
        wallet = cw < 0 ? 0 : cw;
        bank = cb < 0 ? 0 : cb;
        if (cw < 0) { Exec("ROLLBACK;"); err = "unknown character"; return false; }
        if (cw + dWallet < 0 || cb + dBank < 0) { Exec("ROLLBACK;"); err = "insufficient funds"; return false; }
        int rc = SQLITE_DONE;
        {
            Stmt u(db_, "UPDATE characters SET money = ?1, bank = ?2 WHERE uuid = ?3");
            u.Bind(1, cw + dWallet).Bind(2, cb + dBank).Bind(3, cu);
            rc = u.Step();
        }
        for (auto [d, acct] : {std::pair<long long, const char *>{dWallet, "wallet"}, {dBank, "bank"}}) {
            if (!d || rc != SQLITE_DONE) continue;
            Stmt l(db_, "INSERT INTO economy_ledger(character_uuid, delta, reason, created_at) VALUES(?1, ?2, ?3, ?4)");
            l.Bind(1, cu).Bind(2, d).Bind(3, std::string(acct) + ": " + reason).Bind(4, NowIsoUtc());
            rc = l.Step();
        }
        if (rc != SQLITE_DONE) { err = sqlite3_errmsg(db_); Exec("ROLLBACK;"); return false; }
        Exec("COMMIT;");
        wallet = cw + dWallet;
        bank = cb + dBank;
        return true;
    }

    std::vector<LedgerRow> GetLedger(const std::string &cu, int limit) override {
        std::vector<LedgerRow> out;
        Stmt q(db_, "SELECT delta, reason, created_at FROM economy_ledger WHERE character_uuid = ?1 ORDER BY id DESC LIMIT ?2");
        q.Bind(1, cu).Bind(2, (long long)limit);
        while (q.Step() == SQLITE_ROW) out.push_back({q.Int(0), q.Text(1), q.Text(2)});
        return out;
    }

    long long GetBank(const std::string &cu) override {
        Stmt q(db_, "SELECT bank FROM characters WHERE uuid = ?1");
        q.Bind(1, cu);
        return q.Step() == SQLITE_ROW ? q.Int(0) : -1;
    }

    long long GetMoney(const std::string &cu) override {
        Stmt q(db_, "SELECT money FROM characters WHERE uuid = ?1");
        q.Bind(1, cu);
        return q.Step() == SQLITE_ROW ? q.Int(0) : -1;
    }

    std::vector<VehicleRow> ListVehicles(const std::string &owner) override {
        std::vector<VehicleRow> out;
        Stmt q(db_, "SELECT uuid, owner_uuid, mods, state FROM vehicles WHERE owner_uuid = ?1 ORDER BY updated_at");
        q.Bind(1, owner);
        while (q.Step() == SQLITE_ROW) out.push_back({q.Text(0), q.Text(1), q.Text(2), q.Text(3)});
        return out;
    }

    std::optional<VehicleRow> GetVehicle(const std::string &uuid) override {
        Stmt q(db_, "SELECT uuid, owner_uuid, mods, state FROM vehicles WHERE uuid = ?1");
        q.Bind(1, uuid);
        if (q.Step() != SQLITE_ROW) return std::nullopt;
        return VehicleRow{q.Text(0), q.Text(1), q.Text(2), q.Text(3)};
    }

    bool PutVehicle(const VehicleRow &r, const std::string &plate, uint32_t model, double x, double y, double z, double heading, double health) override {
        Stmt q(db_, "INSERT INTO vehicles(uuid, owner_uuid, model, plate, mods, health, pos_x, pos_y, pos_z, heading, state, updated_at) "
                    "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12) "
                    "ON CONFLICT(uuid) DO UPDATE SET mods = excluded.mods, health = excluded.health, pos_x = excluded.pos_x, pos_y = excluded.pos_y, "
                    "pos_z = excluded.pos_z, heading = excluded.heading, state = excluded.state, updated_at = excluded.updated_at");
        q.Bind(1, r.uuid).Bind(2, r.ownerUuid).Bind(3, std::to_string(model)).Bind(4, plate).Bind(5, r.json).Bind(6, health)
         .Bind(7, x).Bind(8, y).Bind(9, z).Bind(10, heading).Bind(11, r.state).Bind(12, NowIsoUtc());
        return q.Step() == SQLITE_DONE;
    }

    std::optional<std::string> GetWorld(const std::string &key) override {
        Stmt q(db_, "SELECT value FROM world_state WHERE key = ?1");
        q.Bind(1, key);
        if (q.Step() != SQLITE_ROW) return std::nullopt;
        return q.Text(0);
    }

    void SetWorld(const std::string &key, const std::string &value) override {
        Stmt q(db_, "INSERT INTO world_state(key, value, updated_at) VALUES(?1, ?2, ?3) "
                    "ON CONFLICT(key) DO UPDATE SET value = excluded.value, updated_at = excluded.updated_at");
        q.Bind(1, key).Bind(2, value).Bind(3, NowIsoUtc()).Step();
    }

private:
    void Exec(const char *sql) {
        char *msg = nullptr;
        if (sqlite3_exec(db_, sql, nullptr, nullptr, &msg) != SQLITE_OK) { LOGE("database", "-", "%s: %s", sql, msg ? msg : "?"); sqlite3_free(msg); }
    }
    sqlite3 *db_ = nullptr;
};

}  // namespace

std::unique_ptr<Storage> MakeSqliteStorage() { return std::make_unique<SqliteStorage>(); }
