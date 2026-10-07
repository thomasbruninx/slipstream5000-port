#include "audio/mixer.hpp"

#include <algorithm>
#include <cmath>

namespace slip {

int Mixer::play(std::shared_ptr<const SoundSample> sample, float gain, float pan, float pitch, bool loop) {
  if (!sample || sample->pcm.empty()) return 0;
  std::lock_guard<std::mutex> l(m_);
  Voice v;
  v.sample = std::move(sample);
  v.gain = gain; v.pan = pan; v.pitch = pitch; v.loop = loop;
  v.id = nextId_++;
  if (nextId_ <= 0) nextId_ = 1;
  v.age = ++clock_;
  if (int(voices_.size()) >= kMaxVoices) {  // steal the oldest non-looping voice, else the quietest
    size_t victim = voices_.size();
    for (size_t i = 0; i < voices_.size(); ++i)
      if (!voices_[i].loop && (victim == voices_.size() || voices_[i].age < voices_[victim].age)) victim = i;
    if (victim == voices_.size()) {
      victim = 0;
      for (size_t i = 1; i < voices_.size(); ++i) if (voices_[i].gain < voices_[victim].gain) victim = i;
    }
    voices_.erase(voices_.begin() + long(victim));
  }
  voices_.push_back(std::move(v));
  return voices_.back().id;
}

void Mixer::set(int voice, float gain, float pan, float pitch) {
  std::lock_guard<std::mutex> l(m_);
  for (Voice& v : voices_)
    if (v.id == voice) { v.gain = gain; v.pan = pan; v.pitch = pitch; return; }
}

void Mixer::stop(int voice) {
  std::lock_guard<std::mutex> l(m_);
  voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [&](const Voice& v) { return v.id == voice; }), voices_.end());
}

void Mixer::stopAll() {
  std::lock_guard<std::mutex> l(m_);
  voices_.clear();
}

bool Mixer::active(int voice) const {
  std::lock_guard<std::mutex> l(m_);
  for (const Voice& v : voices_) if (v.id == voice) return true;
  return false;
}

int Mixer::activeVoices() const {
  std::lock_guard<std::mutex> l(m_);
  return int(voices_.size());
}

void Mixer::render(float* out, int frames) {
  std::lock_guard<std::mutex> l(m_);
  for (Voice& v : voices_) {
    const auto& pcm = v.sample->pcm;
    const double n = double(pcm.size());
    const double step = double(v.sample->rate) * double(v.pitch) / double(outRate_);
    // equal-power pan: -1 = left, +1 = right
    const float a = (std::clamp(v.pan, -1.0f, 1.0f) + 1.0f) * 0.78539816f;
    const float gl = v.gain * master_ * std::cos(a) * 1.41421356f, gr = v.gain * master_ * std::sin(a) * 1.41421356f;  // centre = unity on both sides
    double pos = v.pos;
    int i = 0;
    for (; i < frames; ++i) {
      if (pos >= n) {
        if (!v.loop) break;
        pos = std::fmod(pos, n);
      }
      const size_t i0 = size_t(pos);
      const size_t i1 = i0 + 1 < pcm.size() ? i0 + 1 : (v.loop ? 0 : i0);
      const float f = float(pos - double(i0));
      const float s = pcm[i0] + (pcm[i1] - pcm[i0]) * f;
      out[2 * i] += s * gl;
      out[2 * i + 1] += s * gr;
      pos += step;
    }
    v.pos = pos;
    if (!v.loop && pos >= n) v.sample.reset();  // finished: removed below
  }
  voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return !v.sample; }), voices_.end());
}

}  // namespace slip
