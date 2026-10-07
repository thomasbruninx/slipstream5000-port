// Sound system: sound effects (the original's .SMP samples through a software mixer) and music (the original's .HMP MIDI
// files through FluidSynth + a soundfont), both mixed and played through an SDL3 audio stream.
// Effect ids and their attenuation follow the original's Fx engine (docs/formats/audio.md, docs/audio.md).
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

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

  // Per-track announcer samples (lists at 0x4B38F / 0x4B3B7 of the exe, language letter 'E' = English): `which` 1 plays at the
  // start of the countdown (0x58AF3 -> 0x4B975), 2 one second before the start (0x59063 -> 0x4B99D).
  std::string speechName(int track, int which) const;
  void playSpeech(int track, int which);
  // Ambient loops (0x4B96F / 0x4B658): 1 = PITSLP in the refuel piece, 2 = CROWDLP in CROW*/GRID* pieces, 0 = none. Fades in/out
  // over about a second; a change of loop stops the old one at once.
  void updateAmbient(double dt, int desired);

  // Voice cues of the original (VoiceCue 0x530B8): pilot and announcer lines taken from the 85 entry list of mode 3 in the
  // executable (EF*.SMP / EM*.SMP / EPS*.SMP). One voice at a time: a cue is dropped while the previous line is still playing,
  // and a cue equal to one of the last four is dropped as well. Returns true when the line started.
  bool playCue(int cue);
  int cueCount() const { return int(cues_.size()); }
  const std::string& cueSample(int cue) const;  // "EF93.SMP" ("" when out of range)
  int cueSpeaker(int cue) const;                // pilot number 1..10 of the line (entry +0x18), 0 = announcer
  bool cueBusy() const { return cueVoice_ && mixer_.active(cueVoice_); }

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
  std::string speech_[2][11];
  struct Cue { std::string name; int speaker = 0; };
  std::vector<Cue> cues_;
  int cueVoice_ = 0;
  int cueHistory_[4] = {-1, -1, -1, -1};
  int ambientId_ = 0, ambientVoice_ = 0;
  float ambientVol_ = 0;
};

}  // namespace slip
