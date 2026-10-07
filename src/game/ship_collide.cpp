#include "game/ship_collide.hpp"

#include <algorithm>
#include <cmath>

namespace slip {
namespace {

void corners(const double pos[3], const double M[9], const double lo[3], const double hi[3], double out[8][3]) {
  for (int k = 0; k < 8; ++k) {
    const double c[3] = {(k & 1) ? hi[0] : lo[0], (k & 2) ? hi[1] : lo[1], (k & 4) ? hi[2] : lo[2]};
    for (int i = 0; i < 3; ++i) out[k][i] = pos[i] + M[i] * c[0] + M[3 + i] * c[1] + M[6 + i] * c[2];
  }
}

// Even-odd point-in-polygon test after projecting along the dominant normal axis.
bool insidePolygon(const Scene& s, const MeshPoly& p, const double q[3]) {
  int ax = 0;
  double a = std::fabs(p.normal.x), b = std::fabs(p.normal.y), c = std::fabs(p.normal.z);
  if (b >= a && b >= c) ax = 1; else if (c >= a && c >= b) ax = 2;
  const int u = ax == 0 ? 1 : 0, v = ax == 2 ? 1 : 2;
  bool in = false;
  for (uint32_t i = 0, j = p.count - 1; i < p.count; j = i++) {
    const Vec3& pi = s.track.verts[p.first + i];
    const Vec3& pj = s.track.verts[p.first + j];
    const double xi = u == 0 ? pi.x : pi.y, yi = v == 1 ? pi.y : pi.z;
    const double xj = u == 0 ? pj.x : pj.y, yj = v == 1 ? pj.y : pj.z;
    const double qx = q[u], qy = q[v];
    if (((yi > qy) != (yj > qy)) && (qx < (xj - xi) * (qy - yi) / (yj - yi) + xi)) in = !in;
  }
  return in;
}

void candidatePieces(const Scene& s, const double p[3], std::vector<int>* out) {
  const float f[3] = {float(p[0]), float(p[1]), float(p[2])};
  for (size_t i = 0; i < s.pieceBoxes.size(); ++i)
    if (s.pieceBoxes[i].graph && s.pieceContains(i, f, 512.0f)) out->push_back(int(i));  // slot slack 0x200 (0x37FCC)
}

}  // namespace

ShipHit sweepShipBox(const Scene& scene, const double pos[3], const double M[9], const double lo[3], const double hi[3], const double dir[3], double len) {
  ShipHit best;
  best.dist = len;
  const double off[3] = {pos[0] - scene.origin[0], pos[1] - scene.origin[1], pos[2] - scene.origin[2]};
  double cr[8][3];
  corners(off, M, lo, hi, cr);
  std::vector<int> pcs;
  std::vector<char> seen(scene.pieceBoxes.size(), 0);
  for (int k = 0; k < 8; ++k) {
    pcs.clear();
    candidatePieces(scene, cr[k], &pcs);
    std::vector<int> polyPieces;
    for (int pi : pcs) {
      if (!seen[size_t(pi)]) { seen[size_t(pi)] = 1; polyPieces.push_back(pi); }
      for (int l = 0; l < 3; ++l) {
        int nb = scene.pieceBoxes[size_t(pi)].link[l];
        if (nb >= 0 && !seen[size_t(nb)]) { seen[size_t(nb)] = 1; polyPieces.push_back(nb); }
      }
    }
    for (int pi : polyPieces) {
      seen[size_t(pi)] = 0;
      for (uint32_t id : scene.piecePolys[size_t(pi)]) {
        const MeshPoly& p = scene.track.polys[id];
        if (p.portal || (p.pflags & 0x41) || p.scenery) continue;
        const double cosT = -(p.normal.x * dir[0] + p.normal.y * dir[1] + p.normal.z * dir[2]);
        if (cosT < 16.0 / 16384.0) continue;  // movement must run against the plane (0x3CB88: cmp bx,0x10)
        const Vec3& v0 = scene.track.verts[p.first];
        const double dist = p.normal.x * (cr[k][0] - v0.x) + p.normal.y * (cr[k][1] - v0.y) + p.normal.z * (cr[k][2] - v0.z);
        if (dist < 0) continue;  // behind the polygon (0x3CBE2)
        double t = (dist - kShipCollideMargin) / cosT;  // distance along the movement to reach the margin plane
        if (t >= best.dist) continue;
        double q[3] = {cr[k][0] + dir[0] * std::max(t, 0.0), cr[k][1] + dir[1] * std::max(t, 0.0), cr[k][2] + dir[2] * std::max(t, 0.0)};
        // the hit point on the polygon plane itself (corner moved until it touches the plane, not only the margin plane)
        const double tp = dist / cosT;
        double qp[3] = {cr[k][0] + dir[0] * tp, cr[k][1] + dir[1] * tp, cr[k][2] + dir[2] * tp};
        (void)q;
        if (!insidePolygon(scene, p, qp)) continue;
        best.hit = true;
        best.dist = std::max(t, 0.0);
        best.n[0] = p.normal.x; best.n[1] = p.normal.y; best.n[2] = p.normal.z;
        best.polyFlags = p.pflags;
        best.piece = pi;
      }
    }
  }
  return best;
}

bool shipBoxInsideTrack(const Scene& scene, const double pos[3], const double M[9], const double lo[3], const double hi[3]) {
  const double off[3] = {pos[0] - scene.origin[0], pos[1] - scene.origin[1], pos[2] - scene.origin[2]};
  double cr[8][3];
  corners(off, M, lo, hi, cr);
  std::vector<int> pcs;
  for (int k = 0; k < 8; ++k) {
    pcs.clear();
    candidatePieces(scene, cr[k], &pcs);
    if (pcs.empty()) return false;
  }
  return true;
}

}  // namespace slip
