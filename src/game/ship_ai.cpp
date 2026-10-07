#include "game/ship_ai.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace slip {

AiTables loadAiTables(const GameData& data) {
  AiTables t;
  auto exe = data.read("SLIPSTRM.EXE");
  constexpr size_t kBase = 0x4D854 - 0x10000;  // file offset = VA + kBase
  if (!exe || exe->size() < kBase + 0x54500) return t;
  auto rd = [&](size_t va) { const size_t o = kBase + va; return int32_t((*exe)[o] | ((*exe)[o + 1] << 8) | ((*exe)[o + 2] << 16) | (uint32_t((*exe)[o + 3]) << 24)); };
  for (int i = 1; i <= 10; ++i) t.lapEstimateMs[i] = rd(0x5a4c0 + 4 * size_t(i));
  if (rd(0x5027a + 4) != 214500 || rd(0x502a2 + 4) != 178750) return t;  // sanity: track 1 entries
  for (int i = 1; i <= 10; ++i) { t.minSpeed[i] = rd(0x5027a + 4 * size_t(i)); t.speedRange[i] = rd(0x502a2 + 4 * size_t(i)); }
  // [0x5409C + 4*difficulty] -> table of 10 row pointers -> 4 dwords (object-relative offsets: + 0x10000)
  for (int d = 0; d < 4; ++d) {
    const size_t tab = size_t(rd(0x5409c + 4 * size_t(d))) + 0x10000;
    if (tab < 0x50000 || tab > 0x56000) return t;
    for (int trk = 1; trk <= 10; ++trk) {
      const size_t row = size_t(rd(tab + 4 * size_t(trk - 1))) + 0x10000;
      if (row < 0x50000 || row > 0x56000) return t;
      for (int k = 0; k < 4; ++k) t.tierFactor[d][trk][k] = rd(row + 4 * size_t(k));
    }
  }
  t.fromExecutable = true;
  return t;
}

int aiTierForStartRank(int rank) {
  static const int kTier[11] = {0, 0, 0, 1, 1, 1, 2, 2, 3, 3, 3};  // 0x586DB, indexed by start position 1..10
  return kTier[std::clamp(rank, 1, 10)];
}

