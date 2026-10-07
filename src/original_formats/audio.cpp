#include "original_formats/audio.hpp"

#include <algorithm>
#include <cstring>

namespace slip {

std::optional<SoundSample> parseSample(const std::string& name, const Bytes& smp) {
  if (smp.empty()) return std::nullopt;
  SoundSample s;
  s.name = name;
  s.pcm.resize(smp.size());
  for (size_t i = 0; i < smp.size(); ++i) s.pcm[i] = (float(smp[i]) - 128.0f) / 128.0f;
  return s;
}

namespace {
uint32_t rd32(const Bytes& b, size_t o) { return o + 4 <= b.size() ? uint32_t(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (uint32_t(b[o + 3]) << 24)) : 0; }

void putVlq(Bytes& out, uint32_t v) {  // Standard MIDI File variable length quantity
  uint8_t buf[5];
  int n = 0;
  buf[n++] = v & 0x7f;
  while (v >>= 7) buf[n++] = uint8_t((v & 0x7f) | 0x80);
  while (n--) out.push_back(buf[n]);
}
void put32be(Bytes& out, uint32_t v) { for (int s = 24; s >= 0; s -= 8) out.push_back(uint8_t(v >> s)); }
void put16be(Bytes& out, uint16_t v) { out.push_back(uint8_t(v >> 8)); out.push_back(uint8_t(v)); }
}  // namespace

std::optional<Bytes> hmpToSmf(const Bytes& hmp, HmpInfo* info) {
  static const char kSig[] = "HMIMIDIP013195";
  if (hmp.size() < 0x390 || std::memcmp(hmp.data(), kSig, sizeof(kSig) - 1) != 0) return std::nullopt;
  const uint32_t ntr = rd32(hmp, 0x30);
  if (ntr == 0 || ntr > 64) return std::nullopt;
  Bytes out;
  const char hdr[] = {'M', 'T', 'h', 'd'};
  out.insert(out.end(), hdr, hdr + 4);
  put32be(out, 6);
  put16be(out, 1);
  put16be(out, uint16_t(ntr));
  put16be(out, 120);
  double maxTicks = 0;
  size_t p = 0x388;
  for (uint32_t t = 0; t < ntr; ++t) {
    const uint32_t len = rd32(hmp, p + 4);
    if (len < 12 || p + len > hmp.size()) return std::nullopt;
    const size_t end = p + len;
    size_t i = p + 12;
    Bytes trk;
    if (t == 0) {  // tempo: 1,000,000 us per quarter note with division 120 = 120 ticks per second
      trk.insert(trk.end(), {0x00, 0xFF, 0x51, 0x03, 0x0F, 0x42, 0x40});
    }
    uint32_t tick = 0, lastOut = 0;
    bool ended = false;
    while (i < end && !ended) {
      uint32_t delta = 0;
      int shift = 0;
      for (;;) {  // HMI delta: continuation bytes have bit 7 clear, the last one has it set
        if (i >= end) return std::nullopt;
        const uint8_t b = hmp[i++];
        delta |= uint32_t(b & 0x7f) << shift;
        shift += 7;
        if (b & 0x80) break;
        if (shift > 28) return std::nullopt;
      }
      tick += delta;
      if (i >= end) return std::nullopt;
      const uint8_t st = hmp[i];
      if (st == 0xFF) {
        if (i + 2 >= end + 0) return std::nullopt;
        const uint8_t type = hmp[i + 1], l = hmp[i + 2];
        if (i + 3 + l > end) return std::nullopt;
        putVlq(trk, tick - lastOut);
        lastOut = tick;
        trk.push_back(0xFF);
        trk.push_back(type);
        putVlq(trk, l);
        trk.insert(trk.end(), hmp.begin() + long(i) + 3, hmp.begin() + long(i) + 3 + l);
        i += 3 + l;
        if (type == 0x2F) ended = true;
      } else if (st >= 0x80 && st < 0xF0) {
        const int n = ((st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0) ? 1 : 2;
        if (i + 1 + size_t(n) > end) return std::nullopt;
        const uint8_t d1 = hmp[i + 1], d2 = n == 2 ? hmp[i + 2] : 0;
        i += 1 + size_t(n);
        if ((st & 0xF0) == 0xB0 && d1 >= 108 && d1 <= 119) continue;  // HMI loop / branch / beat controllers
        putVlq(trk, tick - lastOut);
        lastOut = tick;
        trk.push_back(st);
        trk.push_back(uint8_t(d1 & 0x7f));
        if (n == 2) trk.push_back(uint8_t(d2 & 0x7f));
      } else {
        return std::nullopt;  // running status / sysex are not used by the game's files
      }
    }
    if (!ended) { putVlq(trk, 0); trk.insert(trk.end(), {0xFF, 0x2F, 0x00}); }
    maxTicks = std::max(maxTicks, double(tick));
    const char th[] = {'M', 'T', 'r', 'k'};
    out.insert(out.end(), th, th + 4);
    put32be(out, uint32_t(trk.size()));
    out.insert(out.end(), trk.begin(), trk.end());
    p = end;
  }
  if (info) {
    info->tracks = int(ntr);
    info->seconds = double(rd32(hmp, 0x3C));
    info->lengthTicks = maxTicks;
  }
  return out;
}

}  // namespace slip
