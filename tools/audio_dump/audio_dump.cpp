// slipstream_audio_dump: render music and/or effects of the original game to a WAV file without a sound device.
//   slipstream_audio_dump --music INGAME2.HMP --seconds 20 --out /tmp/music.wav [--soundfont FILE] [--data DIR]
//   slipstream_audio_dump --sfx CRASH.SMP --out /tmp/crash.wav
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "audio/audio_system.hpp"

using namespace slip;

static void put32(FILE* f, uint32_t v) { std::fwrite(&v, 4, 1, f); }
static void put16(FILE* f, uint16_t v) { std::fwrite(&v, 2, 1, f); }

int main(int argc, char** argv) {
  std::string data, music, sfx, out = "audio_dump.wav", sf;
  double seconds = 10;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() { if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", a.c_str()); std::exit(2); } return std::string(argv[++i]); };
    if (a == "--data") data = next();
    else if (a == "--music") music = next();
    else if (a == "--sfx") sfx = next();
    else if (a == "--seconds") seconds = std::atof(next().c_str());
    else if (a == "--out") out = next();
    else if (a == "--soundfont") sf = next();
    else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
  }
  auto dir = findGameDirectory(data);
  std::string err;
  auto gd = dir.empty() ? nullptr : GameData::open(dir, &err);
  if (!gd) { std::fprintf(stderr, "game data not found: %s\n", err.c_str()); return 1; }
  AudioConfig cfg;
  cfg.openDevice = false;
  cfg.soundfont = sf;
  AudioSystem audio;
  std::string warn;
  audio.init(*gd, cfg, &warn);
  if (!warn.empty()) std::fputs(warn.c_str(), stderr);
  if (!music.empty() && !audio.playMusic(music, false)) { std::fprintf(stderr, "cannot play %s\n", music.c_str()); return 1; }
  if (!sfx.empty()) {
    for (int f = 1; f <= 16; ++f)
      if (sfx == fxSampleName(Fx(f))) audio.playFx(Fx(f));
  }
  const int rate = cfg.sampleRate;
  const int total = int(seconds * rate);
  std::vector<float> mix(size_t(total) * 2);
  for (int pos = 0; pos < total; pos += 1024) audio.renderBlock(mix.data() + size_t(pos) * 2, std::min(1024, total - pos));
  double peak = 0, sum = 0;
  for (float v : mix) { peak = std::max(peak, double(std::fabs(v))); sum += double(v) * v; }
  std::printf("rendered %.1f s: peak %.3f rms %.4f (music %s, soundfont %s)\n", seconds, peak, std::sqrt(sum / double(mix.size())),
              audio.musicReady() ? "ready" : "off", audio.soundfontPath().c_str());
  FILE* f = std::fopen(out.c_str(), "wb");
  if (!f) return 1;
  const uint32_t bytes = uint32_t(mix.size() * 2);
  std::fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
  put32(f, 16); put16(f, 1); put16(f, 2); put32(f, uint32_t(rate)); put32(f, uint32_t(rate) * 4); put16(f, 4); put16(f, 16);
  std::fwrite("data", 1, 4, f); put32(f, bytes);
  for (float v : mix) { const int16_t s = int16_t(std::lround(std::max(-1.0f, std::min(1.0f, v)) * 32767.0f)); std::fwrite(&s, 2, 1, f); }
  std::fclose(f);
  std::printf("wrote %s\n", out.c_str());
  return 0;
}
