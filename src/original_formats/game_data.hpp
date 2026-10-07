// Access to the user's original Slipstream 5000 installation (read-only).
// Implements the .RES archive container (see docs/formats/res-archive.md) and the
// engine's resolution order: primary archive -> secondary archive -> loose file.
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace slip {

using Bytes = std::vector<uint8_t>;

// Normalises "chiclwa2.spr" / "NAME    .EXT" to "NAME.EXT" (upper case, no padding).
std::string normalizeName(const std::string& n);

class GameData {
 public:
  // dir = folder containing SLIPSTRM.RES / SLIPCD.RES (or the GOG install root).
  static std::unique_ptr<GameData> open(const std::filesystem::path& dir, std::string* error);
  ~GameData();

  bool exists(const std::string& name) const;
  std::optional<Bytes> read(const std::string& name) const;
  // Names (normalised) of all entries with the given extension (without dot), sorted.
  std::vector<std::string> list(const std::string& ext) const;
  // '*' / '?' wildcard match like the engine's ResFindIDs; returns sorted names.
  std::vector<std::string> glob(const std::string& pattern) const;

  const std::filesystem::path& root() const { return root_; }
  size_t entryCount() const { return index_.size(); }
  std::vector<std::string> archiveNames() const;

 private:
  struct Archive;
  struct Entry {
    int archive;
    uint32_t offset, size;
  };
  GameData() = default;
  bool addArchive(const std::filesystem::path& p, std::string* error);

  std::filesystem::path root_;
  std::vector<std::unique_ptr<Archive>> archives_;
  std::map<std::string, Entry> index_;  // first archive wins (primary before secondary)
};

// Searches a few likely locations (env SLIPSTREAM_DATA, cwd, ~/Downloads/slip5000...).
std::filesystem::path findGameDirectory(const std::string& hint);
// Remembers the folder in ~/Library/Application Support/Slipstream/data_dir.txt for later launches.
void rememberGameDirectory(const std::filesystem::path& dir);

}  // namespace slip
