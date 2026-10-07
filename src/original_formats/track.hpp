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
  uint16_t flags = 0;     // TRC polygon +8: bit0 = portal to a neighbouring piece (CONFIRMED, 0x3A069/0x3A14x), bit3 = piece-extent frame
  uint8_t list = 0;       // 0 = list at TRC record +4 (also the cell-bounding planes), 1 = list at +6
  uint32_t offset = 0;    // offset of this polygon in the .TRC (portal links refer to it)
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
struct PieceLink {  // TRD piece entry +4/+8/+0xC: neighbour piece reached through a portal polygon
  uint32_t pieceOffset = 0;  // TRD offset of the neighbour entry (0 = none)
  uint32_t portalOffset = 0; // TRC offset of the portal polygon in this piece's record
};
struct TrackNode {  // path node of the racing line (TRD node list, header +8; 0x32 bytes each; used by the AI, 0x3BEA2)
  uint32_t offset = 0;
  int next = -1, prev = -1, alt = -1;  // +0 next, +2 previous, +4 alternative route (refuel branch)
  Vec3i pos;                         // +0xC
  int32_t width = 0;                 // +0x18
  uint16_t merge = 0;                // +6: non-zero ends an alternative route's walk to the refuel pad (InitRefuel 0x3D6FC)
  bool pit = false;                  // runtime +0x28 = 0xFFFF: the alternative route from this node passes the refuel pad (InitRefuel 0x3D568)
  int32_t remain = 0;                // +0x24: distance along the route to the lap line (rank key, 0x3BD0D)
  uint16_t straight = 0x8000;        // +8: 0x8000 = no turn ahead (0x351A4 sums 0x8000 - this)
};
struct TrackPiece {
  uint16_t light = 0x4000;     // TRD entry +0x20 (TrackSlotGetLight 0x3526C): below 0x2000 the cockpit console switches to its dark frame
  int node = -1;               // TRD entry +0x1E: path node of this piece
  uint32_t trdOffset = 0;
  PieceLink links[3];
  int record = -1;
  Vec3i pos;
  int group = -1;
};
struct SceneryInstance {
  uint32_t entryOffset = 0;   // TRD offset of the entry (identifies it in group trees)
  std::string shape;
  Vec3i pos;
  int group = -1;             // TRD group index (BSP leaf)
  uint16_t visMask = 0xFFFF;  // entry +0x36: drawn only if (visMask & camera mask) != 0 (CONFIRMED, ShapeDraw caller 0x37931)
  uint32_t radius = 0;        // entry +0x1C: bounding radius used for the projected-size cull (CONFIRMED use, 0x37931)
  bool billboard = false;     // entry +0x38 != 0: orientation replaced by a yaw facing the camera (CONFIRMED, 0x379C9)
  int16_t matrix[9]{};  // 2.14 rows as stored; orientation convention SPECULATIVE
};
// TRK cell table = binary space partition over a lattice of 2^20-unit cells (CONFIRMED on all 10 tracks):
// internal record: `kind` selects an axis-aligned lattice plane, child +2 is the HIGH side, child +4 the LOW side;
// leaf (kind 0xFFFF): +6 = TRD group offset whose pieces and scenery lie inside the cell. The engine walks it
// back-to-front relative to the camera (0x37601), which is the painter's order of the track draw.
struct BspNode {
  int axis = -1;   // 0 x, 1 y, 2 z; -1 for a leaf
  int point = 0;   // plane position in lattice cells (world = point * 2^20)
  int hi = -1, lo = -1;  // child node indices
  int group = -1;  // leaf: TRD group index
};
// Per-group draw-order tree (TRD group +6; same format as the SHP sort tree walked by 0x2739C, CONFIRMED):
// 24-byte nodes {u32 childA, u32 childB, u32 item, u16 type, s16 d (-1 = leaf), s16 normal[3] (2.14)}.
// Child A lies on the positive side of the plane, B on the negative side (verified on all groups); the walk
// visits the side NOT containing the camera first, then the node's own item, then the camera's side.
struct GroupTreeNode {
  int a = -1, b = -1;     // child node indices
  uint32_t item = 0;      // TRD offset of a piece entry (type 2) or scenery entry (other types); 0 = none
  uint16_t type = 0;
  bool leaf = false;
  float n[3] = {0, 0, 0};  // unit normal
  float plane = 0;         // plane offset in world units: n.p = plane (plane passes through the group point `point`)
  int point = -1;          // group point index (node +0x10, CONFIRMED: plane through that point, 0x193FF/0x273C8)
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
  std::vector<TrackNode> nodes;
  int lapPieceA = -1, lapPieceB = -1;  // TRD header +4 / +6 (TrackSlotGetLapPieces 0x34C32 / 0x34C3F): the lap line lies between these two pieces
  int refuelPiece = -1;        // piece holding the "Refuel 3" polygon (InitRefuel), -1 = none
  std::vector<BspNode> bsp;   // bsp[0] is the root (first TRK cell record)
  int groupCount = 0;
  std::vector<std::vector<GroupTreeNode>> groupTrees;  // per group; node 0 is the root
  std::vector<std::array<int32_t, 3>> pieceEntryPos, sceneryEntryPos;  // unused helpers (kept empty)
  std::array<Vec3i, 10> start{};
  uint16_t trkVersion = 0;
  bool portalOnly = false;  // TRK +0x9E != 0 ([0x33D08]): only portal-reached pieces are drawn (CONFIRMED use, 0x3A60C)
};

// Canonical file base names, in the executable's order (1..10).
const std::array<const char*, 10>& trackBaseNames();
const std::array<const char*, 10>& trackDisplayNames();

// Returns false and fills *error on failure.
bool loadTrack(const GameData& data, int index1to10, Track* out, std::string* error);

}  // namespace slip
