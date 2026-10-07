#include "audio/audio_system.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "original_formats/audio.hpp"

#ifndef SLIP_SOURCE_RESOURCE_DIR
#define SLIP_SOURCE_RESOURCE_DIR ""
#endif

namespace slip {

namespace fs = std::filesystem;

const char* fxSampleName(Fx fx) {
  switch (fx) {  // pointer table 0x4B7C4 -> sample slot -> name list at 0x4AEBE
    case Fx::Crash: return "CRASH.SMP";
    case Fx::Scrape2: return "SCRAPE2.SMP";
    case Fx::Scrape1: return "SCRAPE1.SMP";
    case Fx::Blaster: return "BLASTER.SMP";
    case Fx::Missile: return "MISSILE.SMP";
    case Fx::BonusCollect: return "BONUSCOL.SMP";
    case Fx::MineDrop: return "MINEDROP.SMP";
    case Fx::WaterHit: return "WATERHIT.SMP";
    case Fx::Explosion: return "EXPLOSN.SMP";
    case Fx::LaserHit: return "LASERHIT.SMP";
    case Fx::Disruptor: return "DISRUPTR.SMP";
    case Fx::EngineStart: return "ENGSTART.SMP";
    case Fx::Bomber: return "BOMBER.SMP";
    case Fx::Scramble: return "SCRAMBLE.SMP";
    case Fx::Hyperneutron: return "HYPERNEU.SMP";
    case Fx::Ambler: return "AMBLER.SMP";
  }
  return "";
}

std::string findSoundfont(const std::string& hint) {
  std::vector<fs::path> cands;
  if (!hint.empty()) return fs::exists(hint) ? hint : std::string();  // an explicit choice is never silently replaced
  if (const char* env = std::getenv("SLIPSTREAM_SOUNDFONT")) cands.push_back(env);
  const char* base = SDL_GetBasePath();
  if (base) {
    cands.push_back(fs::path(base) / ".." / "Resources" / "GeneralUser-GS.sf2");  // inside Slipstream.app/Contents/MacOS
    cands.push_back(fs::path(base) / "resources" / "GeneralUser-GS.sf2");
    cands.push_back(fs::path(base) / ".." / "resources" / "GeneralUser-GS.sf2");
  }
  if (std::string(SLIP_SOURCE_RESOURCE_DIR).size()) cands.push_back(fs::path(SLIP_SOURCE_RESOURCE_DIR) / "GeneralUser-GS.sf2");
  cands.push_back(fs::path("resources") / "GeneralUser-GS.sf2");
  for (const auto& c : cands) {
    std::error_code ec;
    if (fs::exists(c, ec)) return fs::weakly_canonical(c, ec).string();
  }
  return "";
}

AudioSystem::AudioSystem() = default;
AudioSystem::~AudioSystem() { shutdown(); }

bool AudioSystem::init(const GameData& data, const AudioConfig& cfg, std::string* warnings) {
  shutdown();
  data_ = &data;
  cfg_ = cfg;
  auto warn = [&](const std::string& s) { if (warnings) *warnings += s + "\n"; };
  if (!cfg.enabled) return false;
  enabled_ = true;
  int rate = cfg.sampleRate;
  if (cfg.openDevice) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
      warn(std::string("audio: SDL audio unavailable: ") + SDL_GetError());
      enabled_ = false;
      return false;
    }
    sdlAudioInit_ = true;
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = cfg.sampleRate;
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioSystem::audioCallback, this);
    if (!stream_) {
      warn(std::string("audio: cannot open the playback device: ") + SDL_GetError());
      enabled_ = false;
      return false;
    }
  }
  mixer_.setOutputRate(rate);
  soundfont_ = findSoundfont(cfg.soundfont);
  if (soundfont_.empty()) {
    warn(cfg.soundfont.empty() ? "audio: GeneralUser-GS.sf2 not found, music is off (use --soundfont FILE)"
                               : "audio: soundfont '" + cfg.soundfont + "' not found, music is off");
  } else if (!MusicPlayer::available()) {
    warn("audio: built without FluidSynth, music is off");
  } else {
    std::string err;
    if (!music_.init(rate, soundfont_, &err)) warn("audio: " + err);
  }
  applyVolumes();
  if (stream_) SDL_ResumeAudioStreamDevice(stream_);
  return true;
}

