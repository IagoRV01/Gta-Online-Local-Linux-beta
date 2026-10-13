// Storage tests against a real SQLite database in memory (same schema the server uses).
#include <cstdio>
#include <string>
#include "schema_sql.h"
#include "storage.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main() {
    auto db = MakeSqliteStorage();
    std::string err;
    CHECK(db->Open(":memory:", kSchemaSql, err));

    // accounts
    PlayerRow p{"11111111-1111-4111-8111-111111111111", "Alex", "hash", "player", false};
    CHECK(db->CreatePlayer(p));
    CHECK(!db->CreatePlayer(p));                                 // unique uuid/name
    auto found = db->FindPlayer("alex");                         // case-insensitive
    CHECK(found && found->uuid == p.uuid && found->tokenHash == "hash");
    CHECK(!db->FindPlayer("nobody"));

    // profile revisions
    CharacterRow c;
    c.uuid = "22222222-2222-4222-8222-222222222222";
    c.playerUuid = p.uuid;
    c.json = "{\"v\":1}";
    c.name = "Gori";
    c.model = "mp_m_freemode_01";
    c.money = 5000;
    uint32_t rev = 0;
    CHECK(db->SaveCharacter(c, 0, rev, err) == SaveResult::Ok && rev == 1);
    CHECK(db->SaveCharacter(c, 0, rev, err) == SaveResult::Conflict && rev == 1);   // stale base
    c.json = "{\"v\":2}";
    c.money = 999999999;                                          // a save must not change the balance
    CHECK(db->SaveCharacter(c, 1, rev, err) == SaveResult::Ok && rev == 2);
    auto loaded = db->LoadCharacter(p.uuid);
    CHECK(loaded && loaded->revision == 2 && loaded->json == "{\"v\":2}");
    CHECK(db->GetMoney(c.uuid) == 5000);
    CharacterRow other = c;
    other.playerUuid = "33333333-3333-4333-8333-333333333333";
    CHECK(db->SaveCharacter(other, 2, rev, err) == SaveResult::Invalid);           // not your character

    // economy
    long long bal = 0;
    CHECK(db->AdjustMoney(c.uuid, 2500, "job", bal, err) && bal == 7500);
    CHECK(db->AdjustMoney(c.uuid, -7000, "car", bal, err) && bal == 500);
    CHECK(!db->AdjustMoney(c.uuid, -501, "too much", bal, err) && bal == 500 && err == "insufficient funds");
    CHECK(db->GetMoney(c.uuid) == 500);
    CHECK(!db->AdjustMoney("nope", 10, "x", bal, err));
    CHECK(db->GetMoney("nope") == -1);
    long long w2 = 0, b2 = 0;
    CHECK(db->AdjustAccounts(c.uuid, -300, 300, "deposit", w2, b2, err) && w2 == 200 && b2 == 300);   // deposit
    CHECK(!db->AdjustAccounts(c.uuid, 0, -301, "too much", w2, b2, err) && err == "insufficient funds");
    CHECK(db->AdjustAccounts(c.uuid, 100, -100, "withdraw", w2, b2, err) && w2 == 300 && b2 == 200);
    CHECK(db->GetBank(c.uuid) == 200 && db->GetMoney(c.uuid) == 300);
    auto ledger = db->GetLedger(c.uuid, 3);   // newest first, one row per account touched
    CHECK(ledger.size() == 3 && ledger[0].delta == -100 && ledger[0].reason == "bank: withdraw" && ledger[1].reason == "wallet: withdraw" && ledger[2].delta == 300);
    CHECK(db->GetLedger("nope", 16).empty());

    // personal vehicles
    Storage::VehicleRow vr{"44444444-4444-4444-8444-444444444444", c.uuid, "{\"plate\":\"GORI\"}", "active"};
    CHECK(db->PutVehicle(vr, "GORI", 123, 1, 2, 3, 90, 1000));
    vr.state = "stored";
    vr.json = "{\"plate\":\"GORI\",\"v\":2}";
    CHECK(db->PutVehicle(vr, "GORI", 123, 4, 5, 6, 180, 800));   // upsert
    auto list = db->ListVehicles(c.uuid);
    CHECK(list.size() == 1 && list[0].state == "stored" && list[0].json == vr.json);
    auto gv = db->GetVehicle(vr.uuid);
    CHECK(gv && gv->ownerUuid == c.uuid);
    CHECK(!db->GetVehicle("nope"));
    CHECK(db->ListVehicles("someone else").empty());

    // world key/value
    db->SetWorld("weather", "RAIN");
    db->SetWorld("weather", "CLEAR");
    auto w = db->GetWorld("weather");
    CHECK(w && *w == "CLEAR");
    CHECK(!db->GetWorld("missing"));

    std::printf("storage tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
