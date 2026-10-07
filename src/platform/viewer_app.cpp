#include "platform/viewer_app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace slip {

namespace {
constexpr double kPi = 3.14159265358979323846;
void yawMatrix(double yaw, float* R) {
  float c = float(std::cos(yaw)), s = float(std::sin(yaw));
  float m[9] = {c, 0, s, 0, 1, 0, -s, 0, c};
  std::copy(m, m + 9, R);
}
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
  renderer_.cullBackfaces = driving_;  // roofs seen from outside disappear while driving
  if (driving_) {
    player_ = grid_[size_t(std::clamp(opt_.ship, 0, 9))];
    player_.speed = 0;
    simAccum_ = 0;
  } else {
    placeCameraAtStart();
  }
}

void ViewerApp::update(double dt, const InputState& in) {
  if (dt > 0) fpsAvg_ = fpsAvg_ * 0.9 + (1.0 / dt) * 0.1;
  if (mode_ == AppMode::Track && scene_) {
    if (driving_) {
      simAccum_ += dt;
      const double step = 1.0 / 120.0;
      int guard = 0;
      while (simAccum_ >= step && guard++ < 16) {
        stepShip(player_, ShipInput{in.throttle, in.brake, in.steer}, step, params_[size_t(player_.ship)], *scene_, simCfg_);
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
  renderer_.visMask = (mode_ == AppMode::Track && useVisMask_) ? scene_->visMaskAt(cam_.pos[0], cam_.pos[1], cam_.pos[2]) : 0xFFFF;
  MeshTransform xf;
  if (mode_ == AppMode::Track) {
    xf.pos[0] = scene_->origin[0]; xf.pos[1] = scene_->origin[1]; xf.pos[2] = scene_->origin[2];
    renderer_.drawMesh(*scene_, scene_->track, xf);
    for (int i = 0; i < 10; ++i) {
      const ShipState& s = (driving_ && i == player_.ship) ? player_ : grid_[size_t(i)];
      if (scene_->shipMeshes[size_t(i)].polys.empty()) continue;
      MeshTransform sx;
      sx.pos[0] = s.x; sx.pos[1] = s.y; sx.pos[2] = s.z;
      yawMatrix(s.yaw, sx.R);  // ART models face +z (smok/fan1 reference points are at -z)
      renderer_.drawMesh(*scene_, scene_->shipMeshes[size_t(i)], sx);
    }
  } else {
    renderer_.drawMesh(*scene_, scene_->track, xf);
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
    if (driving_) {
      std::snprintf(buf, sizeof buf, "DRIVE ship %d  speed %.0f u/s  pos %.0f %.0f %.0f  [W/S throttle/brake, A/D steer, Space = free cam]", player_.ship, player_.speed, player_.x, player_.y, player_.z);
      l.push_back(buf);
      l.push_back("(placeholder handling - not the original physics)");
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