namespace {

// 0x21F87: the game's cheap length estimate, max + (mid + min) / 4
double est(const double* v) {
  double a[3] = {std::fabs(v[0]), std::fabs(v[1]), std::fabs(v[2])};
  std::sort(a, a + 3);
  return a[2] + (a[1] + a[0]) * 0.25;
}
double estv(double x, double y, double z) { const double v[3] = {x, y, z}; return est(v); }
double np(const Track& t, int n, int k) { const Vec3i& p = t.nodes[size_t(n)].pos; return k == 0 ? p.x : k == 1 ? p.y : p.z; }
int nextNode(const Track& t, int n, bool branch) {  // 0x3BF4C
  const TrackNode& nd = t.nodes[size_t(n)];
  return branch && nd.alt >= 0 ? nd.alt : nd.next;
}
double shipSpeed(const ShipState& s) {  // slot +0x2C = |velocity| (RaceSlotHover)
  double v[3];
  const double fy = std::clamp(s.m[7], -0.25, 0.25);
  const double factor = -fy / 8.0 + (0x200 - std::fabs(s.m[1]) * 0x4000 / 32.0) / 16384.0 + (0x1000 - 40.0 * s.damageA) / 16384.0 + 0x2c00 / 16384.0;
  for (int i = 0; i < 3; ++i) v[i] = s.m[6 + i] * s.speed * factor + s.slide[i];
  return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}
unsigned nextRandom(AiState& a) { a.rng = a.rng * 1103515245u + 12345u; return (a.rng >> 8) & 0xffffu; }  // RandomNext, 16 bit

int pieceNodeAt(const Scene& scene, const ShipState& s) {  // [piece+0x1E] of the piece containing the ship (0x36E57)
  const Track& t = scene.track_data;
  const float p[3] = {float(s.x - scene.origin[0]), float(s.y - scene.origin[1]), float(s.z - scene.origin[2])};
  for (size_t i = 0; i < scene.pieceBoxes.size() && i < t.pieces.size(); ++i)
    if (scene.pieceBoxes[i].graph && scene.pieceContains(i, p, 512.0f) && t.pieces[i].node >= 0) return t.pieces[i].node;
  return -1;
}
int pieceIndexAt(const Scene& scene, const ShipState& s) {
  const float p[3] = {float(s.x - scene.origin[0]), float(s.y - scene.origin[1]), float(s.z - scene.origin[2])};
  for (size_t i = 0; i < scene.pieceBoxes.size(); ++i)
    if (scene.pieceBoxes[i].graph && scene.pieceContains(i, p, 512.0f)) return int(i);
  return -1;
}

// 0x3BEA2: target node = the piece's node, or the next one once closer than 0x800
int targetNode(const Scene& scene, const ShipState& s, AiState& a) {
  const Track& t = scene.track_data;
  const int pn = pieceNodeAt(scene, s);
  if (pn >= 0) a.node = pn;
  if (a.node < 0) {
    double best = 1e30;
    for (size_t i = 0; i < t.nodes.size(); ++i) {
      const double d = estv(np(t, int(i), 0) - s.x, np(t, int(i), 1) - s.y, np(t, int(i), 2) - s.z);
      if (d < best) { best = d; a.node = int(i); }
    }
  }
  int cur = a.node;
  if (a.branch && t.nodes[size_t(cur)].prev >= 0 && t.nodes[size_t(t.nodes[size_t(cur)].prev)].alt >= 0)
    cur = t.nodes[size_t(t.nodes[size_t(cur)].prev)].alt;  // 0x3BEE5: with the branch flag set the first node after a split is the alternative route's
  if (estv(np(t, cur, 0) - s.x, np(t, cur, 1) - s.y, np(t, cur, 2) - s.z) < 0x800) cur = nextNode(t, cur, a.branch);
  return cur;
}

struct Frame { double f[3], r[3], u[3]; };
Frame nodeFrame(const Track& t, int prev, int cur) {  // 0x51823 / 0x3BA2B: forward = prev -> cur, right = (f.z, 0, -f.x), up = f x r
  Frame fr{};
  double f[3] = {np(t, cur, 0) - np(t, prev, 0), np(t, cur, 1) - np(t, prev, 1), np(t, cur, 2) - np(t, prev, 2)};
  const double l = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
  if (l > 1e-6) for (double& x : f) x /= l; else { f[0] = 0; f[1] = 0; f[2] = 1; }
  for (int i = 0; i < 3; ++i) fr.f[i] = f[i];
  fr.r[0] = f[2]; fr.r[1] = 0; fr.r[2] = -f[0];
  const double rl = std::sqrt(fr.r[0] * fr.r[0] + fr.r[2] * fr.r[2]);
  if (rl > 1e-6) { fr.r[0] /= rl; fr.r[2] /= rl; } else { fr.r[0] = 1; fr.r[2] = 0; }
  fr.u[0] = f[1] * fr.r[2] - f[2] * fr.r[1];
  fr.u[1] = f[2] * fr.r[0] - f[0] * fr.r[2];
  fr.u[2] = f[0] * fr.r[1] - f[1] * fr.r[0];
  return fr;
}

struct Neighbours { int ahead = -1, along = -1, behind = -1; double dAhead = 1e30, dAlong = 1e30, dBehind = 1e30; };
// TrackSlotGetNeighbours 0x3B6D0 for ship `me`
Neighbours findNeighbours(RaceContext& ctx, size_t me) {
  const Scene& scene = *ctx.scene;
  const Track& t = scene.track_data;
  Neighbours nb;
  ShipState& S = *ctx.ships[me];
  const int myNode = targetNode(scene, S, *ctx.state[me]);
  const double myPos[3] = {S.x, S.y, S.z};
  const double d0 = estv(np(t, myNode, 0) - S.x, np(t, myNode, 1) - S.y, np(t, myNode, 2) - S.z);
  const double aheadLimit = d0 - S.extent, behindLimit = d0 + S.extent;  // [0x3B99C], [0x3B9A0]
  for (size_t o = 0; o < ctx.ships.size(); ++o) {
    if (o == me) continue;
    const ShipState& O = *ctx.ships[o];
    if (!O.hasBox) continue;
    const double dist = estv(O.x - myPos[0], O.y - myPos[1], O.z - myPos[2]);
    const int oNode = targetNode(scene, O, *ctx.state[o]);
    if (oNode == myNode) {
      const double dO = estv(O.x - np(t, oNode, 0), O.y - np(t, oNode, 1), O.z - np(t, oNode, 2)) + O.extent;
      if (dO <= aheadLimit) { if (dist <= nb.dAhead) { nb.ahead = int(o); nb.dAhead = dist; } }
      else if (dO >= behindLimit) { if (dist <= nb.dBehind) { nb.behind = int(o); nb.dBehind = dist; } }
      else if (dist <= nb.dAlong) { nb.along = int(o); nb.dAlong = dist; }
    } else {
      const double dot = (O.x - S.x) * S.m[6] + (O.y - S.y) * S.m[7] + (O.z - S.z) * S.m[8];  // along my heading (0x3B8F3)
      if (dot >= 0) { if (dist <= nb.dAhead) { nb.ahead = int(o); nb.dAhead = dist; } }
      else if (dist <= nb.dBehind) { nb.behind = int(o); nb.dBehind = dist; }
    }
  }
  return nb;
}

// 0x3BA0C: lateral position (right, up) of ship o relative to its own node line
void lateralOf(RaceContext& ctx, size_t o, double* lx, double* ly) {
  const Track& t = ctx.scene->track_data;
  const ShipState& O = *ctx.ships[o];
  const int cur = targetNode(*ctx.scene, O, *ctx.state[o]);
  const int prev = t.nodes[size_t(cur)].prev >= 0 ? t.nodes[size_t(cur)].prev : cur;
  const Frame fr = nodeFrame(t, prev, cur);
  const double d[3] = {O.x - np(t, cur, 0), O.y - np(t, cur, 1), O.z - np(t, cur, 2)};
  *lx = fr.r[0] * d[0] + fr.r[1] * d[1] + fr.r[2] * d[2];
  *ly = fr.u[0] * d[0] + fr.u[1] * d[1] + fr.u[2] * d[2];
}
}  // namespace

