#include "audio/music_player.hpp"

#include <algorithm>
#include <vector>

#ifdef SLIP_HAVE_FLUIDSYNTH
#include <fluidsynth.h>
#endif

namespace slip {

#ifdef SLIP_HAVE_FLUIDSYNTH

struct MusicPlayer::Impl {
  fluid_settings_t* settings = nullptr;
  fluid_synth_t* synth = nullptr;
  fluid_player_t* player = nullptr;
  std::mutex m;
  std::vector<float> tmp;
  float volume = 1.0f;
  bool loop = false;
  Bytes pendingLoop;  // started when the intro player reaches switchTick
  int switchTick = 0;
  bool startPlayer(const Bytes& smf, int loops);
  ~Impl() {
    if (player) delete_fluid_player(player);
    if (synth) delete_fluid_synth(synth);
    if (settings) delete_fluid_settings(settings);
  }
};

bool MusicPlayer::Impl::startPlayer(const Bytes& smf, int loops) {  // caller holds m
  if (player) {
    fluid_player_stop(player);
    delete_fluid_player(player);
    player = nullptr;
  }
  fluid_synth_all_sounds_off(synth, -1);
  fluid_synth_system_reset(synth);
  player = new_fluid_player(synth);
  if (!player) return false;
  if (fluid_player_add_mem(player, smf.data(), smf.size()) != FLUID_OK) {
    delete_fluid_player(player);
    player = nullptr;
    return false;
  }
  fluid_player_set_loop(player, loops);
  return fluid_player_play(player) == FLUID_OK;
}

MusicPlayer::MusicPlayer() : impl_(std::make_unique<Impl>()) {}
MusicPlayer::~MusicPlayer() = default;
bool MusicPlayer::available() { return true; }

bool MusicPlayer::init(int sampleRate, const std::string& soundfontPath, std::string* error) {
  std::lock_guard<std::mutex> l(impl_->m);
  fluid_set_log_function(FLUID_INFO, nullptr, nullptr);  // keep the console quiet
  fluid_set_log_function(FLUID_DBG, nullptr, nullptr);
  impl_->settings = new_fluid_settings();
  if (!impl_->settings) { if (error) *error = "FluidSynth: cannot create settings"; return false; }
  fluid_settings_setnum(impl_->settings, "synth.sample-rate", double(sampleRate));
  fluid_settings_setint(impl_->settings, "synth.polyphony", 128);
  fluid_settings_setnum(impl_->settings, "synth.gain", 0.6);
  fluid_settings_setint(impl_->settings, "synth.threadsafe-api", 1);
  impl_->synth = new_fluid_synth(impl_->settings);
  if (!impl_->synth) { if (error) *error = "FluidSynth: cannot create synthesizer"; return false; }
  if (fluid_synth_sfload(impl_->synth, soundfontPath.c_str(), 1) < 0) {
    if (error) *error = "FluidSynth: cannot load soundfont '" + soundfontPath + "'";
    delete_fluid_synth(impl_->synth);
    impl_->synth = nullptr;
    return false;
  }
  return true;
}

bool MusicPlayer::ready() const { return impl_->synth != nullptr; }

bool MusicPlayer::play(const Bytes& smf, bool loop) {
  std::lock_guard<std::mutex> l(impl_->m);
  if (!impl_->synth) return false;
  impl_->pendingLoop.clear();
  impl_->loop = loop;
  return impl_->startPlayer(smf, loop ? -1 : 1);
}

bool MusicPlayer::playSegments(const Bytes& intro, const Bytes& loop, int switchTick) {
  std::lock_guard<std::mutex> l(impl_->m);
  if (!impl_->synth) return false;
  impl_->pendingLoop = loop;
  impl_->switchTick = switchTick;
  impl_->loop = false;
  return impl_->startPlayer(intro, 1);
}

void MusicPlayer::stop() {
  std::lock_guard<std::mutex> l(impl_->m);
  impl_->pendingLoop.clear();
  if (impl_->player) {
    fluid_player_stop(impl_->player);
    delete_fluid_player(impl_->player);
    impl_->player = nullptr;
  }
  if (impl_->synth) fluid_synth_all_sounds_off(impl_->synth, -1);
}

bool MusicPlayer::playing() const {
  std::lock_guard<std::mutex> l(impl_->m);
  return impl_->player && fluid_player_get_status(impl_->player) == FLUID_PLAYER_PLAYING;
}

void MusicPlayer::setVolume(float v) {
  std::lock_guard<std::mutex> l(impl_->m);
  impl_->volume = std::clamp(v, 0.0f, 1.0f);
}

void MusicPlayer::render(float* out, int frames) {
  std::lock_guard<std::mutex> l(impl_->m);
  if (!impl_->synth || !impl_->player) return;
  if (!impl_->pendingLoop.empty() && (fluid_player_get_status(impl_->player) != FLUID_PLAYER_PLAYING || fluid_player_get_current_tick(impl_->player) >= impl_->switchTick)) {  // intro reached the loop end: loop
    Bytes loop = std::move(impl_->pendingLoop);
    impl_->pendingLoop.clear();
    // keep ringing notes: no all-sounds-off / reset between the segments
    fluid_player_stop(impl_->player);
    delete_fluid_player(impl_->player);
    impl_->player = new_fluid_player(impl_->synth);
    if (!impl_->player || fluid_player_add_mem(impl_->player, loop.data(), loop.size()) != FLUID_OK) return;
    fluid_player_set_loop(impl_->player, -1);
    fluid_player_play(impl_->player);
  }
  if (impl_->volume <= 0) return;
  impl_->tmp.assign(size_t(frames) * 2, 0.0f);
  if (fluid_synth_write_float(impl_->synth, frames, impl_->tmp.data(), 0, 2, impl_->tmp.data(), 1, 2) != FLUID_OK) return;
  for (int i = 0; i < frames * 2; ++i) out[i] += impl_->tmp[size_t(i)] * impl_->volume;
}

#else  // no FluidSynth: silent stub

struct MusicPlayer::Impl {};
MusicPlayer::MusicPlayer() : impl_(std::make_unique<Impl>()) {}
MusicPlayer::~MusicPlayer() = default;
bool MusicPlayer::available() { return false; }
bool MusicPlayer::init(int, const std::string&, std::string* error) {
  if (error) *error = "built without FluidSynth: music is disabled";
  return false;
}
bool MusicPlayer::ready() const { return false; }
bool MusicPlayer::play(const Bytes&, bool) { return false; }
bool MusicPlayer::playSegments(const Bytes&, const Bytes&, int) { return false; }
void MusicPlayer::stop() {}
bool MusicPlayer::playing() const { return false; }
void MusicPlayer::setVolume(float) {}
void MusicPlayer::render(float*, int) {}

#endif

}  // namespace slip
