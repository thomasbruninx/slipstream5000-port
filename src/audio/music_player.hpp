// MIDI music through FluidSynth (soundfont rendered into the game's own mix; FluidSynth's audio drivers are not used).
// Without FluidSynth at build time (SLIP_HAVE_FLUIDSYNTH undefined) the class is a silent stub.
#pragma once
#include <memory>
#include <mutex>
#include <string>

#include "original_formats/game_data.hpp"

namespace slip {

class MusicPlayer {
 public:
  MusicPlayer();
  ~MusicPlayer();
  static bool available();

  // Creates the synth at `sampleRate` and loads the soundfont. Returns false (with *error) when that fails.
  bool init(int sampleRate, const std::string& soundfontPath, std::string* error);
  bool ready() const;

  // Starts a Standard MIDI File from memory; loops forever when `loop`.
  bool play(const Bytes& smf, bool loop);
  // Plays `intro` once, then `loop` forever (empty loop: the song just ends); the loop starts when the intro reaches `switchTick`.
  bool playSegments(const Bytes& intro, const Bytes& loop, int switchTick);
  void stop();
  bool playing() const;
  void setVolume(float v);  // 0..1 (synth gain)

  // Adds `frames` stereo frames (interleaved) of the current music to `out`.
  void render(float* out, int frames);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace slip