int readConfiguredDifficulty(const GameData& data) {
  auto cfg = data.read("SLIPSTRM.CFG");
  if (!cfg || cfg->size() < 160 || (*cfg)[0] != 'V') return 1;
  return std::clamp(int((*cfg)[157]) | (int((*cfg)[158]) << 8), 0, 2);
}

RaceInfo buildRaceInfo(const Scene& scene, int shipCount) {
  RaceInfo r;
  r.shipCount = shipCount;
  const Track& t = scene.track_data;
  r.lapDist.assign(t.nodes.size(), -1);
  if (t.nodes.empty()) return r;
  auto edge = [&](int a, int b) { return estv(np(t, b, 0) - np(t, a, 0), np(t, b, 1) - np(t, a, 1), np(t, b, 2) - np(t, a, 2)); };
  int n = 0;
  r.lapDist[0] = 0;
  for (int guard = 0; guard < 10000; ++guard) {
    const int nx = t.nodes[size_t(n)].next;
    if (nx < 0) break;
    if (nx == 0) { r.lapLength = r.lapDist[size_t(n)] + edge(n, 0); break; }
    if (r.lapDist[size_t(nx)] >= 0) break;
    r.lapDist[size_t(nx)] = r.lapDist[size_t(n)] + edge(n, nx);
    n = nx;
  }
  for (size_t i = 0; i < t.nodes.size(); ++i) {  // alternative routes continue from their split node
    if (t.nodes[i].alt < 0 || r.lapDist[i] < 0) continue;
    int w = t.nodes[i].alt, from = int(i);
    for (int guard = 0; guard < 1000 && w >= 0 && r.lapDist[size_t(w)] < 0; ++guard) {
      r.lapDist[size_t(w)] = r.lapDist[size_t(from)] + edge(from, w);
      from = w;
      w = t.nodes[size_t(w)].next;
    }
  }
  for (double& d : r.lapDist) if (d < 0) d = 0;
  return r;
}

