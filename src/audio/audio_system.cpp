#include "audio/audio_system.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
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
  {  // announcer lists from the user's executable (4-char names, first letter = language)
    static const char* kDefault1[10] = {"EM01", "EF12", "EM09", "EF08", "EF10", "EF06", "EM03", "EM05", "EM07", "EM11"};
    static const char* kDefault2[10] = {"EM02", "EF13", "EM10", "EF09", "EF11", "EF07", "EM04", "EM06", "EM08", "EM12"};
    for (int i = 0; i < 10; ++i) { speech_[0][i + 1] = kDefault1[i]; speech_[1][i + 1] = kDefault2[i]; }
    if (auto exe = data.read("SLIPSTRM.EXE")) {
      constexpr size_t kBase = 0x4D854 - 0x10000;
      for (int w = 0; w < 2; ++w) {
        const size_t va = w == 0 ? 0x4b38f : 0x4b3b7;
        bool ok = exe->size() > kBase + va + 40;
        for (int i = 0; ok && i < 10; ++i) {
          const char* c = reinterpret_cast<const char*>(exe->data() + kBase + va + 4 * size_t(i));
          ok = c[0] == 'E' && (c[1] == 'M' || c[1] == 'F') && c[2] >= '0' && c[2] <= '9';
        }
        if (ok)
          for (int i = 0; i < 10; ++i) speech_[w][i + 1].assign(reinterpret_cast<const char*>(exe->data() + kBase + va + 4 * size_t(i)), 4);
      }
    }
  }
  cues_.clear();
  if (auto exe = data.read("SLIPSTRM.EXE")) {  // cue list of mode 3: pointer table 0x52EE4, count + 0x1C byte entries (name at +0, pilot at +0x18)
    constexpr size_t kBase = 0x4D854 - 0x10000;
    auto rd = [&](size_t va) -> uint32_t {
      const size_t o = kBase + va;
      return o + 4 <= exe->size() ? uint32_t((*exe)[o]) | (uint32_t((*exe)[o + 1]) << 8) | (uint32_t((*exe)[o + 2]) << 16) | (uint32_t((*exe)[o + 3]) << 24) : 0u;
    };
    narration_.clear();
    if (const uint32_t p2 = rd(0x52EE4 + 2 * 4)) {  // mode 2: the ten pilot narrations
      const size_t l2 = size_t(p2) + 0x10000;
      const uint32_t n2 = rd(l2);
      if (n2 == 10)
        for (uint32_t i = 0; i < n2; ++i) {
          const char* nm = reinterpret_cast<const char*>(exe->data() + kBase + l2 + 4 + size_t(i) * 0x1C);
          narration_.emplace_back(nm, strnlen(nm, 14));
        }
    }
    resultCues_.clear();
    if (const uint32_t p1 = rd(0x52EE4 + 1 * 4)) {  // mode 1: the results screen lines
      const size_t l1 = size_t(p1) + 0x10000;
      const uint32_t n1 = rd(l1);
      if (n1 == 12)
        for (uint32_t i = 0; i < n1; ++i) {
          const char* nm = reinterpret_cast<const char*>(exe->data() + kBase + l1 + 4 + size_t(i) * 0x1C);
          resultCues_.emplace_back(nm, strnlen(nm, 14));
        }
    }
    const uint32_t ptr = rd(0x52EE4 + 3 * 4);
    if (ptr) {
      const size_t list = size_t(ptr) + 0x10000;
      const uint32_t n = rd(list);
      if (n > 0 && n < 200 && kBase + list + 4 + size_t(n) * 0x1C <= exe->size()) {
        for (uint32_t i = 0; i < n; ++i) {
          const size_t e = list + 4 + size_t(i) * 0x1C;
          const char* nm = reinterpret_cast<const char*>(exe->data() + kBase + e);
          Cue c;
          c.name.assign(nm, strnlen(nm, 14));
          c.speaker = int(rd(e + 0x18));
          cues_.push_back(std::move(c));
        }
      }
    }
  }
  for (int& h : cueHistory_) h = -1;
  cueVoice_ = 0;
  applyVolumes();
  if (stream_) SDL_ResumeAudioStreamDevice(stream_);
  return true;
}

const std::string& AudioSystem::cueSample(int cue) const {
  static const std::string kEmpty;
  return cue >= 0 && cue < int(cues_.size()) ? cues_[size_t(cue)].name : kEmpty;
}

int AudioSystem::cueSpeaker(int cue) const { return cue >= 0 && cue < int(cues_.size()) ? cues_[size_t(cue)].speaker : 0; }

const std::string& AudioSystem::narrationSample(int ship) const {
  static const std::string kEmpty;
  return ship >= 0 && ship < int(narration_.size()) ? narration_[size_t(ship)] : kEmpty;
}

bool AudioSystem::playNarration(int ship) {
  if (!enabled_ || ship < 0 || ship >= int(narration_.size())) return false;
  if (cueVoice_) mixer_.stop(cueVoice_);
  cueVoice_ = 0;
  auto s = sample(narration_[size_t(ship)]);
  if (!s) return false;
  cueVoice_ = mixer_.play(s, 1.0f);
  cueSpeakerNow_ = 0;
  return cueVoice_ != 0;
}

