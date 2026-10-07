// Audio tests: HMP -> SMF conversion, SMP decoding and the mixer (the data-dependent parts are skipped without game data).
#include <cmath>
#include <cstdio>
#include <memory>

#include "audio/audio_system.hpp"
#include "audio/mixer.hpp"
#include "original_formats/audio.hpp"
#include "original_formats/game_data.hpp"

using namespace slip;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static Bytes tinyHmp() {  // 1 channel track + end, hand built in the HMP layout
  Bytes b(0x388, 0);
  const char sig[] = "HMIMIDIP013195";
  std::copy(sig, sig + 14, b.begin());
  b[0x30] = 1;  // one track
  const uint8_t trk[] = {
      0x01, 0x00, 0x00, 0x00, 0, 0, 0, 0,  // placeholder, replaced below
  };
  (void)trk;
  // chunk: track 0, length = 12 + data, channel hint 0
  Bytes data = {
      0x80, 0xB0, 0x6E, 0xFF,        // delta 0, CC110 (HMI loop, dropped)
      0x80, 0xC0, 0x1E,              // delta 0, program 30
      0x80, 0x90, 0x40, 0x64,        // delta 0, note on
      0x7F, 0x81, 0x80, 0x40, 0x00,  // delta 127 + (1<<7) = 255 ticks, note off
      0x80, 0xFF, 0x2F, 0x00};       // end
  const uint32_t len = 12 + uint32_t(data.size());
  for (int i = 0; i < 4; ++i) b.push_back(0);
  b.push_back(uint8_t(len)); b.push_back(0); b.push_back(0); b.push_back(0);
  for (int i = 0; i < 4; ++i) b.push_back(0);
  b.insert(b.end(), data.begin(), data.end());
  return b;
}