LapEvent lapCrossing(AiState& a, int oldPiece, int newPiece, int pieceA, int pieceB, int totalLaps, int finishedSoFar) {
  if (pieceA < 0 || pieceB < 0 || oldPiece == newPiece) return LapEvent::None;
  if (newPiece == pieceA) {
    if (oldPiece != pieceB) return LapEvent::None;
    if (a.back) { a.back = false; return LapEvent::None; }  // 0x5A5B4: returning over the line after a backwards crossing
    if (a.finished) return LapEvent::None;
    if (a.laps == 0) { a.laps = 1; return LapEvent::Started; }  // 0x5A5D7
    a.lastLap = a.lapTime;                                       // 0x5A5E0: lap time, best lap
    if (a.bestLap == 0 || a.lapTime < a.bestLap) a.bestLap = a.lapTime;
    a.lapTime = 0;
    ++a.laps;
    if (a.laps - 1 >= totalLaps) {                               // 0x5A66D
      a.finished = true;
      a.finishRank = finishedSoFar + 1;
      a.finishTime = a.raceTime;
      return LapEvent::Finished;
    }
    return LapEvent::Lap;
  }
  if (newPiece == pieceB && oldPiece == pieceA) a.back = true;   // 0x5A5A0
  return LapEvent::None;
}

void assignRanks(const std::vector<AiState*>& st, const std::vector<double>& remain) {
  const size_t n = st.size();
  std::vector<int> order;
  std::vector<bool> taken(n + 2, false);
  for (size_t i = 0; i < n; ++i) {
    st[i]->prevRank = st[i]->rank;
    if (st[i]->finished && st[i]->finishRank >= 1 && size_t(st[i]->finishRank) <= n) { st[i]->rank = st[i]->finishRank; taken[size_t(st[i]->finishRank)] = true; }
    else order.push_back(int(i));
  }
  std::stable_sort(order.begin(), order.end(), [&](int x, int y) {
    const int lx = st[size_t(x)]->laps - (st[size_t(x)]->back ? 1 : 0), ly = st[size_t(y)]->laps - (st[size_t(y)]->back ? 1 : 0);
    if (lx != ly) return lx > ly;
    return remain[size_t(x)] < remain[size_t(y)];
  });
  size_t r = 1;
  for (int i : order) {
    while (r <= n && taken[r]) ++r;
    st[size_t(i)]->rank = int(r++);
  }
}

double startBonusForRank(int rank) {
  static const int kBonus[11] = {0, 12288, 11264, 10240, 8192, 6144, 4096, 2048, 1024, 512, 256};  // 0x50252
  return rank >= 1 && rank <= 10 ? kBonus[rank] / 16384.0 : 0.0;
}

namespace {
// Slot cell tracking: the piece only changes to a piece that is linked to the current one (portal), else a full search.
int stepPiece(const Scene& scene, const ShipState& s, int cur) {
  const Track& t = scene.track_data;
  const float p[3] = {float(s.x - scene.origin[0]), float(s.y - scene.origin[1]), float(s.z - scene.origin[2])};
  auto inside = [&](int k, float slack) { return k >= 0 && size_t(k) < scene.pieceBoxes.size() && scene.pieceBoxes[size_t(k)].graph && scene.pieceContains(size_t(k), p, slack); };
  if (inside(cur, 0.0f)) return cur;
  if (cur >= 0 && size_t(cur) < t.pieces.size()) {
    for (const PieceLink& l : t.pieces[size_t(cur)].links) {
      if (!l.pieceOffset) continue;
      for (size_t k = 0; k < t.pieces.size(); ++k)
        if (t.pieces[k].trdOffset == l.pieceOffset && inside(int(k), 0.0f)) return int(k);
    }
    if (inside(cur, 512.0f)) return cur;
  }
  const int found = pieceIndexAt(scene, s);
  return found >= 0 ? found : cur;
}
}  // namespace

