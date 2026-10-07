#include "game/doors.hpp"

#include <cmath>

namespace slip {

namespace {
double len(const double* v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
}  // namespace

void Doors::build(const Scene& scene) {
  list.clear();
  const Track& t = scene.track_data;
  int doorsMat = -1;
  for (size_t i = 0; i < scene.materials.size(); ++i)
    if (scene.materials[i].upperName == "DOORS") { doorsMat = int(i); break; }
  for (size_t pi = 0; pi < t.pieces.size(); ++pi) {
    const TrackPiece& pc = t.pieces[pi];
    const TrackRecord& rec = t.records[size_t(pc.record)];
    for (const TrackPolygon& poly : rec.polys) {
      if (poly.list != 0 || !(poly.flags & 0x20) || poly.index.size() < 4 || list.size() >= 8) continue;
      double v[4][3];
      bool ok = true;
      for (int k = 0; k < 4; ++k) {
        if (poly.index[size_t(k)] >= rec.verts.size()) { ok = false; break; }
        const Vec3i& w = rec.verts[poly.index[size_t(k)]];
        v[k][0] = double(pc.pos.x) + w.x; v[k][1] = double(pc.pos.y) + w.y; v[k][2] = double(pc.pos.z) + w.z;
      }
      if (!ok) continue;
      Door d;
      d.piece = int(pi);
      double e[3], e1[3], e2[3];
      for (int i = 0; i < 3; ++i) { e[i] = v[0][i] - v[3][i]; e1[i] = v[1][i] - v[0][i]; e2[i] = v[3][i] - v[0][i]; d.c[i] = (v[0][i] + v[2][i]) * 0.5; }
      const double el = len(e);
      if (el < 1) continue;
      double snap[3];
      if (e[1] / el >= 0.75) { snap[0] = 0; snap[1] = 1; snap[2] = 0; }
      else if (e[1] / el <= -0.75) { snap[0] = 0; snap[1] = -1; snap[2] = 0; }
      else {
        const double h = std::sqrt(e[0] * e[0] + e[2] * e[2]);
        snap[0] = h > 0 ? e[0] / h : 0; snap[1] = 0; snap[2] = h > 0 ? e[2] / h : 0;
      }
      const double b = el * 0.5;  // |v0 - v3| / 2
      for (int i = 0; i < 3; ++i) {
        d.s[i] = -snap[i];
        d.open[i] = d.c[i] + d.s[i] * (2 * b - b / 16);
        d.closed[i] = (d.c[i] + d.open[i]) * 0.5;
        d.pos[i] = d.closed[i];
      }
      // panel: the Dummy quad's rectangle, centred on the slot origin, both faces
      const double cornerSign[4][2] = {{-0.5, -0.5}, {0.5, -0.5}, {0.5, 0.5}, {-0.5, 0.5}};
      const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
      double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
      const double nl = len(n);
      if (nl < 1e-6) continue;
      for (int i = 0; i < 3; ++i) n[i] /= nl;
      for (int face = 0; face < 2; ++face) {
        MeshPoly p;
        p.first = uint32_t(d.mesh.verts.size());
        p.count = 4;
        p.material = doorsMat;
        p.hasUV = true;
        p.normal = {float(face ? -n[0] : n[0]), float(face ? -n[1] : n[1]), float(face ? -n[2] : n[2])};
        for (int k = 0; k < 4; ++k) {
          const int kk = face ? 3 - k : k;
          Vec3 c;
          c.x = float(e1[0] * cornerSign[kk][0] + e2[0] * cornerSign[kk][1]);
          c.y = float(e1[1] * cornerSign[kk][0] + e2[1] * cornerSign[kk][1]);
          c.z = float(e1[2] * cornerSign[kk][0] + e2[2] * cornerSign[kk][1]);
          d.mesh.verts.push_back(c);
          d.mesh.uv.push_back(uv[kk][0]);
          d.mesh.uv.push_back(uv[kk][1]);
        }
        d.mesh.polys.push_back(p);
      }
      list.push_back(std::move(d));
    }
  }
}

void Doors::step(double dt) {
  for (Door& d : list) {
    double remaining = d.speed * dt;
    for (int guard = 0; guard < 4 && remaining > 0; ++guard) {
      const double* target = d.state == 0 ? d.open : d.closed;
      const double sg = d.state == 0 ? 1.0 : -1.0;
      // distance left to the end stop along the slide direction
      double left = 0;
      for (int i = 0; i < 3; ++i) left += (target[i] - d.pos[i]) * d.s[i] * sg;
      if (left > remaining) {
        for (int i = 0; i < 3; ++i) d.pos[i] += d.s[i] * sg * remaining;
        break;
      }
      for (int i = 0; i < 3; ++i) d.pos[i] = target[i];
      remaining -= std::max(left, 0.0);
      d.state = d.state == 0 ? -1 : 0;  // reached the end: flip (xor [edi+4], -1)
      d.speed = 0x37dc;
      if (left <= 0 && remaining <= 0) break;
    }
  }
}

}  // namespace slip