void AudioSystem::shutdown() {
  if (stream_) {
    SDL_DestroyAudioStream(stream_);  // also closes the device; stops the callback before the members go away
    stream_ = nullptr;
  }
  if (sdlAudioInit_) { SDL_QuitSubSystem(SDL_INIT_AUDIO); sdlAudioInit_ = false; }
  mixer_.stopAll();
  music_.stop();
  enabled_ = false;
}

std::shared_ptr<const SoundSample> AudioSystem::sample(const std::string& name) {
  auto it = cache_.find(name);
  if (it != cache_.end()) return it->second;
  std::shared_ptr<const SoundSample> out;
  if (data_)
    if (auto b = data_->read(name))
      if (auto s = parseSample(name, *b)) out = std::make_shared<SoundSample>(std::move(*s));
  cache_[name] = out;
  return out;
}

void AudioSystem::applyVolumes() {
  mixer_.setMasterVolume(sfxOn_ ? cfg_.master * cfg_.sfx : 0.0f);
  music_.setVolume(musicOn_ ? cfg_.master * cfg_.music : 0.0f);
}

void AudioSystem::setVolumes(float master, float music, float sfx) {
  cfg_.master = master; cfg_.music = music; cfg_.sfx = sfx;
  applyVolumes();
}

void AudioSystem::playFx(Fx fx, float volume) {
  if (!enabled_) return;
  if (auto s = sample(fxSampleName(fx))) mixer_.play(s, volume);
}

void AudioSystem::playFxAt(Fx fx, const double pos[3], const double listener[3], bool ownEvent) {
  if (!enabled_) return;
  float vol = 1.0f;
  if (!ownEvent) {
    const double d[3] = {pos[0] - listener[0], pos[1] - listener[1], pos[2] - listener[2]};
    // 0x21F87: max + (mid + min) / 4
    double a[3] = {std::fabs(d[0]), std::fabs(d[1]), std::fabs(d[2])};
    std::sort(a, a + 3);
    const double dist = a[2] + (a[1] + a[0]) * 0.25;
    if (dist > 0x11df0) return;  // out of earshot (0x4B620)
    vol = float((0x11df0 - dist) / 0x11df0);  // 0x4B777..0x4B78C: volume = 0x7FFF * (0x11DF0 - d) / 0x11DF0
  }
  playFx(fx, vol);
}

int AudioSystem::engineStart() {
  if (!enabled_) return 0;
  auto s = sample("LOW.SMP");  // [0x4B004]: the engine loop of FxAddEngine
  return s ? mixer_.play(s, 0.8f, 0.0f, 1.0f, true) : 0;
}

void AudioSystem::engineSet(int voice, double shipSpeed) {
  if (voice) mixer_.set(voice, 0.8f, 0.0f, float(1.0 + shipSpeed / 4.0 / 65536.0));
}

void AudioSystem::engineStop(int voice) { if (voice) mixer_.stop(voice); }

bool AudioSystem::playMusic(const std::string& hmpName, bool loop) {
  if (!enabled_ || !music_.ready() || !data_) return false;
  auto b = data_->read(hmpName);
  if (!b) return false;
  auto smf = hmpToSmf(*b);
  if (!smf) return false;
  musicName_ = hmpName;
  return music_.play(*smf, loop);
}

void AudioSystem::stopMusic() {
  music_.stop();
  musicName_.clear();
}

void AudioSystem::renderBlock(float* out, int frames) {
  std::fill(out, out + size_t(frames) * 2, 0.0f);
  mixer_.render(out, frames);
  music_.render(out, frames);
  for (int i = 0; i < frames * 2; ++i) out[i] = std::clamp(out[i], -1.0f, 1.0f);
}

void AudioSystem::audioCallback(void* userdata, SDL_AudioStream* stream, int additional, int /*total*/) {
  auto* self = static_cast<AudioSystem*>(userdata);
  constexpr int kChunk = 1024;
  std::vector<float> buf(size_t(kChunk) * 2);
  int frames = additional / int(2 * sizeof(float));
  while (frames > 0) {
    const int n = std::min(frames, kChunk);
    self->renderBlock(buf.data(), n);
    SDL_PutAudioStreamData(stream, buf.data(), n * 2 * int(sizeof(float)));
    frames -= n;
  }
}

}  // namespace slip