void updateRace(RaceContext& ctx) {
  const Track& t = ctx.scene->track_data;
  if (t.nodes.empty()) return;
  RaceStatus local;
  RaceStatus& st = ctx.status ? *ctx.status : local;
  const size_t n = ctx.ships.size();
  std::vector<double> remain(n, 0);
  std::vector<AiState*> recs;
  for (size_t i = 0; i < n; ++i) {
    ShipState& s = *ctx.ships[i];
    AiState& a = *ctx.state[i];
    recs.push_back(&a);
    const int prevNode = a.node;
    const int node = targetNode(*ctx.scene, s, a);
    (void)prevNode;
    if (ctx.running) {  // 0x5A542: clocks only run after the countdown; the race clock stops at the finish
      if (!a.finished) { a.raceTime += ctx.dt; a.lapTime += ctx.dt; }
    }
    const int np_ = s.wrecked ? a.piece : stepPiece(*ctx.scene, s, a.piece);
    if (np_ != a.piece) {
      const int old = a.piece;
      a.piece = np_;
      if (old >= 0) {
        const LapEvent ev = lapCrossing(a, old, np_, t.lapPieceA, t.lapPieceB, ctx.totalLaps, st.finishedCount);
        if (ev == LapEvent::Started) st.events.push_back({int(i), 0});
        else if (ev == LapEvent::Lap) st.events.push_back({int(i), 1});
        else if (ev == LapEvent::Finished) { ++st.finishedCount; st.events.push_back({int(i), 1}); st.events.push_back({int(i), 2}); }
      }
    }
    a.progress = ctx.race->lapDist[size_t(node)] - estv(np(t, node, 0) - s.x, np(t, node, 1) - s.y, np(t, node, 2) - s.z);
    remain[i] = ctx.race->lapLength - a.progress;  // 0x3BD0D: distance to the line along the route
    a.lap = std::max(0, a.laps - 1);
  }
  st.prevBestAiRank = st.bestAiRank;
  assignRanks(recs, remain);
  int best = 10;
  for (size_t i = 0; i < n; ++i) if (!recs[i]->human) best = std::min(best, recs[i]->rank);  // 0x50446
  st.bestAiRank = best;
  for (size_t i = 0; i < n; ++i) {  // 0x50BB4..0x50BE0: the human took the place of the best AI ship
    AiState& a = *recs[i];
    if (a.human && a.rank != a.prevRank && st.bestAiRank != st.prevBestAiRank && a.rank == st.prevBestAiRank && a.rank < st.bestAiRank) st.events.push_back({int(i), 3});
  }
  // race end (0x5A74F..0x5A793): five seconds after the second AI ship finished, or when no AI ship is left racing
  int aiDone = 0, aiRacing = 0;
  for (size_t i = 0; i < n; ++i) if (!recs[i]->human) { if (recs[i]->finished) ++aiDone; else ++aiRacing; }
  if (st.endTimer < 0 && !st.over && (aiDone >= 2 || (aiRacing == 0 && n > 0))) st.endTimer = 5.0;
  if (st.endTimer >= 0 && ctx.running) {
    st.endTimer -= ctx.dt;
    if (st.endTimer < 0) {
      st.over = true;
      // 0x5A461: ships still racing get a projected finish clock: time left in the lap at the lap estimate (90 s per lap) plus the full laps to go
      const double lap = ctx.tables ? ctx.tables->lapEstimateMs[std::clamp(ctx.scene->trackIndex, 1, 10)] / 1000.0 : 90.0;
      for (size_t i = 0; i < n; ++i) {
        AiState& a = *recs[i];
        if (a.finished) continue;
        a.finishTime = a.raceTime + remain[i] / std::max(1.0, ctx.race->lapLength) * lap + std::max(0, ctx.totalLaps - a.laps) * lap;
        a.projected = true;
        a.finished = true;
        a.finishRank = a.rank;
      }
    }
  }
}

