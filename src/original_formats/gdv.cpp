#include "original_formats/gdv.hpp"

#include <algorithm>
#include <cstring>

namespace slip {

namespace {
uint32_t rl32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
uint16_t rl16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
struct FixedSize { int id, w, h; };
const FixedSize kSizes[] = {{0, 320, 200}, {1, 640, 200}, {2, 320, 167}, {3, 320, 180}, {4, 320, 400}, {5, 320, 170}, {6, 160, 85}, {7, 160, 83}, {8, 160, 90},
                            {9, 280, 128}, {10, 320, 240}, {11, 320, 201}, {16, 640, 400}, {17, 640, 200}, {18, 640, 180}, {19, 640, 167}, {20, 640, 170}, {21, 320, 240}};
uint32_t pal6(unsigned r, unsigned g, unsigned b) { return 0xFF000000u | (r << 18) | (g << 10) | (b << 2); }
}  // namespace

bool GdvDecoder::open(std::vector<uint8_t> data, std::string* error) {
  data_ = std::move(data);
  method2Prefix_ = false;
  unsupported_ = 0;
  std::fill(std::begin(pal_), std::end(pal_), 0xFF000000u);
  size_t p = 0;
  if (data_.size() > 0x40 && std::memcmp(data_.data(), "GDV Format", 10) == 0) p = 0x34;  // LOGO_S.GDV starts with a text banner (0x30 bytes) and a zero dword
  if (data_.size() < p + 0x18 || rl32(&data_[p]) != 0x29111994) { if (error) *error = "not a GDV file"; return false; }
  const uint8_t* h = &data_[p];
  int sizeId = rl16(h + 4);
  if (sizeId == 1 && rl16(h + 20) == 0) { sizeId = 0; method2Prefix_ = true; }  // LOGO_S.GDV: size id 1 but its frames cover 320x200 (see decompress, method 2)
  frames_ = rl16(h + 6);
  fps_ = rl16(h + 8);
  const unsigned snd = rl16(h + 10);
  if (fps_ == 0) { if (error) *error = "bad frame rate"; return false; }
  audio_ = snd & 1;
  channels_ = 1 + ((snd & 2) ? 1 : 0);
  audio16_ = snd & 4;
  dpcm_ = snd & 8;
  audioRate_ = audio_ ? rl16(h + 12) : 0;
  const unsigned depth = rl16(h + 14);
  w_ = rl16(h + 20);
  h_ = rl16(h + 22);
  p += 24;
  if (w_ == 0 || h_ == 0) {
    for (const FixedSize& s : kSizes) if (s.id == sizeId) { w_ = s.w; h_ = s.h; }
    if (w_ == 0) { if (error) *error = "unknown frame size"; return false; }
  }
  if (depth & 1) {
    if (data_.size() < p + 768) { if (error) *error = "truncated header"; return false; }
    for (int i = 0; i < 256; ++i) pal_[i] = pal6(data_[p + size_t(i) * 3], data_[p + size_t(i) * 3 + 1], data_[p + size_t(i) * 3 + 2]);
    p += 768;
  }
  if (audio_) audioNominal_ = audioSize_ = size_t(audioRate_ / fps_) * size_t(channels_) * (audio16_ ? 2 : 1) / (dpcm_ ? 2 : 1);
  pos_ = p;
  frame_.assign(size_t(w_) * size_t(h_) + kPreamble, 0);
  for (int i = 0; i < 2; ++i) for (int j = 0; j < 256; ++j) for (int k = 0; k < 8; ++k) frame_[size_t(i) * 2048 + size_t(j) * 8 + size_t(k)] = uint8_t(j);
  index_ = 0;
  return true;
}

namespace {
struct Bits8 { uint8_t queue = 0, fill = 0; };
struct Bits32 { uint32_t queue = 0; int fill = 0; };
}  // namespace

bool GdvDecoder::decompress(int method, unsigned skip, const uint8_t* src, size_t n) {
  uint8_t* f = frame_.data();
  const size_t cap = frame_.size();
  size_t out = kPreamble + skip;
  size_t in = 0;
  auto byte = [&]() -> int { return in < n ? src[in++] : 0; };
  auto le16 = [&]() -> int { const int a = byte(); return a | (byte() << 8); };
  written_ = 0;
  auto put = [&](int v) { if (out < cap) f[out] = uint8_t(v); ++out; ++written_; };
  auto skipOut = [&](size_t len) { out += len; };
  auto copy = [&](int offset, size_t len) {  // LZ copy inside the frame buffer (overlap allowed, offsets relative to the write position)
    if (offset == -1) { const int c = out >= 1 && out - 1 < cap ? f[out - 1] : 0; for (size_t i = 0; i < len; ++i) put(c); return; }
    long start = offset < 0 ? long(out) + offset : long(out) + offset;
    for (size_t i = 0; i < len; ++i, ++start) put(start >= 0 && size_t(start) < cap ? f[start] : 0);
  };
  Bits8 b8;
  auto bits2 = [&]() { if (b8.fill == 0) { b8.queue |= uint8_t(byte()); b8.fill = 8; } const int r = b8.queue >> 6; b8.queue = uint8_t(b8.queue << 2); b8.fill -= 2; return r; };
  Bits32 b32;
  auto fill32 = [&]() { const int a = le16(); b32.queue = uint32_t(a) | (uint32_t(le16()) << 16); b32.fill = 32; };
  auto bitsN = [&](int nb) {
    const int r = int(b32.queue & ((1u << nb) - 1));
    b32.queue >>= nb; b32.fill -= nb;
    if (b32.fill <= 16) { b32.queue |= uint32_t(le16()) << b32.fill; b32.fill += 16; }
    return r;
  };
  if (method == 2) {
    for (int c = 0; c < 256; ++c) for (int i = 0; i < 16; ++i) f[c * 16 + i] = uint8_t(c);
    out = kPreamble;
    if (method2Prefix_) in = 2;  // LOGO_S.GDV: a u16 precedes the tag stream (the stream then covers exactly 320x200 pixels; INFERRED, FFmpeg cannot decode this file)
    while (out < cap && in < n) {
      const int tag = bits2();
      if (tag == 0) put(byte());
      else if (tag == 1) { const int b = byte(); const int len = (b & 15) + 3, top = (b >> 4) & 15; copy((byte() << 4) + top - 4096, size_t(len)); }
      else if (tag == 2) skipOut(size_t(byte()) + 2);
      else break;
    }
    return true;
  }
  if (method == 5) {
    while (out < cap && in < n) {
      const int tag = bits2();
      if (in >= n) return false;
      if (tag == 0) put(byte());
      else if (tag == 1) { const int b = byte(); const int len = (b & 15) + 3, top = b >> 4; copy((byte() << 4) + top - 4096, size_t(len)); }
      else if (tag == 2) {
        const int b = byte();
        if (b == 0) return true;
        const int len = b != 0xFF ? b : le16();
        skipOut(size_t(len) + 1);
      } else { const int b = byte(); copy(-(b >> 2) - 1, size_t(b & 3) + 2); }
    }
    return true;
  }
  const bool use8 = method == 8;  // methods 6 and 8
  fill32();
  while (out < cap && in < n) {
    const int tag = bitsN(2);
    if (tag == 0) {
      if (bitsN(1) == 0) put(byte());
      else {
        size_t len = 2;
        int lbits = 0;
        for (;;) {
          ++lbits;
          const int val = bitsN(lbits);
          len += size_t(val);
          if (val != (1 << lbits) - 1) break;
          if (lbits >= 16) return false;
        }
        for (size_t i = 0; i < len; ++i) put(byte());
      }
    } else if (tag == 1) {
      size_t len;
      if (bitsN(1) == 0) len = size_t(bitsN(4)) + 2;
      else { const int bb = byte(); if ((bb & 0x80) == 0) len = size_t(bb) + 18; else { const int top = (bb & 0x7F) << 8; len = size_t(top + byte() + 146); } }
      skipOut(len);
    } else if (tag == 2) {
      const int subtag = bitsN(2);
      if (subtag != 3) {
        const int top = bitsN(4) << 8;
        const int offs = top + byte();
        if (subtag != 0 || offs <= 0xF80) copy(offs - 4096, size_t(subtag) + 3);
        else {
          if (offs == 0xFFF) return true;
          const int realOff = ((offs >> 4) & 7) + 1;
          const int len = ((offs & 15) + 2) * 2;
          const int c1 = out >= size_t(realOff) ? f[out - size_t(realOff)] : 0, c2 = out >= size_t(realOff) ? f[out - size_t(realOff) + 1] : 0;
          for (int i = 0; i < len / 2; ++i) { put(c1); put(c2); }
        }
      } else { const int b = byte(); copy(-((b & 0x7F) + 1), (b & 0x80) == 0 ? 2 : 3); }
    } else {
      int len, off;
      if (use8) {
        const int b = byte();
        if ((b & 0xC0) == 0xC0) { len = (b & 0x3F) + 8; const int q = bitsN(4); off = (q << 8) + byte() + 1; }
        else {
          int ofs1;
          if ((b & 0x80) == 0) { len = (b >> 4) + 6; ofs1 = b & 15; } else { len = (b & 0x3F) + 14; ofs1 = bitsN(4); }
          off = (ofs1 << 8) + byte() - 4096;
        }
      } else {
        const int b = byte();
        len = (b >> 4) == 0xF ? byte() + 21 : (b >> 4) + 6;
        off = ((b & 15) << 8) + byte() - 4096;
      }
      copy(off, size_t(len));
    }
  }
  return true;
}

bool GdvDecoder::next() {
  if (index_ >= frames_ || pos_ >= data_.size()) return false;
  audioChunk_.clear();
  if (audio_ && audioSize_) {
    if (pos_ + audioSize_ > data_.size()) return false;
    // LOGO_S.GDV stores 2452 bytes per frame where rate / fps gives 2450: when the video marker is not where expected, look for it just after
    if (pos_ + audioSize_ + 2 <= data_.size() && rl16(&data_[pos_ + audioSize_]) != 0x1305)
      for (size_t extra = 1; extra <= 64 && pos_ + audioSize_ + extra + 4 <= data_.size(); ++extra)
        if (rl16(&data_[pos_ + audioSize_ + extra]) == 0x1305) { audioSize_ += extra; break; }
    audioChunk_.assign(data_.begin() + long(pos_), data_.begin() + long(pos_ + audioSize_));
    pos_ += audioSize_;
    // the extra bytes are the leading ones (a copy of the end of the video chunk: "ff 05"), not samples: keeping them put a click into every frame
    if (audioSize_ > audioNominal_) audioChunk_.erase(audioChunk_.begin(), audioChunk_.begin() + long(audioSize_ - audioNominal_));
  }
  if (pos_ + 4 > data_.size() || rl16(&data_[pos_]) != 0x1305) return false;
  const size_t size = rl16(&data_[pos_ + 2]) + 4u;
  pos_ += 4;
  if (pos_ + size > data_.size()) return false;
  const uint8_t* v = &data_[pos_];
  pos_ += size;
  const uint32_t flags = rl32(v);
  const int method = int(flags & 15);
  if (method == 4 || method == 7 || method > 8) return false;
  if (flags & 0x30) ++unsupported_;  // half-size coded frames (rescale step) are not ported
  if (method < 2) {
    if (size < 4 + 768) return false;
    if (method == 1) std::fill(frame_.begin() + long(kPreamble), frame_.end(), 0);
    for (int i = 0; i < 256; ++i) pal_[i] = pal6(v[4 + i * 3], v[5 + i * 3], v[6 + i * 3]);
  } else if (method != 3) {
    if (!decompress(method, flags >> 8, v + 4, size - 4)) return false;
  }
  ++index_;
  return true;
}

void GdvDecoder::appendAudio(std::vector<float>* pcm) const {
  if (!audio_) return;
  if (audio16_ && !dpcm_) { for (size_t i = 0; i + 1 < audioChunk_.size(); i += 2) pcm->push_back(float(int16_t(audioChunk_[i] | (audioChunk_[i + 1] << 8))) / 32768.0f); return; }
  for (uint8_t b : audioChunk_) pcm->push_back((float(b) - 128.0f) / 128.0f);  // 8 bit unsigned (the DPCM variant is not used by the game's movies)
}

}  // namespace slip
