#include "game/profile_store.h"
#include <windows.h>
#include <cstdio>
#include <ctime>
#include "common/json.h"
#include "common/log.h"

namespace profile_store {

static std::string g_dir, g_path;   // g_dir ends with a backslash

void Init(const std::string &dir) {
    g_dir = dir;
    g_path = dir + "character.json";
    CreateDirectoryA(dir.c_str(), nullptr);
}
std::string Path() { return g_path; }
bool Exists() { return GetFileAttributesA(g_path.c_str()) != INVALID_FILE_ATTRIBUTES; }

static bool LoadFrom(const std::string &path, character::Character &out, std::string &err) {
    json::Value v;
    if (!json::LoadFile(path, v, err)) return false;
    return character::FromJson(v, out, err);
}

bool Load(character::Character &out, std::string &err) {
    if (LoadFrom(g_path, out, err)) return true;
    std::string err2;
    if (LoadFrom(g_path + ".bak", out, err2)) {
        LOGW("profile", out.name.c_str(), "main profile unreadable (%s); loaded the .bak copy (rev %u)", err.c_str(), out.revision);
        return true;
    }
    return false;
}

static std::string NowIso() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_s(&tm, &t);
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return b;
}

bool Save(character::Character &c) {
    std::vector<std::string> problems;
    if (!character::Validate(c, problems)) {
        LOGE("profile", c.name.c_str(), "refusing to save an invalid profile (%d problems)", (int)problems.size());
        return false;
    }
    c.updatedAt = NowIso();
    std::string tmp = g_path + ".tmp";
    if (!json::SaveFile(tmp, character::ToJson(c))) {
        LOGE("profile", c.name.c_str(), "cannot write %s", tmp.c_str());
        return false;
    }
    if (Exists()) CopyFileA(g_path.c_str(), (g_path + ".bak").c_str(), FALSE);
    if (!MoveFileExA(tmp.c_str(), g_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        LOGE("profile", c.name.c_str(), "replace failed (error %lu)", GetLastError());
        return false;
    }
    LOGD("profile", c.name.c_str(), "cached (server rev %u) at %.1f %.1f %.1f", c.revision, c.pos.x, c.pos.y, c.pos.z);
    return true;
}

static std::string SyncPath() { return g_dir + "sync.json"; }
static std::string TokenPath() { return g_dir + "token"; }

SyncState LoadSync() {
    SyncState s;
    json::Value v;
    std::string err;
    if (!json::LoadFile(SyncPath(), v, err)) return s;   // no file: a Phase 1 cache, never uploaded
    s.known = true;
    s.serverRevision = (uint32_t)v.Num("serverRevision", 0);
    s.dirty = v.Flag("dirty", true);
    return s;
}

void SaveSync(const SyncState &s) {
    json::Value v = json::Value::MakeObject();
    v.Set("serverRevision", json::Value::MakeNumber(s.serverRevision));
    v.Set("dirty", json::Value::MakeBool(s.dirty));
    std::string tmp = SyncPath() + ".tmp";
    if (json::SaveFile(tmp, v)) MoveFileExA(tmp.c_str(), SyncPath().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

std::string LoadToken() {
    FILE *f = std::fopen(TokenPath().c_str(), "rb");
    if (!f) return "";
    char b[200] = {0};
    size_t n = std::fread(b, 1, sizeof b - 1, f);
    std::fclose(f);
    std::string t(b, n);
    while (!t.empty() && (t.back() == '\n' || t.back() == '\r' || t.back() == ' ')) t.pop_back();
    return t;
}

void SaveToken(const std::string &token) {
    FILE *f = std::fopen(TokenPath().c_str(), "wb");
    if (!f) { LOGE("profile", "-", "cannot write the token file"); return; }
    std::fputs(token.c_str(), f);
    std::fclose(f);
}

void Delete() {
    DeleteFileA(g_path.c_str());
    DeleteFileA((g_path + ".bak").c_str());
    DeleteFileA(SyncPath().c_str());
}

}  // namespace profile_store
