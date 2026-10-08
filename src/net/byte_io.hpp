// Little-endian byte writer / reader for the network protocol (explicit field serialization, no struct dumps).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace slip::net {

using Bytes = std::vector<uint8_t>;

class Writer {
 public:
  Bytes buf;
  void u8(uint32_t v) { buf.push_back(uint8_t(v)); }
  void u16(uint32_t v) { u8(v); u8(v >> 8); }
  void u32(uint32_t v) { u16(v); u16(v >> 16); }
  void i16(int v) { u16(uint16_t(int16_t(v))); }
  void i32(int32_t v) { u32(uint32_t(v)); }
  void str(const std::string& s) { const size_t n = s.size() > 255 ? 255 : s.size(); u8(uint32_t(n)); buf.insert(buf.end(), s.begin(), s.begin() + long(n)); }
};

class Reader {
 public:
  Reader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
  explicit Reader(const Bytes& b) : p_(b.data()), n_(b.size()) {}
  bool ok() const { return ok_; }
  size_t remaining() const { return n_ - pos_; }
  uint32_t u8() { if (pos_ + 1 > n_) { ok_ = false; return 0; } return p_[pos_++]; }
  uint32_t u16() { const uint32_t a = u8(); return a | (u8() << 8); }
  uint32_t u32() { const uint32_t a = u16(); return a | (u16() << 16); }
  int i16() { return int16_t(u16()); }
  int32_t i32() { return int32_t(u32()); }
  std::string str() {
    const size_t n = u8();
    if (pos_ + n > n_) { ok_ = false; return {}; }
    std::string s(reinterpret_cast<const char*>(p_ + pos_), n);
    pos_ += n;
    return s;
  }

 private:
  const uint8_t* p_;
  size_t n_, pos_ = 0;
  bool ok_ = true;
};

}  // namespace slip::net
