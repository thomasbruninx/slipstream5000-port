#include "original_formats/formats.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace slip {

namespace {
struct Rd {
  const uint8_t* p;
  size_t n;
  bool ok = true;
  bool in(size_t o, size_t len) {
    if (o > n || len > n - o) { ok = false; return false; }
    return true;
  }
  uint16_t u16(size_t o) { return in(o, 2) ? uint16_t(p[o] | (p[o + 1] << 8)) : 0; }
  int16_t s16(size_t o) { return int16_t(u16(o)); }
  uint32_t u32(size_t o) { return in(o, 4) ? (p[o] | (p[o + 1] << 8) | (p[o + 2] << 16) | (uint32_t(p[o + 3]) << 24)) : 0; }
  int32_t s32(size_t o) { return int32_t(u32(o)); }
  uint8_t u8(size_t o) { return in(o, 1) ? p[o] : 0; }
};

std::string trimmed(const uint8_t* p, size_t n) {
  size_t len = 0;
  while (len < n && p[len]) ++len;
  while (len && (p[len - 1] == ' ')) --len;
  return std::string(reinterpret_cast<const char*>(p), len);
}
std::string lower(std::string s) {
  for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::string fourcc(uint32_t v) {  // tags are stored as little-endian dwords of a multi-char constant
  std::string s;
  for (int i = 3; i >= 0; --i) s.push_back(char((v >> (8 * i)) & 0xff));
  return s;
}
}  // namespace

std::optional<Palette> parsePalette(const uint8_t* p, size_t n) {
  Rd r{p, n};
  Palette pal;
  pal.first = r.u16(0);
  pal.count = r.u16(2);
  if (!r.ok || pal.first + pal.count > 256 || n < size_t(4 + 3 * pal.count)) return std::nullopt;
  for (int i = 0; i < pal.count; ++i) {
    auto c6 = [&](int k) { return uint32_t(p[4 + 3 * i + k] & 63); };
    auto c8 = [](uint32_t v) { return (v << 2) | (v >> 4); };  // 6-bit VGA -> 8-bit
    pal.rgba[size_t(pal.first + i)] = (c8(c6(0)) << 16) | (c8(c6(1)) << 8) | c8(c6(2));
  }
  return pal;
}

void fillDefaultTail(Palette& pal) {
  // Placeholder colours for indices the .PAL does not define (248..255). SPECULATIVE.
  for (int i = pal.first + pal.count; i < 256; ++i) {
    uint32_t v = 0x40 + uint32_t(i - 248) * 0x20;
    pal.rgba[size_t(i)] = (0 << 16) | (v << 8) | 0xc0;
  }
  if (pal.count <= 252) pal.rgba[252] = 0x0020d040;  // GreenNav ship light (colour assumed)
}

std::optional<Font> parseFont(const Bytes& b) {
  if (b.size() < 16 || b[0] != 'F' || b[1] != 'O' || b[2] != 'N' || b[3] != 'T') return std::nullopt;
  Rd r{b.data(), b.size()};
  Font f;
  f.cellW = r.u16(4);
  f.height = r.u16(6);
  f.first = b[8];
  f.last = b[9];
  const size_t table = r.u16(10);
  if (f.cellW <= 0 || f.height <= 0 || f.last < f.first) return std::nullopt;
  for (int c = f.first; c <= f.last; ++c) {
    const size_t o = table + 4 * size_t(c - f.first);
    Font::Glyph g;
    g.offset = int(r.u16(o));
    g.advance = int(r.u16(o + 2));
    if (!r.ok || size_t(g.offset) + size_t(f.cellW) * size_t(f.height) > b.size()) return std::nullopt;
    f.glyphs.push_back(g);
  }
  f.data = b;
  return f;
}

std::vector<std::pair<std::string, std::string>> parseStringTable(const Bytes& b) {
  std::vector<std::pair<std::string, std::string>> out;
  size_t p = 0;
  while (p + 6 <= b.size()) {
    if (b[p] == 0xFF && b[p + 1] == 0xFF) break;
    const std::string tag(reinterpret_cast<const char*>(b.data() + p), 4);
    const size_t len = size_t(b[p + 4]) | (size_t(b[p + 5]) << 8);
    p += 6;
    if (p + len > b.size()) break;
    std::string text(reinterpret_cast<const char*>(b.data() + p), len);
    while (!text.empty() && text.back() == '\0') text.pop_back();
    out.emplace_back(tag, text);
    p += len;
  }
  return out;
}

std::optional<Sprite> parseSprite(const Bytes& b) {
  Rd r{b.data(), b.size()};
  Sprite s;
  s.w = r.u16(0);
  s.h = r.u16(2);
  s.hdr4 = r.u16(4);
  s.hdr6 = r.u16(6);
  s.hdr8 = r.u16(8);
  size_t pix = size_t(s.w) * size_t(s.h);
  if (!r.ok || s.w <= 0 || s.h <= 0 || 16 + pix > b.size()) return std::nullopt;
  s.pixels.assign(b.begin() + 16, b.begin() + 16 + long(pix));
  if (b.size() > 16 + pix) s.palette = parsePalette(b.data() + 16 + pix, b.size() - 16 - pix);
  return s;
}

int MaterialSet::find(const std::string& name) const {
  std::string want = lower(name);
  while (!want.empty() && want.back() == ' ') want.pop_back();
  for (size_t i = 0; i < mats.size(); ++i)
    if (lower(mats[i].name) == want) return int(i);
  return -1;
}

std::optional<MaterialSet> parseMaterials(const Bytes& b) {
  Rd r{b.data(), b.size()};
  uint16_t count = r.u16(0), ver = r.u16(2);
  if (!r.ok || ver != 1 || b.size() != size_t(4 + 46 * count)) return std::nullopt;
  MaterialSet ms;
  for (int i = 0; i < count; ++i) {
    const uint8_t* e = b.data() + 4 + 46 * i;
    Material m;
    m.name = trimmed(e, 16);
    m.palStart = e[0x10];
    m.palEnd = e[0x11];
    m.pattern = trimmed(e + 0x22, 12);
    if (!m.pattern.empty() && (uint8_t(m.pattern[0]) == 0xff || !std::isprint(uint8_t(m.pattern[0])))) m.pattern.clear();
    std::memcpy(m.raw, e, 46);
    ms.mats.push_back(std::move(m));
  }
  return ms;
}

std::optional<Shape> parseShape(const Bytes& b) {
  Rd r{b.data(), b.size()};
  if (r.u16(0) != 12 || r.u32(8) != b.size()) return std::nullopt;
  Shape s;
  s.vertexShift = r.u16(2);
  s.flags = r.u16(4);
  uint32_t sortOff = r.u32(0xc), ptsOff = r.u32(0x10), polyOff = r.u32(0x14), matOff = r.u32(0x18);
  s.hasSortTree = sortOff != 0;
  s.radius = r.s32(0x1c);
  for (int i = 0; i < 6; ++i) s.bbox[i] = r.s32(0x20 + 4 * size_t(i));
  uint16_t nv = r.u16(ptsOff);
  for (int i = 0; i < nv; ++i) {
    size_t o = ptsOff + 2 + 6 * size_t(i);
    s.verts.push_back({int32_t(r.s16(o)) << s.vertexShift, int32_t(r.s16(o + 2)) << s.vertexShift,
                       int32_t(r.s16(o + 4)) << s.vertexShift});
  }
  uint16_t np = r.u16(polyOff);
  size_t q = polyOff + 2;
  for (int i = 0; i < np && r.ok; ++i) {
    uint16_t info = r.u16(q);
    size_t N = info & 0x3fff;
    Polygon p;
    p.nx = r.s16(q + 2);
    p.ny = r.s16(q + 4);
    p.nz = r.s16(q + 6);
    p.material = r.u16(q + 8);
    p.flags = r.u16(q + 10);
    size_t o = q + 12;
    for (size_t k = 0; k < N; ++k) p.index.push_back(r.u16(o + 2 * k));
    o += 2 * N;
    if (info & 0x4000) {  // CONFIRMED order: indices, per-vertex normals, then UVs
      for (size_t k = 0; k < N; ++k) p.vnormals.push_back({r.s16(o + 6 * k), r.s16(o + 6 * k + 2), r.s16(o + 6 * k + 4)});
      o += 6 * N;
    }
    if (info & 0x8000) {
      for (size_t k = 0; k < N; ++k) p.uv.push_back({r.u16(o + 4 * k), r.u16(o + 4 * k + 2)});
      o += 4 * N;
    }
    q = o;
    s.polys.push_back(std::move(p));
  }
  uint16_t nm = r.u16(matOff);
  for (int i = 0; i < nm; ++i) {
    size_t o = matOff + 2 + 18 * size_t(i);
    if (!r.in(o, 18)) break;
    s.materials.push_back({trimmed(b.data() + o, 16), r.u16(o + 16)});
  }
  if (!r.ok) return std::nullopt;
  return s;
}

std::optional<Art> parseArt(const Bytes& b) {
  Rd r{b.data(), b.size()};
  if (r.u32(0) != 18) return std::nullopt;
  Art a;
  a.minExt = {r.s32(4), r.s32(8), r.s32(12)};
  a.maxExt = {r.s32(16), r.s32(20), r.s32(24)};
  uint32_t root = r.u32(0x20);
  struct Item { uint32_t off; int parent; };
  std::vector<Item> stack{{root, -1}};
  std::vector<uint32_t> seen;
  while (!stack.empty() && r.ok && seen.size() < 64) {
    Item it = stack.back();
    stack.pop_back();
    if (it.off == 0 || it.off + 0x1d0 > b.size()) continue;
    if (std::find(seen.begin(), seen.end(), it.off) != seen.end()) continue;
    seen.push_back(it.off);
    ArtNode n;
    n.parent = it.parent;
    n.tag = fourcc(r.u32(it.off));
    n.offset = {r.s32(it.off + 0x10), r.s32(it.off + 0x14), r.s32(it.off + 0x18)};
    for (int i = 0; i < 8; ++i) {
      std::string s = trimmed(b.data() + it.off + 0x1c + 14 * size_t(i), 12);
      if (!s.empty()) n.shapes.push_back(s);
      std::string s2 = trimmed(b.data() + it.off + 0x8c + 14 * size_t(i), 12);
      if (!s2.empty()) n.shadowShapes.push_back(s2);
    }
    uint32_t rc = r.u32(it.off + 0x1cc);
    for (uint32_t i = 0; i < rc && i < 16; ++i) {
      size_t o = it.off + 0x1d0 + 16 * size_t(i);
      n.refPoints.push_back({fourcc(r.u32(o)), {r.s32(o + 4), r.s32(o + 8), r.s32(o + 12)}});
    }
    int self = int(a.nodes.size());
    a.nodes.push_back(std::move(n));
    // parent for children: child pointer -> parent = self; siblings share this node's parent
    uint32_t child = r.u32(it.off + 4), sib = r.u32(it.off + 8);
    if (sib) stack.push_back({sib, it.parent});
    if (child) stack.push_back({child, self});
  }
  if (!r.ok || a.nodes.empty()) return std::nullopt;
  return a;
}

std::optional<MathsTables> parseMaths(const Bytes& b) {
  Rd r{b.data(), b.size()};
  uint32_t o0 = r.u16(0), o2 = r.u16(2), o4 = r.u16(4);
  MathsTables t;
  for (int i = 0; i < 8193; ++i) {
    t.sin.push_back(r.u16(o0 + 2 * size_t(i)));
    t.asin.push_back(r.u16(o2 + 2 * size_t(i)));
  }
  for (size_t o = o4; o + 2 <= b.size(); o += 2) t.atan.push_back(r.u16(o));
  if (!r.ok || t.atan.size() != 4097) return std::nullopt;
  return t;
}

}  // namespace slip
