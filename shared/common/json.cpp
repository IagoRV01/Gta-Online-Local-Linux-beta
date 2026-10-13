#include "common/json.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace json {

const Value *Value::Find(const std::string &key) const {
    if (type != Object) return nullptr;
    for (auto &kv : obj) if (kv.first == key) return &kv.second;
    return nullptr;
}
Value *Value::Find(const std::string &key) { return const_cast<Value *>(static_cast<const Value *>(this)->Find(key)); }

const Value *Value::Path(const std::string &dotted) const {
    const Value *cur = this;
    size_t start = 0;
    while (cur && start <= dotted.size()) {
        size_t dot = dotted.find('.', start);
        std::string part = dotted.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        cur = cur->Find(part);
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return cur;
}
Value *Value::Path(const std::string &dotted) { return const_cast<Value *>(static_cast<const Value *>(this)->Path(dotted)); }

Value &Value::Set(const std::string &key, Value v) {
    if (type != Object) { *this = MakeObject(); }
    for (auto &kv : obj) if (kv.first == key) { kv.second = std::move(v); return kv.second; }
    obj.emplace_back(key, std::move(v));
    return obj.back().second;
}

double Value::Num(const std::string &dotted, double def) const {
    const Value *v = Path(dotted);
    return v && v->type == Number ? v->num : def;
}
std::string Value::Str(const std::string &dotted, const std::string &def) const {
    const Value *v = Path(dotted);
    return v && v->type == String ? v->str : def;
}
bool Value::Flag(const std::string &dotted, bool def) const {
    const Value *v = Path(dotted);
    return v && v->type == Bool ? v->b : def;
}

// ------------------------------------------------------------------ parser
namespace {
struct Parser {
    const std::string &s;
    size_t i = 0;
    std::string err;

    int Line() const { int n = 1; for (size_t k = 0; k < i && k < s.size(); ++k) if (s[k] == '\n') ++n; return n; }
    bool Fail(const char *what) { if (err.empty()) err = "line " + std::to_string(Line()) + ": " + what; return false; }
    void Ws() {
        for (;;) {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
            // allow // comments in config files
            if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') { while (i < s.size() && s[i] != '\n') ++i; continue; }
            break;
        }
    }
    bool Lit(const char *w) { size_t n = std::char_traits<char>::length(w); if (s.compare(i, n, w) == 0) { i += n; return true; } return false; }

    bool ParseString(std::string &out) {
        if (s[i] != '"') return Fail("expected string");
        ++i;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c == '\\') {
                if (i >= s.size()) return Fail("bad escape");
                char e = s[i++];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        if (i + 4 > s.size()) return Fail("bad \\u escape");
                        unsigned cp = (unsigned)std::strtoul(s.substr(i, 4).c_str(), nullptr, 16); i += 4;
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                        else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                        break;
                    }
                    default: out += e;
                }
            } else out += c;
        }
        if (i >= s.size()) return Fail("unterminated string");
        ++i;
        return true;
    }

    bool ParseValue(Value &v, int depth) {
        if (depth > 64) return Fail("nesting too deep");
        Ws();
        if (i >= s.size()) return Fail("unexpected end");
        char c = s[i];
        if (c == '{') {
            v = Value::MakeObject(); ++i; Ws();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            for (;;) {
                Ws();
                std::string key;
                if (!ParseString(key)) return false;
                Ws();
                if (i >= s.size() || s[i] != ':') return Fail("expected ':'");
                ++i;
                Value child;
                if (!ParseValue(child, depth + 1)) return false;
                v.obj.emplace_back(std::move(key), std::move(child));
                Ws();
                if (i < s.size() && s[i] == ',') { ++i; Ws(); if (i < s.size() && s[i] == '}') { ++i; return true; } continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return Fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v = Value(); v.type = Value::Array; ++i; Ws();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            for (;;) {
                Value child;
                if (!ParseValue(child, depth + 1)) return false;
                v.arr.push_back(std::move(child));
                Ws();
                if (i < s.size() && s[i] == ',') { ++i; Ws(); if (i < s.size() && s[i] == ']') { ++i; return true; } continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return Fail("expected ',' or ']'");
            }
        }
        if (c == '"') { v = Value(); v.type = Value::String; return ParseString(v.str); }
        if (Lit("true")) { v = Value::MakeBool(true); return true; }
        if (Lit("false")) { v = Value::MakeBool(false); return true; }
        if (Lit("null")) { v = Value(); return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const char *start = s.c_str() + i; char *end = nullptr;
            double d = std::strtod(start, &end);
            if (end == start) return Fail("bad number");
            i += (size_t)(end - start);
            v = Value::MakeNumber(d);
            return true;
        }
        return Fail("unexpected character");
    }
};

void Escape(const std::string &in, std::string &out) {
    out += '"';
    for (char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default: out += c;
        }
    }
    out += '"';
}

void WriteImpl(const Value &v, int indent, int level, std::string &out) {
    std::string pad(indent * (level + 1), ' '), padEnd(indent * level, ' ');
    switch (v.type) {
        case Value::Null: out += "null"; break;
        case Value::Bool: out += v.b ? "true" : "false"; break;
        case Value::Number: {
            char buf[64];
            if (std::fabs(v.num - std::round(v.num)) < 1e-9 && std::fabs(v.num) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", v.num);
            else std::snprintf(buf, sizeof buf, "%.6g", v.num);
            out += buf;
            break;
        }
        case Value::String: Escape(v.str, out); break;
        case Value::Array: {
            // short arrays of scalars stay on one line ([x, y, z])
            bool flat = v.arr.size() <= 6;
            for (auto &e : v.arr) if (e.type == Value::Array || e.type == Value::Object) flat = false;
            out += '[';
            for (size_t k = 0; k < v.arr.size(); ++k) {
                if (flat) { if (k) out += ", "; }
                else { out += k ? ",\n" : "\n"; out += pad; }
                WriteImpl(v.arr[k], indent, level + 1, out);
            }
            if (!flat && !v.arr.empty()) { out += '\n'; out += padEnd; }
            out += ']';
            break;
        }
        case Value::Object: {
            out += '{';
            for (size_t k = 0; k < v.obj.size(); ++k) {
                out += k ? ",\n" : "\n";
                out += pad;
                Escape(v.obj[k].first, out);
                out += ": ";
                WriteImpl(v.obj[k].second, indent, level + 1, out);
            }
            if (!v.obj.empty()) { out += '\n'; out += padEnd; }
            out += '}';
            break;
        }
    }
}
}  // namespace

bool Parse(const std::string &text, Value &out, std::string &err) {
    Parser p{text, 0, {}};
    Value v;
    if (!p.ParseValue(v, 0)) { err = p.err; return false; }
    p.Ws();
    if (p.i != text.size()) { p.Fail("trailing characters"); err = p.err; return false; }
    out = std::move(v);
    return true;
}

std::string Write(const Value &v, int indent) {
    std::string out;
    WriteImpl(v, indent, 0, out);
    out += '\n';
    return out;
}

bool LoadFile(const std::string &path, Value &out, std::string &err) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) { err = "cannot open " + path; return false; }
    std::string text;
    char buf[4096]; size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    std::fclose(f);
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) text.erase(0, 3);
    if (!Parse(text, out, err)) { err = path + ": " + err; return false; }
    return true;
}

bool SaveFile(const std::string &path, const Value &v) {
    std::string tmp = path + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    std::string text = Write(v);
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    if (!ok) { std::remove(tmp.c_str()); return false; }
    std::remove(path.c_str());
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

}  // namespace json
