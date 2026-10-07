#include "platform/viewer_app.hpp"

#include <algorithm>
#include <cctype>
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
  {
    std::string warn;
    audio_.init(*data_, opt.audio, &warn);
    if (!warn.empty()) std::fputs(warn.c_str(), stderr);
  }
  params_ = loadShipParams(*data_);
  aiTables_ = loadAiTables(*data_);
  weaponTable_ = loadWeaponTable(*data_);
  refPoints_ = loadShipRefPoints(*data_);
  {
    std::string spec = opt.weapons, perr;
    if (spec.empty() || spec == "default") playerLoadout_ = defaultPlayerLoadout();
    else if (spec == "none") playerLoadout_ = Loadout{};
    else if (!parseLoadoutOption(spec, &playerLoadout_, &perr)) {
      if (error) *error = "--weapons: " + perr;
      return false;
    }
    auto load = [&](const std::string& name, Sprite* dst) {
      if (auto b = data_->read(name)) if (auto sp = parseSprite(*b)) *dst = *sp;
    };
    for (size_t i = 0; i < 6; ++i) { load("BONUS" + std::to_string(i) + ".SPR", &bonusSprites_[i]); load("EXPL" + std::to_string(i + 1) + ".SPR", &explSprites_[i]); }
    for (size_t i = 0; i < 4; ++i) load("FIRE" + std::to_string(i + 1) + ".SPR", &fireSprites_[i]);
  }
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
  cockpit_ = opt.cockpit;
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
  playTrackMusic();
  status_ = std::string("Track ") + trackBaseNames()[size_t(idx - 1)] + " (" + trackDisplayNames()[size_t(idx - 1)] + ")";
  return true;
}

void ViewerApp::playTrackMusic() {
  if (opt_.noMusic || !audio_.musicReady()) return;
  if (opt_.music.empty() && !audio_.currentMusic().empty()) return;  // keep the running song across track changes
  std::string name = opt_.music;
  if (name.empty()) {  // PlayTrackIntro 0x57A79 / DoGame3D 0x586F2 pick one of INGAME2/3/4 at random (6 only under low memory)
    static const char* kSongs[] = {"INGAME2.HMP", "INGAME3.HMP", "INGAME4.HMP"};
    name = kSongs[(unsigned(std::rand()) >> 4) % 3];
  }
  audio_.playMusic(name, true);
}

int ViewerApp::ambientForPlayer() const {  // 0x58CB6..0x58CED
  if (!scene_) return 0;
  const float p[3] = {float(player_.x - scene_->origin[0]), float(player_.y - scene_->origin[1]), float(player_.z - scene_->origin[2])};
  int best = -1;
  double bestVol = 1e300;
  for (size_t k = 0; k < scene_->pieceBoxes.size(); ++k) {
    const auto& b = scene_->pieceBoxes[k];
    if (!b.graph || !scene_->pieceContains(k, p, 512.0f)) continue;
    const double vol = double(b.hi[0] - b.lo[0]) * double(b.hi[1] - b.lo[1]) * double(b.hi[2] - b.lo[2]);
    if (vol < bestVol) { bestVol = vol; best = int(k); }
  }
  if (best < 0) return 0;
  if (best == scene_->track_data.refuelPiece) return 1;  // TrackSlotCheckRefuel: in the refuel piece -> PITSLP
  const auto& pc = scene_->track_data.pieces[size_t(best)];
  const std::string& nm = scene_->track_data.records[size_t(pc.record)].name;  // TrackSlotGetName: CROW* / GRID* -> CROWDLP
  auto starts = [&](const char* pre) { for (int i = 0; i < 4; ++i) if (i >= int(nm.size()) || std::toupper(static_cast<unsigned char>(nm[size_t(i)])) != pre[i]) return false; return true; };
  return starts("CROW") || starts("GRID") ? 2 : 0;
}

