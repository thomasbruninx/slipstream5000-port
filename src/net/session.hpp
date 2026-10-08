// Multiplayer session: lobby, full-mesh formation, clock synchronisation and message routing on top of ITransport. SDL- and game-free so it
// can be tested headlessly. The lobby host (player id 0) is the authority for race settings and the grid only; the game layer (game/netplay)
// sends the in-race messages through broadcast() and reads them with drain().
#pragma once
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "net/discovery.hpp"
#include "net/protocol.hpp"
#include "net/transport.hpp"

namespace slip::net {

class Session {
 public:
  enum class State { Idle, Joining, Lobby, Racing, Closed };
  struct Inbound { int from; Bytes data; };                       // race messages (State, ProjSpawn, ...)
  struct Event { enum Type { Joined, Left, Closed, HostChanged } type; int player; std::string text; };

  explicit Session(std::unique_ptr<ITransport> t = makeTcpTransport(), uint16_t discoveryPort = kDiscoveryPort);

  bool host(const std::string& name, uint32_t dataHash, uint16_t port, std::string* err);
  bool join(const std::string& host, uint16_t port, const std::string& name, uint32_t dataHash, std::string* err);  // asynchronous
  void leave();
  void update(double dt);  // pump sockets, discovery, pings

  State state() const { return state_; }
  const std::string& error() const { return error_; }
  bool isHost() const { return myId_ >= 0 && myId_ == hostId_; }
  int hostId() const { return hostId_; }
  int myId() const { return myId_; }
  uint16_t listenPort() const { return transport_->listenPort(); }
  const Lobby& lobby() const { return lobby_; }

  // lobby
  void setIntent(int ship, bool ready);                                 // my ship / ready flag (the host validates uniqueness)
  void hostSettings(int track, int laps, int difficulty, bool aiFill);  // host only
  bool hostCanStart() const;
  bool hostStart(uint32_t seed);                                        // host only: builds the grid, sends Start to everyone, enters Racing
  bool takeStart(Start* out, uint32_t* startLocalMs);                   // pending Start (also on the host); startLocalMs = local clock time of the countdown start
  void returnToLobby();                                                 // after the race

  // race traffic
  void broadcast(const Bytes& msg);
  void sendTo(int player, const Bytes& msg);
  void sendToHost(const Bytes& msg);
  std::vector<Inbound> drain();
  std::vector<Event> drainEvents();
  std::vector<int> connectedPlayers() const;  // ready mesh links (not me)
  std::string playerName(int id) const;

  // clock
  uint32_t nowMs() const;
  uint32_t hostTimeMs() const { return nowMs() + uint32_t(offset_); }  // estimated clock of the host
  double rttMs() const { return rtt_; }                 // to the host
  double rttOf(int player) const { auto it = rttMap_.find(player); return it == rttMap_.end() ? -1.0 : it->second; }  // -1 unknown

 private:
  struct Peer { int player = -1; bool hello = false, outgoing = false, isHostLink = false; std::string name, ip; uint16_t port = 0; };
  void handle(PeerId pid, const Bytes& data);
  void onHello(PeerId pid, Peer& p, const Hello& h);
  void onWelcome(const Welcome& w);
  void onGone(PeerId pid);
  void lobbyChanged();
  void sendTo(PeerId pid, const Bytes& msg) { transport_->send(pid, msg); }
  PeerId pidOf(int player) const;
  int freePlayerId() const;
  int freeShip(int except) const;
  void close(const std::string& why);
  void migrateHost();
  void fail(const std::string& why) { error_ = why; }

  std::unique_ptr<ITransport> transport_;
  std::unique_ptr<IDiscovery> disc_;
  uint16_t discPort_;
  State state_ = State::Idle;
  std::string error_, name_;
  uint32_t dataHash_ = 0, sessionId_ = 0;
  int myId_ = -1, hostId_ = 0;
  std::map<int, double> rttMap_;
  Lobby lobby_;
  std::map<PeerId, Peer> peers_;
  PeerId hostLink_ = 0;
  std::set<int> known_;
  std::vector<Inbound> inbound_;
  std::vector<Event> events_;
  bool startPending_ = false;
  Start start_;
  uint32_t startLocal_ = 0;
  // clock
  int64_t t0_ = 0;
  int32_t offset_ = 0;
  double rtt_ = 0, bestRtt_ = 1e9, pingTimer_ = 0;
  uint32_t pingId_ = 0;
  int pingBurst_ = 6;
  std::string hostAddr_;
};

}  // namespace slip::net