bool AudioSystem::playResultCue(int place, unsigned rand) {
  static const int kPlace[12] = {2, 0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0};  // 0x5ACBC
  if (!enabled_ || resultCues_.empty()) return false;
  const int entry = place == 1 ? int(rand & 1) : kPlace[std::clamp(place, 0, 11)];
  if (entry < 0 || size_t(entry) >= resultCues_.size()) return false;
  if (cueVoice_) mixer_.stop(cueVoice_);
  cueVoice_ = 0;
  auto s = sample(resultCues_[size_t(entry)]);
  if (!s) return false;
  cueVoice_ = mixer_.play(s, 1.0f);
  cueSpeakerNow_ = 0;
  return cueVoice_ != 0;
}

bool AudioSystem::playCue(int cue, bool menu) {
  if (!enabled_ || cue < 0 || cue >= int(cues_.size())) return false;
  if (menu) { if (cueVoice_) mixer_.stop(cueVoice_); cueVoice_ = 0; }
  else {
  for (int h : cueHistory_) if (h == cue) return false;        // 0x530DC..0x53106: not one of the last four cues again
  if (cueVoice_ && mixer_.active(cueVoice_)) return false; }     // 0x53131..0x5313F: the previous line is still playing -> dropped
  auto s = sample(cues_[size_t(cue)].name);
  if (!s) return false;
  cueVoice_ = mixer_.play(s, 1.0f);
  for (int i = 3; i > 0; --i) cueHistory_[i] = cueHistory_[i - 1];  // 0x531B5..0x531E4
  cueHistory_[0] = cue;
  cueSpeakerNow_ = cues_[size_t(cue)].speaker;  // 0x531E1: [0x52EF4] = entry +0x18
  return cueVoice_ != 0;
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

void AudioSystem::playNamed(const std::string& name, float volume) {
  if (!enabled_) return;
  if (auto s = sample(name)) mixer_.play(s, volume);
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
  if (voice) mixer_.set(voice, 0.8f * engineGain_, 0.0f, float(1.0 + shipSpeed / 4.0 / 65536.0));
}

void AudioSystem::engineStop(int voice) { if (voice) mixer_.stop(voice); }

std::string AudioSystem::speechName(int track, int which) const {
  if (track < 1 || track > 10 || which < 1 || which > 2) return "";
  return speech_[which - 1][track] + ".SMP";
}

void AudioSystem::playSpeech(int track, int which) {
  if (!enabled_) return;
  const std::string n = speechName(track, which);
  if (n.empty()) return;
  if (auto s = sample(n)) mixer_.play(s, 1.0f);
}

void AudioSystem::updateAmbient(double dt, int desired) {
  if (!enabled_) return;
  const float rate = float(2.0 * dt);  // 4B658: volume += 2*dt (2.14) towards 0x7FFF, i.e. a full fade in about 1 s
  if (desired != 0) {
    if (ambientId_ != desired) {  // new loop replaces the old one (the volume carries over)
      if (ambientVoice_) mixer_.stop(ambientVoice_);
      ambientVoice_ = 0;
      if (auto s = sample(desired == 1 ? "PITSLP.SMP" : "CROWDLP.SMP")) ambientVoice_ = mixer_.play(s, ambientVol_, 0.0f, 1.0f, true);
      ambientId_ = desired;
    }
    ambientVol_ = std::min(1.0f, ambientVol_ + rate);
  } else if (ambientId_ != 0) {
    ambientVol_ = std::max(0.0f, ambientVol_ - rate);
    if (ambientVol_ <= 0) {
      if (ambientVoice_) mixer_.stop(ambientVoice_);
      ambientVoice_ = 0;
      ambientId_ = 0;
    }
  }
  if (ambientVoice_) mixer_.set(ambientVoice_, ambientVol_ * 0.8f, 0.0f, 1.0f);
}

bool AudioSystem::playMusic(const std::string& hmpName, bool loop) {
  if (!enabled_ || !music_.ready() || !data_) return false;
  auto b = data_->read(hmpName);
  if (!b) return false;
  auto seg = hmpToSegments(*b);
  if (!seg) return false;
  musicName_ = hmpName;
  musicPart_ = 0;
  // HMI loop markers: intro up to the first loop end, then that loop forever; songs without markers loop whole when asked
  if (!seg->loop.empty()) return music_.playSegments(seg->intro, seg->loop, int(seg->loopEndTick - seg->introStartTick));
  return music_.play(seg->intro, loop);
}

bool AudioSystem::setMusicPart(int part) {
  if (!enabled_ || !music_.ready() || !data_ || musicName_ != "INTRO.HMP") return false;
  part = std::clamp(part, 0, 10);
  if (part == musicPart_) return true;
  int id = 0x3f;  // waiting music (0x55F14)
  if (part > 0)
    if (auto exe = data_->read("SLIPSTRM.EXE")) {
      const size_t o = 0x4D854 + (0x55f24 - 0x10000) + 4 * size_t(part);
      if (o + 4 <= exe->size()) id = int((*exe)[o] | ((*exe)[o + 1] << 8));
    }
  auto b = data_->read(musicName_);
  if (!b) return false;
  auto seg = hmpLocationSegments(*b, id);
  if (!seg) return false;
  musicPart_ = part;
  return music_.playSegments(seg->intro, seg->loop, int(seg->loopEndTick - seg->introStartTick));
}

void AudioSystem::stopMusic() {
  music_.stop();
  musicName_.clear();
  musicPart_ = 0;
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