void ViewerApp::setupCombat() {
  std::vector<PickupSpot> spots;
  if (opt_.pickups) spots = loadPickupSpots(*data_, track_);
  combat_.init(weaponTable_, refPoints_, track_, spots, unsigned(std::rand()) | 1u);
  for (int k = 0; k < kWeaponCount; ++k) {
    const int mi = Scene::weaponMeshIndex(k);
    if (mi >= 0 && scene_->weaponMeshRadius[size_t(mi)] > 0) combat_.setProjectileRadius(k, std::max(500.0f, scene_->weaponMeshRadius[size_t(mi)]));
  }
  for (int i = 0; i < 10; ++i) combat_.setLoadout(i, i == player_.ship ? playerLoadout_ : (opt_.aiWeapons ? aiLoadout(i) : Loadout{}));
  lastLap_ = 0;
  resultCueTimer_ = -1;
  cyclePending_ = false;
}

void ViewerApp::stepCombat(double step, const InputState& in, bool held) {
  CombatContext cc;
  cc.scene = scene_.get();
  cc.humanShip = player_.ship;
  cc.controls.assign(10, CombatControls{});
  for (int i = 0; i < 10; ++i) {
    cc.ships.push_back(i == player_.ship ? &player_ : &grid_[size_t(i)]);
    cc.human.push_back(i == player_.ship);
    cc.finished.push_back(held || !aiEnabled_ || ai_[size_t(i)].lap >= opt_.laps);  // the AI holds its fire on the grid and after the finish
    cc.shipClass.push_back(i + 1);
  }
  if (!held) {
    cc.controls[size_t(player_.ship)].fire = in.fire;
    cc.controls[size_t(player_.ship)].cycle = cyclePending_;
  }
  cyclePending_ = false;
  combat_.step(cc, step);
}

std::string ViewerApp::combatLine() const {
  const CombatState& c = combat_.combat[size_t(player_.ship)];
  const auto& tb = combat_.table();
  auto name = [&](int id) { return id >= 0 ? tb.w[size_t(id)].name : std::string("-"); };
  auto mark = [&](int slot) { return c.selected == slot ? '>' : ' '; };
  char buf[400];
  std::snprintf(buf, sizeof buf, "%cBLASTER %3.0f%%  %cA %s x%d %3.0f%%  %cB %s x%d %3.0f%%  %cBOOST %s %3.0f%%%s  lock %s", mark(0), c.energy[0] * 100,
                mark(1), name(c.load.weaponA).c_str(), c.load.ammoA, c.energy[1] * 100, mark(2), name(c.load.weaponB).c_str(), c.load.ammoB, c.energy[2] * 100,
                mark(3), c.load.booster >= 0 ? tb.boosters[size_t(c.load.booster)].name.c_str() : "-", c.boosterFuel * 100, player_.boosterOn ? " ON" : "",
                c.lockTarget >= 0 ? std::to_string(c.lockTarget).c_str() : "-");
  std::string out = buf;
  if (player_.reverseTime > 0) out += "  REVERSED " + std::to_string(int(std::ceil(player_.reverseTime))) + "s";
  if (player_.halfCapTime > 0) out += "  SLOWED " + std::to_string(int(std::ceil(player_.halfCapTime))) + "s";
  if (player_.hyperTime > 0) out += "  OVERSTEER " + std::to_string(int(std::ceil(player_.hyperTime))) + "s";
  if (player_.forceThrottleTime > 0) out += "  THROTTLE JAMMED " + std::to_string(int(std::ceil(player_.forceThrottleTime))) + "s";
  if (player_.boosterFreeTime > 0) out += "  FREE BOOST " + std::to_string(int(std::ceil(player_.boosterFreeTime))) + "s";
  return out;
}

