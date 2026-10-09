// .ANN scripts (InitScript 0x5816E, interpreters 0x573B7 / 0x57A79): the TV commentary of the championship. 16 byte header (8 character name of the string table
// and sample set, then a dword at +8: 0 = the male reporter's face, nonzero = the female one), then commands (u16 opcode, little endian operands). See docs/championship.md.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "original_formats/formats.hpp"

namespace slip {

struct AnnCmd {
  enum class Op : uint16_t { WaitPiece = 0, Wait = 1, Voice = 2, End = 3, Face = 4, Message = 5, WaitDist = 6 };
  Op op = Op::Wait;
  uint32_t a = 0, b = 0;          // WaitPiece: the two dwords of the piece name; WaitDist: a = lap distance
  int ms = 0;                     // Wait
  std::string tag;                // Voice / Message: "EC01" (string table key; the sample is this tag with the language letter in front)
  std::vector<uint8_t> face;      // Face: the animation program
};

struct AnnScript {
  std::string name;               // "CHIINT  "
  uint32_t reporter = 0;          // header dword at +8 (0 male, else female)
  std::vector<AnnCmd> cmds;
};

std::optional<AnnScript> parseAnn(const Bytes& b);

}  // namespace slip