void updateWrecks(RaceContext& ctx) {
  const Track& t = ctx.scene->track_data;
  if (t.nodes.empty()) return;
  for (size_t i = 0; i < ctx.ships.size(); ++i) {
    ShipState& s = *ctx.ships[i];
    if (!s.wrecked) continue;
    AiState& a = *ctx.state[i];
    const int pn = pieceNodeAt(*ctx.scene, s);
    const int cur = targetNode(*ctx.scene, s, a);
    const int nx = nextNode(t, cur, a.branch) >= 0 ? nextNode(t, cur, a.branch) : cur;
    for (int k = 0; k < 3; ++k) s.wreckAim[k] = np(t, nx, k);  // 0x3B54E: the node ahead
    s.wreckAimValid = true;
    if (s.wreckNode < 0) {
      s.wreckNode = pn;
      const double dist = estv(s.wreckAim[0] - s.x, s.wreckAim[1] - s.y, s.wreckAim[2] - s.z);
      s.wreckTimer += std::min(5.0, std::floor(dist / 0x77240 * 1000.0) / 1000.0);  // 0x3E9C9..0x3E9E3
      double d[3] = {s.wreckAim[0] - s.x, s.wreckAim[1] - s.y, s.wreckAim[2] - s.z};
      const double l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
      if (l > 1) for (int k = 0; k < 3; ++k) s.wreckHead[k] = d[k] / l;
    }
    const bool leftPiece = s.wreckLanded && pn >= 0 && pn != s.wreckNode;
    if (s.wreckRecover || leftPiece || s.wreckTime > 12.0) {
      const int prev = t.nodes[size_t(cur)].prev >= 0 ? t.nodes[size_t(cur)].prev : cur;
      const Frame fr = nodeFrame(t, prev, cur);  // 0x3ED15..0x3ED1F: orientation from the track direction
      for (int k = 0; k < 3; ++k) { s.m[k] = fr.r[k]; s.m[3 + k] = fr.u[k]; s.m[6 + k] = fr.f[k]; }
      s.wrecked = false;
      s.wreckRecover = false;
      s.wreckNode = -1;
      s.speed = s.savedSpeed;
      s.slide[0] = s.slide[1] = s.slide[2] = 0;
      s.recentHit = 0;
      s.steerAxis = s.pitchAxis = 0;
    }
  }
}

void applyTrailingBoost(RaceContext& ctx, size_t humanIndex) {
  AiState& a = *ctx.state[humanIndex];
  // 0x51C70..0x51CA0: the human is exactly one place behind the best AI ship ([0x50438] + 1 == rank), the ship ahead is more than
  // 0x595B0 away and it is not the last lap (the original tests slot data +0x1C against 0x1DC90 there, which is nearly always
  // smaller, so the boost is effectively off on the final lap).
  int bestAi = 10;
  for (size_t i = 0; i < ctx.state.size(); ++i) if (!ctx.state[i]->human) bestAi = std::min(bestAi, ctx.state[i]->rank);
  if (a.rank != bestAi + 1) return;
  if (a.laps == ctx.totalLaps) return;
  const Neighbours nb = findNeighbours(ctx, humanIndex);
  if (nb.ahead >= 0 && nb.dAhead > 0x595b0) ctx.ships[humanIndex]->boostTime = 6.0;  // [slot+0x3E] = 0x1770 ms
}

