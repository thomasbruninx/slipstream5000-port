// Message transport between peers. The game layer only talks to ITransport (opaque peer ids, framed messages), so an internet relay /
// NAT-traversing transport can replace the plain TCP one later. Messages are byte strings of up to kMaxMessage bytes; the first byte is the
// protocol message type.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "net/byte_io.hpp"

namespace slip::net {

using PeerId = uint32_t;  // transport level connection id (not the session player id)
constexpr size_t kMaxMessage = 8192;

struct NetEvent {
  enum Type { Connected, Disconnected, Message } type = Message;
  PeerId peer = 0;
  bool incoming = false;  // Connected: the remote side connected to us (false: our connect() succeeded)
  Bytes data;             // Message
};

class ITransport {
 public:
  virtual ~ITransport() = default;
  virtual bool listen(uint16_t port) = 0;                  // port 0 = any
  virtual uint16_t listenPort() const = 0;
  virtual PeerId connect(const std::string& host, uint16_t port) = 0;  // asynchronous: Connected / Disconnected follow; 0 on immediate failure
  virtual void send(PeerId peer, const Bytes& msg) = 0;
  virtual void disconnect(PeerId peer) = 0;
  virtual void poll(std::vector<NetEvent>* out) = 0;        // non-blocking
  virtual std::string peerAddress(PeerId peer) const = 0;  // remote ip
  virtual size_t peerCount() const = 0;
};

std::unique_ptr<ITransport> makeTcpTransport();

}  // namespace slip::net
