// Minimal JSON DOM for config files (parse + pretty write, key order preserved so files diff cleanly).
// Supports what configs need: objects, arrays, numbers, strings (\" \\ \n \t \uXXXX ascii), bools, null.
// Pure: no GTA dependency.
#pragma once
#include <string>
#include <vector>
#include <utility>

namespace json {

struct Value {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;

    static Value MakeNumber(double v) { Value x; x.type = Number; x.num = v; return x; }
    static Value MakeString(std::string s) { Value x; x.type = String; x.str = std::move(s); return x; }
    static Value MakeBool(bool v) { Value x; x.type = Bool; x.b = v; return x; }
    static Value MakeObject() { Value x; x.type = Object; return x; }

    const Value *Find(const std::string &key) const;      // object member, or nullptr
    Value *Find(const std::string &key);
    const Value *Path(const std::string &dotted) const;    // "ball.friction"; nullptr if any step is missing
    Value *Path(const std::string &dotted);
    Value &Set(const std::string &key, Value v);           // insert or replace member (keeps position)

    double Num(const std::string &dotted, double def) const;
    std::string Str(const std::string &dotted, const std::string &def) const;
    bool Flag(const std::string &dotted, bool def) const;
};

// Returns false and fills err ("line N: ...") on malformed input.
bool Parse(const std::string &text, Value &out, std::string &err);
std::string Write(const Value &v, int indent = 2);

bool LoadFile(const std::string &path, Value &out, std::string &err);
bool SaveFile(const std::string &path, const Value &v);

}  // namespace json
