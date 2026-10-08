// Gremlin Digital Video (.GDV): the intro / logo movies (INTRO.GDV, LOGO_S.GDV). Container: 16 byte header (magic 0x29111994, size id, frame
// count, fps, sound flags, sample rate, depth flags, fixed palette) followed, for every frame, by the audio chunk (rate / fps samples) and a video
// chunk (u16 0x1305, u16 size, u32 flags, data). Video: palette-indexed, LZ77 style delta coding against the previous frame in methods 2, 5, 6, 8.
// Written from the format description of the open decoders (FFmpeg gdv); see docs/formats/gdv.md.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace slip {

class GdvDecoder {
 public:
  bool open(std::vector<uint8_t> data, std::string* error);
  int width() const { return w_; }
  int height() const { return h_; }
  int frameCount() const { return frames_; }
  int fps() const { return fps_; }
  int audioRate() const { return audioRate_; }
  bool hasAudio() const { return audio_; }
  // Fills the frame buffer with a picture before the first frame (movies that only code changes against a still, like LOGO_S.GDV).
  void preload(const uint8_t* pixels, size_t n) { std::copy(pixels, pixels + std::min(n, frame_.size() - kPreamble), frame_.begin() + long(kPreamble)); }
  void setSize(int w, int h) { w_ = w; h_ = h; }
  // Decodes the next frame; false at the end of the stream (or on damaged data).
  bool next();
  int frameIndex() const { return index_; }  // frames decoded so far
  const uint8_t* pixels() const { return frame_.data() + kPreamble; }  // w * h palette indices
  const uint32_t* palette() const { return pal_; }  // 256 ARGB entries
  // Audio of the frame just decoded: mono unsigned 8 bit (or the converted samples), appended to `pcm` as floats -1..1.
  void appendAudio(std::vector<float>* pcm) const;
  size_t lastWritten() const { return written_; }  // bytes the last frame wrote (diagnostics)
  int unsupportedFrames() const { return unsupported_; }  // frames that used the (unported) half-size flags

 private:
  static constexpr size_t kPreamble = 4096;
  bool decompress(int method, unsigned skip, const uint8_t* src, size_t n);
  std::vector<uint8_t> data_, frame_, audioChunk_;
  size_t pos_ = 0;
  int w_ = 0, h_ = 0, frames_ = 0, fps_ = 12, index_ = 0, audioRate_ = 22050, channels_ = 1, unsupported_ = 0;
  bool method2Prefix_ = false;  // LOGO_S.GDV (size id 1): method 2 frames start with a u16
  bool audio_ = false, audio16_ = false, dpcm_ = false;
  size_t audioNominal_ = 0, audioSize_ = 0, written_ = 0;
  uint32_t pal_[256] = {};
};

}  // namespace slip
