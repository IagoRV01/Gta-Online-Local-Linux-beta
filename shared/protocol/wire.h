// Bounds-checked little-endian byte writer/reader for the game protocol.
// The reader never reads past the buffer: any overrun flips ok() to false and returns zeros, so a
// malformed or truncated packet is rejected as a whole by the caller (never partially applied).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace wire {

class Writer {
public:
    void U8(uint8_t v) { buf_.push_back(v); }
    void U16(uint16_t v) { Raw(&v, 2); }
    void U32(uint32_t v) { Raw(&v, 4); }
    void U64(uint64_t v) { Raw(&v, 8); }
    void I16(int16_t v) { Raw(&v, 2); }
    void I32(int32_t v) { Raw(&v, 4); }
    void F32(float v) { Raw(&v, 4); }
    // Strings: u16 length + bytes (no terminator). Longer strings are truncated to maxLen.
    void Str(const std::string &s, size_t maxLen = 1024) {
        size_t n = s.size() < maxLen ? s.size() : maxLen;
        U16((uint16_t)n);
        Raw(s.data(), n);
    }
    void Raw(const void *p, size_t n) { auto *b = (const uint8_t *)p; buf_.insert(buf_.end(), b, b + n); }
    const std::vector<uint8_t> &Bytes() const { return buf_; }
    size_t Size() const { return buf_.size(); }

private:
    std::vector<uint8_t> buf_;
};

class Reader {
public:
    Reader(const uint8_t *data, size_t size) : p_(data), end_(data + size) {}
    uint8_t U8() { uint8_t v = 0; Raw(&v, 1); return v; }
    uint16_t U16() { uint16_t v = 0; Raw(&v, 2); return v; }
    uint32_t U32() { uint32_t v = 0; Raw(&v, 4); return v; }
    uint64_t U64() { uint64_t v = 0; Raw(&v, 8); return v; }
    int16_t I16() { int16_t v = 0; Raw(&v, 2); return v; }
    int32_t I32() { int32_t v = 0; Raw(&v, 4); return v; }
    // Non-finite floats (NaN/inf) mark the packet bad: nothing downstream has to re-check them.
    float F32() { float v = 0; Raw(&v, 4); if (!std::isfinite(v)) { ok_ = false; v = 0; } return v; }
    std::string Str(size_t maxLen = 1024) {
        uint16_t n = U16();
        if (!ok_ || n > maxLen || (size_t)(end_ - p_) < n) { ok_ = false; return {}; }
        std::string s((const char *)p_, n);
        p_ += n;
        return s;
    }
    bool ok() const { return ok_; }
    size_t Remaining() const { return (size_t)(end_ - p_); }
    bool Done() const { return ok_ && p_ == end_; }   // fully consumed, no trailing garbage

private:
    void Raw(void *out, size_t n) {
        if (!ok_ || (size_t)(end_ - p_) < n) { ok_ = false; return; }
        std::memcpy(out, p_, n);
        p_ += n;
    }
    const uint8_t *p_, *end_;
    bool ok_ = true;
};

}  // namespace wire
