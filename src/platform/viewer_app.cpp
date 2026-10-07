#include "platform/viewer_app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace slip {

namespace {
constexpr double kPi = 3.14159265358979323846;
}  // namespace

bool ViewerApp::init(const AppOptions& opt, std::string* error) {
  opt_ = opt;
  std::filesystem::path dir = findGameDirectory(opt.dataDir);
  if (dir.empty()) {
    if (error) *error = "Original game files not found. Pass --data <folder with SLIPSTRM.RES> or set SLIPSTREAM_DATA.";
    return false;
  }
  data_ = GameData::open(dir, error);
  if (!data_) return false;
  rememberGameDirectory(dir);
  params_ = loadShipParams(*data_);
  renderer_.resize(opt.width, opt.height);
  cam_.fovY = 1.15f;
  shapes_ = data_->list("SHP");
  sprites_ = data_->list("SPR");
  auto withExt = [](std::string n, const char* ext) {
    n = normalizeName(n);
    if (n.find('.') == std::string::npos) n += ext;
    return n;
  };
  for (size_t i = 0; i < shapes_.size(); ++i)
    if (!opt.shape.empty() && shapes_[i] == withExt(opt.shape, ".SHP")) shapeIdx_ = int(i);
  for (size_t i = 0; i < sprites_.size(); ++i)
    if (!opt.sprite.empty() && sprites_[i] == withExt(opt.sprite, ".SPR")) spriteIdx_ = int(i);
  mode_ = opt.mode;
  track_ = std::clamp(opt.track, 1, 10);
  if (mode_ == AppMode::Track) {
    if (!loadTrack(track_, error)) return false;
    if (opt.drive) toggleDrive();
  } else if (mode_ == AppMode::Model) {
    loadModel(shapeIdx_);
  } else {
    loadSprite(spriteIdx_);
  }
  if (opt.haveCam) {
    cam_.pos[0] = opt.cam[0]; cam_.pos[1] = opt.cam[1]; cam_.pos[2] = opt.cam[2];
    cam_.yaw = opt.camYaw; cam_.pitch = opt.camPitch;
  }
  return true;
}

bool ViewerApp::loadTrack(int idx, std::string* err) {
  auto sc = std::make_unique<Scene>();
  if (!buildScene(*data_, idx, sc.get(), err, opt_.shipScale)) return false;
  scene_ = std::move(sc);
  doors_.build(*scene_);
  track_ = idx;
  const auto& st = scene_->startPos;
  double hx = st[0][0] - st[1][0], hz = st[0][2] - st[1][2];
  double yaw = std::atan2(hx, hz);
  for (int i = 0; i < 10; ++i) {
    grid_[size_t(i)] = ShipState{st[size_t(i)][0], st[size_t(i)][1], st[size_t(i)][2], yaw, 0, 0, i};
  }
  driving_ = false;
  player_ = grid_[size_t(std::clamp(opt_.ship, 0, 9))];
  placeCameraAtStart();
  status_ = std::string("Track ") + trackBaseNames()[size_t(idx - 1)] + " (" + trackDisplayNames()[size_t(idx - 1)] + ")";
  return true;
}

void ViewerApp::placeCameraAtStart() {
  const ShipState& s = grid_[0];
  cam_.pos[0] = s.x - std::sin(s.yaw) * 130000;
  cam_.pos[1] = s.y + 45000;
  cam_.pos[2] = s.z - std::cos(s.yaw) * 130000;
  cam_.yaw = float(s.yaw);
  cam_.pitch = -0.15f;
}