int main() {
  // SMP
  {
    Bytes smp = {0x80, 0xFF, 0x00, 0x80};
    auto s = parseSample("T.SMP", smp);
    CHECK(s && s->pcm.size() == 4 && s->rate == 11025);
    CHECK(s && std::fabs(s->pcm[0]) < 1e-6f && s->pcm[1] > 0.99f && s->pcm[2] < -0.99f);
  }
  // HMP -> SMF
  {
    HmpInfo info;
    auto smf = hmpToSmf(tinyHmp(), &info);
    CHECK(smf.has_value());
    if (smf) {
      CHECK(std::string(smf->begin(), smf->begin() + 4) == "MThd");
      CHECK((*smf)[9] == 1 && (*smf)[11] == 1);  // format 1, one track
      CHECK((*smf)[12] == 0 && (*smf)[13] == 120);  // division 120
      CHECK(info.lengthTicks == 255);
      // no CC110 left in the converted track, program change and note events kept
      bool cc = false, pc = false, on = false;
      for (size_t i = 22; i + 2 < smf->size(); ++i) {
        if ((*smf)[i] == 0xB0 && (*smf)[i + 1] == 110) cc = true;
        if ((*smf)[i] == 0xC0 && (*smf)[i + 1] == 30) pc = true;
        if ((*smf)[i] == 0x90 && (*smf)[i + 1] == 0x40) on = true;
      }
      CHECK(!cc && pc && on);
    }
    Bytes bad = tinyHmp();
    bad[0] = 'X';
    CHECK(!hmpToSmf(bad).has_value());
  }
  // mixer: a looping unit sample at centre, pitch 2 reads twice as fast, finished voices are freed
  {
    auto s = std::make_shared<SoundSample>();
    s->rate = 1000;
    s->pcm = {0.5f, 0.5f, 0.5f, 0.5f};
    Mixer m(1000);
    int v = m.play(s, 1.0f, 0.0f, 1.0f, false);
    CHECK(v > 0 && m.active(v));
    float out[16] = {};
    m.render(out, 8);
    CHECK(std::fabs(out[0] - 0.5f) < 1e-4f && std::fabs(out[1] - 0.5f) < 1e-4f);  // centre = unity both sides
    CHECK(std::fabs(out[7 * 2]) < 1e-6f);  // finished after 4 frames
    CHECK(!m.active(v));
    int p = m.play(s, 1.0f, 1.0f, 2.0f, false);  // hard right, double speed -> done after 2 frames
    float o2[16] = {};
    m.render(o2, 4);
    CHECK(o2[0] < 1e-4f && std::fabs(o2[1] - 1.4142f * 0.5f) < 1e-3f);
    CHECK(!m.active(p));
    int l = m.play(s, 1.0f, 0.0f, 1.0f, true);
    float o3[40] = {};
    m.render(o3, 20);
    CHECK(m.active(l) && std::fabs(o3[2 * 19] - 0.5f) < 1e-4f);
    m.stop(l);
    CHECK(m.activeVoices() == 0);
  }
  // real data (optional)
  auto root = findGameDirectory("");
  if (!root.empty()) {
    std::string err;
    auto d = GameData::open(root, &err);
    if (d) {
      for (const char* n : {"INGAME2.HMP", "INGAME3.HMP", "INGAME4.HMP", "INGAME6.HMP", "INTRO.HMP", "WIN.HMP", "LOSE.HMP"}) {
        auto b = d->read(n);
        HmpInfo info;
        auto smf = b ? hmpToSmf(*b, &info) : std::nullopt;
        CHECK(smf.has_value());
        // 120 ticks per second: the longest track matches the header's length in seconds (INTRO's header differs, see docs)
        if (smf && std::string(n) != "INTRO.HMP") CHECK(std::fabs(info.lengthTicks / 120.0 - info.seconds) < 2.0);
      }
      // HMI loop markers: INGAME2 loops ticks 2..3595 forever (first loop end), INTRO 6748..8545, WIN/LOSE have none
      {
        auto seg = hmpToSegments(*d->read("INGAME2.HMP"));
        CHECK(seg && !seg->loop.empty() && seg->loopStartTick == 2 && seg->loopEndTick == 3595);
        auto in = hmpToSegments(*d->read("INTRO.HMP"));
        CHECK(in && in->loopStartTick == 6748 && in->loopEndTick == 8545);
        auto win = hmpToSegments(*d->read("WIN.HMP"));
        CHECK(win && win->loop.empty() && !win->intro.empty());
      }
      {  // announcer samples per track exist (lists read from the exe) and every fixed effect sample exists
        AudioConfig cfg;
        cfg.openDevice = false;
        AudioSystem a;
        a.init(*d, cfg, nullptr);
        for (int trk = 1; trk <= 10; ++trk)
          for (int w = 1; w <= 2; ++w) CHECK(d->exists(a.speechName(trk, w)));
        CHECK(a.speechName(1, 1) == "EM01.SMP" && a.speechName(2, 1) == "EF12.SMP" && a.speechName(1, 2) == "EM02.SMP");
        for (int f = 1; f <= 16; ++f) if (f != 13 || true) CHECK(d->exists(fxSampleName(Fx(f))));
        CHECK(d->exists("PITSLP.SMP") && d->exists("CROWDLP.SMP") && d->exists("LOW.SMP"));
      }
      {  // voice cues of mode 3: 85 entries (exe list), all samples exist; one voice at a time, no repeat of the last four
        AudioConfig cfg;
        cfg.openDevice = false;
        AudioSystem a;
        a.init(*d, cfg, nullptr);
        CHECK(a.cueCount() == 85);
        CHECK(a.cueSample(0) == "EF93.SMP" && a.cueSample(14) == "EM102.SMP" && a.cueSample(65) == "EPS0.SMP" && a.cueSample(84) == "EM101.SMP");
        CHECK(a.cueSpeaker(14) == 1 && a.cueSpeaker(23) == 10 && a.cueSpeaker(65) == 0);
        for (int c = 0; c < a.cueCount(); ++c) CHECK(d->exists(a.cueSample(c)));
        CHECK(a.playCue(14));
        CHECK(a.cueBusy());
        CHECK(!a.playCue(15));              // the previous line is still playing -> dropped
        float buf[2048];
        a.renderBlock(buf, 1024);
        float peak = 0;
        for (float v : buf) peak = std::max(peak, std::fabs(v));
        CHECK(peak > 0.05f);
        for (int i = 0; i < 400 && a.cueBusy(); ++i) a.renderBlock(buf, 1024);  // let it finish
        CHECK(!a.cueBusy());
        CHECK(!a.playCue(14));              // one of the last four cues
        CHECK(a.playCue(15));
      }
      CHECK(d->list("SMP").size() == 980);
      auto b = d->read("CRASH.SMP");
      CHECK(b && parseSample("CRASH.SMP", *b).has_value());
    }
  }
  std::printf(failures ? "audio: %d failure(s)\n" : "audio ok\n", failures);
  return failures ? 1 : 0;
}
