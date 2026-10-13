// Persistence interface of the game server. The server logic only talks to this interface;
// SqliteStorage is the first driver, a PostgreSQL driver can implement the same methods later.
// Schema: server/database/schema.sql (applied by Open, versioned in schema_version).
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct PlayerRow {
    std::string uuid, name, tokenHash, role;
    bool banned = false;
};

struct CharacterRow {
    std::string uuid, playerUuid, json;   // json = character::ToJson (the authoritative profile)
    uint32_t revision = 0;
    // denormalised copies of json fields, for queries / GodEye
    std::string name, model;
    long long money = 0, bank = 0;
    double x = 0, y = 0, z = 0, heading = 0;
};

enum class SaveResult { Ok, Conflict, Invalid, Error };

class Storage {
public:
    virtual ~Storage() = default;
    virtual bool Open(const std::string &path, const std::string &schemaSql, std::string &err) = 0;

    virtual std::optional<PlayerRow> FindPlayer(const std::string &name) = 0;
    virtual bool CreatePlayer(const PlayerRow &p) = 0;
    virtual void TouchPlayer(const std::string &uuid) = 0;   // last_seen_at = now

    virtual std::optional<CharacterRow> LoadCharacter(const std::string &playerUuid) = 0;
    // Inserts (baseRevision 0, no row yet) or updates when the stored revision == baseRevision.
    // On success newRevision = baseRevision + 1 and row.json is stored with that revision.
    virtual SaveResult SaveCharacter(const CharacterRow &row, uint32_t baseRevision, uint32_t &newRevision, std::string &err) = 0;

    virtual long long StartSession(const std::string &playerUuid, const std::string &address) = 0;
    virtual void EndSession(long long id, const std::string &reason) = 0;
    virtual void Event(const std::string &kind, const std::string &actorUuid, const std::string &dataJson) = 0;
    // Economy: the `money` column is the only authoritative balance. AdjustMoney is atomic, refuses
    // to go below zero, and writes a ledger row. GetMoney returns -1 if the character is unknown.
    virtual bool AdjustMoney(const std::string &characterUuid, long long delta, const std::string &reason, long long &balance, std::string &err) = 0;
    virtual long long GetMoney(const std::string &characterUuid) = 0;
    // Wallet and bank in one transaction (e.g. deposit = -x wallet, +x bank); both must stay >= 0.
    virtual bool AdjustAccounts(const std::string &characterUuid, long long dWallet, long long dBank, const std::string &reason,
                                long long &wallet, long long &bank, std::string &err) = 0;
    virtual long long GetBank(const std::string &characterUuid) = 0;
    // The newest ledger rows (reason "wallet: ..." / "bank: ..." as AdjustAccounts writes them).
    struct LedgerRow { long long delta; std::string reason, at; };
    virtual std::vector<LedgerRow> GetLedger(const std::string &characterUuid, int limit) = 0;
    // Personal vehicles (Phase 8). `json` is vehicle::ToJson; state/position columns are kept in sync.
    struct VehicleRow { std::string uuid, ownerUuid, json, state; };
    virtual std::vector<VehicleRow> ListVehicles(const std::string &ownerCharacterUuid) = 0;
    virtual std::optional<VehicleRow> GetVehicle(const std::string &uuid) = 0;
    virtual bool PutVehicle(const VehicleRow &row, const std::string &plate, uint32_t model, double x, double y, double z, double heading, double health) = 0;
    virtual std::optional<std::string> GetWorld(const std::string &key) = 0;
    virtual void SetWorld(const std::string &key, const std::string &value) = 0;
};

std::unique_ptr<Storage> MakeSqliteStorage();
std::string NowIsoUtc();