void ViewerApp::loadModel(int idx) {
  if (shapes_.empty()) return;
  shapeIdx_ = (idx % int(shapes_.size()) + int(shapes_.size())) % int(shapes_.size());
  auto sc = std::make_unique<Scene>();
  std::string err;
  if (!buildShapePreview(*data_, shapes_[size_t(shapeIdx_)], sc.get(), &err)) {
    status_ = err;
    scene_.reset();
    return;
  }
  scene_ = std::move(sc);
  double mn[3] = {1e30, 1e30, 1e30}, mx[3] = {-1e30, -1e30, -1e30};
  for (auto& v : scene_->track.verts) {
    mn[0] = std::min<double>(mn[0], v.x); mx[0] = std::max<double>(mx[0], v.x);
    mn[1] = std::min<double>(mn[1], v.y); mx[1] = std::max<double>(mx[1], v.y);
    mn[2] = std::min<double>(mn[2], v.z); mx[2] = std::max<double>(mx[2], v.z);
  }
  if (scene_->track.verts.empty()) { mn[0]=mn[1]=mn[2]=-1; mx[0]=mx[1]=mx[2]=1; }
  double r = 0;
  for (int k = 0; k < 3; ++k) { modelCenter_[k] = (mn[k] + mx[k]) * 0.5; r = std::max(r, (mx[k] - mn[k]) * 0.5); }
  orbitDist_ = std::max(r, 100.0) * 2.1;
  cam_.nearPlane = float(std::max(5.0, r * 0.02));
  status_ = "Shape " + shapes_[size_t(shapeIdx_)] + " (" + std::to_string(scene_->track.polys.size()) + " polys)";
}

void ViewerApp::loadSprite(int idx) {
  if (sprites_.empty()) return;
  spriteIdx_ = (idx % int(sprites_.size()) + int(sprites_.size())) % int(sprites_.size());
  auto b = data_->read(sprites_[size_t(spriteIdx_)]);
  if (!b) return;
  auto s = parseSprite(*b);
  if (!s) { status_ = "bad sprite"; return; }
  sprite_ = *s;
  if (s->palette) {
    spritePal_ = *s->palette;
  } else {
    auto pb = data_->read("CHICAGO.PAL");
    if (pb) if (auto p = parsePalette(pb->data(), pb->size())) spritePal_ = *p;
  }
  fillDefaultTail(spritePal_);
  status_ = "Sprite " + sprites_[size_t(spriteIdx_)] + " " + std::to_string(s->w) + "x" + std::to_string(s->h) +
            (s->palette ? " (embedded palette)" : " (CHICAGO palette)");
}

void ViewerApp::nextItem(int d) {
  if (mode_ == AppMode::Track) {
    int t = (track_ - 1 + d + 10) % 10 + 1;
    std::string e;
    if (!loadTrack(t, &e)) status_ = e;
  } else if (mode_ == AppMode::Model) {
    loadModel(shapeIdx_ + d);
  } else {
    loadSprite(spriteIdx_ + d);
  }
}

void ViewerApp::setMode(AppMode m) {
  if (m == mode_) return;
  mode_ = m;
  std::string e;
  if (m == AppMode::Track) { if (!loadTrack(track_, &e)) status_ = e; }
  else if (m == AppMode::Model) loadModel(shapeIdx_);
  else loadSprite(spriteIdx_);
}

void ViewerApp::selectShip(int i) {
  opt_.ship = std::clamp(i, 0, 9);
  if (driving_) { driving_ = false; toggleDrive(); }
}

void ViewerApp::toggleDrive() {
  if (mode_ != AppMode::Track || !scene_) return;
  driving_ = !driving_;
  cullOverride_ = -1;
  if (driving_) {
    player_ = grid_[size_t(std::clamp(opt_.ship, 0, 9))];
    player_.speed = 0;
    setShipBoxFromMesh(player_, scene_->shipMeshes[size_t(std::clamp(opt_.ship, 0, 9))]);
    simAccum_ = 0;
  } else {
    placeCameraAtStart();
  }
}

