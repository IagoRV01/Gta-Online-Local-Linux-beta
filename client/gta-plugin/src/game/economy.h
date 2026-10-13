// Phase 8: money is owned by the server. The client applies a change at once (so purchases feel
// instant), asks the server (MoneyChange), and takes the server's balances as the truth when the
// answer arrives. Offline changes are queued in cache\economy_pending.json and replayed (validated
// by the server) on the next connection.
// Like GTA Online there are two accounts: the wallet (cash on hand) and the Maze Bank account.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "protocol/protocol.h"

namespace economy {

struct Accounts {
    std::function<int64_t()> wallet, bank;
    std::function<void(int64_t)> setWallet, setBank;
};

// The accessors read/write the YOU character's balances (owned by session).
void Init(const std::string &cacheDir, Accounts acc);
// account: proto::kWallet, kBank or kDeposit (delta > 0 moves wallet -> bank, < 0 withdraws).
bool Change(uint8_t account, int64_t delta, const std::string &reason);   // false: not enough money locally
bool Change(int64_t delta, const std::string &reason);                    // wallet (compatibility)
// GTA Online rules: a purchase is paid from the wallet when it covers it, otherwise from the bank,
// otherwise from both (wallet emptied first). preferBank: the internet / phone pay from the bank.
bool Spend(int64_t amount, const std::string &reason, bool preferBank = false);
int64_t Total();
int64_t Wallet();
int64_t Bank();
void OnResult(const proto::MoneyResultMsg &m);
void OnConnected();                                      // replay queued offline changes
int PendingCount();
// The Maze Bank transaction log (server ledger, newest first). RequestLedger asks the server; the
// answer replaces Ledger() when it arrives (LedgerRevision() counts the answers).
void RequestLedger();
const std::vector<proto::LedgerRow> &Ledger();
int LedgerRevision();
void OnLedger(const proto::LedgerMsg &m);
// Called after every balance change (HUD animation): wallet delta, bank delta.
void SetListener(std::function<void(int64_t, int64_t)> fn);

}  // namespace economy
