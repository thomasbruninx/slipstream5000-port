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

namespace {
struct Ev {
  uint32_t tick = 0;
  Bytes data;       // full event bytes (status + data) or FF type len data
  bool meta = false;
  uint8_t status = 0, d1 = 0;
};
struct ParsedHmp {
  std::vector<std::vector<Ev>> tracks;
  double seconds = 0;
};

std::optional<ParsedHmp> parseHmp(const Bytes& hmp) {
  static const char kSig[] = "HMIMIDIP013195";
  if (hmp.size() < 0x390 || std::memcmp(hmp.data(), kSig, sizeof(kSig) - 1) != 0) return std::nullopt;
  const uint32_t ntr = rd32(hmp, 0x30);
  if (ntr == 0 || ntr > 64) return std::nullopt;
  ParsedHmp out;
  out.seconds = double(rd32(hmp, 0x3C));
  size_t p = 0x388;
  for (uint32_t t = 0; t < ntr; ++t) {
    const uint32_t len = rd32(hmp, p + 4);
    if (len < 12 || p + len > hmp.size()) return std::nullopt;
    const size_t end = p + len;
    size_t i = p + 12;
    std::vector<Ev> evs;
    uint32_t tick = 0;
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
      Ev e;
      e.tick = tick;
      e.status = st;
      if (st == 0xFF) {
        if (i + 2 >= end) return std::nullopt;
        const uint8_t type = hmp[i + 1], l = hmp[i + 2];
        if (i + 3 + l > end) return std::nullopt;
        e.meta = true;
        e.d1 = type;
        e.data.assign(hmp.begin() + long(i), hmp.begin() + long(i) + 3 + l);
        i += 3 + l;
        if (type == 0x2F) ended = true;
      } else if (st >= 0x80 && st < 0xF0) {
        const int n = ((st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0) ? 1 : 2;
        if (i + 1 + size_t(n) > end) return std::nullopt;
        e.data = {st, hmp[i + 1]};  // raw bytes: marker controllers carry values >= 128, masked when written
        e.d1 = hmp[i + 1];
        if (n == 2) e.data.push_back(hmp[i + 2]);
        i += 1 + size_t(n);
      } else {
        return std::nullopt;
      }
      evs.push_back(std::move(e));
    }
    out.tracks.push_back(std::move(evs));
    p = end;
  }
  return out;
}

bool isMarker(const Ev& e) { return !e.meta && (e.status & 0xF0) == 0xB0 && e.d1 >= 108 && e.d1 <= 119; }
bool isNoteEv(const Ev& e) { return !e.meta && ((e.status & 0xF0) == 0x80 || (e.status & 0xF0) == 0x90); }

// Builds an SMF from the events with tick in [from, to); events before `from` that set channel state are replayed at tick 0.
Bytes buildSmf(const ParsedHmp& hp, uint32_t from, uint32_t to, bool carryState) {
  Bytes out;
  const char hdr[] = {'M', 'T', 'h', 'd'};
  out.insert(out.end(), hdr, hdr + 4);
  put32be(out, 6);
  put16be(out, 1);
  put16be(out, uint16_t(hp.tracks.size()));
  put16be(out, 120);
  for (size_t t = 0; t < hp.tracks.size(); ++t) {
    Bytes trk;
    if (t == 0) trk.insert(trk.end(), {0x00, 0xFF, 0x51, 0x03, 0x0F, 0x42, 0x40});  // 120 ticks per second
    uint32_t lastOut = 0;
    auto emit = [&](uint32_t tick, const Ev& e) {
      putVlq(trk, tick - lastOut);
      lastOut = tick;
      if (e.meta) {
        trk.insert(trk.end(), e.data.begin(), e.data.end());
      } else {
        trk.push_back(e.data[0]);
        trk.push_back(uint8_t(e.data[1] & 0x7f));
        if (e.data.size() > 2) trk.push_back(uint8_t(e.data[2] & 0x7f));
      }
    };
    if (carryState && from > 0) {  // last value of every program / controller / pitch bend before `from`
      std::vector<const Ev*> state;
      for (const Ev& e : hp.tracks[t]) {
        if (e.tick >= from) break;
        if (e.meta || isMarker(e) || isNoteEv(e)) continue;
        const uint8_t hi = e.status & 0xF0;
        if (hi == 0xB0 && e.d1 == 123) continue;  // all notes off
        if (hi != 0xB0 && hi != 0xC0 && hi != 0xE0) continue;
        for (auto& s : state)
          if (s && s->status == e.status && (hi != 0xB0 || s->d1 == e.d1)) s = nullptr;
        state.push_back(&e);
      }
      for (const Ev* s : state) if (s) emit(0, *s);
    }
    for (const Ev& e : hp.tracks[t]) {
      if (e.tick < from || e.tick >= to) continue;
      if (e.meta) { if (e.d1 == 0x2F) continue; }
      else if (isMarker(e)) continue;
      emit(e.tick - from, e);
    }
    putVlq(trk, (to - from) - lastOut);
    trk.insert(trk.end(), {0xFF, 0x2F, 0x00});
    const char th[] = {'M', 'T', 'r', 'k'};
    out.insert(out.end(), th, th + 4);
    put32be(out, uint32_t(trk.size()));
    out.insert(out.end(), trk.begin(), trk.end());
  }
  return out;
}

uint32_t songEnd(const ParsedHmp& hp) {
  uint32_t m = 0;
  for (const auto& tr : hp.tracks) for (const Ev& e : tr) m = std::max(m, e.tick);
  return m;
}
}  // namespace

std::optional<MusicSegments> hmpToSegments(const Bytes& hmp) {
  auto hp = parseHmp(hmp);
  if (!hp) return std::nullopt;
  const uint32_t end = songEnd(*hp);
  // first loop end (CC 111) and the latest start (CC 109) with the same id before it
  bool found = false;
  uint32_t endTick = 0, endId = 0;
  for (const auto& tr : hp->tracks)
    for (const Ev& e : tr)
      if (!e.meta && (e.status & 0xF0) == 0xB0 && e.d1 == 111 && (!found || e.tick < endTick)) {
        endTick = e.tick; endId = e.data.size() > 2 ? e.data[2] : 0; found = true;
      }
  MusicSegments s;
  if (found) {
    bool haveStart = false;
    uint32_t startTick = 0;
    for (const auto& tr : hp->tracks)
      for (const Ev& e : tr)
        if (!e.meta && (e.status & 0xF0) == 0xB0 && e.d1 == 109 && e.tick <= endTick && e.data.size() > 2 && e.data[2] == endId && (!haveStart || e.tick > startTick)) {
          startTick = e.tick; haveStart = true;
        }
    if (haveStart && endTick > startTick + 60) {
      s.intro = buildSmf(*hp, 0, endTick + 1, false);
      s.loop = buildSmf(*hp, startTick, endTick + 1, true);
      s.loopStartTick = startTick;
      s.loopEndTick = endTick;
      return s;
    }
  }
  s.intro = buildSmf(*hp, 0, end + 1, false);
  return s;
}

std::optional<Bytes> hmpToSmf(const Bytes& hmp, HmpInfo* info) {
  auto hp = parseHmp(hmp);
  if (!hp) return std::nullopt;
  if (info) {
    info->tracks = int(hp->tracks.size());
    info->seconds = hp->seconds;
    info->lengthTicks = songEnd(*hp);
  }
  return buildSmf(*hp, 0, songEnd(*hp) + 1, false);
}

}  // namespace slip