ShipInput aiControl(RaceContext& ctx, size_t index, double dt) {
  ShipInput out;
  out.direct = true;
  const Scene& scene = *ctx.scene;
  const Track& t = scene.track_data;
  ShipState& s = *ctx.ships[index];
  AiState& ai = *ctx.state[index];
  const ShipParams& params = *ctx.params[index];
  if (t.nodes.empty() || s.wrecked) return out;
  const double sp[3] = {s.x, s.y, s.z};

  // doors: a ship standing on a door's piece opens it faster (TrackSlotFindDoor + 0x35564: state 0, speed 0x53CA)
  if (ctx.doors) {
    const int pi = pieceIndexAt(scene, s);
    for (Door& d : ctx.doors->list)
      if (d.piece == pi) { d.state = 0; d.speed = 0x53ca; }
  }

  // branch decision at split nodes (TrackSlotCheckBranch 0x3544F + 0x5135E..0x51437)
  const Neighbours nb = findNeighbours(ctx, index);
  {
    const int pn = pieceNodeAt(scene, s);
    if (pn >= 0) {
      // TrackSlotCheckBranch 0x3544F: 1 = the previous node has an alternative (we are just past a split), 2 / 3 = the NEXT node
      // (by the branch flag, 0x3BF4C) is a split / a pit entrance, 0 = nothing to decide
      const TrackNode& n = t.nodes[size_t(pn)];
      int state = 0;
      if (n.prev >= 0 && t.nodes[size_t(n.prev)].alt >= 0) state = 1;
      else {
        const int nx = nextNode(t, pn, ai.branch);
        if (nx >= 0 && t.nodes[size_t(nx)].alt >= 0) state = t.nodes[size_t(nx)].pit ? 3 : 2;
      }
      if (state == 3) {  // pit entrance ahead: damaged ships (> 50 on either counter) take it
        ai.branch = s.damageA > 50 || s.damageB > 50;
      } else if (state == 2 && !ai.human) {  // ordinary split: AI ships only, never first or among the last two, not while someone is alongside
        if (nb.along >= 0 || ai.rank == 1 || ai.rank >= ctx.race->shipCount - 1) ai.branch = false;
        else {
          unsigned thresh = 0xa00;
          if (nb.behind >= 0 && nb.dBehind >= 0x77240) thresh = 0x6000;
          ai.branch = nextRandom(ai) < thresh;
        }
      }
    }
  }

  const int cur0 = targetNode(scene, s, ai);
  const bool branch = ai.branch;
  int cur = cur0;
  int prev = t.nodes[size_t(cur)].prev >= 0 ? t.nodes[size_t(cur)].prev : cur;
  int next = nextNode(t, cur, branch);
  if (next < 0) next = cur;

  const double speed = shipSpeed(s);
  const double width = std::max<double>(t.nodes[size_t(cur)].width, 0x3190);  // 0x3BADE
  const double dist = estv(np(t, cur, 0) - sp[0], np(t, cur, 1) - sp[1], np(t, cur, 2) - sp[2]);  // [0x51344]

  // ---- avoidance (0x51488..0x5153C) ----
  int followSlot = -1;
  double followDist = 1e30;
  ai.startDelay = std::max(0.0, ai.startDelay - dt);
  if (ai.startDelay > 0) {
    // no avoidance for the first 3 s, offsets kept
  } else if (nb.ahead >= 0 && nb.dAhead <= 0x17d40) {
    const ShipState& O = *ctx.ships[size_t(nb.ahead)];
    const bool faster = speed > shipSpeed(O);  // 0x515D2: carry clear when my speed is higher
    bool room = false;
    double ox = 0, oy = 0;
    if (faster) {  // 0x515F2
      double lx, ly;
      lateralOf(ctx, size_t(nb.ahead), &lx, &ly);
      const double lat = std::sqrt(lx * lx + ly * ly);
      const double eo = O.extent + 0x1310, em = s.extent + 0x1310;
      const double rest = width - (eo - lat);
      if (rest >= 0 && em <= rest) {
        const double mag = (eo - lat) + std::floor(rest / 2);
        const double l = std::sqrt(lx * lx + ly * ly);
        if (l > 1e-6) { ox = -lx / l * mag; oy = -ly / l * mag; } else { ox = mag; oy = 0; }
        room = true;
      }
    }
    if (room) {
      ai.offX = ox; ai.offY = oy;
    } else {
      if (nb.along < 0) { ai.offX = 0; ai.offY = 0; }
      followSlot = nb.ahead; followDist = nb.dAhead;
    }
  } else {
    if (nb.along < 0) { ai.offX = 0; ai.offY = 0; }
    if (nb.ahead >= 0) { followSlot = nb.ahead; followDist = nb.dAhead; }
  }

  // ---- aim point (0x51688..0x51911) ----
  const double T = std::floor(speed * 0x1e8 / 0x2cb) / 4.0 + width * 2 + std::floor(width / 2) + 0x5f50;
  int pn = prev, cn = cur;
  if (dist <= T) { pn = cur; cn = next; }  // shift the node window (0x51750..0x51778)
  double c[3] = {np(t, cn, 0), np(t, cn, 1), np(t, cn, 2)};
  double back[3] = {np(t, pn, 0) - c[0], np(t, pn, 1) - c[1], np(t, pn, 2) - c[2]};
  const double bl = std::sqrt(back[0] * back[0] + back[1] * back[1] + back[2] * back[2]);
  double target[3] = {c[0], c[1], c[2]};
  if (bl > 1e-6) {
    double ebp = dist - T;
    if (ebp < 0) ebp += est(back);
    for (int i = 0; i < 3; ++i) target[i] = c[i] + back[i] / bl * ebp;  // 0x517E7..0x51811
  }
  // lateral offset, clamped to the room left in the corridor (0x51694..0x516F9)
  double offX = ai.offX, offY = ai.offY;
  if (offX != 0 || offY != 0) {
    const double limit = width - s.extent - 0x988;
    const double l = std::sqrt(offX * offX + offY * offY);
    if (l > limit && l > 0) { const double k = std::max(0.0, limit) / l; offX *= k; offY *= k; }
    const Frame fr = nodeFrame(t, pn, cn);
    for (int i = 0; i < 3; ++i) target[i] += fr.r[i] * offX + fr.u[i] * offY;
  }
  double dv[3] = {target[0] - sp[0], target[1] - sp[1], target[2] - sp[2]};
  const double dl = std::sqrt(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]);
  if (dl > 1e-6) for (double& x : dv) x /= dl;

  // aim in the bank-free frame (0x51A45..0x51A72)
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

  // ---- target speed (0x5191B..0x51A3A) ----
  double tgt = 0xae8f8;
  if (dist <= params.f6) {
    double acc = 0, sumLen = 0;
    double last[3] = {sp[0], sp[1], sp[2]};
    int n = cur;
    for (int guard = 0; guard < 200; ++guard) {
      const double d[3] = {np(t, n, 0) - last[0], np(t, n, 1) - last[1], np(t, n, 2) - last[2]};
      sumLen += est(d);
      for (int i = 0; i < 3; ++i) last[i] = np(t, n, i);
      acc += 0x8000 - t.nodes[size_t(n)].straight;
      if (sumLen >= 0x5f500) break;
      n = nextNode(t, n, branch);
    }
    const double ebx = std::max(0.0, 0x4000 - acc);
    if (ebx != 0x3000) {
      const int ti = std::clamp(scene.trackIndex, 1, 10);
      tgt = std::floor(double(ctx.tables->speedRange[ti]) * ebx / 16384.0) + ctx.tables->minSpeed[ti];
    }
  }
  // keep behind a slower ship that blocks the way (0x51970..0x519C2)
  if (followSlot >= 0 && followDist < 0xbea0) {
    double lim = shipSpeed(*ctx.ships[size_t(followSlot)]);
    if (followDist < 0x5f50) lim -= 0x1bee;
    else if (followDist >= 0x9880) lim += 0x138d;
    tgt = std::min(tgt, lim);
  }
  if (!ai.human) tgt = std::min(tgt, 214500.0);  // AI ships (record +0xD != 0) are capped at 0x345E4 (0x51A0D)
  out.throttle = tgt >= speed ? 1.0f : 0.0f;
  return out;
}

}  // namespace slip
