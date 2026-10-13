// Maze Bank for the YOU character, like GTA Online: walk up to any ATM (the four ATM props of the
// map) and press E to deposit or withdraw; the F10 menu offers online banking (deposits only,
// withdrawals need an ATM). Every operation is a server-validated economy change.
#pragma once
#include <sstream>
#include <string>

namespace bank {

void Frame(int me);              // every frame while YOU is active (ATM prompt)
void Menu(bool atAtm);           // blocking menu (Maze Bank)
bool HandleCommand(const std::string &v, std::istringstream &in);   // bank.* test commands

}  // namespace bank
