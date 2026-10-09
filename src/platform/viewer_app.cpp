#include "platform/viewer_app.hpp"

#include "game/settings_file.hpp"

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
  aiTables_.difficulty = opt.difficulty >= 0 ? opt.difficulty : readConfiguredDifficulty(*data_);
  pause_.load(*data_);
  settings_.music = opt.audio.music; settings_.sfx = opt.audio.sfx; settings_.difficulty = aiTables_.difficulty;
  weaponTable_ = loadWeaponTable(*data_);
  if (auto cfg = data_->read("SLIPSTRM.CFG"))  // config word [0x492FE] (file offset 177): Track map on / off
    if (cfg->size() > 178 && (*cfg)[0] == 'V') settings_.trackMap = ((*cfg)[177] | ((*cfg)[178] << 8)) != 0;
  {
    const SavedConfig sc = loadConfig();  // the port's own config file wins over the original's CFG; --difficulty wins over both
    if (sc.present) {
      const float m = settings_.music, f = settings_.sfx;
      settings_ = sc.settings;
      if (opt.audio.music != 0.8f) settings_.music = m;  // explicit --music-volume / --sfx-volume
      if (opt.audio.sfx != 1.0f) settings_.sfx = f;
      if (opt.difficulty < 0) aiTables_.difficulty = settings_.difficulty;
      progress_ = sc.progress;
    }
    settings_.difficulty = aiTables_.difficulty;
    applySettings();
    renderer_.shadows = settings_.shadows;
  }
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
    for (size_t i = 0; i < 6; ++i) load("BONUS" + std::to_string(i) + ".SPR", &bonusSprites_[i]);
    {  // the particle sprites (RaceBangInstallSprites 0x4FB76): Smk(Gry|Blk)[F]n, Expl[F]n, Firen
      static const char* kNames[4][2] = {{"SMKGRY", "SMKGRYF"}, {"SMKBLK", "SMKBLKF"}, {"EXPL", "EXPLF"}, {"FIRE", nullptr}};
      for (int f = 0; f < 4; ++f)
        for (int l = 0; l < 2; ++l) {
          if (!kNames[f][l]) continue;
          partSprites_[f][l].resize(size_t(kPartFrames[f][l]));
          for (int i = 0; i < kPartFrames[f][l]; ++i) load(std::string(kNames[f][l]) + std::to_string(i + 1) + ".SPR", &partSprites_[f][l][size_t(i)]);
        }
    }
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
  view_ = std::clamp(opt.view, 0, 2);
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
  netInit();
  if (opt.front && mode_ == AppMode::Track) {
    front_ = std::make_unique<FrontEnd>();
    if (front_->init(*data_, &audio_)) {
      front_->setFlyThrough(true);
      front_->setProgress(progress_, opt.unlockAll);
      front_->setSettings(&settings_);
      front_->setDefaults(track_, opt_.laps, opt.weapons == "default" ? Loadout{} : playerLoadout_);
      front_->setEconomy(&weaponTable_, aiTables_.difficulty, 750);
      front_->start(opt.skipIntro);
      frontActive_ = true;
    } else front_.reset();
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
  placeGrid();
  driving_ = false;
  player_ = grid_[size_t(std::clamp(opt_.ship, 0, 9))];
  placeCameraAtStart();
  playTrackMusic();
  status_ = std::string("Track ") + trackBaseNames()[size_t(idx - 1)] + " (" + trackDisplayNames()[size_t(idx - 1)] + ")";
  return true;
}

void ViewerApp::placeGrid() {
  const auto& st = scene_->startPos;
  const double yaw = std::atan2(scene_->startDir[0], scene_->startDir[2]);  // TRK +0x12 (RaceInitRacer 0x34C95); the grid vector 0 -> 1 gives the same angle to within a degree
  for (int i = 0; i < 10; ++i) {
    const auto& p = st[size_t(std::clamp(gridSlot_[size_t(i)], 0, 9))];
    grid_[size_t(i)] = ShipState{p[0], p[1], p[2], yaw, 0, 0, i};
  }
  player_ = grid_[size_t(std::clamp(opt_.ship, 0, 9))];
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
  combat_.init(weaponTable_, refPoints_, track_, spots, netSeed_ ? netSeed_ : replayMode_ ? raceSeed_ : (raceSeed_ = unsigned(std::rand()) | 1u));
  combat_.difficulty = aiTables_.difficulty;
  for (int k = 0; k < kWeaponCount; ++k) {
    const int mi = Scene::weaponMeshIndex(k);
    if (mi >= 0 && scene_->weaponMeshRadius[size_t(mi)] > 0) combat_.setProjectileRadius(k, std::max(500.0f, scene_->weaponMeshRadius[size_t(mi)]));
  }
  for (int i = 0; i < 10; ++i) combat_.setLoadout(i, i == player_.ship ? playerLoadout_ : (opt_.aiWeapons ? aiLoadout(i) : Loadout{}));
  lastLap_ = 0;
  resultCueTimer_ = -1;
  cyclePending_ = false;
  raceClock_ = 0;
  for (int k = 0; k < 4; ++k) for (int c = 0; c < 3; ++c) combat_.droneFrag[k][c] = scene_->droneFragPoints[size_t(k)][size_t(c)];
  combat_.classicAi = std::getenv("SLIP_CLASSIC_AI") != nullptr;  // test / purist switch: the original's weapon logic
}

void ViewerApp::stepCombat(double step, const InputState& in, bool held) {
  CombatContext cc;
  cc.scene = scene_.get();
  std::vector<ShipState> doorBoxes;
  for (size_t d = 0; d < doors_.list.size(); ++d) doorBoxes.push_back(doors_.proxy(d));
  for (const ShipState& b : doorBoxes) cc.obstacles.push_back(&b);
  cc.humanShip = player_.ship;
  if (!held) raceClock_ += step;
  if (std::getenv("SLIP_PARTICLE_DEMO") && !held && raceClock_ >= 0.5 && raceClock_ < 0.5 + step) {  // test hook: one of every effect ahead of the player
    const double* f = player_.m + 6;
    const double* r = player_.m;
    auto at = [&](double ahead, double side, double up, double* o) { for (int k = 0; k < 3; ++k) o[k] = (&player_.x)[k] + f[k] * ahead + r[k] * side + player_.m[3 + k] * up; };
    double p[3];
    at(70000, -30000, 0, p); combat_.particles.addEmitter(1, p, 2.0, 0x6fb8);
    at(70000, 0, 0, p); combat_.particles.fireball(p);
    at(70000, 30000, 0, p); combat_.particles.debris(p, 4, player_.ship);
    at(110000, -20000, 5000, p); combat_.particles.debris(p, 4, 10);
    at(110000, 20000, 0, p); combat_.particles.addEmitter(3, p, 4.0);
    at(50000, 0, 6000, p); combat_.particles.addEmitter(0, p, 5.0);
    at(45000, -12000, -3000, p); { const double n[3] = {r[0], r[1], r[2]}; combat_.scrapeEffects(p, n, 60000, false, -1); }  // sparks on a wall to the left
    at(45000, 12000, -3000, p); { const double n[3] = {0, 1, 0}; combat_.scrapeEffects(p, n, 60000, true, -1); }  // water droplets
    if (std::getenv("SLIP_PARTICLE_DEMO")[0] == '2') combat_.shipDestroyed(player_.ship, player_);  // SLIP_PARTICLE_DEMO=2: also destroy the player's craft
  }
  cc.raceTime = raceClock_;
  if (const char* v = std::getenv("SLIP_SELECT")) if (raceClock_ > 0 && raceClock_ < 0.1) combat_.combat[size_t(player_.ship)].selected = std::atoi(v);  // test hook: select weapon slot n
  if (const char* v = std::getenv("SLIP_VIEW")) if (raceClock_ > 0 && raceClock_ < 0.1) view_ = std::atoi(v);  // test hook: start in camera view n
  cc.drones = netplay_ || introMode_ ? nullptr : &drones_.targets;
  cc.controls.assign(10, CombatControls{});
  for (int i = 0; i < 10; ++i) {
    cc.ships.push_back(netplay_ && !netplay_->present(i) ? nullptr : i == player_.ship ? &player_ : &grid_[size_t(i)]);
    cc.human.push_back(i == player_.ship);
    cc.remote.push_back(netplay_ && netplay_->remote(i));
    cc.finished.push_back(held || !aiEnabled_ || ai_[size_t(i)].finished || raceStatus_.over);  // the AI holds its fire on the grid and after the finish
    cc.shipClass.push_back(i + 1);
  }
  if (!held) {
    cc.controls[size_t(player_.ship)].fire = in.fire;
    cc.controls[size_t(player_.ship)].cycle = cyclePending_;
  }
  cyclePending_ = false;
  if (std::getenv("SLIP_PIT_LOG")) {  // test hook: who is in the refuel piece
    static int lastIn[10] = {0};
    for (int i = 0; i < 10; ++i) {
      bool in = false;
      if (!cc.ships[size_t(i)]) continue;
      if (scene_->track_data.refuelPiece >= 0) {
        const ShipState& sh = *cc.ships[size_t(i)];
        const float q[3] = {float(sh.x - scene_->origin[0]), float(sh.y - scene_->origin[1]), float(sh.z - scene_->origin[2])};
        in = scene_->pieceContains(size_t(scene_->track_data.refuelPiece), q, 512.0f);
      }
      if (in != bool(lastIn[i])) std::fprintf(stderr, "pit ship %d %s  t=%.1f damage %.1f/%.1f branch %d\n", i, in ? "enters" : "leaves", ai_[size_t(i)].raceTime, cc.ships[size_t(i)]->damageA, cc.ships[size_t(i)]->damageB, ai_[size_t(i)].branch);
      lastIn[i] = in;
    }
  }
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
    } else if (voicesOn()) {
      audio_.playCue(e.id);
    }
  }
  combat_.events.clear();
  if (driving_ && player_.sfxOverDamage > 0) {  // 0x52122..0x5213A: the human pilot's ship is breaking up: cue 2 or 3
    player_.sfxOverDamage = 0;
    combat_.shipDestroyed(player_.ship, player_);
    if (gameOverTimer_ <= 0 && !finished_) gameOverTimer_ = 4.0;  // 0x4411A: the human's ship is destroyed -> GAME OVER
    if (voicesOn()) audio_.playCue((std::rand() & 1) ? cues::shipBreaking1 : cues::shipBreaking2);
  }
  for (int i = 0; i < 10; ++i) {
    if (grid_[size_t(i)].sfxOverDamage > 0 && i != player_.ship) combat_.shipDestroyed(i, grid_[size_t(i)]);
    grid_[size_t(i)].sfxOverDamage = 0;
  }
  for (int i = 0; i < 10; ++i) {
    ShipState& s = i == player_.ship ? player_ : grid_[size_t(i)];
    if (s.cueContact > 0 && voicesOn()) audio_.playCue(cues::contact(i + 1));  // 0x509B7
    s.cueContact = 0;
  }
  for (int i = 0; i < 10; ++i) {
    ShipState& s = (driving_ && i == player_.ship) ? player_ : grid_[size_t(i)];
    const bool own = driving_ && i == player_.ship;
    const double pos[3] = {s.x, s.y, s.z};
    for (int k = 0; k < s.nWallFx; ++k) combat_.scrapeEffects(s.wallFx[k].pos, s.wallFx[k].n, s.wallFx[k].speed, s.wallFx[k].water, s.wallFx[k].material);
    s.nWallFx = 0;
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
    for (int i = 0; i < 10; ++i) { setShipBoxFromMesh(grid_[size_t(i)], scene_->shipMeshes[size_t(i)], 1.0 / scene_->shipScale); grid_[size_t(i)].speed = 0; ai_[size_t(i)] = AiState{}; ai_[size_t(i)].startRank = gridSlot_[size_t(i)] + 1;
      const int tier = aiTierForStartRank(gridSlot_[size_t(i)] + 1), trk = std::clamp(scene_->trackIndex, 1, 10);
      grid_[size_t(i)].speedFactor = aiTables_.fromExecutable ? aiTables_.tierFactor[aiTables_.difficulty][trk][tier] / 16384.0 : 1.0;
    }
    player_.speedFactor = 1.0;
    if (const char* dm = std::getenv("SLIP_DAMAGE")) {  // test hook: start with this much damage on every ship
      for (int i = 0; i < 10; ++i) { grid_[size_t(i)].damageA = grid_[size_t(i)].damageB = std::atof(dm); }
      player_.damageA = player_.damageB = std::atof(dm);
    }
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
    countdown_ = opt_.countdown && !introMode_ ? 5.0 : 0.0;
    countdownStage_ = opt_.countdown && !introMode_ ? 0 : 4;
    if (!opt_.countdown || introMode_) engineVoice_ = audio_.engineStart();
    lapsDone_ = 0;
    finishRank_ = 0;
    finished_ = false;
    hudAssets_.load(*data_, opt_.ship, scene_->palette);
    finalLapTimer_ = gameOverTimer_ = lapPopupTimer_ = shakeTimer_ = 0; lastLapShown_ = -1; prevDamage_ = 0;
    raceStatus_ = RaceStatus{};
    startPhase_ = 15.0;
    raceOverHandled_ = false;
    resultsTimer_ = 0;
    if (!replayMode_) replayRec_.clear();
    replayPos_ = 0; replayEnded_ = false;
    drones_.reset();
    drones_.radius = double(scene_->droneRadius) * 0.9;
    setupCombat();
  } else {
    countdown_ = 0;
    audio_.updateAmbient(10.0, 0);
    audio_.engineStop(engineVoice_);
    engineVoice_ = 0;
    placeCameraAtStart();
  }
}

