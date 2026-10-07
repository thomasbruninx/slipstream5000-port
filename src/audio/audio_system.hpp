// Sound system: sound effects (the original's .SMP samples through a software mixer) and music (the original's .HMP MIDI
// files through FluidSynth + a soundfont), both mixed and played through an SDL3 audio stream.
// Effect ids and their attenuation follow the original's Fx engine (docs/formats/audio.md, docs/audio.md).
#pragma once
#include <map>
#include <memory>
#include <string>

#include "audio/mixer.hpp"
#include "audio/music_player.hpp"
#include "original_formats/game_data.hpp"

struct SDL_AudioStream;

namespace slip {

// Effect ids of the original's FxPlay (RaceSlot* callers pass them in EDX; table at 0x4B7C4 maps them to the SMP slot).
enum class Fx : int {
  Crash = 1, Scrape2 = 2, Scrape1 = 3, Blaster = 4, Missile = 5, BonusCollect = 6, MineDrop = 7, WaterHit = 8,
  Explosion = 9, LaserHit = 10, Disruptor = 11, EngineStart = 12, Bomber = 13, Scramble = 14, Hyperneutron = 15, Ambler = 16,
};
const char* fxSampleName(Fx fx);  // "CRASH.SMP", ...

struct AudioConfig {
  bool enabled = true;          // false: no device, no music, no effects
  bool openDevice = true;       // false: offline mode (renderBlock() only), used by tools/tests
  std::string soundfont;        // empty = default search (see findSoundfont)
  float master = 1.0f, music = 0.8f, sfx = 1.0f;
  int sampleRate = 44100;
};

// Soundfont lookup: hint -> $SLIPSTREAM_SOUNDFONT -> <app>/../Resources/GeneralUser-GS.sf2 -> <exe dir>/resources/ ->
// source tree resources (compile-time) -> ./resources/. Returns "" when nothing exists.
std::string findSoundfont(const std::string& hint);

class AudioSystem {
 public:
  AudioSystem();
  ~AudioSystem();

  // Never fails hard: problems (no device, no soundfont) disable the affected part and are reported via *warnings.
  bool init(const GameData& data, const AudioConfig& cfg, std::string* warnings);
  void shutdown();
  bool deviceOpen() const { return stream_ != nullptr; }
  bool musicReady() const { return music_.ready(); }
  const std::string& soundfontPath() const { return soundfont_; }

  // --- effects ---
  void playFx(Fx fx, float volume = 1.0f);
  // Positional effect: the original attenuates linearly to zero at 0x11DF0 (73200) units and plays the listener's own
  // events (own ship) at full volume; no panning.
  void playFxAt(Fx fx, const double pos[3], const double listener[3], bool ownEvent);
  int engineStart();                            // looped engine sample, returns a voice id
  void engineSet(int voice, double shipSpeed);  // pitch = 1 + speed/4/65536 (FxAddEngine 0x4B86D / 0x4B570)
  void engineStop(int voice);

  // --- music ---
  bool playMusic(const std::string& hmpName, bool loop = true);
  void stopMusic();
  const std::string& currentMusic() const { return musicName_; }
  void setVolumes(float master, float music, float sfx);
  void toggleMusic() { musicOn_ = !musicOn_; applyVolumes(); }
  void toggleSfx() { sfxOn_ = !sfxOn_; applyVolumes(); }
  bool musicOn() const { return musicOn_; }
  bool sfxOn() const { return sfxOn_; }

  // Mixes effects + music into `out` (interleaved stereo float, `frames` frames; out is overwritten).
  void renderBlock(float* out, int frames);

 private:
  std::shared_ptr<const SoundSample> sample(const std::string& name);
  void applyVolumes();
  static void audioCallback(void* userdata, SDL_AudioStream* stream, int additional, int total);

  const GameData* data_ = nullptr;
  AudioConfig cfg_;
  Mixer mixer_{44100};
  MusicPlayer music_;
  SDL_AudioStream* stream_ = nullptr;
  std::map<std::string, std::shared_ptr<const SoundSample>> cache_;
  std::string soundfont_, musicName_;
  bool musicOn_ = true, sfxOn_ = true, enabled_ = false;
  bool sdlAudioInit_ = false;
};

}  // namespace slip
