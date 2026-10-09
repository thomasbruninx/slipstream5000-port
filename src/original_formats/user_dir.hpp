// Per-user folder of the port (config.txt, records.txt, saved games, remembered game folder, gamecontrollerdb.txt):
// macOS ~/Library/Application Support/Slipstream, Windows %APPDATA%\Slipstream, Linux $XDG_CONFIG_HOME/slipstream (default ~/.config/slipstream).
#pragma once
#include <cstdlib>
#include <string>

namespace slip {

inline std::string homeDir() {
#ifdef _WIN32
  if (const char* p = std::getenv("USERPROFILE")) return p;
#endif
  if (const char* h = std::getenv("HOME")) return h;
  return "";
}

inline std::string userDataDir() {
#if defined(_WIN32)
  if (const char* a = std::getenv("APPDATA")) return std::string(a) + "/Slipstream";
  return (homeDir().empty() ? std::string(".") : homeDir()) + "/Slipstream";
#elif defined(__APPLE__)
  return (homeDir().empty() ? std::string(".") : homeDir()) + "/Library/Application Support/Slipstream";
#else
  if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) return std::string(x) + "/slipstream";
  return (homeDir().empty() ? std::string(".") : homeDir()) + "/.config/slipstream";
#endif
}

}  // namespace slip
