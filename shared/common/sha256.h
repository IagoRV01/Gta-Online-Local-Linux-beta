// SHA-256 (FIPS 180-4), small self-contained implementation. Used to store player tokens hashed.
#pragma once
#include <cstdint>
#include <string>

namespace sha256 {

std::string Hex(const std::string &data);   // lowercase hex digest
std::string RandomToken(int bytes = 24);     // hex of OS-random bytes (std::random_device)

}  // namespace sha256
