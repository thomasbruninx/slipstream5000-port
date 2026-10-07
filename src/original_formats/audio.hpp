// Audio data of the original game (see docs/formats/audio.md).
//  * .SMP : raw 8-bit unsigned mono PCM, no header; played at 11025 Hz (the HMI SOS digital driver is opened at
//           0x2B11 = 11025 Hz in SoundInit 0x10AA4). CONFIRMED for the rate/format, STRONGLY INFERRED for "all samples".
//  * .HMP : HMI "HMIMIDIP013195" music files. CONFIRMED structure: header with track count (+0x30), 0x388 = first track
//           chunk {u32 track, u32 length (including this 12 byte header), u32 channel hint}, delta times are
//           little-endian base-128 with the *last* byte having bit 7 set (the opposite of Standard MIDI Files), events
//           are ordinary MIDI events (always with a status byte) and FF meta events. Controllers 108..119 carry HMI
//           loop/branch/beat data and are dropped. Timing: 120 ticks per second (CONFIRMED against the header's
//           length in seconds for INGAME2/3/4/6, LOSE, WIN).
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "original_formats/game_data.hpp"

namespace slip {

struct SoundSample {
  std::string name;
  std::vector<float> pcm;  // -1..1, mono
  int rate = 11025;
};
std::optional<SoundSample> parseSample(const std::string& name, const Bytes& smp);

struct HmpInfo {
  int tracks = 0;
  double seconds = 0;     // header field (+0x3C)
  double lengthTicks = 0; // longest track
};
// Converts an HMP file to a type-1 Standard MIDI File (division 120, tempo 1,000,000 us per quarter note, i.e. 120 ticks/s).
std::optional<Bytes> hmpToSmf(const Bytes& hmp, HmpInfo* info = nullptr);

}  // namespace slip
