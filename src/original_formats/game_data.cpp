#include "original_formats/game_data.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "original_formats/user_dir.hpp"

namespace slip {

namespace {
const char kKey[] = "SOFTWAREREFINERY";  // XOR key for archive name fields (exe VA 0x1F180)

uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }

bool wildMatch(const char* p, const char* s) {
  if (!*p) return !*s;
  if (*p == '*') return wildMatch(p + 1, s) || (*s && wildMatch(p, s + 1));
  if (*s && (*p == '?' || *p == *s)) return wildMatch(p + 1, s + 1);
  return false;
}
}  // namespace

std::string normalizeName(const std::string& n) {
  std::string out;
  for (char c : n) {
    if (c == '\0') break;
    if (c == ' ') continue;
    out.push_back(char(std::toupper(static_cast<unsigned char>(c))));
  }
  return out;
}

struct GameData::Archive {
  std::filesystem::path path;
  FILE* f = nullptr;
  ~Archive() {
    if (f) std::fclose(f);
  }
};

GameData::~GameData() = default;

bool GameData::addArchive(const std::filesystem::path& p, std::string* error) {
  auto a = std::make_unique<Archive>();
  a->path = p;
  a->f = std::fopen(p.string().c_str(), "rb");
  if (!a->f) {
    if (error) *error = "cannot open " + p.string();
    return false;
  }
  std::fseek(a->f, 0, SEEK_END);
  long size = std::ftell(a->f);
  if (size < 12) return false;
  uint8_t tail[4];
  std::fseek(a->f, size - 4, SEEK_SET);
  if (std::fread(tail, 1, 4, a->f) != 4) return false;
  uint32_t diroff = rd32(tail);
  if (diroff + 8 > uint32_t(size)) {
    if (error) *error = p.string() + ": bad directory offset";
    return false;
  }
  uint8_t hdr[4];
  std::fseek(a->f, diroff, SEEK_SET);
  if (std::fread(hdr, 1, 4, a->f) != 4) return false;
  uint32_t h = rd32(hdr);
  uint32_t count = h & 0x7fffffffu;
  bool obfuscated = (h & 0x80000000u) != 0;
  if (count > 5000 || diroff + 4 + count * 28u + 4 != uint32_t(size)) {
    if (error) *error = p.string() + ": directory does not end at EOF-4 (not a Slipstream RES?)";
    return false;
  }
  Bytes dir(count * 28u);
  if (std::fread(dir.data(), 1, dir.size(), a->f) != dir.size()) return false;
  int ai = int(archives_.size());
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* e = dir.data() + i * 28;
    char name[17];
    for (int k = 0; k < 16; ++k) name[k] = char(obfuscated ? (e[4 + k] ^ uint8_t(kKey[k])) : e[4 + k]);
    name[16] = 0;
    std::string key = normalizeName(name);
    Entry en{ai, rd32(e + 20), rd32(e + 24)};
    index_.emplace(key, en);  // emplace keeps the first (primary) definition
  }
  archives_.push_back(std::move(a));
  return true;
}

std::unique_ptr<GameData> GameData::open(const std::filesystem::path& dir, std::string* error) {
  std::unique_ptr<GameData> g(new GameData());
  g->root_ = dir;
  namespace fs = std::filesystem;
  // Order mirrors the engine: EXE-adjacent SLIPSTRM.RES first, then SLIPCD.RES.
  const char* order[] = {"SLIPSTRM.RES", "SLIPMAX.RES", "SLIPCD.RES"};
  bool any = false;
  std::string last;
  bool haveStrm = false;
  for (const char* n : order) {
    if (std::string(n) == "SLIPMAX.RES" && haveStrm) continue;  // identical to SLIPSTRM.RES
    fs::path p = dir / n;
    std::error_code ec;
    if (!fs::exists(p, ec)) continue;
    std::string e;
    if (g->addArchive(p, &e)) {
      any = true;
      if (std::string(n) == "SLIPSTRM.RES") haveStrm = true;
    } else {
      last = e;
    }
  }
  if (!any) {
    if (error) *error = "no usable SLIP*.RES archive found in " + dir.string() + (last.empty() ? "" : " (" + last + ")");
    return nullptr;
  }
  return g;
}

