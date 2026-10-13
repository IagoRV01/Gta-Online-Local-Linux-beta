// Minimal TOML reader for server.toml / client.toml. Supports what our config files use:
//   [section]            tables (one level; dotted keys become "section.key")
//   key = "string" | 'literal' | 123 | -1.5 | true | false
//   # comments, blank lines
// Arrays, inline tables and multi-line strings are rejected with an error (fail loudly, never guess).
// Pure C++, shared by the server and the client.
#pragma once
#include <map>
#include <string>

namespace toml {

struct Doc {
    std::map<std::string, std::string> values;   // "network.game_port" -> "4499" (strings unquoted)

    bool Has(const std::string &key) const { return values.count(key) != 0; }
    std::string Str(const std::string &key, const std::string &def) const;
    long long Int(const std::string &key, long long def) const;
    double Num(const std::string &key, double def) const;
    bool Bool(const std::string &key, bool def) const;
};

bool Parse(const std::string &text, Doc &out, std::string &err);   // err = "line N: ..."
bool LoadFile(const std::string &path, Doc &out, std::string &err);

}  // namespace toml