void ViewerApp::drainSounds(const double listener[3]) {
  static const bool log = std::getenv("SLIP_COMBAT_LOG") != nullptr;  // test hook: print every effect / cue
  for (const CombatEvent& e : combat_.events) {
    if (log) std::fprintf(stderr, "combat %s %d ship %d\n", e.kind == CombatEvent::Fx ? "fx" : "cue", e.id, e.ship);
    if (e.kind == CombatEvent::Fx) {
      if (e.id >= 1 && e.id <= 16) audio_.playFxAt(Fx(e.id), e.pos, listener, driving_ && e.ship == player_.ship);
    } else if (opt_.voices) {
      audio_.playCue(e.id);
    }
  }
  combat_.events.clear();
  if (driving_ && player_.sfxOverDamage > 0) {  // 0x52122..0x5213A: the human pilot's ship is breaking up: cue 2 or 3
    player_.sfxOverDamage = 0;
    if (opt_.voices) audio_.playCue((std::rand() & 1) ? cues::shipBreaking1 : cues::shipBreaking2);
  }
  for (int i = 0; i < 10; ++i) grid_[size_t(i)].sfxOverDamage = 0;
  for (int i = 0; i < 10; ++i) {
    ShipState& s = (driving_ && i == player_.ship) ? player_ : grid_[size_t(i)];
    const bool own = driving_ && i == player_.ship;
    const double pos[3] = {s.x, s.y, s.z};
    for (; s.sfxWallLight > 0; --s.sfxWallLight) audio_.playFxAt(Fx::Scrape1, pos, listener, own);
    for (; s.sfxWallHard > 0; --s.sfxWallHard) audio_.playFxAt(Fx::Scrape2, pos, listener, own);
    for (; s.sfxWater > 0; --s.sfxWater) audio_.playFxAt(Fx::WaterHit, pos, listener, own);
    for (; s.sfxContact > 0; --s.sfxContact) audio_.playFxAt(Fx::Explosion, pos, listener, own);
    for (; s.sfxWreck > 0; --s.sfxWreck) audio_.playFxAt(Fx::Crash, pos, listener, own);
  }
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
    setShipBoxFromMesh(player_, scene_->shipMeshes[size_t(std::clamp(opt_.ship, 0, 9))], 1.0 / scene_->shipScale);
    for (int i = 0; i < 10; ++i) { setShipBoxFromMesh(grid_[size_t(i)], scene_->shipMeshes[size_t(i)], 1.0 / scene_->shipScale); grid_[size_t(i)].speed = 0; ai_[size_t(i)] = AiState{}; ai_[size_t(i)].startRank = i + 1;
      const int tier = aiTierForStartRank(i + 1), trk = std::clamp(scene_->trackIndex, 1, 10);
      grid_[size_t(i)].speedFactor = aiTables_.fromExecutable ? aiTables_.tierFactor[aiTables_.difficulty][trk][tier] / 16384.0 : 1.0;
    }
    player_.speedFactor = 1.0;
    if (const char* place = std::getenv("SLIP_PLACE")) {  // test hook: start at x,y,z,yaw (radians)
      double v[4];
      if (std::sscanf(place, "%lf,%lf,%lf,%lf", &v[0], &v[1], &v[2], &v[3]) == 4) {
        player_.x = v[0]; player_.y = v[1]; player_.z = v[2]; player_.yaw = v[3];
        player_.matrixInit = false;
      }
    }
    race_ = buildRaceInfo(*scene_, 10);
    simAccum_ = 0;
    audio_.engineStop(engineVoice_);
    engineVoice_ = 0;  // the engine voices are enabled one second before the start (0x59063 -> 0x4B4D0)
    countdown_ = opt_.countdown ? 5.0 : 0.0;
    countdownStage_ = opt_.countdown ? 0 : 4;
    if (!opt_.countdown) engineVoice_ = audio_.engineStart();
    lapsDone_ = 0;
    finishRank_ = 0;
    finished_ = false;
    setupCombat();
  } else {
    countdown_ = 0;
    audio_.updateAmbient(10.0, 0);
    audio_.engineStop(engineVoice_);
    engineVoice_ = 0;
    placeCameraAtStart();
  }
}

