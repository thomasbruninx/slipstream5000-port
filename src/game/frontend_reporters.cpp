// The reporters' scenes of the championship (sub_573B7): before every race the SLIP T.V. reporters talk about the track in front of the turning globe, once
// before and once after the TV fly-through. The scene is driven by the track's .ANN script (original_formats/ann): waits, voice lines with their subtitles and
// the program of the face animation, whose layers (mouth, eyes, lids, brows) are drawn over FACE.SPR / FFACE.SPR (face engine 0x41C0A..0x41F53). CONFIRMED.
#include <algorithm>
#include <cstdio>

#include "game/frontend.hpp"
#include "game/hud.hpp"

namespace slip {

namespace {
constexpr int W = 320, H = 200;
uint32_t argb(uint32_t rgb) { return 0xff000000u | (rgb & 0xffffffu); }
int transparentOf(const Sprite& s) { return s.hdr8 == 0xFFFF ? -1 : int(s.hdr8 & 0xFF); }
const char* const kIntroNames[10] = {"CHIINT", "HAWINT", "TOKINT", "NORINT", "CAVINT", "COLINT", "AMAINT", "LONINT", "EGYINT", "NYCINT"};  // table 0x57930
// face layers (table 0x41A80 male / 0x41AC2 female): sprite pattern, number of frames, offset from the face's corner
struct Layer { const char* pat; int count, x, y; };
constexpr Layer kMale[4] = {{"MOUTH_", 15, 26, 45}, {"EYES_", 20, 31, 40}, {"LIDS_", 4, 31, 40}, {"BROWS_", 6, 28, 25}};
constexpr Layer kFemale[4] = {{"FMOUTH_", 15, 31, 51}, {"FEYES_", 20, 32, 45}, {"FLIDS_", 4, 32, 45}, {"FBROWS_", 6, 31, 30}};
constexpr int kFaceX = 182, kFaceY = 25;  // sub_41F53 arguments at 0x5763A
}  // namespace

std::string FrontEnd::text(const std::string& file, const std::string& tag) {
  if (!tables_.count(file))
    if (auto b = data_->read(file)) tables_[file] = parseStringTable(*b);
  return str(file, tag);
}

void FrontEnd::startReporters(int phase) {
  rep_ = Reporters{};
  rep_.phase = phase;
  rep_.track = setup_.track;
  std::string name = kIntroNames[std::clamp(setup_.track, 1, 10) - 1];
  if (phase != 0) name[5] = '1';  // 0x574F6: "CHIINT" -> "CHIIN1"
  if (auto b = data_->read(name + ".ANN"))
    if (auto a = parseAnn(*b)) { rep_.script = std::move(*a); rep_.ready = true; }
  rep_.female = rep_.script.reporter != 0;
  rep_.table = name + ".ST0";
  go(Screen::Reporters);
  if (!rep_.ready) reportersEnd();
}

void FrontEnd::introFinished() {
  if (champ_.active()) startReporters(1);
}

void FrontEnd::skipReporters() {  // Esc / Enter / click end the scene (0x5787B..0x578A1)
  if (rep_.voice && audio_) audio_->stopVoice(rep_.voice);
  rep_.voice = 0;
  reportersEnd();
}

void FrontEnd::reportersEnd() {
  if (rep_.voice && audio_) audio_->stopVoice(rep_.voice);
  rep_.voice = 0;
  if (rep_.phase == 0) {
    introWanted_ = flyThrough_;  // the TV fly-through follows when the application can show it
    if (!introWanted_) startReporters(1);
  } else {
    if (audio_) {  // 0x55DFA / 0x55EFC: the waiting music, then the pilot's own section
      if (audio_->currentMusic() != "INTRO.HMP") audio_->playMusic("INTRO.HMP", true);
      audio_->setMusicPart(setup_.ship + 1);
    }
    go(Screen::Garage);
  }
}

void FrontEnd::updateReporters(double dt) {
  turnGlobe(std::clamp(rep_.track, 1, 10) - 1, dt);
  if (!rep_.ready) return;
  rep_.clock += dt;
  // the face program: op 8 waits until its clock reaches the value, op 6 sets a layer's frame, op 5 ends
  if (rep_.faceOn) {
    rep_.faceClock += dt * 1000.0;
    const auto& f = rep_.face;
    auto w16 = [&](size_t p) { return p + 2 <= f.size() ? int(f[p] | (f[p + 1] << 8)) : 0; };
    while (rep_.faceOn && rep_.facePc + 2 <= f.size()) {
      const int op = w16(rep_.facePc);
      if (op == 8) { if (rep_.faceClock >= w16(rep_.facePc + 2)) rep_.facePc += 4; else break; }
      else if (op == 6) {
        const int layer = w16(rep_.facePc + 2), frame = w16(rep_.facePc + 4);
        if (layer >= 0 && layer < 4) rep_.frame[layer] = frame < (rep_.female ? kFemale : kMale)[layer].count ? frame : 0;
        rep_.facePc += 6;
      } else rep_.faceOn = false;  // op 5 (and anything else) ends the thread
    }
  }
  if (rep_.wait > 0) { rep_.wait -= dt; if (rep_.wait > 0) return; }
  const bool talking = rep_.voiceEnd > 0 && rep_.clock < rep_.voiceEnd;
  while (rep_.pc < rep_.script.cmds.size()) {
    const AnnCmd& c = rep_.script.cmds[rep_.pc];
    if (c.op == AnnCmd::Op::Face) {
      rep_.face = c.face; rep_.facePc = 0; rep_.faceClock = 0; rep_.faceOn = true;  // 0x577B4: a new face thread (English only)
      ++rep_.pc;
    } else if (c.op == AnnCmd::Op::Message) {
      rep_.tag = c.tag; ++rep_.pc;
    } else if (c.op == AnnCmd::Op::Voice) {
      rep_.tag = c.tag;
      const std::string name = "E" + c.tag.substr(1) + ".SMP";  // InitScript 0x581F7: the first character is replaced by the language letter
      rep_.voiceEnd = -1;
      if (auto b = data_->read(name))
        if (auto sm = parseSample(name, *b)) {
          rep_.voiceEnd = rep_.clock + double(sm->pcm.size() / 2) / double(std::max(1, sm->rate));
          if (audio_) rep_.voice = audio_->playPcm(std::make_shared<SoundSample>(std::move(*sm)), 1.0f);
        }
      ++rep_.pc;
    } else if (c.op == AnnCmd::Op::Wait) {
      rep_.wait = c.ms / 1000.0; ++rep_.pc;
      return;
    } else if (c.op == AnnCmd::Op::End) {  // 0x57788: the scene ends when the last voice line is over
      if (talking) return;
      reportersEnd();
      return;
    } else {  // piece / distance waits are only valid in the fly-through
      ++rep_.pc;
    }
  }
  if (rep_.pc >= rep_.script.cmds.size() && !talking) reportersEnd();
}

void FrontEnd::drawReporters() {
  const Sprite* bg = spr("STARS.SPR");
  if (bg && bg->palette) usePalette(*bg->palette);
  HudCanvas c; c.fb = buf_.data(); c.w = W; c.h = H; c.pal = &pal_;
  if (bg) c.blit(*bg, 0, 0, -1);
  drawGlobe(std::clamp(rep_.track, 1, 10) - 1, 91, 109);
  if (const Sprite* t = spr("CH_TRACK.SPR")) {  // the title plate with the track's name
    c.blit(*t, t->hdr4, t->hdr6, transparentOf(*t));
    const char* names[10] = {"Chicago", "Hawaii", "Tokyo", "Norway", "France", "Arizona", "Amazon", "London", "Egypt", "New York"};
    if (const Font* f = font("STARFONT.FNT")) { const std::string ti = names[std::clamp(rep_.track, 1, 10) - 1]; drawText(*f, ti, t->hdr4 + (t->w - f->textWidth(ti)) / 2, t->hdr6 + (t->h - f->height) / 2, -1); }
  }
  // the reporter: FACE.SPR / FFACE.SPR with the four animated layers
  const Layer* layers = rep_.female ? kFemale : kMale;
  if (const Sprite* face = spr(rep_.female ? "FFACE.SPR" : "FACE.SPR")) {
    if (face->palette) usePalette(*face->palette);
    c.blit(*face, kFaceX, kFaceY, transparentOf(*face));
    for (int l = 0; l < 4; ++l)
      if (const Sprite* s = spr(std::string(layers[l].pat) + char('A' + std::clamp(rep_.frame[l], 0, layers[l].count - 1)) + ".SPR"))
        c.blit(*s, kFaceX + layers[l].x, kFaceY + layers[l].y, transparentOf(*s));
  }
  // the subtitle: SMALL.FNT in the box x 184..308, y 127..196, dark text and the same text one pixel to the right in gold (0x57696..0x576FC)
  if (!rep_.tag.empty())
    if (const Font* f = font("SMALL.FNT")) {
      const std::string msg = text(rep_.table, rep_.tag);
      std::vector<std::string> lines;
      std::string line, word;
      for (size_t i = 0; i <= msg.size(); ++i) {
        if (i == msg.size() || msg[i] == ' ') {
          if (!line.empty() && f->textWidth(line + " " + word) > 125) { lines.push_back(line); line.clear(); }
          line += (line.empty() ? "" : " ") + word;
          word.clear();
        } else word += msg[i];
      }
      if (!line.empty()) lines.push_back(line);
      const int total = int(lines.size()) * f->height;
      int y = 127 + std::max(0, (196 - 127 + 1 - total) / 2);
      for (const std::string& l : lines) {
        drawText(*f, l, 184 + (125 - f->textWidth(l)) / 2, y, 0x84);
        drawText(*f, l, 185 + (125 - f->textWidth(l)) / 2, y, 0x8d);
        y += f->height;
      }
    }
}

}  // namespace slip
