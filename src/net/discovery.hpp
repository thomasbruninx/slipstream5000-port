// Session discovery. LanDiscovery announces / finds games with UDP broadcasts on the local subnet; the interface is what an internet
// matchmaking client (query a server for the session list, register a session) would implement later.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace slip::net {

struct SessionInfo {
  std::string name;       // host's player name / session title
  uint8_t track = 1, laps = 6, players = 1, maxPlayers = 10;
  uint32_t version = 0, dataHash = 0;
  std::string host;       // address to connect to (filled from the packet source when browsing)
  uint16_t port = 0;
  double age = 0;         // seconds since the last announcement (browsing)
};

class IDiscovery {
 public:
  virtual ~IDiscovery() = default;
  virtual bool startAnnounce(const SessionInfo& info) = 0;
  virtual void updateAnnounce(const SessionInfo& info) = 0;   // lobby changed (players, track ...)
  virtual void stopAnnounce() = 0;
  virtual bool startBrowse() = 0;
  virtual void stopBrowse() = 0;
  virtual void update(double dt) = 0;                         // call every frame
  virtual std::vector<SessionInfo> sessions() const = 0;      // sessions seen in the last few seconds
};

std::unique_ptr<IDiscovery> makeLanDiscovery(uint16_t discoveryPort);

// Encoding of one announcement datagram (exposed for tests).
std::vector<uint8_t> encodeAnnouncement(const SessionInfo& s);
bool decodeAnnouncement(const std::vector<uint8_t>& b, SessionInfo* s);

}  // namespace slip::net
