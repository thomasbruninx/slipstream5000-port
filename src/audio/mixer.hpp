// Software sample mixer (no SDL, unit-testable): up to 32 voices of mono float samples with gain, pan, pitch and looping,
// mixed into an interleaved stereo float buffer. Safe to call from the game thread while another thread renders.
#pragma once
#include <memory>
#include <mutex>
#include <vector>

#include "original_formats/audio.hpp"

namespace slip {

class Mixer {
 public:
  explicit Mixer(int outRate = 44100) : outRate_(outRate) {}
  int outputRate() const { return outRate_; }
  void setOutputRate(int r) { std::lock_guard<std::mutex> l(m_); outRate_ = r; }

  // Returns a voice id (> 0) or 0 when nothing could be started. pitch 1.0 = the sample's own rate.
  int play(std::shared_ptr<const SoundSample> sample, float gain = 1.0f, float pan = 0.0f, float pitch = 1.0f, bool loop = false);
  void set(int voice, float gain, float pan, float pitch);  // no-op for finished voices
  void stop(int voice);
  void stopAll();
  bool active(int voice) const;
  int activeVoices() const;
  void setMasterVolume(float v) { std::lock_guard<std::mutex> l(m_); master_ = v; }

  // Adds the mix of all voices to `out` (frames * 2 floats, left/right interleaved).
  void render(float* out, int frames);

 private:
  struct Voice {
    std::shared_ptr<const SoundSample> sample;
    double pos = 0;
    float gain = 1, pan = 0, pitch = 1;
    bool loop = false;
    int id = 0;
    unsigned long long age = 0;
  };
  static constexpr int kMaxVoices = 32;
  mutable std::mutex m_;
  std::vector<Voice> voices_;
  int outRate_;
  int nextId_ = 1;
  float master_ = 1.0f;
  unsigned long long clock_ = 0;
};

}  // namespace slip
