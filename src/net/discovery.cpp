#include "net/discovery.hpp"

#include <algorithm>

#include "net/byte_io.hpp"
#include "net/socket.hpp"

namespace slip::net {

namespace {
constexpr uint32_t kMagic = 0x53444c53;  // "SLDS"
constexpr double kAnnounceEvery = 1.0, kExpire = 4.0;

class LanDiscovery : public IDiscovery {
 public:
  explicit LanDiscovery(uint16_t port) : port_(port) { netInit(); }
  ~LanDiscovery() override { closeSocket(fd_); }

  bool startAnnounce(const SessionInfo& info) override { info_ = info; announcing_ = true; since_ = kAnnounceEvery; return ensureSocket(); }
  void updateAnnounce(const SessionInfo& info) override { info_ = info; since_ = kAnnounceEvery; }
  void stopAnnounce() override { announcing_ = false; }
  bool startBrowse() override { browsing_ = true; return ensureSocket(); }
  void stopBrowse() override { browsing_ = false; found_.clear(); }

  void update(double dt) override {
    if (fd_ < 0) return;
    if (announcing_) {
      since_ += dt;
      if (since_ >= kAnnounceEvery) {
        since_ = 0;
        const auto pkt = encodeAnnouncement(info_);
        udpSendTo(fd_, subnetBroadcast(), port_, pkt.data(), pkt.size());
        udpSendTo(fd_, "255.255.255.255", port_, pkt.data(), pkt.size());
        udpSendTo(fd_, "127.255.255.255", port_, pkt.data(), pkt.size());  // same machine: loopback broadcast reaches every socket on the port
        udpSendTo(fd_, "127.0.0.1", port_, pkt.data(), pkt.size());
      }
    }
    uint8_t buf[512];
    for (;;) {
      std::string ip;
      uint16_t from = 0;
      const long n = udpRecvFrom(fd_, buf, sizeof buf, &ip, &from);
      if (n < 0) break;
      if (!browsing_) continue;
      SessionInfo s;
      if (!decodeAnnouncement(std::vector<uint8_t>(buf, buf + n), &s)) continue;
      s.host = ip;
      s.age = 0;
      auto it = std::find_if(found_.begin(), found_.end(), [&](const SessionInfo& o) { return o.host == s.host && o.port == s.port; });
      if (it == found_.end()) found_.push_back(s); else *it = s;
    }
    for (SessionInfo& s : found_) s.age += dt;
    found_.erase(std::remove_if(found_.begin(), found_.end(), [](const SessionInfo& s) { return s.age > kExpire; }), found_.end());
  }

  std::vector<SessionInfo> sessions() const override { return found_; }

 private:
  bool ensureSocket() {
    if (fd_ >= 0) return true;
    fd_ = udpOpen(port_, true);
    return fd_ >= 0;
  }
  uint16_t port_;
  int fd_ = -1;
  bool announcing_ = false, browsing_ = false;
  double since_ = 0;
  SessionInfo info_;
  std::vector<SessionInfo> found_;
};
}  // namespace

std::vector<uint8_t> encodeAnnouncement(const SessionInfo& s) {
  Writer w;
  w.u32(kMagic); w.u32(s.version); w.u32(s.dataHash);
  w.str(s.name); w.u8(s.track); w.u8(s.laps); w.u8(s.players); w.u8(s.maxPlayers); w.u16(s.port);
  return w.buf;
}

bool decodeAnnouncement(const std::vector<uint8_t>& b, SessionInfo* s) {
  Reader r(b);
  if (r.u32() != kMagic) return false;
  s->version = r.u32(); s->dataHash = r.u32();
  s->name = r.str(); s->track = uint8_t(r.u8()); s->laps = uint8_t(r.u8()); s->players = uint8_t(r.u8()); s->maxPlayers = uint8_t(r.u8()); s->port = uint16_t(r.u16());
  return r.ok();
}

std::unique_ptr<IDiscovery> makeLanDiscovery(uint16_t discoveryPort) { return std::make_unique<LanDiscovery>(discoveryPort); }

}  // namespace slip::net