void ViewerApp::update(double dt, const InputState& in) {
  if (dt > 0) fpsAvg_ = fpsAvg_ * 0.9 + (1.0 / dt) * 0.1;
  if (dt > 0 && dt < 1.0) animSeconds_ += dt;
  if (dt > 0 && dt < 1.0 && mode_ == AppMode::Track) doors_.step(dt);
  if (mode_ == AppMode::Track && scene_) {
    if (driving_) {
      simAccum_ += dt;
      const double step = 1.0 / 120.0;
      int guard = 0;
      while (simAccum_ >= step && guard++ < 16) {
        stepShip(player_, ShipInput{in.throttle, in.brake, in.steer, in.pitch}, step, params_[size_t(player_.ship)], *scene_, simCfg_);
        simAccum_ -= step;
      }
      // chase camera
      double behind = 110000, above = 28000;
      double tx = player_.x - std::sin(player_.yaw) * behind, tz = player_.z - std::cos(player_.yaw) * behind;
      double k = std::min(1.0, 8.0 * dt);
      cam_.pos[0] += (tx - cam_.pos[0]) * k;
      cam_.pos[1] += (player_.y + above - cam_.pos[1]) * k;
      cam_.pos[2] += (tz - cam_.pos[2]) * k;
      double dyaw = player_.yaw - cam_.yaw;
      while (dyaw > kPi) dyaw -= 2 * kPi;
      while (dyaw < -kPi) dyaw += 2 * kPi;
      cam_.yaw += float(dyaw * k);
      cam_.pitch = -0.22f;
    } else {
      cam_.yaw += in.lookDX;
      cam_.pitch = std::clamp(cam_.pitch - in.lookDY, -1.5f, 1.5f);
      double speed = (in.fast ? 3.0e6 : 5.0e5);
      float cp = std::cos(cam_.pitch), sp = std::sin(cam_.pitch), cy = std::cos(cam_.yaw), sy = std::sin(cam_.yaw);
      double f[3] = {sy * cp, sp, cy * cp}, r[3] = {cy, 0, -sy};
      for (int k = 0; k < 3; ++k) cam_.pos[k] += (f[k] * in.moveForward + r[k] * in.moveRight) * speed * dt;
      cam_.pos[1] += in.moveUp * speed * dt;
    }
  } else if (mode_ == AppMode::Model) {
    orbitYaw_ += in.lookDX;
    orbitPitch_ = std::clamp(orbitPitch_ + in.lookDY, -1.4, 1.4);
    orbitDist_ *= std::pow(0.5, double(in.moveForward) * dt * 2.0);
    cam_.pos[0] = modelCenter_[0] - std::sin(orbitYaw_) * std::cos(orbitPitch_) * orbitDist_;
    cam_.pos[1] = modelCenter_[1] + std::sin(orbitPitch_) * orbitDist_;
    cam_.pos[2] = modelCenter_[2] - std::cos(orbitYaw_) * std::cos(orbitPitch_) * orbitDist_;
    cam_.yaw = float(orbitYaw_);
    cam_.pitch = float(-orbitPitch_);
  }
}

void ViewerApp::drawSprite() {
  uint32_t* fb = renderer_.framebuffer();
  int W = renderer_.width(), H = renderer_.height();
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) fb[size_t(y) * size_t(W) + size_t(x)] = ((x / 16 + y / 16) & 1) ? 0xff303030u : 0xff383838u;
  if (sprite_.w <= 0) return;
  int scale = std::max(1, std::min(W / sprite_.w, H / sprite_.h));
  int ox = (W - sprite_.w * scale) / 2, oy = (H - sprite_.h * scale) / 2;
  for (int y = 0; y < sprite_.h * scale; ++y) {
    int py = oy + y;
    if (py < 0 || py >= H) continue;
    for (int x = 0; x < sprite_.w * scale; ++x) {
      int px = ox + x;
      if (px < 0 || px >= W) continue;
      uint8_t idx = sprite_.pixels[size_t(y / scale) * size_t(sprite_.w) + size_t(x / scale)];
      if (idx == 0) continue;  // index 0 transparent (INFERRED)
      fb[size_t(py) * size_t(W) + size_t(px)] = 0xff000000u | spritePal_.rgba[idx];
    }
  }
}