bool GameData::exists(const std::string& name) const {
  std::string k = normalizeName(name);
  if (index_.count(k)) return true;
  std::error_code ec;
  return std::filesystem::exists(root_ / k, ec);
}

std::optional<Bytes> GameData::read(const std::string& name) const {
  std::string k = normalizeName(name);
  auto it = index_.find(k);
  if (it != index_.end()) {
    const Entry& e = it->second;
    Archive* a = archives_[size_t(e.archive)].get();
    Bytes b(e.size);
    std::fseek(a->f, long(e.offset), SEEK_SET);
    if (std::fread(b.data(), 1, b.size(), a->f) != b.size()) return std::nullopt;
    return b;
  }
  // loose-file fallback (the original engine does the same)
  std::filesystem::path p = root_ / k;
  FILE* f = std::fopen(p.string().c_str(), "rb");
  if (!f) return std::nullopt;
  std::fseek(f, 0, SEEK_END);
  long sz = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  Bytes b(static_cast<size_t>(sz));
  size_t got = std::fread(b.data(), 1, b.size(), f);
  std::fclose(f);
  if (got != b.size()) return std::nullopt;
  return b;
}

std::vector<std::string> GameData::list(const std::string& ext) const {
  std::string e = "." + normalizeName(ext);
  std::vector<std::string> out;
  for (auto& [k, v] : index_)
    if (k.size() > e.size() && k.compare(k.size() - e.size(), e.size(), e) == 0) out.push_back(k);
  return out;
}

std::vector<std::string> GameData::glob(const std::string& pattern) const {
  std::string p = normalizeName(pattern);
  std::vector<std::string> out;
  for (auto& [k, v] : index_)
    if (wildMatch(p.c_str(), k.c_str())) out.push_back(k);
  return out;
}

std::vector<std::string> GameData::archiveNames() const {
  std::vector<std::string> v;
  for (auto& a : archives_) v.push_back(a->path.filename().string());
  return v;
}

std::filesystem::path findGameDirectory(const std::string& hint) {
  namespace fs = std::filesystem;
  std::vector<fs::path> cands;
  if (!hint.empty()) cands.emplace_back(hint);
  if (const char* e = std::getenv("SLIPSTREAM_DATA")) cands.emplace_back(e);
  cands.emplace_back(".");
  if (const std::string home = homeDir(); !home.empty()) {
    cands.emplace_back(fs::path(home) / "Downloads" / "slip5000");
    cands.emplace_back(fs::path(home) / "Games" / "slip5000");
    cands.emplace_back(fs::path(home) / "slip5000");
    cands.emplace_back(fs::path(home) / "GOG Games" / "Slipstream 5000");
  }
#ifdef _WIN32
  cands.emplace_back("C:/GOG Games/Slipstream 5000");
  cands.emplace_back("C:/Program Files (x86)/GOG Galaxy/Games/Slipstream 5000");
#endif
  // remembered location (written by the application after a successful start)
  {
    FILE* f = std::fopen((userDataDir() + "/data_dir.txt").c_str(), "r");
    if (f) {
      char line[1024] = {0};
      if (std::fgets(line, sizeof line, f)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (!l.empty()) cands.emplace_back(l);
      }
      std::fclose(f);
    }
  }
  for (auto& c : cands) {
    std::error_code ec;
    if (fs::exists(c / "SLIPSTRM.RES", ec) || fs::exists(c / "SLIPCD.RES", ec)) return c;
  }
  return {};
}

void rememberGameDirectory(const std::filesystem::path& dir) {
  std::error_code ec;
  std::filesystem::path d = userDataDir();
  std::filesystem::create_directories(d, ec);
  FILE* f = std::fopen((d / "data_dir.txt").string().c_str(), "w");
  if (!f) return;
  std::fprintf(f, "%s\n", std::filesystem::absolute(dir, ec).string().c_str());
  std::fclose(f);
}

}  // namespace slip
