#include "original_formats/ann.hpp"

namespace slip {

namespace {
uint32_t u16(const Bytes& b, size_t p) { return p + 2 <= b.size() ? uint32_t(b[p] | (b[p + 1] << 8)) : 0; }
uint32_t u32(const Bytes& b, size_t p) { return u16(b, p) | (u16(b, p + 2) << 16); }
}  // namespace

std::optional<AnnScript> parseAnn(const Bytes& b) {
  if (b.size() < 18) return std::nullopt;
  AnnScript s;
  s.name.assign(b.begin(), b.begin() + 8);
  s.reporter = u32(b, 8);
  size_t p = 16;
  while (p + 2 <= b.size()) {
    AnnCmd c;
    const uint32_t op = u16(b, p);
    if (op > 6) return std::nullopt;
    c.op = AnnCmd::Op(op);
    switch (c.op) {
      case AnnCmd::Op::WaitPiece: c.a = u32(b, p + 2); c.b = u32(b, p + 6); p += 10; break;
      case AnnCmd::Op::Wait: c.ms = int(u16(b, p + 2)); p += 4; break;
      case AnnCmd::Op::Voice: c.tag.assign(b.begin() + long(p) + 2, b.begin() + long(p) + 6); p += 8; break;
      case AnnCmd::Op::End: p += 2; break;
      case AnnCmd::Op::Face: {
        const size_t n = u16(b, p + 2);
        if (p + 4 + n > b.size()) return std::nullopt;
        c.face.assign(b.begin() + long(p) + 4, b.begin() + long(p) + 4 + long(n));
        p += 4 + n;
        break;
      }
      case AnnCmd::Op::Message: c.tag.assign(b.begin() + long(p) + 2, b.begin() + long(p) + 6); p += 6; break;
      case AnnCmd::Op::WaitDist: c.a = u32(b, p + 2); p += 6; break;
    }
    if (p > b.size()) return std::nullopt;
    s.cmds.push_back(std::move(c));
    if (s.cmds.back().op == AnnCmd::Op::End) break;
  }
  return s;
}

}  // namespace slip