void ViewerApp::render() {
  if (mode_ == AppMode::Sprite) {
    renderer_.beginFrame(cam_, 0, 0);
    drawSprite();
    return;
  }
  uint32_t sky = 0xff5a7fa8u, ground = 0xff2a2a2eu;
  if (mode_ == AppMode::Model) { sky = ground = 0xff20242cu; }
  renderer_.beginFrame(cam_, sky, ground);
  if (!scene_) return;
  // The original always culls back-facing track polygons (0x3948C, 0x193FF); legacy mode only culls while driving.
  if (std::getenv("SLIP_NOCULL")) cullOverride_ = 0;
  renderer_.cullBackfaces = cullOverride_ >= 0 ? cullOverride_ != 0 : (mode_ == AppMode::Track && (painter_ || driving_));
  renderer_.shadows = !std::getenv("SLIP_NOSHADOW");
  renderer_.animTimer = uint32_t(animSeconds_ * 16384.0);
  renderer_.portalCulling = useVisMask_ && !std::getenv("SLIP_NOPORTAL");
  if (mode_ == AppMode::Track) renderer_.computePortalVisibility(*scene_); else renderer_.portalCulling = false;
  renderer_.visMask = (mode_ == AppMode::Track && useVisMask_) ? scene_->visMaskAt(cam_.pos[0], cam_.pos[1], cam_.pos[2]) : 0xFFFF;
  MeshTransform xf;
  if (mode_ == AppMode::Track) buildShadowCasters(); else renderer_.shadowCasters.clear();
  if (mode_ == AppMode::Track) {
    xf.pos[0] = scene_->origin[0]; xf.pos[1] = scene_->origin[1]; xf.pos[2] = scene_->origin[2];
    if (painter_ && !scene_->bsp.empty()) { renderTrackPainter(xf); return; }
    renderer_.drawMesh(*scene_, scene_->track, xf);
    // Camera-facing scenery: orientation is a yaw towards the camera (CONFIRMED 0x379C9: rows (b,0,-a),(0,1,0),(a,0,b)
    // applied as v*M, with (a,b) = normalised (object - camera) in x/z). Stored here as R = M^T.
    for (const Billboard& bb : scene_->billboards) {
      if (!renderer_.visAllows(bb.vis)) continue;
      MeshTransform bx;
      for (int k = 0; k < 3; ++k) bx.pos[k] = scene_->origin[size_t(k)] + double(k == 0 ? bb.pos.x : k == 1 ? bb.pos.y : bb.pos.z);
      if (!renderer_.sceneryBigEnough(bx.pos, bb.radius)) continue;
      double dx = bx.pos[0] - cam_.pos[0], dz = bx.pos[2] - cam_.pos[2];
      double len = std::sqrt(dx * dx + dz * dz);
      float a = len > 1e-3 ? float(dx / len) : 0.0f, b = len > 1e-3 ? float(dz / len) : 1.0f;
      const float R[9] = {b, 0, a, 0, 1, 0, -a, 0, b};
      for (int k = 0; k < 9; ++k) bx.R[k] = R[k];
      renderer_.drawMesh(*scene_, bb.mesh, bx);
    }
    for (int i = 0; i < 10; ++i) {
      const ShipState& s = (driving_ && i == player_.ship) ? player_ : grid_[size_t(i)];
      if (scene_->shipMeshes[size_t(i)].polys.empty()) continue;
      MeshTransform sx;
      sx.pos[0] = s.x; sx.pos[1] = s.y; sx.pos[2] = s.z;
      shipRenderMatrix(s, sx.R);  // ART models face +z (smok/fan1 reference points are at -z)
      renderer_.drawMesh(*scene_, scene_->shipMeshes[size_t(i)], sx);
    }
    for (const Door& d : doors_.list) {
      MeshTransform dx;
      for (int k = 0; k < 3; ++k) dx.pos[k] = d.pos[k];
      renderer_.drawMesh(*scene_, d.mesh, dx);
    }
  } else {
    renderer_.drawMesh(*scene_, scene_->track, xf);
  }
}

