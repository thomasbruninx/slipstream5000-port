#include "net/transport.hpp"

#include <poll.h>

#include <algorithm>

#include "net/socket.hpp"

namespace slip::net {

namespace {

struct Conn {
  int fd = -1;
  bool connecting = false;
  bool outgoing = false;
  std::string addr;
  Bytes in, out;
};

class TcpTransport : public ITransport {
 public:
  TcpTransport() { netInit(); }
  ~TcpTransport() override {
    closeSocket(listenFd_);
    for (auto& [id, c] : conns_) closeSocket(c.fd);
  }

  bool listen(uint16_t port) override {
    closeSocket(listenFd_);
    listenFd_ = tcpListen(port, &listenPort_);
    return listenFd_ >= 0;
  }
  uint16_t listenPort() const override { return listenPort_; }

  PeerId connect(const std::string& host, uint16_t port) override {
    const int fd = tcpConnectStart(host, port);
    if (fd < 0) return 0;
    Conn c;
    c.fd = fd;
    c.connecting = true;
    c.outgoing = true;
    c.addr = host;
    const PeerId id = next_++;
    conns_[id] = std::move(c);
    return id;
  }

  void send(PeerId peer, const Bytes& msg) override {
    auto it = conns_.find(peer);
    if (it == conns_.end() || msg.empty() || msg.size() > kMaxMessage) return;
    Conn& c = it->second;
    const uint32_t n = uint32_t(msg.size());
    c.out.push_back(uint8_t(n));
    c.out.push_back(uint8_t(n >> 8));
    c.out.insert(c.out.end(), msg.begin(), msg.end());
    if (!c.connecting) flush(peer, c);
  }

  void disconnect(PeerId peer) override {
    auto it = conns_.find(peer);
    if (it == conns_.end()) return;
    closeSocket(it->second.fd);
    conns_.erase(it);
  }

  std::string peerAddress(PeerId peer) const override {
    auto it = conns_.find(peer);
    return it == conns_.end() ? std::string() : it->second.addr;
  }
  size_t peerCount() const override { return conns_.size(); }

  void poll(std::vector<NetEvent>* out) override {
    // accept
    while (listenFd_ >= 0) {
      std::string addr;
      uint16_t port = 0;
      const int fd = tcpAccept(listenFd_, &addr, &port);
      if (fd < 0) break;
      Conn c;
      c.fd = fd;
      c.addr = addr;
      const PeerId id = next_++;
      conns_[id] = std::move(c);
      NetEvent e;
      e.type = NetEvent::Connected; e.peer = id; e.incoming = true;
      out->push_back(std::move(e));
    }
    std::vector<pollfd> fds;
    std::vector<PeerId> ids;
    for (auto& [id, c] : conns_) {
      pollfd p{};
      p.fd = c.fd;
      p.events = POLLIN | ((c.connecting || !c.out.empty()) ? POLLOUT : 0);
      fds.push_back(p);
      ids.push_back(id);
    }
    if (!fds.empty()) ::poll(fds.data(), nfds_t(fds.size()), 0);
    std::vector<PeerId> dead = pendingDead_;  // failed sends
    pendingDead_.clear();
    for (size_t k = 0; k < fds.size(); ++k) {
      const PeerId id = ids[k];
      auto it = conns_.find(id);
      if (it == conns_.end()) continue;
      Conn& c = it->second;
      const short re = fds[k].revents;
      if (c.connecting) {
        if (re & (POLLOUT | POLLERR | POLLHUP)) {
          bool failed = false;
          connectFinished(c.fd, &failed);
          if (failed || (re & (POLLERR | POLLHUP))) { dead.push_back(id); continue; }
          c.connecting = false;
          NetEvent e;
          e.type = NetEvent::Connected; e.peer = id; e.incoming = false;
          out->push_back(std::move(e));
          flush(id, c);
        }
        continue;
      }
      if (re & POLLIN) {
        uint8_t buf[4096];
        for (;;) {
          const long r = sockRecv(c.fd, buf, sizeof buf);
          if (r < 0) { dead.push_back(id); break; }
          if (r == 0) break;
          c.in.insert(c.in.end(), buf, buf + r);
          if (size_t(r) < sizeof buf) break;
        }
        // frames: u16 length + payload
        size_t pos = 0;
        while (c.in.size() - pos >= 2) {
          const size_t n = size_t(c.in[pos]) | (size_t(c.in[pos + 1]) << 8);
          if (n == 0 || n > kMaxMessage) { dead.push_back(id); break; }
          if (c.in.size() - pos - 2 < n) break;
          NetEvent e;
          e.type = NetEvent::Message; e.peer = id;
          e.data.assign(c.in.begin() + long(pos) + 2, c.in.begin() + long(pos) + 2 + long(n));
          out->push_back(std::move(e));
          pos += 2 + n;
        }
        c.in.erase(c.in.begin(), c.in.begin() + long(pos));
      } else if (re & (POLLERR | POLLHUP | POLLNVAL)) {
        dead.push_back(id);
        continue;
      }
      if (!c.out.empty() && (re & POLLOUT)) flush(id, c);
    }
    std::sort(dead.begin(), dead.end());
    dead.erase(std::unique(dead.begin(), dead.end()), dead.end());
    for (PeerId id : dead) {
      auto it = conns_.find(id);
      if (it == conns_.end()) continue;
      closeSocket(it->second.fd);
      conns_.erase(it);
      NetEvent e;
      e.type = NetEvent::Disconnected; e.peer = id;
      out->push_back(std::move(e));
    }
  }

 private:
  void flush(PeerId id, Conn& c) {
    while (!c.out.empty()) {
      const long r = sockSend(c.fd, c.out.data(), c.out.size());
      if (r < 0) { pendingDead_.push_back(id); return; }
      if (r == 0) return;
      c.out.erase(c.out.begin(), c.out.begin() + r);
    }
  }
  int listenFd_ = -1;
  uint16_t listenPort_ = 0;
  PeerId next_ = 1;
  std::map<PeerId, Conn> conns_;
  std::vector<PeerId> pendingDead_;
};

}  // namespace

std::unique_ptr<ITransport> makeTcpTransport() { return std::make_unique<TcpTransport>(); }

}  // namespace slip::net