void ViewerApp::update(double dt, const InputState& in) {
  if (dt > 0) fpsAvg_ = fpsAvg_ * 0.9 + (1.0 / dt) * 0.1;
  if (dt > 0 && dt < 1.0) animSeconds_ += dt;
  if (dt > 0 && dt < 1.0 && mode_ == AppMode::Track) doors_.step(dt);
  if (mode_ == AppMode::Track && scene_) {
    if (driving_) {
      const bool held = countdown_ > 0;  // ships wait on the grid during the countdown (INFERRED: the original starts the race at 0)
      if (held && dt > 0 && dt < 1.0) {
        if (countdownStage_ == 0) { countdownStage_ = 1; audio_.playSpeech(track_, 1); }  // 0x58AF3
        countdown_ -= dt;
        if (countdownStage_ == 1 && countdown_ <= 3.0) { countdownStage_ = 2; audio_.playFx(Fx::EngineStart); }  // 0x59048: effect 0xC
        if (countdownStage_ == 2 && countdown_ <= 1.0) {  // 0x59063
          countdownStage_ = 3;
          audio_.playSpeech(track_, 2);
          engineVoice_ = audio_.engineStart();
        }
        if (countdown_ <= 0) { countdown_ = 0; countdownStage_ = 4; }
      }
      simAccum_ += dt;
      const double step = 1.0 / 120.0;
      int guard = 0;
      while (simAccum_ >= step && guard++ < 16) {
        // ships indexed by ship number; ai_[i].human marks the player
        RaceContext ctx;
        ctx.scene = scene_.get(); ctx.tables = &aiTables_; ctx.race = &race_; ctx.doors = &doors_;
        for (int i = 0; i < 10; ++i) {
          ctx.ships.push_back(i == player_.ship ? &player_ : &grid_[size_t(i)]);
          ctx.state.push_back(&ai_[size_t(i)]);
          ctx.params.push_back(&params_[size_t(i)]);
          ai_[size_t(i)].human = i == player_.ship;
        }
        std::vector<ShipState*> all;
        std::vector<std::array<double, 3>> start;
        all.push_back(&player_);
        for (int i = 0; i < 10; ++i)
          if (i != player_.ship) all.push_back(&grid_[size_t(i)]);
        for (ShipState* s : all) start.push_back({s->x, s->y, s->z});
        updateRace(ctx);
        updateWrecks(ctx);
        applyTrailingBoost(ctx, size_t(player_.ship));
        stepShip(player_, held ? ShipInput{} : ShipInput{in.throttle, in.brake, in.steer, in.pitch}, step, params_[size_t(player_.ship)], *scene_, simCfg_);
        for (size_t k = 1; k < all.size(); ++k) {
          const int id = all[k]->ship;
          ShipInput ci = !held && aiEnabled_ && !simCfg_.assist ? aiControl(ctx, size_t(id), step) : ShipInput{};
          stepShip(*all[k], ci, step, params_[size_t(id)], *scene_, simCfg_);
        }
        if (!simCfg_.assist) resolveShipPairs(all, start, step, *scene_, simCfg_);
        stepCombat(step, in, held);
        simAccum_ -= step;
      }
      {
        const double listener[3] = {player_.x, player_.y, player_.z};
        drainSounds(listener);
        audio_.engineSet(engineVoice_, player_.wrecked ? 0.0 : player_.speed);
        audio_.updateAmbient(dt, ambientForPlayer());
        // laps and finish (RaceUpdate role): the lap counter counts completed circuits of the node chain
        lapsDone_ = ai_[size_t(player_.ship)].lap;
        if (lapsDone_ > lastLap_ && countdown_ <= 0) {  // 0x5A5FE..0x5A613: crossing the lap line announces the position (EPS0..9)
          lastLap_ = lapsDone_;
          if (opt_.voices) audio_.playCue(cues::positionAnnounce(std::clamp(ai_[size_t(player_.ship)].rank, 1, 10)));
        }
        if (resultCueTimer_ > 0 && (resultCueTimer_ -= dt) <= 0 && opt_.voices) {  // results screen line (0x5A9A9..0x5A9CC)
          const int rk = finishRank_;
          audio_.playCue(rk == 1 ? (std::rand() & 1) : rk + 1);
        }
        if (!finished_ && lapsDone_ >= opt_.laps && countdown_ <= 0) {
          finished_ = true;
          finishRank_ = ai_[size_t(player_.ship)].rank;
          resultCueTimer_ = 4.0;
          if (opt_.voices) audio_.playCue(cues::finishLine(player_.ship + 1));  // 0x5A6AA..0x5A6B4 (dropped while the position line plays)
          // results screen: 0x5A8D0 plays LOSE.HMP when the finishing position is below 4th, else WIN.HMP (0x5625B)
          if (!opt_.noMusic) audio_.playMusic(finishRank_ > 3 ? "LOSE.HMP" : "WIN.HMP", false);
        }
      }
      if (cockpit_) {
        // First person view (0x44F64): camera at the ship's 'head' reference point with the ship's full orientation (incl. bank).
        const double* h = refPoints_[size_t(player_.ship)].head;
        const double* m = player_.m;
        for (int k = 0; k < 3; ++k) cam_.pos[k] = (&player_.x)[k] + m[k] * h[0] + m[3 + k] * h[1] + m[6 + k] * h[2];
        cam_.yaw = float(std::atan2(m[6], m[8]));
        cam_.pitch = float(std::asin(std::clamp(m[7], -1.0, 1.0)));
        const double cy = std::cos(cam_.yaw), sy = std::sin(cam_.yaw), cp = std::cos(cam_.pitch), sp = std::sin(cam_.pitch);
        const double r0[3] = {cy, 0, -sy}, u0[3] = {-sy * sp, cp, -cy * sp};  // unrolled basis for this yaw / pitch
        cam_.roll = float(std::atan2(m[3] * r0[0] + m[4] * r0[1] + m[5] * r0[2], m[3] * u0[0] + m[4] * u0[1] + m[5] * u0[2]));
      } else {
      cam_.roll = 0;
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
      }
    } else {
      cam_.roll = 0;
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
    if (painter_ && !scene_->bsp.empty()) { renderTrackPainter(xf); drawCombatOverlay(); return; }
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
      if (scene_->shipMeshes[size_t(i)].polys.empty() || (driving_ && cockpit_ && i == player_.ship)) continue;
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
    drawCombatOverlay();
  } else {
    renderer_.drawMesh(*scene_, scene_->track, xf);
  }
}

// Projectiles (the original's .SHP models), beams, bonus sprites, explosions and the lock marker, drawn over the finished frame.
void ViewerApp::drawCombatOverlay() {
  if (!driving_ || !scene_) return;
  const Scene& sc = *scene_;
  auto transparent = [](const Sprite& sp) { return sp.hdr8 == 0xFFFF ? -1 : int(sp.hdr8 & 0xFF); };
  for (const Projectile& p : combat_.projectiles) {
    const double* f = p.m + 6;
    if (p.kind == kBlaster || p.kind == kDisrupter) {  // 0x5C4BB: a line from the previous to the new head position (colours are placeholders)
      const double a[3] = {p.pos[0] - f[0] * 26000, p.pos[1] - f[1] * 26000, p.pos[2] - f[2] * 26000};
      renderer_.drawLineWorld(a, p.pos, p.kind == kBlaster ? 0xfffff070u : 0xff9080ffu);
      continue;
    }
    const int mi = Scene::weaponMeshIndex(p.kind);
    if (mi < 0 || sc.weaponMeshes[size_t(mi)].polys.empty()) continue;
    MeshTransform xf;
    for (int k = 0; k < 3; ++k) xf.pos[k] = p.pos[k];
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) xf.R[i * 3 + j] = float(p.m[j * 3 + i]);
    renderer_.drawMesh(sc, sc.weaponMeshes[size_t(mi)], xf);
  }
  for (const Pickup& pk : combat_.pickups) {  // bonus object: BONUS<type>.SPR billboard (size = the 0x2620 collision cube)
    const Sprite& sp = bonusSprites_[size_t(std::clamp(pk.type, 0, 5))];
    renderer_.drawSpriteWorld(sp, sp.palette ? *sp.palette : sc.palette, pk.pos, 2.0 * 0x2620, transparent(sp));
  }
  for (const Explosion& x : combat_.explosions) {
    const double u = x.age / x.life;
    const Sprite& sp = x.kind == 1 ? fireSprites_[size_t(int(x.age * 8) & 3)] : explSprites_[size_t(std::min(5, int(u * 6)))];
    renderer_.drawSpriteWorld(sp, sp.palette ? *sp.palette : sc.palette, x.pos, x.kind == 1 ? 18000 + 6000 * x.age : 16000 + 26000 * u, transparent(sp));
  }
  const CombatState& c = combat_.combat[size_t(player_.ship)];
  if (c.lockTarget >= 0) {  // lock marker
    const ShipState& t = grid_[size_t(c.lockTarget)];
    const double tp[3] = {t.x, t.y, t.z};
    float sx, sy, z;
    if (renderer_.projectToScreen(tp, &sx, &sy, &z)) {
      const int r = std::max(8, int(t.extent * 1.3 / z * (renderer_.height() * 0.5 / std::tan(cam_.fovY * 0.5))));
      renderer_.drawRectScreen(int(sx) - r, int(sy) - r, int(sx) + r, int(sy) + r, 0xffff3030u);
    }
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
    if (sc.shipMeshes[size_t(i)].polys.empty() || (driving_ && cockpit_ && i == player_.ship)) continue;  // no own ship from inside the cockpit
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
    {
      std::snprintf(buf, sizeof buf, "audio: %s | soundfont %s | music %s %s (M) | effects %s (N)", audio_.deviceOpen() ? "device" : "silent", audio_.soundfontPath().empty() ? "-" : audio_.soundfontPath().c_str(), audio_.currentMusic().empty() ? "-" : audio_.currentMusic().c_str(), audio_.musicOn() ? "on" : "off", audio_.sfxOn() ? "on" : "off");
      l.push_back(buf);
    }
    if (driving_) {
      std::snprintf(buf, sizeof buf, "DRIVE ship %d  speed %.0f u/s  pos %.0f %.0f %.0f  hits %d", player_.ship, player_.speed, player_.x, player_.y, player_.z, player_.hits);
      l.push_back(buf);
      if (countdown_ > 0) { std::snprintf(buf, sizeof buf, "GET READY  %d", int(std::ceil(countdown_))); l.push_back(buf); }
      else if (finished_) { std::snprintf(buf, sizeof buf, "FINISHED - position %d of 10", finishRank_); l.push_back(buf); }
      else { std::snprintf(buf, sizeof buf, "%s lap %d/%d  position %d", lapsDone_ == opt_.laps - 1 ? "FINAL LAP!!" : "", std::min(lapsDone_ + 1, opt_.laps), opt_.laps, ai_[size_t(player_.ship)].rank); l.push_back(buf); }
      l.push_back(combatLine());
      std::snprintf(buf, sizeof buf, "damage engine %.0f%% steering %.0f%%  credits %d  projectiles %zu  pickups %zu", player_.damageA, player_.damageB, combat_.combat[size_t(player_.ship)].credits, combat_.projectiles.size(), combat_.pickups.size());
      l.push_back(buf);
      l.push_back("W/S throttle/brake, A/D steer, E/Q nose up/down, F fire, X next weapon (booster: F switches it), V cockpit/chase view, Space = free cam");
      l.push_back(aiEnabled_ ? "AI ships racing (F9 = stop them)" : "AI off (F9)");
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