// Track draw in the original's order (CONFIRMED structure, docs/research-log.md). The frame (0x3924F) is:
//  1. portal walk from the camera piece (0x39C58) -> windows of the reached pieces, union window;
//  2. unless the track is portal-only (TRK +0x9E): a plain far-to-near BSP traversal (0x3AA60 -> 0x375E3 with the
//     default callbacks) that draws EVERY frustum-visible piece and scenery item inside the union window;
//  3. the collected list (0x3A491): reached pieces in their own windows, scenery only from the first leaf (far-to-near)
//     that holds a reached piece (flag [0x33EEC]), and the ships standing on reached pieces (drawn with their piece).
// Items are painted over each other; only inside an item is depth tested.
void ViewerApp::renderTrackPainter(const MeshTransform& xf) {
  const Scene& sc = *scene_;
  const auto& win = renderer_.pieceWindows();
  const bool havePortals = !win.empty();
  std::vector<int> order;
  sc.bspOrder(cam_.pos, &order);

  struct Item { int kind; size_t idx; double dist; };  // 0 piece, 1 scenery instance, 2 billboard
  std::vector<std::vector<Item>> byGroup(size_t(std::max(sc.groupCount, 1)));
  const double cx = cam_.pos[0] - sc.origin[0], cy = cam_.pos[1] - sc.origin[1], cz = cam_.pos[2] - sc.origin[2];
  auto dist = [&](double x, double y, double z) { return std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy) + (z - cz) * (z - cz)); };
  for (size_t i = 0; i < sc.pieceBoxes.size(); ++i) {
    const auto& b = sc.pieceBoxes[i];
    if (b.empty || sc.piecePolys[i].empty()) continue;
    int g = sc.pieceGroup[i];
    if (g < 0 || size_t(g) >= byGroup.size()) continue;
    byGroup[size_t(g)].push_back({0, i, dist((b.lo[0] + b.hi[0]) * 0.5, (b.lo[1] + b.hi[1]) * 0.5, (b.lo[2] + b.hi[2]) * 0.5)});
  }
  for (size_t i = 0; i < sc.track.instances.size(); ++i) {
    const auto& in = sc.track.instances[i];
    if (in.group < 0 || size_t(in.group) >= byGroup.size() || sc.instPolys[i].empty()) continue;
    byGroup[size_t(in.group)].push_back({1, i, dist(in.center.x, in.center.y, in.center.z)});
  }
  for (size_t i = 0; i < sc.billboards.size(); ++i) {
    const auto& bb = sc.billboards[i];
    if (bb.group < 0 || size_t(bb.group) >= byGroup.size()) continue;
    byGroup[size_t(bb.group)].push_back({2, i, dist(bb.pos.x, bb.pos.y, bb.pos.z)});
  }
  // order inside each leaf: the group's draw-order tree (far side first); items it does not list stay in front, by distance
  for (size_t g = 0; g < byGroup.size(); ++g) {
    auto& items = byGroup[g];
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.dist > b.dist; });
    std::vector<Scene::ItemRef> tord;
    sc.groupOrder(int(g), cam_.pos, &tord);
    std::vector<Item> ordered, listed;
    std::vector<uint8_t> used(items.size(), 0);
    for (const auto& r : tord)
      for (size_t k = 0; k < items.size(); ++k)
        if (!used[k] && items[k].kind == r.kind && items[k].idx == r.idx) { used[k] = 1; listed.push_back(items[k]); break; }
    for (size_t k = 0; k < items.size(); ++k) if (!used[k]) ordered.push_back(items[k]);
    ordered.insert(ordered.end(), listed.begin(), listed.end());
    items = std::move(ordered);
  }
  // ship -> piece it stands on (smallest containing portal-graph piece)
  std::vector<std::vector<int>> shipsOnPiece(sc.pieceBoxes.size());
  std::vector<int> looseShips;
  for (int i = 0; i < 10; ++i) {
    if (sc.shipMeshes[size_t(i)].polys.empty()) continue;
    const ShipState& s = (driving_ && i == player_.ship) ? player_ : grid_[size_t(i)];
    const float p[3] = {float(s.x - sc.origin[0]), float(s.y - sc.origin[1]), float(s.z - sc.origin[2])};
    int best = -1;
    double bestVol = 1e300;
    for (size_t k = 0; k < sc.pieceBoxes.size(); ++k) {
      const auto& b = sc.pieceBoxes[k];
      if (!b.graph || !sc.pieceContains(k, p)) continue;
      double vol = double(b.hi[0] - b.lo[0]) * double(b.hi[1] - b.lo[1]) * double(b.hi[2] - b.lo[2]);
      if (vol < bestVol) { bestVol = vol; best = int(k); }
    }
    if (best >= 0) shipsOnPiece[size_t(best)].push_back(i); else looseShips.push_back(i);
  }

  int itemId = 0;
  auto drawShip = [&](int i, int item) {
    const ShipState& s = (driving_ && i == player_.ship) ? player_ : grid_[size_t(i)];
    MeshTransform sx;
    sx.pos[0] = s.x; sx.pos[1] = s.y; sx.pos[2] = s.z;
    shipRenderMatrix(s, sx.R);  // ART models face +z (smok/fan1 reference points are at -z)
    renderer_.drawMesh(sc, sc.shipMeshes[size_t(i)], sx, nullptr, item);
  };
  auto drawDoor = [&](const Door& d, int item) {
    MeshTransform dx;
    for (int k = 0; k < 3; ++k) dx.pos[k] = d.pos[k];
    renderer_.drawMesh(sc, d.mesh, dx, nullptr, item);
  };
  auto drawScenery = [&](const Item& it) {
    if (it.kind == 1) {
      const auto& in = sc.track.instances[it.idx];
      const double c[3] = {sc.origin[0] + in.center.x, sc.origin[1] + in.center.y, sc.origin[2] + in.center.z};
      if (!renderer_.sceneryBigEnough(c, in.radius)) return;
      renderer_.drawMesh(sc, sc.track, xf, &sc.instPolys[it.idx], itemId++);
    } else {
      const Billboard& bb = sc.billboards[it.idx];
      if (!renderer_.visAllows(bb.vis)) return;
      MeshTransform bx;
      for (int k = 0; k < 3; ++k) bx.pos[k] = sc.origin[size_t(k)] + double(k == 0 ? bb.pos.x : k == 1 ? bb.pos.y : bb.pos.z);
      if (!renderer_.sceneryBigEnough(bx.pos, bb.radius)) return;
      double dx = bx.pos[0] - cam_.pos[0], dz = bx.pos[2] - cam_.pos[2];
      double len = std::sqrt(dx * dx + dz * dz);
      float a = len > 1e-3 ? float(dx / len) : 0.0f, b = len > 1e-3 ? float(dz / len) : 1.0f;
      const float R[9] = {b, 0, a, 0, 1, 0, -a, 0, b};
      for (int k = 0; k < 9; ++k) bx.R[k] = R[k];
      renderer_.drawMesh(sc, bb.mesh, bx, nullptr, itemId++);
    }
  };

  const auto uni = renderer_.unionWindow();
  const bool twoPass = havePortals && !sc.portalOnly && uni.vis;
  if (twoPass) {
    // pass 2 of the frame: everything in the frustum, clipped to the union window of the reached pieces
    std::vector<SoftwareRenderer::WinRect> saved = win;
    for (int g : order) {
      for (const Item& it : byGroup[size_t(g)]) {
        if (it.kind == 0) {
          const auto& pb = sc.pieceBoxes[it.idx];
          if (!renderer_.boxInFrustum(sc, pb.lo, pb.hi)) continue;
          renderer_.setPieceWindow(it.idx, uni);
          renderer_.drawMesh(sc, sc.track, xf, &sc.piecePolys[it.idx], itemId++);
        } else {
          renderer_.setSceneryWindow(&uni);
          drawScenery(it);
          renderer_.setSceneryWindow(nullptr);
        }
      }
    }
    for (size_t i = 0; i < saved.size(); ++i) renderer_.setPieceWindow(i, saved[i]);
  }

  bool allowed = !havePortals || showAllScenery_;  // [0x33EEC]
  for (int g : order) {
    auto& items = byGroup[size_t(g)];
    if (!allowed && havePortals)
      for (const Item& it : items)
        if (it.kind == 0 && sc.pieceBoxes[it.idx].graph && win[it.idx].vis) allowed = true;
    for (const Item& it : items) {
      if (it.kind == 0) {
        const auto& pb = sc.pieceBoxes[it.idx];
        const bool reached = !havePortals || (pb.graph ? win[it.idx].vis : allowed);
        if (!reached) continue;
        const int item = itemId++;
        renderer_.drawMesh(sc, sc.track, xf, &sc.piecePolys[it.idx], item);
        for (int sh : shipsOnPiece[it.idx]) drawShip(sh, item);  // entities of a piece are drawn with it (0x39B9C)
        for (const Door& d : doors_.list) if (d.piece == int(it.idx)) drawDoor(d, item);
      } else if (allowed) {
        drawScenery(it);
      }
    }
  }
  for (int i : looseShips) drawShip(i, itemId++);
  // ships on pieces that were not drawn this frame are not drawn (slot draw only runs for visited pieces)
  if (!havePortals)
    for (size_t k = 0; k < shipsOnPiece.size(); ++k)
      for (int sh : shipsOnPiece[k]) drawShip(sh, itemId++);
}

