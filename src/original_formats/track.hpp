// Track data: .TRK (cells/start grid), .TRC (geometry records), .TRD (placements/objects).
// CONFIRMED: container structure, piece placement (world = piecePos + (vertex << 6)).
// UNKNOWN: TRK cell semantics, TRD auxiliary lists, scenery orientation convention.
#pragma once
#include <array>
#include <string>
#include <vector>

#include "original_formats/formats.hpp"

namespace slip {

struct TrackPolygon {
  std::vector<uint16_t> index;
  int16_t nx = 0, ny = 0, nz = 0;
  uint16_t material = 0;  // local id in the track's TRC material table
  std::vector<std::array<uint16_t, 2>> uv;  // empty when no texture coordinates
};
struct TrackRecord {
  uint32_t offset = 0;                 // offset of the record inside the .TRC
  std::vector<Vec3i> verts;            // local vertices, already << 6
  std::vector<TrackPolygon> polys;     // list A and list B concatenated
  std::string name;                    // e.g. "GRID", may be empty
  uint16_t visFlags = 0xFFFF;          // record +0x16: visibility class bits (CONFIRMED use: ANDed with the camera record's mask)
  int16_t bbox[6]{};                   // as stored (also << 6)
};
struct TrackPiece {
  int record = -1;
  Vec3i pos;
  int group = -1;
};
struct SceneryInstance {
  std::string shape;
  Vec3i pos;
  uint16_t visMask = 0xFFFF;  // entry +0x36: drawn only if (visMask & camera mask) != 0 (CONFIRMED, ShapeDraw caller 0x37931)
  int16_t matrix[9]{};  // 2.14 rows as stored; orientation convention SPECULATIVE
};
struct Track {
  int index = 0;  // 1..10
  std::string name;
  Palette palette;
  MaterialSet materials;                       // track MAT followed by CARS.MAT
  std::vector<MaterialRef> trcMaterials;       // TRC material table: local id -> name
  std::vector<TrackRecord> records;
  std::vector<TrackPiece> pieces;
  std::vector<SceneryInstance> scenery;
  std::array<Vec3i, 10> start{};
  uint16_t trkVersion = 0;
};

// Canonical file base names, in the executable's order (1..10).
const std::array<const char*, 10>& trackBaseNames();
const std::array<const char*, 10>& trackDisplayNames();

// Returns false and fills *error on failure.
bool loadTrack(const GameData& data, int index1to10, Track* out, std::string* error);

}  // namespace slip
