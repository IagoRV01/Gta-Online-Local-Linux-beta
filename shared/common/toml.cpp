#include "common/toml.h"
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace toml {

static std::string Trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static bool ValidKey(const std::string &k) {
    if (k.empty()) return false;
    for (char c : k)
        if (!(isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')) return false;
    return true;
}

bool Parse(const std::string &text, Doc &out, std::string &err) {
    std::istringstream in(text);
    std::string line, section;
    int n = 0;
    auto fail = [&](const std::string &m) { err = "line " + std::to_string(n) + ": " + m; return false; };
    while (std::getline(in, line)) {
        ++n;
        std::string s = Trim(line);
        if (s.empty() || s[0] == '#') continue;
        if (s[0] == '[') {
            if (s.size() < 3 || s.back() != ']' || s[1] == '[') return fail("bad table header");
            section = Trim(s.substr(1, s.size() - 2));
            if (!ValidKey(section)) return fail("bad table name");
            continue;
        }
        size_t eq = s.find('=');
        if (eq == std::string::npos) return fail("expected key = value");
        std::string key = Trim(s.substr(0, eq)), val = Trim(s.substr(eq + 1));
        if (!ValidKey(key)) return fail("bad key '" + key + "'");
        if (val.empty()) return fail("missing value");
        std::string parsed;
        if (val[0] == '"' || val[0] == '\'') {
            char q = val[0];
            size_t i = 1;
            for (; i < val.size() && val[i] != q; ++i) {
                if (q == '"' && val[i] == '\\' && i + 1 < val.size()) {
                    char e = val[++i];
                    parsed += e == 'n' ? '\n' : e == 't' ? '\t' : e;   // \" \\ and the rest literally
                } else parsed += val[i];
            }
            if (i >= val.size()) return fail("unterminated string");
            std::string rest = Trim(val.substr(i + 1));
            if (!rest.empty() && rest[0] != '#') return fail("text after string");
        } else {
            size_t hash = val.find('#');
            parsed = Trim(hash == std::string::npos ? val : val.substr(0, hash));
            if (parsed[0] == '[' || parsed[0] == '{') return fail("arrays/inline tables are not supported");
            if (parsed != "true" && parsed != "false") {
                char *end = nullptr;
                std::strtod(parsed.c_str(), &end);
                if (!end || *end) return fail("bad value '" + parsed + "' (strings need quotes)");
            }
        }
        std::string full = section.empty() ? key : section + "." + key;
        if (out.values.count(full)) return fail("duplicate key '" + full + "'");
        out.values[full] = parsed;
    }
    return true;
}

bool LoadFile(const std::string &path, Doc &out, std::string &err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "cannot open " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    return Parse(ss.str(), out, err);
}

std::string Doc::Str(const std::string &k, const std::string &def) const {
    auto it = values.find(k);
    return it == values.end() ? def : it->second;
}
long long Doc::Int(const std::string &k, long long def) const {
    auto it = values.find(k);
    return it == values.end() ? def : std::strtoll(it->second.c_str(), nullptr, 10);
}
double Doc::Num(const std::string &k, double def) const {
    auto it = values.find(k);
    return it == values.end() ? def : std::strtod(it->second.c_str(), nullptr);
}
bool Doc::Bool(const std::string &k, bool def) const {
    auto it = values.find(k);
    return it == values.end() ? def : it->second == "true";
}

}  // namespace toml