// Ships cast shadows onto up-facing track polygons of the piece they stand on and its neighbours (0x397B1, 0x3977A).
void ViewerApp::buildShadowCasters() {
  renderer_.shadowCasters.clear();
  if (!scene_) return;
  const Scene& sc = *scene_;
  for (int i = 0; i < 10; ++i) {
    if (sc.shipMeshes[size_t(i)].polys.empty()) continue;
    const ShipState& s = (driving_ && i == player_.ship) ? player_ : grid_[size_t(i)];
    ShadowCaster c;
    c.mesh = &sc.shipMeshes[size_t(i)];
    c.xf.pos[0] = s.x; c.xf.pos[1] = s.y; c.xf.pos[2] = s.z;
    shipRenderMatrix(s, c.xf.R);
    const float p[3] = {float(s.x - sc.origin[0]), float(s.y - sc.origin[1]), float(s.z - sc.origin[2])};
    double bestVol = 1e300;
    for (size_t k = 0; k < sc.pieceBoxes.size(); ++k) {
      const auto& b = sc.pieceBoxes[k];
      if (!b.graph || !sc.pieceContains(k, p)) continue;
      const double vol = double(b.hi[0] - b.lo[0]) * double(b.hi[1] - b.lo[1]) * double(b.hi[2] - b.lo[2]);
      if (vol < bestVol) { bestVol = vol; c.piece = int(k); }
    }
    renderer_.shadowCasters.push_back(c);
  }
}

