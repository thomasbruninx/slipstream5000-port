#include "game/ship_ai.hpp"

#include <algorithm>
#include <cmath>

namespace slip {

AiTables loadAiTables(const GameData& data) {
  AiTables t;
  auto exe = data.read("SLIPSTRM.EXE");
  constexpr size_t kBase = 0x4D854 - 0x10000;  // file offset = VA + kBase
  if (!exe || exe->size() < kBase + 0x502d0) return t;
  auto rd = [&](size_t va) { const size_t o = kBase + va; return int32_t((*exe)[o] | ((*exe)[o + 1] << 8) | ((*exe)[o + 2] << 16) | (uint32_t((*exe)[o + 3]) << 24)); };
  if (rd(0x5027a + 4) != 214500 || rd(0x502a2 + 4) != 178750) return t;  // sanity: track 1 entries
  for (int i = 1; i <= 10; ++i) { t.minSpeed[i] = rd(0x5027a + 4 * size_t(i)); t.speedRange[i] = rd(0x502a2 + 4 * size_t(i)); }
  t.fromExecutable = true;
  return t;
}

namespace {
// 0x21F87: the game's cheap length estimate, max + (mid + min) / 4
double est(const double* v) {
  double a[3] = {std::fabs(v[0]), std::fabs(v[1]), std::fabs(v[2])};
  std::sort(a, a + 3);
  return a[2] + (a[1] + a[0]) * 0.25;
}
double nodePos(const Track& t, int n, int k) { const Vec3i& p = t.nodes[size_t(n)].pos; return k == 0 ? p.x : k == 1 ? p.y : p.z; }
int nextNode(const Track& t, int n, bool branch) {  // 0x3BF4C
  const TrackNode& nd = t.nodes[size_t(n)];
  return branch && nd.alt >= 0 ? nd.alt : nd.next;
}
}  // namespace

ShipInput aiControl(const ShipState& s, AiState& ai, const Scene& scene, const AiTables& tables, const ShipParams& params) {
  ShipInput out;
  out.direct = true;
  const Track& t = scene.track_data;
  if (t.nodes.empty() || s.wrecked) return out;
  const double sp[3] = {s.x, s.y, s.z};
  const bool branch = s.damageA > 50 || s.damageB > 50;  // damaged ships take the refuel branch (0x5139A)

  // current node: the node of the piece the ship is in (0x3BEA2); closer than 0x800 -> the next one
  {
    const float p[3] = {float(sp[0] - scene.origin[0]), float(sp[1] - scene.origin[1]), float(sp[2] - scene.origin[2])};
    for (size_t i = 0; i < scene.pieceBoxes.size() && i < t.pieces.size(); ++i)
      if (scene.pieceBoxes[i].graph && scene.pieceContains(i, p, 512.0f) && t.pieces[i].node >= 0) { ai.node = t.pieces[i].node; break; }
    if (ai.node < 0) {  // not in any piece yet: nearest node
      double best = 1e30;
      for (size_t i = 0; i < t.nodes.size(); ++i) {
        const double d[3] = {nodePos(t, int(i), 0) - sp[0], nodePos(t, int(i), 1) - sp[1], nodePos(t, int(i), 2) - sp[2]};
        if (est(d) < best) { best = est(d); ai.node = int(i); }
      }
    }
  }
  int cur = ai.node;
  {
    const double d[3] = {nodePos(t, cur, 0) - sp[0], nodePos(t, cur, 1) - sp[1], nodePos(t, cur, 2) - sp[2]};
    if (est(d) < 0x800) cur = nextNode(t, cur, branch);
  }
  int prev = t.nodes[size_t(cur)].prev >= 0 ? t.nodes[size_t(cur)].prev : cur;
  int next = nextNode(t, cur, branch);
  if (next < 0) next = cur;

  // flat speed (slot +0x2C = |velocity|)
  double vel[3];
  {
    const double fy = std::clamp(s.m[7], -0.25, 0.25);
    const double factor = -fy / 8.0 + (0x200 - std::fabs(s.m[1]) * 0x4000 / 32.0) / 16384.0 + (0x1000 - 40.0 * s.damageA) / 16384.0 + 0x2c00 / 16384.0;
    for (int i = 0; i < 3; ++i) vel[i] = s.m[6 + i] * s.speed * factor + s.slide[i];
  }
  const double speed = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
  const double width = std::max<double>(t.nodes[size_t(cur)].width, 0x3190);  // 0x3BADE

  double toCur[3] = {nodePos(t, cur, 0) - sp[0], nodePos(t, cur, 1) - sp[1], nodePos(t, cur, 2) - sp[2]};
  double dist = est(toCur);  // [0x51344]
  // look-ahead threshold (0x51705..0x51728)
  const double T = std::floor(speed * 0x1e8 / 0x2cb) / 4.0 + width * 2 + std::floor(width / 2) + 0x5f50;
  int pn = prev, cn = cur;
  if (dist <= T) { pn = cur; cn = next; }  // shift the node window (0x51750..0x51778)
  double c[3] = {nodePos(t, cn, 0), nodePos(t, cn, 1), nodePos(t, cn, 2)};
  double back[3] = {nodePos(t, pn, 0) - c[0], nodePos(t, pn, 1) - c[1], nodePos(t, pn, 2) - c[2]};
  const double bl = std::sqrt(back[0] * back[0] + back[1] * back[1] + back[2] * back[2]);
  double target[3] = {c[0], c[1], c[2]};
  if (bl > 1e-6) {
    double ebp = dist - T;
    if (ebp < 0) ebp += est(back);
    for (int i = 0; i < 3; ++i) target[i] = c[i] + back[i] / bl * ebp;  // 0x517E7..0x51811
  }
  double dv[3] = {target[0] - sp[0], target[1] - sp[1], target[2] - sp[2]};
  const double dl = std::sqrt(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]);
  if (dl > 1e-6) for (double& x : dv) x /= dl;

