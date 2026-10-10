// The TV fly-through before a championship race (PlayTrackIntro 0x57A79). One ship (RACER0) flies the lap on the autopilot at the intro speed (factor 1 minus the
// table 0x5040C: Norway 0.5, France 0.5, Arizona 0.25, Amazon 0.375), the TV camera follows it (view F4 of the original, 0x45196) and the commentators talk: the
// track's .ANN script (g_TrackAnnNames 0x580D8) holds waits, voice lines and "wait until the ship is N units from the line" commands; the subtitles come from the
// *PREV string tables and are drawn in SHADE.FNT at the bottom (sub_44511). Esc / Enter / a click end the scene.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/hud.hpp"
#include "platform/viewer_app.hpp"

namespace slip {

namespace {
const char* const kTrackAnn[10] = {"CHICAGO", "HAWAII", "TOKYO", "NORWAY", "CAVE", "COLORADO", "AMAZON", "LONDON", "EGYPT", "NEWYORK"};  // table 0x580D8
const char* const kTrackCam[10] = {"CHICAGO", "HAWAII", "TOKYO", "NORWAY", "CAVE", "CAN", "AMAZON", "LONDON", "EGYPT", "NEWYORK"};      // table 0x430F8
const double kIntroSlow[10] = {0, 0, 0, 0.5, 0.5, 0.25, 0.375, 0, 0, 0};                                                                // table 0x5040C / 0x4000
}  // namespace

void ViewerApp::startFlyThrough(int track) {
  track = std::clamp(track, 1, 10);
  introMode_ = true;
  frontActive_ = false;
  opt_.ship = 0;
  gridSlot_ = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  std::string err;
  if (track != track_) loadTrack(track, &err);
  else placeGrid();
  introScript_ = AnnScript{};
  introPc_ = 0; introWait_ = 0; introClock_ = 0; introVoiceEnd_ = -1; introVoice_ = 0;
  introTag_.clear();
  introText_.clear();
  tvCams_.clear();
  tvCam_ = -1;
  tvZoom_ = 1.0;
  if (auto b = data_->read(std::string(kTrackAnn[track - 1]) + ".ANN"))
    if (auto a = parseAnn(*b)) {
      introScript_ = std::move(*a);
      std::string base = introScript_.name;
      while (!base.empty() && base.back() == ' ') base.pop_back();
      if (auto t = data_->read(base + ".ST0")) introText_ = parseStringTable(*t);
    }
  if (auto b = data_->read(std::string(kTrackCam[track - 1]) + ".CAM"))  // 60 x {s32 x, y, z}; x = -1 marks an unused entry
    for (size_t i = 0; i + 12 <= b->size() && tvCams_.size() < 60; i += 12) {
      std::array<int32_t, 3> c{};
      for (int k = 0; k < 3; ++k) c[size_t(k)] = int32_t(uint32_t((*b)[i + 4 * k]) | (uint32_t((*b)[i + 4 * k + 1]) << 8) | (uint32_t((*b)[i + 4 * k + 2]) << 16) | (uint32_t((*b)[i + 4 * k + 3]) << 24));
      if (c[0] != -1) tvCams_.push_back(c);
    }
  audio_.stopMusic();
  playTrackMusic();  // PlayTrackIntro picks one of the race songs
  view_ = 3;
  driving_ = false;
  toggleDrive();
  player_.speedFactor = 1.0 - kIntroSlow[track - 1];
  cam_.fovY = 1.15f;
}

void ViewerApp::endFlyThrough(bool skipped) {
  (void)skipped;
  if (!introMode_) return;
  if (introVoice_) audio_.stopVoice(introVoice_);
  introVoice_ = 0;
  introMode_ = false;
  view_ = 0;
  cam_.fovY = 1.15f;
  driving_ = true;
  toggleDrive();  // leaves the race state (and the engine sound)
  audio_.stopMusic();
  frontActive_ = true;
  if (front_) front_->introFinished();
}

void ViewerApp::updateFlyThrough(double dt) {
  introClock_ += dt;
  if (introWait_ > 0) { introWait_ -= dt; if (introWait_ > 0) return; }
  const bool talking = introVoiceEnd_ > 0 && introClock_ < introVoiceEnd_;
  while (introPc_ < introScript_.cmds.size()) {
    const AnnCmd& c = introScript_.cmds[introPc_];
    switch (c.op) {
      case AnnCmd::Op::Voice: {
        introTag_ = c.tag;
        introVoiceEnd_ = -1;
        const std::string name = "E" + c.tag.substr(1) + ".SMP";  // InitScript 0x581F7: the first character is replaced by the language letter
        if (auto b = data_->read(name))
          if (auto sm = parseSample(name, *b)) {
            introVoiceEnd_ = introClock_ + double(sm->pcm.size() / 2) / double(std::max(1, sm->rate));
            introVoice_ = audio_.playPcm(std::make_shared<SoundSample>(std::move(*sm)), 1.0f);
          }
        ++introPc_;
        break;
      }
      case AnnCmd::Op::Message: introTag_ = c.tag; ++introPc_; break;
      case AnnCmd::Op::Wait: introWait_ = c.ms / 1000.0; ++introPc_; return;
      case AnnCmd::Op::WaitDist: {  // 0x57EB9: stay while the distance to the line is larger than the value
        const AiState& a = ai_[size_t(player_.ship)];
        const double togo = a.laps <= 0 ? race_.lapLength : race_.lapLength - a.progress;  // before the line: a full lap to go
        if (togo > double(c.a)) return;
        ++introPc_;
        break;
      }
      case AnnCmd::Op::End:  // 0x57DFC: the scene ends when the last line is over
        if (talking) return;
        endFlyThrough(false);
        return;
      default: ++introPc_; break;  // piece waits and face programs do not occur in the fly-through scripts
    }
  }
  if (!talking) endFlyThrough(false);
}

// F4 of the original (0x45196): the camera is the nearest of the track's TV camera positions; it looks at the ship and zooms with the distance
// (1x up to 0x2620 units, 4x from 0x477C0 + 0x2620 on, 0x4530F); a fast ship that passes close to a camera makes the jet-pass sound (0x4B848).
// The TV camera (0x45196, CONFIRMED): the camera point nearest to the ship is used when it lies outside every track piece (0x36669) or when the line from the ship to it is free
// (0x140B2 -> the collision ray 0x35642, Scene::rayBlocked); otherwise the second nearest is tried with the ray, and when that is blocked too the camera of the last frame stays
// (the nearest one when there is none yet).
void ViewerApp::updateTvCamera() {
  cam_.roll = 0;
  const double P[3] = {player_.x, player_.y, player_.z};
  auto dist = [&](size_t i) { const double dx = tvCams_[i][0] - P[0], dy = tvCams_[i][1] - P[1], dz = tvCams_[i][2] - P[2]; return std::sqrt(dx * dx + dy * dy + dz * dz); };
  auto blocked = [&](size_t i) { const double c[3] = {double(tvCams_[i][0]), double(tvCams_[i][1]), double(tvCams_[i][2])}; return scene_->rayBlocked(P, c); };
  auto inTrack = [&](size_t i) {
    const float q[3] = {float(tvCams_[i][0] - scene_->origin[0]), float(tvCams_[i][1] - scene_->origin[1]), float(tvCams_[i][2] - scene_->origin[2])};
    return scene_->pieceAt(q) >= 0;
  };
  int best = -1;
  double bestD = 1e300;
  if (!tvCams_.empty()) {
    int c1 = -1, c2 = -1;
    double d1 = 1e300, d2 = 1e300;
    for (size_t i = 0; i < tvCams_.size(); ++i) {
      const double d = dist(i);
      if (d < d1) { c2 = c1; d2 = d1; c1 = int(i); d1 = d; }
      else if (d < d2) { c2 = int(i); d2 = d; }
    }
    best = c1; bestD = d1;
    if (inTrack(size_t(c1)) && blocked(size_t(c1))) {
      if (c2 >= 0 && !blocked(size_t(c2))) { best = c2; bestD = d2; }
      else if (tvCam_ >= 0) { best = tvCam_; bestD = dist(size_t(tvCam_)); }
    }
  }
  if (best < 0) {  // no camera positions: follow from behind
    cam_.pos[0] = P[0] - std::sin(player_.yaw) * 110000; cam_.pos[1] = P[1] + 28000; cam_.pos[2] = P[2] - std::cos(player_.yaw) * 110000;
    cam_.yaw = float(player_.yaw); cam_.pitch = -0.22f;
    return;
  }
  if (best != tvCam_) {
    if (player_.speed >= 0x2ba3e && bestD <= 0x17d40) audio_.playNamed("JETPASS1.SMP", 1.0f);
    tvCam_ = best;
  }
  const auto& c = tvCams_[size_t(best)];
  cam_.pos[0] = c[0]; cam_.pos[1] = c[1]; cam_.pos[2] = c[2];
  double f[3] = {P[0] - c[0], P[1] - c[1], P[2] - c[2]};
  const double l = std::max(1.0, std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]));
  for (double& x : f) x /= l;
  cam_.yaw = float(std::atan2(f[0], f[2]));
  cam_.pitch = float(std::asin(std::clamp(f[1], -1.0, 1.0)));
  const double t = std::clamp((bestD - 0x2620) / double(0x477c0), 0.0, 1.0);
  tvZoom_ = 1.0 + 3.0 * t;
  cam_.fovY = float(2.0 * std::atan(std::tan(1.15 * 0.5) / tvZoom_));
}