std::vector<std::string> ViewerApp::hudLines() const {
  char buf[200];
  std::vector<std::string> l;
  l.push_back(status_);
  const auto& st = renderer_.stats();
  std::snprintf(buf, sizeof buf, "%.0f fps  polys %u/%u  tris %u", fpsAvg_, st.polysDrawn, st.polysSubmitted, st.trisRastered);
  l.push_back(buf);
  if (mode_ == AppMode::Track) {
    std::snprintf(buf, sizeof buf, "CAM pos %.0f %.0f %.0f  yaw %.3f pitch %.3f  --cam %.0f,%.0f,%.0f,%.3f,%.3f", cam_.pos[0], cam_.pos[1], cam_.pos[2], cam_.yaw, cam_.pitch,
                  cam_.pos[0], cam_.pos[1], cam_.pos[2], cam_.yaw, cam_.pitch);
    l.push_back(buf);
    std::snprintf(buf, sizeof buf, "visibility mask 0x%02X %s (F5 toggles)", renderer_.visMask & 0xFFFF, useVisMask_ ? "on" : "off");
    l.push_back(buf);
    std::snprintf(buf, sizeof buf, "draw order: %s (F6)  scenery: %s (F7)  culling: %s (Tab)",
                  painter_ ? "painter/BSP" : "z-buffer", showAllScenery_ ? "all" : "original cull",
                  renderer_.cullBackfaces ? "on" : "off");
    l.push_back(buf);
    if (driving_) {
      std::snprintf(buf, sizeof buf, "DRIVE ship %d  speed %.0f u/s  pos %.0f %.0f %.0f  hits %d", player_.ship, player_.speed, player_.x, player_.y, player_.z, player_.hits);
      l.push_back(buf);
      l.push_back("W/S throttle/brake, A/D steer, E/Q nose up/down, Space = free cam");
      l.push_back(simCfg_.assist ? "hover assist ON (F8): legacy floor following, no collision" : "original-style flight model (F8 = hover assist): manual pitch, polygon collision");
    } else {
      l.push_back("WASD move, Q/E down/up, mouse look, Shift fast | [ ] track | Space drive | 1-0 ship | F2 models F3 sprites | F4 or Cmd+C copy debug | Esc quit");
    }
  } else if (mode_ == AppMode::Model) {
    l.push_back("[ ] prev/next shape | mouse orbit, W/S zoom | F1 track F3 sprites | Esc quit");
  } else {
    l.push_back("[ ] prev/next sprite | F1 track F2 models | Esc quit");
  }
  return l;
}

}  // namespace slip