void ViewerApp::update(double dt, const InputState& in0) {
  if (frontActive_) {
    front_->update(dt);
    if (front_->wantsQuit()) quit_ = true;
    RaceSetup rs;
    if (front_->takeProgressChanged()) { progress_ = front_->progress(); applyConfig(); }
    if (front_->takeConfigChanged()) { aiTables_.difficulty = settings_.difficulty; combat_.difficulty = settings_.difficulty; applyConfig(); }
    if (front_->takeReplay()) startReplay();
    else if (front_->takeRace(&rs)) startRaceFromFront(rs);
    else if (const int it = front_->takeIntroRequest()) startFlyThrough(it);
    else if (const int nr = front_->takeNetRequest()) {
      frontActive_ = false; netFromFront_ = true; openNetMenu();
      if (nr == 1) netHostGame();
      else if (nr == 2) { browse_ = net::makeLanDiscovery(net::kDiscoveryPort); browse_->startBrowse(); netUi_ = NetUi::Browse; netSel_ = 0; netMsg_.clear(); }
      else if (nr == 3) { netUi_ = NetUi::Address; if (netAddr_.empty()) netAddr_ = "192.168.0."; netMsg_.clear(); }
    }
    return;
  }
  if (netFromFront_ && front_ && !netUiOpen() && !session_ && !driving_) { netFromFront_ = false; returnToFront(); return; }
  if (session_) updateNet(dt);
  showList_ = in0.showList;
  const InputState in = ((netplay_ && pause_.isOpen()) || (netUi_ != NetUi::None && !driving_)) ? InputState{} : in0;  // the multiplayer race goes on behind the pause menu
  if (dt > 0) fpsAvg_ = fpsAvg_ * 0.9 + (1.0 / dt) * 0.1;
  if (dt > 0 && dt < 1.0) animSeconds_ += dt;
  if (dt > 0 && dt < 1.0 && mode_ == AppMode::Track && !driving_) doors_.step(dt);
  if (mode_ == AppMode::Track && scene_) {
    if (driving_ && pause_.isOpen() && !netplay_) {  // 0x58DBB: paused, nothing advances (the camera keeps its place)
      audio_.engineSet(engineVoice_, 0.0);
      return;
    }
    if (driving_) {
      const bool netWait = netStartWaiting();  // multiplayer: everybody's countdown starts at the host's start time
      const bool held = countdown_ > 0 || netWait;  // ships wait on the grid during the countdown (INFERRED: the original starts the race at 0)
      if (countdown_ > 0 && !netWait && dt > 0 && dt < 1.0) {
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
      if (netplay_) {
        Netplay::Bind nb;
        for (int i = 0; i < 10; ++i) { nb.ship[size_t(i)] = i == player_.ship ? &player_ : &grid_[size_t(i)]; nb.ai[size_t(i)] = &ai_[size_t(i)]; }
        nb.combat = &combat_; nb.race = &raceStatus_;
        netplay_->update(dt, nb);
        netClock_ = double(session_->nowMs()) / 1000.0 - simAccum_;  // session time of the first simulation step of this frame
      }
      const double step = 1.0 / 120.0;
      int guard = 0;
      while (simAccum_ >= step && guard++ < 16) {
        // The recording for the replay (0x5BCDC): the controls of every simulation step; the replay feeds them back into the same initial state
        StepRec cur;
        if (replayMode_) {
          if (replayPos_ >= replayRec_.size()) { replayEnded_ = true; simAccum_ = 0; break; }
          cur = replayRec_[replayPos_++];
          cyclePending_ = cur.cycle;
        } else {
          cur = {in.throttle, in.brake, in.steer, in.pitch, in.fire, cyclePending_, held};
          if (!netplay_ && !introMode_) replayRec_.push_back(cur);
        }
        const bool sheld = cur.held;
        InputState sin;
        sin.throttle = cur.throttle; sin.brake = cur.brake; sin.steer = cur.steer; sin.pitch = cur.pitch; sin.fire = cur.fire;
        // ships indexed by ship number; ai_[i].human marks the player
        RaceContext ctx;
        ctx.scene = scene_.get(); ctx.tables = &aiTables_; ctx.race = &race_; ctx.doors = &doors_;
        for (int i = 0; i < 10; ++i) {
          ctx.ships.push_back(i == player_.ship ? &player_ : &grid_[size_t(i)]);
          ctx.state.push_back(&ai_[size_t(i)]);
          ctx.params.push_back(&params_[size_t(i)]);
          ai_[size_t(i)].human = i == player_.ship || (netplay_ && netplay_->humanSlot(i));
          if (netplay_) { ctx.remote.push_back(netplay_->remote(i)); ctx.absent.push_back(!netplay_->present(i)); }
          else if (introMode_) { ctx.remote.push_back(false); ctx.absent.push_back(i != player_.ship); }  // the fly-through has a single ship
        }
        if (netplay_) { ctx.multiplayer = true; ctx.authoritativeEnd = netplay_->isHost(); }
        std::vector<ShipState*> all;
        std::vector<std::array<double, 3>> start;
        all.push_back(&player_);
        for (int i = 0; i < 10; ++i)
          if (i != player_.ship && shipShown(i)) all.push_back(&grid_[size_t(i)]);
        for (ShipState* s : all) start.push_back({s->x, s->y, s->z});
        if (netplay_) {  // the other peers' ships: pose from their snapshots at this instant
          netClock_ += step;
          for (ShipState* s : all) if (netplay_->remote(s->ship)) netplay_->applyRemote(s->ship, *s, ai_[size_t(s->ship)], netClock_);
        }
        ctx.status = &raceStatus_;
        ctx.dt = step;
        ctx.running = !sheld && !raceStatus_.over;
        ctx.totalLaps = opt_.laps;
        if (!sheld) startPhase_ = std::max(0.0, startPhase_ - step);
        for (int i = 0; i < 10; ++i)  // 0x51CC2: speed factor bonus by rank during the first 15 s
          ctx.ships[size_t(i)]->startBonus = opt_.startBonus && startPhase_ > 0 ? startBonusForRank(ai_[size_t(i)].rank) : 0.0;
        doors_.step(step, all);  // door slots update before the ships move (0x3C00E)
        if (!settings_.damage && !netplay_) player_.damageA = player_.damageB = 0;  // configuration "Damage: Off" (RaceSlotDamage 0x52035: the human takes none)
        updateRace(ctx);
        if (netplay_) {
          Netplay::Bind nb;
          for (int i = 0; i < 10; ++i) nb.ai[size_t(i)] = &ai_[size_t(i)];
          netplay_->reconcileFinishRanks(nb);
          if (finished_ && !ai_[size_t(player_.ship)].projected) finishRank_ = ai_[size_t(player_.ship)].finishRank;
        }
        updateWrecks(ctx);
        applyTrailingBoost(ctx, size_t(player_.ship));
        // a finished ship is steered by the autopilot, the player's too (0x51111: record +0xD set -> RaceAIControl)
        static const bool testPilot = std::getenv("SLIP_AUTOPILOT") != nullptr;  // test hook: the player is steered by the AI from the start
        const bool autopilot = (ai_[size_t(player_.ship)].finished || testPilot || introMode_) && aiEnabled_ && !simCfg_.assist;
        ShipInput pin = sheld ? ShipInput{} : autopilot ? aiControl(ctx, size_t(player_.ship), step) : ShipInput{sin.throttle, sin.brake, sin.steer, sin.pitch};
        stepShip(player_, pin, step, params_[size_t(player_.ship)], *scene_, simCfg_);
        for (size_t k = 1; k < all.size(); ++k) {
          const int id = all[k]->ship;
          if (netplay_ && !netplay_->localAi(id)) continue;  // a remote ship: the owner steers it
          ShipInput ci = !sheld && aiEnabled_ && !simCfg_.assist ? aiControl(ctx, size_t(id), step) : ShipInput{};
          stepShip(*all[k], ci, step, params_[size_t(id)], *scene_, simCfg_);
        }
        if (!simCfg_.assist) {
          // doors take part in the contact pass as static boxes that move with the panel (message 0x106 opens the door at 0x6FB8)
          std::vector<ShipState> proxies;
          for (size_t d = 0; d < doors_.list.size(); ++d) proxies.push_back(doors_.proxy(d));
          std::vector<ShipState> remoteCopies;  // the other peers' ships take part in the contacts as moving boxes: only our ships react
          remoteCopies.reserve(all.size());
          std::vector<ShipState*> withDoors;
          for (ShipState* sp : all) {
            if (netplay_ && netplay_->remote(sp->ship)) { remoteCopies.push_back(*sp); withDoors.push_back(&remoteCopies.back()); }
            else withDoors.push_back(sp);
          }
          std::vector<std::array<double, 3>> startDoors = start;
          for (size_t d = 0; d < proxies.size(); ++d) {
            withDoors.push_back(&proxies[d]);
            startDoors.push_back({doors_.list[d].prev[0], doors_.list[d].prev[1], doors_.list[d].prev[2]});
          }
          resolveShipPairs(withDoors, startDoors, step, *scene_, simCfg_);
          for (size_t d = 0; d < proxies.size(); ++d) if (proxies[d].sfxContact > 0) doors_.touch(d);
          size_t rc = 0;
          for (ShipState* sp : all)
            if (netplay_ && netplay_->remote(sp->ship)) { sp->sfxContact += remoteCopies[rc].sfxContact; sp->cueContact += remoteCopies[rc].cueContact; ++rc; }
        }
        if (!introMode_ && !netplay_ && !sheld && !raceStatus_.over) {  // the drones (0x4A291), not while the ships wait on the grid
          std::vector<const ShipState*> dship;
          for (ShipState* sp : all) dship.push_back(sp);
          const double hp[3] = {player_.x, player_.y, player_.z};
          drones_.step(*scene_, step, hp, ai_[size_t(player_.ship)].node, dship, &combat_, lastSetup_.championship);
          for (const DroneWorld::Blast& b : drones_.blasts) combat_.explodeAt(b.pos, !b.wall);
          if (std::getenv("SLIP_DRONE_LOG")) { static size_t last = 99; static int tick = 0; if (drones_.drones.size() != last || ++tick % 240 == 0) { last = drones_.drones.size(); std::fprintf(stderr, "drones %zu blasts %zu\n", last, drones_.blasts.size()); for (const Drone& d : drones_.drones) std::fprintf(stderr, "  drone at %.0f %.0f %.0f node %d dist %.0f\n", d.pos[0], d.pos[1], d.pos[2], d.node, std::sqrt((d.pos[0]-hp[0])*(d.pos[0]-hp[0])+(d.pos[2]-hp[2])*(d.pos[2]-hp[2]))); } }
        }
        if (!introMode_) stepCombat(step, sin, sheld);
        simAccum_ -= step;
      }
      if (replayEnded_) { endReplay(); return; }
      {
        const double listener[3] = {player_.x, player_.y, player_.z};
        drainSounds(listener);
        audio_.engineSet(engineVoice_, player_.wrecked ? 0.0 : player_.speed);
        audio_.updateAmbient(dt, ambientForPlayer());
        // laps, finish and race end (RaceUpdate 0x5A4EC events)
        const AiState& me = ai_[size_t(player_.ship)];
        lapsDone_ = me.lap;
        for (const RaceStatus::Event& e : raceStatus_.events) {
          if (std::getenv("SLIP_RACE_LOG")) std::fprintf(stderr, "race event ship %d kind %d  rank %d laps %d t=%.1f\n", e.ship, e.kind, ai_[size_t(e.ship)].rank, ai_[size_t(e.ship)].laps, ai_[size_t(e.ship)].raceTime);
          if (e.ship != player_.ship) continue;
          if (e.kind == 1) {  // lap line: lap time popup (4 s), "Final Lap!!" (2 s) when the last lap starts (0x4422D / 0x44252)
            lastLapShown_ = me.lastLap; lapPopupTimer_ = 4.0;
            if (me.laps == opt_.laps) finalLapTimer_ = 2.0;
          }
          if (e.kind == 1 && voicesOn()) audio_.playCue(cues::positionAnnounce(std::clamp(me.rank, 1, 10)));  // 0x5A5FE..0x5A613
          if (e.kind == 2) {
            finished_ = true;
            finishRank_ = me.finishRank;
            if (voicesOn()) audio_.playCue(cues::finishLine(player_.ship + 1));  // 0x5A6AA..0x5A6B4 (dropped while the position line plays)
          }
          if (e.kind == 3 && voicesOn()) audio_.playCue((std::rand() & 0x80) ? cues::passLine2(player_.ship + 1) : cues::passLine1(player_.ship + 1));  // 0x50BE7..0x50C00
        }
        raceStatus_.events.clear();
        finalLapTimer_ = std::max(0.0, finalLapTimer_ - dt);
        lapPopupTimer_ = std::max(0.0, lapPopupTimer_ - dt);
        shakeTimer_ = std::max(0.0, shakeTimer_ - dt);
        if (gameOverTimer_ > 0 && (gameOverTimer_ -= dt) <= 0) {  // 0x44022: GAME OVER shown for 4 s, then the race ends
          if (!netplay_) raceStatus_.over = true;
          else {  // multiplayer: the others race on; this ship retires with its current place
            AiState& r = ai_[size_t(player_.ship)];
            r.finished = r.projected = true; r.finishRank = r.rank;
            finished_ = true; finishRank_ = r.rank;
          }
        }
        {
          const double dmg = player_.damageA + player_.damageB;
          if (dmg > prevDamage_ + 1e-9) shakeTimer_ = 0.3;  // 0x440F3: [0x42DBC] = 0x12C ms
          prevDamage_ = dmg;
        }
        if (raceStatus_.over && !raceOverHandled_) {  // race over: results (0x5A820): LOSE.HMP below 4th, else WIN.HMP; result line
          raceOverHandled_ = true;
          if (!finished_) { finished_ = true; finishRank_ = me.rank; }
          if (!opt_.noMusic && (!front_ || netplay_)) audio_.playMusic(finishRank_ > 3 ? "LOSE.HMP" : "WIN.HMP", false);  // with the menus the results screen plays it
          if (voicesOn() && (!front_ || netplay_)) audio_.playCue(finishRank_ == 1 ? (std::rand() & 1) : finishRank_ + 1);  // 0x5A9A9..0x5A9CC (with the menus the results screen plays it)
        }
      }
      if (introMode_) updateFlyThrough(dt);
      if (view_ == 3) {
        updateTvCamera();
      } else if (view_ == 5) {  // F5: free camera orbiting the ship (keypad keys)
        cam_.roll = 0;
        freeAz_ += in.freeAz * dt * 1.6;
        freeEl_ = std::clamp(freeEl_ + in.freeEl * dt * 1.2, -1.4, 1.4);
        freeDist_ = std::clamp(freeDist_ * std::exp(in.freeZoom * dt * 1.2), 15000.0, 900000.0);
        const double az = player_.yaw + freeAz_;
        cam_.pos[0] = player_.x - std::sin(az) * std::cos(freeEl_) * freeDist_;
        cam_.pos[1] = player_.y + std::sin(freeEl_) * freeDist_;
        cam_.pos[2] = player_.z - std::cos(az) * std::cos(freeEl_) * freeDist_;
        cam_.yaw = float(az);
        cam_.pitch = float(-freeEl_);
      } else if (cockpit()) {
        // First person view (0x44F64): camera at the ship's 'head' reference point with the ship's full orientation (incl. bank).
        const double* m = player_.m;
        double fwd[3] = {m[6], m[7], m[8]};
        if (view_ == 4) for (double& x : fwd) x = -x;  // F3: from the cockpit, looking back (0x44F64: the head matrix negated)
        if (view_ == 1) {  // close chase: rigidly attached behind / above the ship, so it banks, pitches and yaws with it
          const double off[3] = {0, 7000, -26000};
          for (int k = 0; k < 3; ++k) cam_.pos[k] = (&player_.x)[k] + m[k] * off[0] + m[3 + k] * off[1] + m[6 + k] * off[2];
          for (int k = 0; k < 3; ++k) fwd[k] = (&player_.x)[k] + m[6 + k] * 18000 - cam_.pos[k];  // look at a point ahead of the ship
          const double l = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
          for (double& x : fwd) x /= l;
        } else {  // first person (0x44F64): the 'head' reference point
          const double* h = refPoints_[size_t(player_.ship)].head;
          for (int k = 0; k < 3; ++k) cam_.pos[k] = (&player_.x)[k] + m[k] * h[0] + m[3 + k] * h[1] + m[6 + k] * h[2];
        }
        cam_.yaw = float(std::atan2(fwd[0], fwd[2]));
        cam_.pitch = float(std::asin(std::clamp(fwd[1], -1.0, 1.0)));
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

void ViewerApp::startRaceFromFront(const RaceSetup& s, bool replay) {
  lastSetup_ = s;
  haveSetup_ = true;
  replayMode_ = replay;
  opt_.ship = s.ship;
  opt_.laps = s.laps;
  playerLoadout_ = s.loadout;
  gridSlot_ = s.grid;
  std::string err;
  if (s.track != track_) loadTrack(s.track, &err);
  else { placeGrid(); doors_.build(*scene_); }  // the same track again: the ships were left where the last race ended, the doors too
  audio_.stopMusic();  // 0x55E3C: the menu / pilot music ends when the race starts; DoGame3D then plays a race song (0x586F2)
  playTrackMusic();
  frontActive_ = false;
  driving_ = false;
  toggleDrive();
  if (replay) view_ = 3;  // the replay starts with the TV camera (F1..F5 change it)
}

void ViewerApp::startReplay() {
  if (haveSetup_ && !replayRec_.empty()) startRaceFromFront(lastSetup_, true);
  else if (front_) { frontActive_ = true; showResultsScreen(true); }
}

// The recording is over (or Esc): back to the results screen, which does not repeat the pilot's line.
void ViewerApp::endReplay() {
  if (!replayMode_) return;
  if (std::getenv("SLIP_REPLAY_TEST")) std::fprintf(stderr, "REPLAY END pos %.1f %.1f %.1f speed %.1f steps %zu rank %d\n", player_.x, player_.y, player_.z, player_.speed, replayPos_, ai_[size_t(player_.ship)].rank);
  replayMode_ = false;
  replayEnded_ = false;
  showResultsScreen(true);
}

void ViewerApp::frontDebugResults() {
  if (!front_) return;
  RaceResult r;
  r.track = 2; r.ship = 1;
  const double t[10] = {377.77, 383.13, 390.75, 398.12, 404.97, 407.17, 423.71, 423.73, 448.42, 453.70};
  for (int i = 0; i < 10; ++i) { r.place[i] = i + 1; r.time[i] = t[i]; }
  r.bestLap = 49.12;
  front_->showResults(r);
  frontActive_ = true;
}

bool ViewerApp::tryShowResults() {
  if (!front_ || netplay_ || !driving_ || frontActive_ || pause_.isOpen() || countdown_ > 0) return false;
  if (!finished_ && !raceStatus_.over) return false;
  showResultsScreen();
  return true;
}

void ViewerApp::showResultsScreen(bool again) {
  RaceResult r;
  if (again) {
    toggleDriveIfRacing();
    audio_.stopMusic();
    lastResult_.noVoice = true;
    front_->showResults(lastResult_);
    frontActive_ = true;
    return;
  }
  r.track = track_; r.ship = player_.ship;
  for (int i = 0; i < 10; ++i) {
    const AiState& a = ai_[size_t(i)];
    r.place[i] = a.finished ? a.finishRank : a.rank;
    r.time[i] = a.finished ? a.finishTime : a.raceTime;
    r.projected[i] = a.projected;
  }
  r.bestLap = ai_[size_t(player_.ship)].bestLap;
  r.remaining = combat_.combat[size_t(player_.ship)].load;
  r.haveRemaining = true;
  lastResult_ = r;
  if (std::getenv("SLIP_REPLAY_TEST")) std::fprintf(stderr, "RACE END   pos %.1f %.1f %.1f speed %.1f steps %zu rank %d\n", player_.x, player_.y, player_.z, player_.speed, replayRec_.size(), ai_[size_t(player_.ship)].rank);
  toggleDrive();
  audio_.stopMusic();
  front_->showResults(r);
  frontActive_ = true;
}

void ViewerApp::returnToFront() {
  if (!front_) return;
  audio_.stopMusic();
  front_->start(true);
  frontActive_ = true;
}

void ViewerApp::frontKey(PauseMenu::Key k) {
  using K = PauseMenu::Key;
  if (!frontActive_) return;
  front_->key(k == K::Up ? FrontEnd::Key::Up : k == K::Down ? FrontEnd::Key::Down : k == K::Left ? FrontEnd::Key::Left : k == K::Right ? FrontEnd::Key::Right : k == K::Select ? FrontEnd::Key::Select : FrontEnd::Key::Back);
}

// Window position (0..1) to the front end's 320x200 screen, shown at 4:3 in the middle of the frame buffer.
void ViewerApp::frontMouse(double nx, double ny, bool click) {
  if (!frontActive_) return;
  const double fw = renderer_.width(), fh = renderer_.height();
  const double rw = std::min(fw, fh * 4.0 / 3.0), rh = rw * 3.0 / 4.0, dx = (fw - rw) / 2, dy = (fh - rh) / 2;
  const double vx = (nx * fw - dx) * 320.0 / rw, vy = (ny * fh - dy) * 200.0 / rh;
  const bool inside = vx >= 0 && vx < 320 && vy >= 0 && vy < 200;
  if (click) front_->click(inside ? int(vx) : -1, inside ? int(vy) : -1);
  else front_->mouseMove(inside ? int(vx) : -1, inside ? int(vy) : -1);
}

void ViewerApp::renderFront() {
  front_->draw();
  uint32_t* fb = renderer_.framebuffer();
  const int fw = renderer_.width(), fh = renderer_.height();
  std::fill(fb, fb + size_t(fw) * size_t(fh), 0xff000000u);
  const int rw = std::min(fw, fh * 4 / 3), rh = rw * 3 / 4, dx = (fw - rw) / 2, dy = (fh - rh) / 2;
  const uint32_t* src = front_->pixels();
  std::vector<int> cols; cols.resize(size_t(rw));
  for (int x = 0; x < rw; ++x) cols[size_t(x)] = std::min(319, x * 320 / rw);
  for (int y = 0; y < rh; ++y) {
    const uint32_t* row = src + size_t(std::min(199, y * 200 / rh)) * 320;
    uint32_t* dst = fb + size_t(y + dy) * size_t(fw) + size_t(dx);
    for (int x = 0; x < rw; ++x) dst[x] = row[cols[size_t(x)]];
  }
  for (const FrontEnd::Preview& p : front_->previews()) if (p.ship >= 0) renderShipPreview(p.ship, p.angle, p.rect, dx, dy, rw, rh, p.fit, p.pal);
}

// The craft turning on the pilot information card (DoViewCar 0x46A94): rendered with the card's own palette into the rectangle of the card, over the
// already composed front end image (pixels the 3D pass leaves at the clear colour keep the card behind them).
// The rear monitor (CONFIGURATION > General "Rear Monitor"; config word [0x492DA], drawn by 0x44A25 from the race loop at 0x58BC7 after the console): a small window at
// (210,99)-(301,147) of the 320x200 screen that shows the world behind the ship from the cockpit position (the head matrix negated), with the label "Rear" in the top left.
// CONFIRMED: position, option, call order. INFERRED: the field of view (the same horizontal angle as the main window), the 1 pixel frame and the label position.
// The weapons monitor (option [0x492DC], 0x44946, CONFIRMED) uses the same window: while a homing missile fired by the player flies (slot kept in [0x43237] by 0x43E92 and cleared when the
// slot is freed) it shows the view from the missile (camera at its position and orientation) with the weapon's name in the corner; the rear view returns when the missile is gone.
void ViewerApp::drawRearMonitor() {
  if (!driving_ || !scene_ || !hudActive() || introMode_ || mode_ != AppMode::Track || rearPass_) return;
  const Projectile* mp = nullptr;
  if (settings_.weaponsMonitor && combat_.monitorProj)
    for (const Projectile& p : combat_.projectiles) if (p.id == combat_.monitorProj && p.alive) mp = &p;
  if (!mp && (!settings_.rearMonitor || view_ == 4 || view_ == 3 || player_.wrecked)) return;
  const int fw = renderer_.width(), fh = renderer_.height();
  const double sx = fw / 320.0, sy = fh / 200.0;
  const int rx0 = 210, ry0 = 99, rx1 = 301, ry1 = 147;
  const int x0 = int(std::floor((rx0 + 1) * sx)), y0 = int(std::floor((ry0 + 1) * sy)), x1 = int(std::floor((rx1) * sx)) - 1, y1 = int(std::floor((ry1) * sy)) - 1;
  uint32_t* fb = renderer_.framebuffer();
  const std::vector<uint32_t> keep(fb, fb + size_t(fw) * size_t(fh));
  const Camera mainCam = cam_;
  const double* m = mp ? mp->m : player_.m;
  double fwd[3] = {mp ? m[6] : -m[6], mp ? m[7] : -m[7], mp ? m[8] : -m[8]};
  if (mp) {
    for (int k = 0; k < 3; ++k) cam_.pos[k] = mp->pos[k];
  } else {
    const double* h = refPoints_[size_t(player_.ship)].head;
    // from the cockpit the monitor looks backwards from the head point; in the third person views (the ship is drawn) it sits just behind the tail so that the tail does not block the view
    double eye[3] = {h[0], h[1], h[2]};
    if (view_ != 0) eye[2] = std::min(eye[2], double(player_.boxLo[2]) - 1500.0);
    for (int k = 0; k < 3; ++k) cam_.pos[k] = (&player_.x)[k] + m[k] * eye[0] + m[3 + k] * eye[1] + m[6 + k] * eye[2];
  }
  cam_.yaw = float(std::atan2(fwd[0], fwd[2]));
  cam_.pitch = float(std::asin(std::clamp(fwd[1], -1.0, 1.0)));
  {
    const double cy = std::cos(cam_.yaw), sy2 = std::sin(cam_.yaw), cp = std::cos(cam_.pitch), sp = std::sin(cam_.pitch);
    const double r0[3] = {cy, 0, -sy2}, u0[3] = {-sy2 * sp, cp, -cy * sp};
    cam_.roll = float(std::atan2(m[3] * r0[0] + m[4] * r0[1] + m[5] * r0[2], m[3] * u0[0] + m[4] * u0[1] + m[5] * u0[2]));
  }
  // same horizontal angle as the main window: the vertical field of view follows from the aspect of the small window
  const double mainW = double(HudLayout::vx1 - HudLayout::vx0 + 1) * sx, monW = double(x1 - x0 + 1), monH = double(y1 - y0 + 1);
  const double tanHalfX = std::tan(mainCam.fovY * 0.5) * mainW / (double(HudLayout::vy1 - HudLayout::vy0 + 1) * sy);
  cam_.fovY = float(2.0 * std::atan(tanHalfX * monH / monW));
  rearPass_ = true;
  const uint32_t sky = 0xff5a7fa8u, ground = 0xff2a2a2eu;
  renderer_.setViewport(x0, y0, x1, y1, float(x0 + x1 + 1) * 0.5f, float(y0 + y1 + 1) * 0.5f);
  renderer_.beginFrame(cam_, sky, ground);
  drawWorld();
  renderer_.resetViewport();
  rearPass_ = false;
  cam_ = mainCam;
  for (int y = 0; y < fh; ++y)  // everything outside the monitor is the finished main frame again
    for (int x = 0; x < fw; ++x)
      if (x < x0 || x > x1 || y < y0 || y > y1) fb[size_t(y) * size_t(fw) + size_t(x)] = keep[size_t(y) * size_t(fw) + size_t(x)];
  HudCanvas c;
  c.fb = fb; c.w = fw; c.h = fh; c.pal = &hudAssets_.palette;
  const int frame = 0xFF;
  c.fillIndex(rx0, ry0, rx1, ry0, frame);  // 1 pixel frame (colour INFERRED)
  c.fillIndex(rx0, ry1, rx1, ry1, frame);
  c.fillIndex(rx0, ry0, rx0, ry1, frame);
  c.fillIndex(rx1, ry0, rx1, ry1, frame);
  if (hudAssets_.small.height > 0) c.text(hudAssets_.small, mp ? combat_.table().w[size_t(std::clamp(mp->kind, 0, kWeaponCount - 1))].name : std::string("Rear"), rx0 + 3, ry0 + 3, 0xFF);
}

void ViewerApp::renderShipPreview(int ship, double angle, const int rect[4], int dx, int dy, int rw, int rh, double fit, const Palette* pal) {
  if (ship < 0 || ship >= 10) return;
  if (!previewCache_[size_t(ship)]) {  // the craft with its own VIEW<n>.MAT materials, built once
    auto sc = std::make_unique<Scene>();
    if (buildShipPreview(*data_, ship, sc.get())) previewCache_[size_t(ship)] = std::move(sc);
    else return;
  }
  Scene& sc = *previewCache_[size_t(ship)];
  sc.palette = pal ? *pal : front_->palette();  // the craft's card palette (the Info screen's own palette is the screen's)
  uint32_t* fb = renderer_.framebuffer();
  const int fw = renderer_.width(), fh = renderer_.height();
  const std::vector<uint32_t> keep(fb, fb + size_t(fw) * size_t(fh));
  const int x0 = dx + rect[0] * rw / 320, y0 = dy + rect[1] * rh / 200, x1 = dx + rect[2] * rw / 320, y1 = dy + rect[3] * rh / 200;
  const Mesh& mesh = sc.shipMeshes[0];
  double radius = 1;
  for (const Vec3& v : mesh.verts) radius = std::max(radius, std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z));
  const double fov = 0.9, el = 0.42, dist = radius / (fit * 2.0 * std::tan(fov / 2.0));
  Camera cam;
  cam.pos[0] = dist * std::sin(angle) * std::cos(el); cam.pos[1] = dist * std::sin(el); cam.pos[2] = dist * std::cos(angle) * std::cos(el);
  cam.yaw = float(std::atan2(-cam.pos[0], -cam.pos[2]));
  cam.pitch = float(std::asin(-cam.pos[1] / dist));
  cam.fovY = float(fov);
  cam.nearPlane = float(radius * 0.2);
  const uint32_t key = 0xff010203u;
  const bool cull = renderer_.cullBackfaces, shadows = renderer_.shadows, portals = renderer_.portalCulling;
  const std::vector<ShadowCaster> casters = std::move(renderer_.shadowCasters);
  renderer_.shadowCasters.clear();
  renderer_.cullBackfaces = true; renderer_.shadows = false; renderer_.portalCulling = false; renderer_.visMask = 0xFFFF;
  renderer_.setViewport(x0, y0, x1, y1, float(x0 + x1 + 1) * 0.5f, float(y0 + y1 + 1) * 0.5f);
  renderer_.beginFrame(cam, key, key);
  MeshTransform xf;
  renderer_.drawMesh(sc, mesh, xf);
  renderer_.resetViewport();
  renderer_.cullBackfaces = cull; renderer_.shadows = shadows; renderer_.portalCulling = portals;
  renderer_.shadowCasters = casters;
  for (int y = 0; y < fh; ++y)
    for (int x = 0; x < fw; ++x) {
      const size_t i = size_t(y) * size_t(fw) + size_t(x);
      if (x < x0 || x > x1 || y < y0 || y > y1 || fb[i] == key) fb[i] = keep[i];
    }
}

void ViewerApp::render() {
  if (frontActive_) { renderFront(); return; }
  renderFrame();
  if (netUi_ != NetUi::None && !driving_ && mode_ == AppMode::Track) drawNetUi();
}

void ViewerApp::renderFrame() {
  if (mode_ == AppMode::Sprite) {
    renderer_.beginFrame(cam_, 0, 0);
    drawSprite();
    return;
  }
  uint32_t sky = 0xff5a7fa8u, ground = 0xff2a2a2eu;
  if (mode_ == AppMode::Model) { sky = ground = 0xff20242cu; }
  int shakeX = 0, shakeY = 0;
  if (shakeTimer_ > 0) {  // 0x448E6: the window centre jitters by -3..+4 pixels while [0x42DBC] runs (0x12C ms after a hit)
    shakeRng_ = shakeRng_ * 1103515245u + 12345u; shakeX = int((shakeRng_ >> 16) & 7) - 3;
    shakeRng_ = shakeRng_ * 1103515245u + 12345u; shakeY = int((shakeRng_ >> 16) & 7) - 3;
  }
  hudShakeX_ = shakeX; hudShakeY_ = shakeY;
  updatePieceLights();
  if (hudActive()) {
    int x0, y0, x1, y1; float pcx, pcy;
    Hud::viewport(renderer_.width(), renderer_.height(), HudLayout::cx + shakeX, HudLayout::cy + shakeY, &x0, &y0, &x1, &y1, &pcx, &pcy);
    renderer_.setViewport(x0, y0, x1, y1, pcx, pcy);
  } else {
    renderer_.resetViewport();
  }
  renderer_.beginFrame(cam_, sky, ground);
  if (!scene_) return;
  drawWorld();
}

// The 3D world from cam_ into the current viewport (track, scenery, ships, doors, then the combat sprites). `painter` selects the debug painter's algorithm.
void ViewerApp::drawWorld() {
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
    if (painter_ && !scene_->bsp.empty() && !rearPass_) { renderTrackPainter(xf); drawCombatOverlay(); return; }
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
      if (scene_->shipMeshes[size_t(i)].polys.empty() || !shipShown(i) || (driving_ && (view_ == 0 || view_ == 4) && i == player_.ship)) continue;
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
    if (rearPass_) drawWorldObjects(); else drawCombatOverlay();
  } else {
    renderer_.drawMesh(*scene_, scene_->track, xf);
  }
}

// Projectiles (the original's .SHP models), beams, bonus sprites, explosions and the lock marker, drawn over the finished frame.
// Smoke puffs, fireballs, missile flames and the debris pieces (game/particles.hpp). A sprite is a square with side 2 x size (0x19ACC).
void ViewerApp::drawParticles(const Scene& sc) {
  const ParticleSystem& ps = combat_.particles;
  auto transparent = [](const Sprite& sp) { return sp.hdr8 == 0xFFFF ? -1 : int(sp.hdr8 & 0xFF); };
  auto draw = [&](PartFam fam, int list, int frame, const double* pos, double size) {
    const auto& v = partSprites_[int(fam)][list];
    if (v.empty()) return;
    const Sprite& sp = v[size_t(std::clamp(frame, 0, int(v.size()) - 1))];
    if (sp.w == 0) return;
    renderer_.drawSpriteWorld(sp, sp.palette ? *sp.palette : sc.palette, pos, 2.0 * size, transparent(sp));
  };
  for (const Puff& p : ps.puffs) draw(p.fam, p.fading() ? 1 : 0, p.frame, p.pos, p.size());
  for (const Emitter& e : ps.emitters)  // the flame at the tail of a missile (the head puff shows the Fire list, 0x277D8)
    if (e.attached && effectDesc(e.type).flame) draw(PartFam::Fire, 0, int(animSeconds_ * 100) % 4, e.pos, effectDesc(e.type).s0 * 1.4);
  for (const Spark& sp : ps.sparks) {  // the colour is the top of the material's ramp (0x281B0: end - 1)
    int m = sp.material;
    if (m < 0) m = sp.kind == 1 ? sc.splashMaterial : sc.sparkMaterial;
    if (m < 0 || m >= int(sc.materials.size())) continue;
    const int idx = std::clamp(int(sc.materials[size_t(m)].palEnd) - 1, 0, 255);
    renderer_.drawStarWorld(sp.pos, sp.size, sp.angle, 0xff000000u | (sc.palette.rgba[size_t(idx)] & 0xffffffu));
  }
  for (const Fireball& f : ps.fireballs) draw(PartFam::Expl, f.fading() ? 1 : 0, f.frame, f.pos, f.currentSize());
  for (const DebrisPiece& d : ps.pieces) {
    const Mesh& m = d.set >= 10 ? sc.droneFragMeshes[size_t(d.piece)] : d.dead ? sc.deadFragMeshes[size_t(d.set)][size_t(d.piece)] : sc.fragMeshes[size_t(d.set)][size_t(d.piece)];
    if (m.polys.empty()) continue;
    MeshTransform xf;
    for (int k = 0; k < 3; ++k) xf.pos[k] = d.pos[k];
    const double a = d.ang[0] * 6.283185307179586, b = d.ang[1] * 6.283185307179586, c = d.ang[2] * 6.283185307179586;
    const double ca = std::cos(a), sa = std::sin(a), cb = std::cos(b), sb = std::sin(b), cc = std::cos(c), sc2 = std::sin(c);
    const double R[9] = {cb * cc, -cb * sc2, sb, ca * sc2 + sa * sb * cc, ca * cc - sa * sb * sc2, -sa * cb, sa * sc2 - ca * sb * cc, sa * cc + ca * sb * sc2, ca * cb};
    for (int i = 0; i < 9; ++i) xf.R[i] = float(R[i]);
    renderer_.drawMesh(sc, m, xf);
  }
}

void ViewerApp::drawWorldObjects() {
  if (!driving_ || !scene_) return;
  const Scene& sc = *scene_;
  auto transparent = [](const Sprite& sp) { return sp.hdr8 == 0xFFFF ? -1 : int(sp.hdr8 & 0xFF); };
  for (const Projectile& p : combat_.projectiles) {
    const double* f = p.m + 6;
    if (p.kind == kBlaster || p.kind == kDisrupter) {  // 0x5C4BB: a line from the previous to the new head position (colours are placeholders)
      // The segment of the last frame (speed 0x77240 / s at about 30 frames a second), drawn with two colours (esi = 0xFD00FE for the blaster, 0x40004F for the disrupter,
      // 0x5C569 / 0x5C829): the head half in the low word's colour, the tail half in the high word's; 0xFD / 0xFE are the executable's UI colours (red / yellow), 0x40 / 0x4F palette entries of the track.
      const double len = 0x77240 / 30.0;
      const double a[3] = {p.pos[0] - f[0] * len, p.pos[1] - f[1] * len, p.pos[2] - f[2] * len};
      const double m[3] = {(a[0] + p.pos[0]) * 0.5, (a[1] + p.pos[1]) * 0.5, (a[2] + p.pos[2]) * 0.5};
      auto rgb = [&](int idx) { return 0xff000000u | (sc.palette.rgba[size_t(idx)] & 0xffffffu); };
      const uint32_t head = p.kind == kBlaster ? 0xffffff00u : rgb(0x4F), tail = p.kind == kBlaster ? 0xffff0000u : rgb(0x40);
      renderer_.drawLineWorld(m, p.pos, head);
      renderer_.drawLineWorld(a, m, tail);
      continue;
    }
    const int mi = Scene::weaponMeshIndex(p.kind);
    if (mi < 0 || sc.weaponMeshes[size_t(mi)].polys.empty()) continue;
    MeshTransform xf;
    for (int k = 0; k < 3; ++k) xf.pos[k] = p.pos[k];
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) xf.R[i * 3 + j] = float(p.m[j * 3 + i]);
    renderer_.drawMesh(sc, sc.weaponMeshes[size_t(mi)], xf);
  }
  if (!drones_.drones.empty() && !sc.droneMesh.polys.empty())  // the drones: DRONE.SHP, turned along their flight direction (the model faces +z)
    for (const Drone& d : drones_.drones) {
      MeshTransform xf;
      for (int k = 0; k < 3; ++k) xf.pos[k] = d.pos[k];
      double f[3] = {d.dir[0], d.dir[1], d.dir[2]};
      double r[3] = {f[2], 0, -f[0]};
      double rl = std::sqrt(r[0] * r[0] + r[2] * r[2]);
      if (rl < 1e-6) { r[0] = 1; r[2] = 0; rl = 1; }
      r[0] /= rl; r[2] /= rl;
      const double u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
      const double m[9] = {r[0], r[1], r[2], u[0], u[1], u[2], f[0], f[1], f[2]};
      for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) xf.R[i * 3 + j] = float(m[j * 3 + i]);
      renderer_.drawMesh(sc, sc.droneMesh, xf);
    }
  for (const Pickup& pk : combat_.pickups) {  // bonus object: BONUS<type>.SPR billboard (size = the 0x2620 collision cube)
    const Sprite& sp = bonusSprites_[size_t(std::clamp(pk.type, 0, 5))];
    renderer_.drawSpriteWorld(sp, sp.palette ? *sp.palette : sc.palette, pk.pos, 2.0 * 0x2620, transparent(sp));
  }
  drawParticles(sc);
}

void ViewerApp::drawCombatOverlay() {
  if (!driving_ || !scene_) return;
  drawWorldObjects();
  const CombatState& c = combat_.combat[size_t(player_.ship)];
  if (!hudActive() && c.lockTarget >= 0) {  // lock marker (the HUD draws the original's TSIGHT sprite instead)
    const ShipState& t = grid_[size_t(c.lockTarget)];
    const double tp[3] = {t.x, t.y, t.z};
    float sx, sy, z;
    if (renderer_.projectToScreen(tp, &sx, &sy, &z)) {
      const int r = std::max(8, int(t.extent * 1.3 / z * (renderer_.height() * 0.5 / std::tan(cam_.fovY * 0.5))));
      renderer_.drawRectScreen(int(sx) - r, int(sy) - r, int(sx) + r, int(sy) + r, 0xffff3030u);
    }
  }
  drawHud();
  drawRearMonitor();
  if (introMode_) drawIntroOverlay();
  if (replayMode_ && hudAssets_.loaded) {
    HudCanvas c;
    c.fb = renderer_.framebuffer(); c.w = renderer_.width(); c.h = renderer_.height(); c.pal = &hudAssets_.palette;
    c.textCentered(hudAssets_.small.height > 0 ? hudAssets_.small : hudAssets_.time, "REPLAY   Esc: end   F1..F5: cameras", 0, 319, 12, 0xFF);
  }
}

// Piece light (0x39AE6..0x39AF8): the light of the piece, and for the refuel piece a fresh random value (14 bit) for every draw: the blue / white
// flashes of the pit. The generator is 0x3667B: x = (x + 1) >> 1, xor 0xB400 when a bit fell out.
void ViewerApp::updatePieceLights() {
  renderer_.pieceLight.clear();
  if (mode_ != AppMode::Track || !scene_) return;
  const Track& t = scene_->track_data;
  renderer_.pieceLight.resize(t.pieces.size(), 1.0f);
  for (size_t i = 0; i < t.pieces.size(); ++i) {
    float light = float(t.pieces[i].light) / 16384.0f;
    if (int(i) == t.refuelPiece && std::getenv("SLIP_NOPITFLICKER") == nullptr) {
      uint16_t x = uint16_t(lightLfsr_ + 1);
      const bool carry = x & 1;
      x >>= 1;
      if (carry) x ^= 0xb400;
      lightLfsr_ = x;
      light = float(x & 0x3fff) / 16384.0f;
    }
    renderer_.pieceLight[i] = std::clamp(light, 0.0f, 1.0f);
  }
}

void ViewerApp::openPause() {
  if (!pausable() || pause_.isOpen()) return;
  pause_.open();
  audio_.engineSet(engineVoice_, 0.0);
}

void ViewerApp::applyConfig() {
  applySettings();
  renderer_.shadows = settings_.shadows;
  if (front_) front_->setEconomy(&weaponTable_, aiTables_.difficulty, 750);
  SavedConfig sc;
  sc.settings = settings_;
  sc.progress = progress_;
  saveConfig(sc);
}

void ViewerApp::applySettings() {
  audio_.setVolumes(opt_.audio.master, settings_.musicOn ? settings_.music : 0.0f, settings_.sfxOn ? settings_.sfx : 0.0f);
  audio_.setEngineGain(settings_.engine == 0 ? 0.0f : settings_.engine == 1 ? 0.5f : 1.0f);
  aiTables_.difficulty = settings_.difficulty;
  combat_.difficulty = settings_.difficulty;
  static const float kDetail[4] = {32.0f, 20.0f, 10.0f, 5.0f};  // scenery size thresholds of the Detail option (0x350C7)
  renderer_.minScenerySize = kDetail[std::clamp(settings_.detail, 0, 3)];
}

void ViewerApp::menuKey(PauseMenu::Key k) {
  if (!pause_.isOpen()) return;
  const PauseMenu::Action act = pause_.key(k, &settings_);
  applySettings();
  if (act == PauseMenu::Action::QuitRace) { if (netplay_) netLeaveRace(); else { toggleDrive(); if (front_) returnToFront(); } }   // 0x591CE: leave the race
  else if (act == PauseMenu::Action::ExitGame) quit_ = true;
}

// Track map (RaceCameraSetup 0x3AF50, CONFIRMED structure): an orthographic camera looking straight down from above the ship, turned with the ship's heading
// (yaw only), so the ship sits at a fixed screen point (per-track centre, table 0x55680) with its heading pointing up; scale = (table 0x556A8 >> 8) world units per
// pixel. Drawn: every path node's links (next, alternative) as green lines (colour 0xFC), the lap line node as a white 3x3 square (0xFF), the other ships as black
// outlined plus-shaped dots (AI grey 0xFB, other humans white 0xFF), the player on top in yellow (0xFE). Everything is clipped to the 3D window.
void ViewerApp::drawMap(const HudCanvas& c) {
  const Track& t = scene_->track_data;
  if (t.nodes.empty()) return;
  const int ti = std::clamp(track_, 1, 10);
  const double unitsPerPx = double(hudAssets_.map.dist[ti]) / 256.0;
  if (unitsPerPx <= 0) return;
  const int cx = hudAssets_.map.cx[ti], cy = hudAssets_.map.cy[ti];
  const double* m = player_.m;
  double fx = m[6], fz = m[8];
  const double fl = std::sqrt(fx * fx + fz * fz);
  if (fl < 1e-3) { fx = std::sin(player_.yaw); fz = std::cos(player_.yaw); } else { fx /= fl; fz /= fl; }
  auto proj = [&](double x, double z, int* sx, int* sy) {  // heading up: X = along the ship's right vector, Y = along its forward vector
    const double dx = x - player_.x, dz = z - player_.z;
    const double X = fz * dx - fx * dz, Y = fx * dx + fz * dz;
    *sx = cx + int(std::floor(X / unitsPerPx));
    *sy = cy - int(std::floor(Y / unitsPerPx));
  };
  const int x0 = HudLayout::vx0, y0 = HudLayout::vy0, x1 = HudLayout::vx1, y1 = HudLayout::vy1;
  for (const TrackNode& n : t.nodes) {
    int ax, ay;
    proj(n.pos.x, n.pos.z, &ax, &ay);
    for (int link : {n.next, n.alt}) {
      if (link < 0 || size_t(link) >= t.nodes.size()) continue;
      int bx, by;
      proj(t.nodes[size_t(link)].pos.x, t.nodes[size_t(link)].pos.z, &bx, &by);
      c.line(ax, ay, bx, by, 0xFC, x0, y0, x1, y1);
    }
  }
  if (t.lapPieceB >= 0 && size_t(t.lapPieceB) < t.pieces.size()) {
    const int nd = t.pieces[size_t(t.lapPieceB)].node;
    if (nd >= 0 && size_t(nd) < t.nodes.size()) {
      int sx, sy;
      proj(t.nodes[size_t(nd)].pos.x, t.nodes[size_t(nd)].pos.z, &sx, &sy);
      for (int yy = sy - 1; yy <= sy + 1; ++yy) for (int xx = sx - 1; xx <= sx + 1; ++xx) c.pixel(xx, yy, 0xFF, x0, y0, x1, y1);
    }
  }
  auto dot = [&](const ShipState& s, int colour) {  // 0x3B2B6: 5x5, black ring, 3x3 core without its corners
    int sx, sy;
    proj(s.x, s.z, &sx, &sy);
    for (int dy = -2; dy <= 2; ++dy)
      for (int dx = -2; dx <= 2; ++dx) {
        const bool edge = std::abs(dx) == 2 || std::abs(dy) == 2, corner = std::abs(dx) >= 1 && std::abs(dy) >= 1;
        if (edge && std::abs(dx) == 2 && std::abs(dy) == 2) continue;
        if (edge && (std::abs(dx) == 2 ? std::abs(dy) > 1 : std::abs(dx) > 1)) continue;
        c.pixel(sx + dx, sy + dy, edge || corner ? 0 : colour, x0, y0, x1, y1);
      }
  };
  for (int i = 0; i < 10; ++i)
    if (i != player_.ship && shipShown(i) && !(netplay_ && netplay_->humanSlot(i))) dot(grid_[size_t(i)], 0xFB);
  for (int i = 0; i < 10; ++i)
    if (i != player_.ship && shipShown(i) && netplay_ && netplay_->humanSlot(i)) dot(grid_[size_t(i)], 0xFF);
  dot(player_, 0xFE);
}

void ViewerApp::drawHud() {
  if (!hudActive()) return;
  const ShipState& me = player_;
  const AiState& rec = ai_[size_t(player_.ship)];
  const CombatState& cs = combat_.combat[size_t(player_.ship)];
  const auto& tb = combat_.table();
  HudState st;
  st.cockpit = view_ == 0;
  const Track& t = scene_->track_data;
  st.consoleFrame = (rec.piece >= 0 && size_t(rec.piece) < t.pieces.size() && t.pieces[size_t(rec.piece)].light < 0x2000) ? 2 : 1;  // 0x3526C
  st.speed = me.speed;
  st.kph = settings_.kph;
  st.rank = rec.rank;
  st.lap = rec.laps;
  st.totalLaps = opt_.laps;
  st.lapTime = rec.lapTime;
  st.lastLapTime = lapPopupTimer_ > 0 ? lastLapShown_ : -1;
  st.finalLapTimer = finalLapTimer_;
  st.gameOverTimer = gameOverTimer_;
  st.countdown = countdown_ > 0 ? int(std::ceil(countdown_)) : 0;
  st.finishedPosition = finished_ ? finishRank_ : 0;
  st.damageA = me.damageA;
  st.damageB = (me.hyperTime > 0 || me.reverseTime > 0) ? 100.0 : me.damageB;  // 0x521C5: scrambled controls read as full damage
  st.selected = cs.selected;
  int wid = cs.selected == 0 ? kBlaster : cs.selected == 1 ? cs.load.weaponA : cs.selected == 2 ? cs.load.weaponB : -1;
  if (wid >= 0) {
    st.weaponName = tb.w[size_t(wid)].name;
    st.ammo = cs.selected == 0 ? -1 : (cs.selected == 1 ? cs.load.ammoA : cs.load.ammoB);
    st.energy = cs.energy[std::clamp(cs.selected, 0, 2)];
    st.canLock = tb.w[size_t(wid)].cone > 0;
  }
  st.boosterFuel = cs.boosterFuel;
  st.booster = me.boosterOn || me.boosterFreeTime > 0;
  st.time = animSeconds_;
  st.centerX = HudLayout::cx + hudShakeX_;
  st.centerY = HudLayout::cy + hudShakeY_;
  if (st.canLock && (cs.lockTarget >= 0 || cs.lockDrone)) {
    const ShipState* tg = cs.lockTarget >= 0 ? &grid_[size_t(cs.lockTarget)] : nullptr;
    const double tp[3] = {tg ? tg->x : cs.lockDronePos[0], tg ? tg->y : cs.lockDronePos[1], tg ? tg->z : cs.lockDronePos[2]};
    float sx, sy, z;
    if (renderer_.projectToScreen(tp, &sx, &sy, &z)) {
      st.lockVisible = true;
      st.lockX = sx * 320.0 / renderer_.width();
      st.lockY = sy * 200.0 / renderer_.height();
    }
  }
  static const int forced = std::getenv("SLIP_PORTRAIT") ? std::atoi(std::getenv("SLIP_PORTRAIT")) : 0;  // test hook
  if (const int sp = forced ? forced : audio_.currentSpeaker(); sp >= 1 && sp <= 10) {  // GetSpeaker 0x53061 -> portrait of pilot class sp
    st.portraitPilot = sp;
    const AiState& pr = ai_[size_t(sp - 1)];
    st.portraitText = pr.finished ? "FINISHED" : std::to_string(pr.rank);
  }
  if (settings_.trackMap && hudAssets_.map.loaded) {  // 0x58B52: the map is drawn after the 3D view, before the console and the text
    HudCanvas mc;
    mc.fb = renderer_.framebuffer(); mc.w = renderer_.width(); mc.h = renderer_.height(); mc.pal = &hudAssets_.palette;
    drawMap(mc);
  }
  hud_.draw(renderer_.framebuffer(), renderer_.width(), renderer_.height(), st, hudAssets_);
  if (netplay_) {
    HudCanvas c;
    c.fb = renderer_.framebuffer(); c.w = renderer_.width(); c.h = renderer_.height(); c.pal = &hudAssets_.palette;
    drawPlayerList(c);
  }
  if (pause_.isOpen()) {
    HudCanvas c;
    c.fb = renderer_.framebuffer(); c.w = renderer_.width(); c.h = renderer_.height(); c.pal = &hudAssets_.palette;
    pause_.draw(c, hudAssets_, settings_);
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
    if (sc.shipMeshes[size_t(i)].polys.empty() || !shipShown(i) || (driving_ && (view_ == 0 || view_ == 4) && i == player_.ship)) continue;  // no own ship from inside the cockpit
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
    if (sc.shipMeshes[size_t(i)].polys.empty() || !shipShown(i)) continue;
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
      const AiState& me = ai_[size_t(player_.ship)];
      if (countdown_ > 0) { std::snprintf(buf, sizeof buf, "GET READY  %d", int(std::ceil(countdown_))); l.push_back(buf); }
      else if (raceStatus_.over) { std::snprintf(buf, sizeof buf, "RACE OVER - position %d of 10  time %.1f%s  best lap %.1f", finishRank_, me.finishTime, me.projected ? " (projected)" : "", me.bestLap); l.push_back(buf); }
      else if (finished_) { std::snprintf(buf, sizeof buf, "FINISHED - position %d of 10  time %.1f  (autopilot, race ends %s)", finishRank_, me.raceTime, raceStatus_.endTimer >= 0 ? "soon" : "when the others are in"); l.push_back(buf); }
      else {
        std::snprintf(buf, sizeof buf, "%s lap %d/%d  position %d  time %.1f  last %.1f  best %.1f%s", me.laps == opt_.laps ? "FINAL LAP!!" : "", std::max(1, me.laps), opt_.laps, me.rank, me.raceTime, me.lastLap,
                      me.bestLap, opt_.startBonus && startPhase_ > 0 && countdown_ <= 0 ? "  START BOOST" : "");
        l.push_back(buf);
      }
      l.push_back(combatLine());
      std::snprintf(buf, sizeof buf, "damage engine %.0f%% steering %.0f%%  credits %d  projectiles %zu  pickups %zu", player_.damageA, player_.damageB, combat_.combat[size_t(player_.ship)].credits, combat_.projectiles.size(), combat_.pickups.size());
      l.push_back(buf);
      l.push_back("Space accelerate, cursor keys steer / pitch (up = nose down), Alt fire, Ctrl select weapon (booster: fire switches it), F1 cockpit F2 chase F3 rear F4 TV F5 free (keypad), Esc pause; debug: Ctrl+Shift+key");
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


// ---- multiplayer -------------------------------------------------------------------------------------------------------------

void ViewerApp::netInit() {
  uint32_t h = 2166136261u;  // identifies the game data set: every player needs the same files (same tracks, tables, ship data)
  auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
  mix(uint32_t(data_->entryCount()));
  if (auto exe = data_->read("SLIPSTRM.EXE")) { mix(uint32_t(exe->size())); for (size_t i = 0; i < exe->size(); i += 509) mix((*exe)[i]); }
  netDataHash_ = h;
  if (opt_.netName.empty()) {
    const char* u = std::getenv("USER");
    opt_.netName = u && *u ? u : "Pilot";
  }
  if (opt_.netName.size() > 16) opt_.netName.resize(16);
  if (mode_ == AppMode::Track && scene_ && !hudAssets_.loaded) hudAssets_.load(*data_, 0, scene_->palette);  // fonts for the menu
  if (opt_.netRole == "host") netHostGame();
  else if (opt_.netRole == "join") netJoinGame(opt_.netHost, opt_.netPort);
}

void ViewerApp::openNetMenu() {
  if (mode_ != AppMode::Track || !scene_ || driving_) return;
  if (!hudAssets_.loaded) hudAssets_.load(*data_, 0, scene_->palette);
  if (netUi_ == NetUi::None) { netUi_ = session_ ? NetUi::Lobby : NetUi::Main; netSel_ = 0; }
}

void ViewerApp::netLeaveSession() {
  if (session_) session_->leave();
  netplay_.reset();
  session_.reset();
  browse_.reset();
  netSeed_ = 0;
}

void ViewerApp::netHostGame() {
  netLeaveSession();
  session_ = std::make_unique<net::Session>();
  std::string err;
  if (!session_->host(opt_.netName, netDataHash_, uint16_t(opt_.netPort), &err)) { netMsg_ = err; session_.reset(); netUi_ = NetUi::Main; return; }
  session_->hostSettings(track_, opt_.laps, aiTables_.difficulty, true);
  session_->setIntent(opt_.ship, true);
  netUi_ = NetUi::Lobby; netSel_ = 0; netMsg_.clear();
  netAutoReady_ = true;
  if (opt_.netHeadless) netUi_ = NetUi::None;
  std::fprintf(stderr, "multiplayer: hosting on TCP port %u as %s\n", unsigned(session_->listenPort()), opt_.netName.c_str());
}

void ViewerApp::netJoinGame(const std::string& host, int port) {
  netLeaveSession();
  session_ = std::make_unique<net::Session>();
  std::string err;
  if (!session_->join(host, uint16_t(port), opt_.netName, netDataHash_, &err)) { netMsg_ = err; session_.reset(); netUi_ = NetUi::Main; return; }
  netUi_ = NetUi::Lobby; netSel_ = 0; netMsg_ = "Connecting to " + host + "...";
  netAutoReady_ = opt_.netHeadless;
  if (opt_.netHeadless) netUi_ = NetUi::None;
}

void ViewerApp::netLeaveRace() {
  netLeaveSession();
  if (driving_) toggleDrive();
  netUi_ = NetUi::Main; netSel_ = 0;
}

void ViewerApp::startNetRace(const net::Start& st, uint32_t startLocal) {
  netplay_ = std::make_unique<Netplay>(session_.get(), st);
  opt_.laps = st.laps;
  settings_.difficulty = aiTables_.difficulty = st.difficulty;
  netSeed_ = st.seed ? st.seed : 1;
  opt_.ship = netplay_->mySlot();
  opt_.countdown = true;
  std::string err;
  if (st.track != track_ || !scene_) loadTrack(st.track, &err);
  driving_ = false;
  toggleDrive();
  applySettings();
  netStartLocal_ = startLocal;
  netEndTimer_ = 0;
  netUi_ = NetUi::None;
  for (int i = 0; i < 10; ++i) {  // empty seats: no ship on the grid
    grid_[size_t(i)].hasBox = grid_[size_t(i)].hasBox && netplay_->present(i);
    ai_[size_t(i)].human = netplay_->humanSlot(i);
    if (!netplay_->present(i)) ai_[size_t(i)].finished = true;
  }
  ai_[size_t(opt_.ship)].human = true;
  std::fprintf(stderr, "multiplayer: race starts on track %d, %d laps, I am ship %d (player %d), %d humans\n", st.track, st.laps, opt_.ship, netplay_->myId(), netplay_->humans());
}

void ViewerApp::updateNet(double dt) {
  if (browse_) browse_->update(dt);
  session_->update(dt);
  using S = net::Session::State;
  if (session_->state() == S::Closed) {
    netMsg_ = session_->error().empty() ? "Session closed" : session_->error();
    std::fprintf(stderr, "multiplayer: %s\n", netMsg_.c_str());
    if (opt_.netHeadless) { quit_ = true; return; }  // keep the race state for the summary
    const bool wasRacing = driving_ && netplay_;
    netLeaveSession();
    if (wasRacing) { toggleDrive(); }
    netUi_ = opt_.netHeadless ? NetUi::None : NetUi::Main;
    if (opt_.netHeadless) quit_ = true;
    return;
  }
  if (netplay_ && netplay_->sessionClosed()) return;
  if (session_->state() == S::Lobby) {
    const net::Lobby& lb = session_->lobby();
    if (netAutoReady_ && !lb.players.empty() && session_->myId() >= 0) {  // headless / command line: ready as soon as the lobby is joined
      session_->setIntent(opt_.ship, true);
      netAutoReady_ = false;
    }
    if (session_->isHost() && opt_.netPlayers > 0 && int(lb.players.size()) >= opt_.netPlayers && session_->hostCanStart()) {
      session_->hostSettings(opt_.track, opt_.laps, aiTables_.difficulty, true);
      session_->hostStart(unsigned(std::rand()) | 1u);
    }
  }
  net::Start st;
  uint32_t at = 0;
  if (session_->state() == S::Racing && !netplay_ && session_->takeStart(&st, &at)) startNetRace(st, at);
  if (opt_.netHeadless && netplay_ && driving_) {  // progress log of the test runs
    static double acc = 0;
    acc += dt;
    if (acc >= 10.0) {
      acc = 0;
      const AiState& me = ai_[size_t(player_.ship)];
      std::fprintf(stderr, "t=%.0f ship %d speed %.0f laps %d rank %d hits %d damage %.0f/%.0f wrecked %d pos %.0f %.0f %.0f ping %.1f ms\n", me.raceTime, player_.ship, player_.speed, me.laps, me.rank, player_.hits, player_.damageA,
                   player_.damageB, player_.wrecked ? 1 : 0, player_.x, player_.y, player_.z, netplay_->pingMs());
    }
  }
  if (opt_.netHeadless && raceOverHandled_ && netplay_) {
    netEndTimer_ += dt;
    if (netEndTimer_ > 3.0) quit_ = true;
  }
}

std::string ViewerApp::netSummary() const {
  std::string out;
  char buf[200];
  for (int i = 0; i < 10; ++i) {
    const AiState& a = ai_[size_t(i)];
    if (netplay_ && !netplay_->present(i)) continue;
    std::snprintf(buf, sizeof buf, "slot %d  %-10s owner %d  laps %d  finished %d  rank %d  finishRank %d%s\n", i, netplay_ ? netplay_->name(i).c_str() : "-", netplay_ ? netplay_->owner(i) : 0, a.laps,
                  a.finished ? 1 : 0, a.rank, a.finishRank, a.projected ? " (projected)" : "");
    out += buf;
  }
  std::snprintf(buf, sizeof buf, "race over %d  my ship %d  rank %d", raceStatus_.over ? 1 : 0, player_.ship, finishRank_);
  out += buf;
  if (netplay_) {
    const auto& st = netplay_->stats;
    std::snprintf(buf, sizeof buf, "\nnet: states in %d  projectiles out/in %d/%d  hits out/in %d/%d  pickups out/in %d/%d  damage %.0f/%.0f", st.states, st.spawnsOut, st.spawnsIn, st.hitsOut, st.hitsIn, st.pickupsOut, st.pickupsIn, player_.damageA, player_.damageB);
    out += buf;
  }
  return out;
}

// Tab: names, places and pings of everybody in the race (ping = round trip to the player who simulates the ship; AI ships show the host's).
void ViewerApp::drawPlayerList(const HudCanvas& c) {
  const Font& sm = hudAssets_.small.height > 0 ? hudAssets_.small : hudAssets_.time;
  if (netplay_->notice().empty() == false && netplay_->noticeAge() < 6.0) c.textCentered(sm, netplay_->notice(), 4, 315, 12, 0xFE);
  if (!showList_) return;
  std::vector<int> order;
  for (int i = 0; i < 10; ++i) if (netplay_->present(i)) order.push_back(i);
  std::sort(order.begin(), order.end(), [&](int a, int b) { return ai_[size_t(a)].rank < ai_[size_t(b)].rank; });
  const int rh = sm.height + 3, h = int(order.size()) * rh + sm.height + 14, y0 = 90 - h / 2;
  c.darken(40, y0 - 4, 279, y0 + h, 70);
  c.fillIndex(40, y0 - 5, 279, y0 - 5, 7); c.fillIndex(40, y0 + h + 1, 279, y0 + h + 1, 7);
  c.text(sm, "POS  PILOT", 48, y0, 0xFE); c.text(sm, "LAP", 170, y0, 0xFE); c.text(sm, "PING", 222, y0, 0xFE);
  int y = y0 + sm.height + 6;
  for (int i : order) {
    const AiState& a = ai_[size_t(i)];
    const bool me = i == player_.ship;
    const int idx = me ? 0xFF : 0xFB;
    char b[64];
    std::snprintf(b, sizeof b, "%2d   %s%s", a.rank, netplay_->name(i).c_str(), netplay_->owner(i) == netplay_->hostId() && netplay_->humanSlot(i) ? " (host)" : "");
    c.text(sm, b, 48, y, idx);
    std::snprintf(b, sizeof b, "%d/%d", std::clamp(a.laps, 1, opt_.laps), opt_.laps);
    c.text(sm, a.finished ? "END" : b, 170, y, idx);
    const double ping = netplay_->slotPing(i);
    if (me) c.text(sm, "-", 222, y, idx);
    else if (ping < 0) c.text(sm, "...", 222, y, idx);
    else { std::snprintf(b, sizeof b, "%.0f ms", ping); c.text(sm, b, 222, y, idx); }
    y += rh;
  }
}

void ViewerApp::netKey(PauseMenu::Key k) {
  using K = PauseMenu::Key;
  auto trackName = [](int t) { return std::string(trackDisplayNames()[size_t(std::clamp(t, 1, 10) - 1)]); };
  (void)trackName;
  switch (netUi_) {
    case NetUi::None: return;
    case NetUi::Main: {
      if (k == K::Up) netSel_ = (netSel_ + 3) % 4;
      else if (k == K::Down) netSel_ = (netSel_ + 1) % 4;
      else if (k == K::Back) netUi_ = NetUi::None;
      else if (k == K::Select) {
        if (netSel_ == 0) netHostGame();
        else if (netSel_ == 1) { browse_ = net::makeLanDiscovery(net::kDiscoveryPort); browse_->startBrowse(); netUi_ = NetUi::Browse; netSel_ = 0; netMsg_.clear(); }
        else if (netSel_ == 2) { netUi_ = NetUi::Address; if (netAddr_.empty()) netAddr_ = "192.168.0."; netMsg_.clear(); }
        else netUi_ = NetUi::None;
      }
      return;
    }
    case NetUi::Browse: {
      const auto list = browse_ ? browse_->sessions() : std::vector<net::SessionInfo>{};
      const int n = int(list.size());
      if (k == K::Up && n) netSel_ = (netSel_ + n - 1) % n;
      else if (k == K::Down && n) netSel_ = (netSel_ + 1) % n;
      else if (k == K::Back) { browse_.reset(); netUi_ = netFromFront_ ? NetUi::None : NetUi::Main; netSel_ = 1; }
      else if (k == K::Select && n) {
        const net::SessionInfo& si = list[size_t(std::clamp(netSel_, 0, n - 1))];
        const std::string host = si.host;
        const int port = si.port;
        browse_.reset();
        netJoinGame(host, port);
      }
      return;
    }
    case NetUi::Address: {
      if (k == K::Back) { netUi_ = netFromFront_ ? NetUi::None : NetUi::Main; netSel_ = 2; }
      else if (k == K::Select && !netAddr_.empty()) {
        std::string host = netAddr_;
        int port = opt_.netPort;
        const size_t c = host.rfind(':');
        if (c != std::string::npos) { port = std::atoi(host.c_str() + c + 1); host.resize(c); }
        netJoinGame(host, port);
      }
      return;
    }
    case NetUi::Lobby: {
      if (!session_) { netUi_ = NetUi::Main; return; }
      const bool host = session_->isHost();
      const net::Lobby lb = session_->lobby();
      int myShip = 0;
      bool ready = false;
      for (const net::LobbyEntry& e : lb.players) if (e.id == session_->myId()) { myShip = e.ship; ready = e.ready != 0; }
      const int rows = 6;  // track, laps, difficulty, my ship, ready / start, leave
      if (k == K::Up) netSel_ = (netSel_ + rows - 1) % rows;
      else if (k == K::Down) netSel_ = (netSel_ + 1) % rows;
      else if (k == K::Back) { netLeaveSession(); netUi_ = netFromFront_ ? NetUi::None : NetUi::Main; netSel_ = 0; }
      else if (k == K::Left || k == K::Right || k == K::Select) {
        const int d = k == K::Left ? -1 : 1;
        if (netSel_ == 0 && host && k != K::Select) { const int t = (lb.track - 1 + d + 10) % 10 + 1; session_->hostSettings(t, lb.laps, lb.difficulty, lb.aiFill); if (t != track_) { std::string e; loadTrack(t, &e); } }
        else if (netSel_ == 1 && host && k != K::Select) session_->hostSettings(lb.track, std::clamp(lb.laps + d, 1, 20), lb.difficulty, lb.aiFill);
        else if (netSel_ == 2 && host && k != K::Select) session_->hostSettings(lb.track, lb.laps, std::clamp(lb.difficulty + d, 0, 2), lb.aiFill);
        else if (netSel_ == 3 && k != K::Select) { opt_.ship = (myShip + d + 10) % 10; session_->setIntent(opt_.ship, ready); }
        else if (netSel_ == 4 && k == K::Select) {
          if (host) { if (session_->hostCanStart()) session_->hostStart(unsigned(std::rand()) | 1u); else netMsg_ = "Waiting for everybody to be ready"; }
          else session_->setIntent(myShip, !ready);
        } else if (netSel_ == 5 && k == K::Select) { netLeaveSession(); netUi_ = netFromFront_ ? NetUi::None : NetUi::Main; netSel_ = 0; }
      }
      return;
    }
  }
}

void ViewerApp::netText(const std::string& t) {
  if (netUi_ != NetUi::Address) return;
  for (char c : t) if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == ':' || c == '-') if (netAddr_.size() < 40) netAddr_ += c;
}

void ViewerApp::netBackspace() {
  if (netUi_ == NetUi::Address && !netAddr_.empty()) netAddr_.pop_back();
}

void ViewerApp::drawNetUi() {
  if (!hudAssets_.loaded) return;
  HudCanvas c;
  c.fb = renderer_.framebuffer(); c.w = renderer_.width(); c.h = renderer_.height(); c.pal = &hudAssets_.palette;
  c.darken(0, 0, 319, 199, 60);
  const Font& f = hudAssets_.menu.height > 0 ? hudAssets_.menu : hudAssets_.time;
  const Font& sm = hudAssets_.small.height > 0 ? hudAssets_.small : f;
  auto row = [&](int y, const std::string& text, bool sel) {
    if (sel) c.fillIndex(30, y - 2, 289, y + f.height + 1, 0x33);
    c.textCentered(f, text, 30, 289, y, sel ? 0xFF : 0xFB);
  };
  auto box = [&](int x0, int y0, int x1, int y1) { c.fillIndex(x0 - 2, y0 - 2, x1 + 2, y1 + 2, 7); c.fillIndex(x0, y0, x1, y1, 0); };
  box(20, 14, 299, 186);
  c.textCentered(f, "Multiplayer", 20, 299, 20, 0xFE);
  const int y0 = 20 + f.height + 12, rowH = f.height + 5;
  switch (netUi_) {
    case NetUi::None: return;
    case NetUi::Main: {
      static const char* items[4] = {"Host Game", "Search LAN", "Direct IP", "Back"};
      for (int i = 0; i < 4; ++i) row(y0 + i * rowH, items[i], i == netSel_);
      break;
    }
    case NetUi::Browse: {
      const auto list = browse_ ? browse_->sessions() : std::vector<net::SessionInfo>{};
      if (list.empty()) c.textCentered(sm, "Searching for games on the local network...", 20, 299, y0, 0xFB);
      for (size_t i = 0; i < list.size() && i < 8; ++i) {
        const net::SessionInfo& si = list[i];
        char b[120];
        std::snprintf(b, sizeof b, "%s  %s  %d/10", si.name.c_str(), trackDisplayNames()[size_t(std::clamp<int>(si.track, 1, 10) - 1)], si.players);
        const bool ok = si.version == net::kProtocolVersion && si.dataHash == netDataHash_;
        row(y0 + int(i) * rowH, ok ? b : std::string(b) + " (incompatible)", int(i) == netSel_);
      }
      break;
    }
    case NetUi::Address: {
      c.textCentered(sm, "Address of the host (HOST or HOST:PORT), Enter to join", 20, 299, y0, 0xFB);
      row(y0 + rowH + 6, netAddr_ + ((int(animSeconds_ * 2) & 1) ? "_" : " "), true);
      break;
    }
    case NetUi::Lobby: {
      if (!session_) break;
      const net::Lobby& lb = session_->lobby();
      if (session_->state() == net::Session::State::Joining) { c.textCentered(sm, netMsg_.empty() ? "Connecting..." : netMsg_, 20, 299, y0, 0xFB); break; }
      const bool host = session_->isHost();
      int myShip = 0;
      bool ready = false;
      for (const net::LobbyEntry& e : lb.players) if (e.id == session_->myId()) { myShip = e.ship; ready = e.ready != 0; }
      char b[120];
      int y = y0 - 4;
      c.text(sm, "Players", 28, y, 0xFE);
      y += sm.height + 4;
      for (const net::LobbyEntry& e : lb.players) {
        std::snprintf(b, sizeof b, "%s%s", e.name.c_str(), e.id == 0 ? " (host)" : e.ready ? " (ready)" : "");
        c.text(sm, b, 28, y, e.id == session_->myId() ? 0xFF : 0xFB);
        std::snprintf(b, sizeof b, "ship %d", e.ship + 1);
        c.text(sm, b, 118, y, 0xFB);
        y += sm.height + 2;
      }
      const int rh = sm.height + 6;
      auto srow = [&](int yy, const std::string& text, bool sel) {
        if (sel) c.fillIndex(166, yy - 2, 291, yy + sm.height + 1, 0x33);
        c.text(sm, text, 170, yy, sel ? 0xFF : 0xFB);
      };
      y = y0 - 4;
      c.text(sm, host ? "Race settings" : "Race settings (host)", 170, y, 0xFE);
      y += sm.height + 6;
      std::snprintf(b, sizeof b, "Track: %s", trackDisplayNames()[size_t(std::clamp<int>(lb.track, 1, 10) - 1)]);
      srow(y, b, netSel_ == 0); y += rh;
      std::snprintf(b, sizeof b, "Laps: %d", lb.laps); srow(y, b, netSel_ == 1); y += rh;
      static const char* diff[3] = {"Easy", "Normal", "Hard"};
      std::snprintf(b, sizeof b, "Difficulty: %s", diff[std::clamp<int>(lb.difficulty, 0, 2)]); srow(y, b, netSel_ == 2); y += rh;
      std::snprintf(b, sizeof b, "My ship: %d", myShip + 1); srow(y, b, netSel_ == 3); y += rh;
      srow(y, host ? "START RACE" : ready ? "Ready (cancel)" : "READY", netSel_ == 4); y += rh;
      srow(y, "Leave", netSel_ == 5);
      break;
    }
  }
  c.textCentered(sm, netUi_ == NetUi::Address ? "Type the address, Enter joins, Esc goes back" : "Up Down select   Left Right change   Enter OK   Esc back", 20, 299, 160, 0xFB);
  if (!netMsg_.empty() && netUi_ != NetUi::Lobby) c.textCentered(sm, netMsg_, 20, 299, 172, 0xFE);
  else if (!netMsg_.empty() && session_ && session_->state() == net::Session::State::Lobby) c.textCentered(sm, netMsg_, 20, 299, 172, 0xFE);
}

}  // namespace slip