// The black frame of the race window and the subtitle (sub_44511): SHADE.FNT, centred between x 8 and 311, vertically in y 170..197.
void ViewerApp::drawIntroOverlay() {
  if (!hudAssets_.loaded) return;
  HudCanvas c;
  c.fb = renderer_->framebuffer(); c.w = renderer_->width(); c.h = renderer_->height(); c.pal = &hudAssets_.palette;
  c.fill(0, 0, 319, 7, 0xff000000u);
  c.fill(0, 193, 319, 199, 0xff000000u);
  c.fill(0, 8, 3, 192, 0xff000000u);
  c.fill(316, 8, 319, 192, 0xff000000u);
  if (introTag_.empty()) return;
  std::string msg;
  for (const auto& [k, v] : introText_) if (k == introTag_) msg = v;
  const Font& f = hudAssets_.shade.height > 0 ? hudAssets_.shade : hudAssets_.small;
  std::vector<std::string> lines;
  std::string line, word;
  for (size_t i = 0; i <= msg.size(); ++i) {
    if (i == msg.size() || msg[i] == ' ') {
      if (!line.empty() && f.textWidth(line + " " + word) > 300) { lines.push_back(line); line.clear(); }
      line += (line.empty() ? "" : " ") + word;
      word.clear();
    } else word += msg[i];
  }
  if (!line.empty()) lines.push_back(line);
  const int total = int(lines.size()) * f.height;
  int y = 170 + std::max(0, (197 - 170 + 1 - total) / 2);
  for (const std::string& l : lines) { c.textDirectCentered(f, l, 8, 311, y); y += f.height; }
}

}  // namespace slip