  // aim in the bank-free frame: zero the roll terms of the snapshot and re-orthonormalise (0x51A45..0x51A72)
  double m[9];
  std::copy(s.m, s.m + 9, m);
  m[1] = 0; m[3] = 0;
  {
    auto norm = [](double* v) { const double l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 0) for (int i = 0; i < 3; ++i) v[i] /= l; };
    norm(m + 6);
    const double d = m[0] * m[6] + m[1] * m[7] + m[2] * m[8];
    for (int i = 0; i < 3; ++i) m[i] -= d * m[6 + i];
    norm(m);
    m[3] = m[7] * m[2] - m[8] * m[1]; m[4] = m[8] * m[0] - m[6] * m[2]; m[5] = m[6] * m[1] - m[7] * m[0];
  }
  const double lx = m[0] * dv[0] + m[1] * dv[1] + m[2] * dv[2];  // right
  const double ly = m[3] * dv[0] + m[4] * dv[1] + m[5] * dv[2];  // up
  out.steer = float(std::clamp(lx * 16384.0, -double(0x800), double(0x800)) * 8.0 / 16384.0);
  out.pitch = float(std::clamp(ly * 16384.0, -double(0x800), double(0x800)) * 8.0 / 16384.0);

  // target speed (0x5191B..0x51A3A): curvature of the next 0x5F500 units of the chain
  double tgt = 0xae8f8;
  if (dist <= params.f6) {
    double acc = 0, sumLen = 0;
    double last[3] = {sp[0], sp[1], sp[2]};
    int n = cur;
    for (int guard = 0; guard < 200; ++guard) {
      const double d[3] = {nodePos(t, n, 0) - last[0], nodePos(t, n, 1) - last[1], nodePos(t, n, 2) - last[2]};
      sumLen += est(d);
      for (int i = 0; i < 3; ++i) last[i] = nodePos(t, n, i);
      acc += 0x8000 - t.nodes[size_t(n)].straight;
      if (sumLen >= 0x5f500) break;
      n = nextNode(t, n, branch);
    }
    double ebx = std::max(0.0, 0x4000 - acc);
    if (ebx != 0x3000) {
      const int ti = std::clamp(scene.trackIndex, 1, 10);
      tgt = std::floor(double(tables.speedRange[ti]) * ebx / 16384.0) + tables.minSpeed[ti];
    }
  }
  tgt = std::min(tgt, 214500.0);  // AI ships (record +0xD != 0) are capped at 0x345E4
  out.throttle = tgt >= speed ? 1.0f : 0.0f;
  return out;
}

}  // namespace slip
