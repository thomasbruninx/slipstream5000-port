// Network layer tests: wire protocol round trips, framed TCP transport (including a 10 peer localhost mesh), LAN discovery on loopback.
#include <chrono>
#include <cstdio>
#include <thread>

#include "net/discovery.hpp"
#include "net/protocol.hpp"
#include "net/session.hpp"
#include "net/transport.hpp"

using namespace slip::net;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

template <class F>
static bool waitFor(F f, int ms = 3000) {
  const auto t0 = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(ms)) {
    if (f()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return f();
}

int main() {
  {  // protocol round trips
    Hello h; h.dataHash = 0xdeadbeef; h.sessionId = 77; h.playerId = 3; h.listenPort = 51500; h.name = "Ace";
    Hello h2; CHECK(decode(encode(h), &h2) && h2.dataHash == 0xdeadbeef && h2.name == "Ace" && h2.playerId == 3 && h2.listenPort == 51500);
    Welcome w; w.yourId = 4; w.peers = {{0, "192.168.1.5", 51500}, {2, "192.168.1.9", 51501}};
    Welcome w2; CHECK(decode(encode(w), &w2) && w2.peers.size() == 2 && w2.peers[1].ip == "192.168.1.9" && w2.peers[1].port == 51501);
    Lobby l; l.track = 6; l.laps = 5; l.players = {{0, "A", 2, 1}, {1, "B", 7, 0}};
    Lobby l2; CHECK(decode(encode(l), &l2) && l2.track == 6 && l2.players.size() == 2 && l2.players[1].ship == 7);
    Start s; s.track = 3; s.seed = 99; s.startAtMs = 123456; s.slots[0] = {1, 0, "A"}; s.slots[5] = {2, kNoPlayer, "AI"};
    Start s2; CHECK(decode(encode(s), &s2) && s2.seed == 99 && s2.slots[0].name == "A" && s2.slots[5].kind == 2 && s2.startAtMs == 123456);
    State st; st.slot = 4; st.seq = 65535; st.pos[0] = -4633413; st.pos[2] = 8000000; st.m[4] = toQ14(0.5); st.speed = 214500; st.slide[1] = -3; st.damageA = 5050; st.flags = 5; st.lockTarget = -1; st.laps = 2;
    State st2; CHECK(decode(encode(st), &st2) && st2.seq == 65535 && st2.pos[0] == -4633413 && st2.pos[2] == 8000000 && st2.speed == 214500 && st2.slide[1] == -3 && st2.lockTarget == -1 && st2.flags == 5);
    CHECK(std::abs(fromQ14(st2.m[4]) - 0.5) < 1e-3);
    CHECK(encode(st).size() < 100);  // ~90 bytes per snapshot
    ProjSpawn p; p.id = 1234567; p.owner = 3; p.kind = 4; p.target = 6; p.pos[1] = 99; p.speed = 300000; p.lifeCs = 500;
    ProjSpawn p2; CHECK(decode(encode(p), &p2) && p2.id == 1234567 && p2.target == 6 && p2.lifeCs == 500);
    LapEvent le; le.slot = 2; le.laps = 3; le.finished = 1; le.finishRank = 2; le.lapTimeMs = 51000; le.finishTimeMs = 160000;
    LapEvent le2; CHECK(decode(encode(le), &le2) && le2.finishRank == 2 && le2.finishTimeMs == 160000);
    DoorSync d; d.doors = {{1000, -1, 14300}, {-5, 0, 28600}};
    DoorSync d2; CHECK(decode(encode(d), &d2) && d2.doors.size() == 2 && d2.doors[1].pos == -5 && d2.doors[0].state == -1);
    Bytes junk = {uint8_t(Msg::Lobby), 5};  // truncated
    Lobby l3; CHECK(!decode(junk, &l3));
    CHECK(msgType(encodeRaceOver()) == Msg::RaceOver && msgType(encodeLeave()) == Msg::Leave);
  }
  {  // framed TCP: partial frames, bursts, large messages, disconnect
    auto a = makeTcpTransport(), b = makeTcpTransport();
    CHECK(a->listen(0) && a->listenPort() != 0);
    const PeerId toA = b->connect("127.0.0.1", a->listenPort());
    CHECK(toA != 0);
    std::vector<NetEvent> ea, eb;
    PeerId aSide = 0;
    CHECK(waitFor([&] { a->poll(&ea); b->poll(&eb); bool x = false, y = false; for (auto& e : ea) if (e.type == NetEvent::Connected && e.incoming) { x = true; aSide = e.peer; } for (auto& e : eb) if (e.type == NetEvent::Connected && !e.incoming) y = true; return x && y; }));
    for (int i = 0; i < 500; ++i) { Bytes m(1 + size_t(i % 90), uint8_t(i)); m[0] = uint8_t(Msg::State); b->send(toA, m); }
    Bytes big(5000, 0x5a); big[0] = uint8_t(Msg::Lobby);
    b->send(toA, big);
    ea.clear();
    int msgs = 0;
    bool bigOk = false;
    waitFor([&] { a->poll(&ea); for (auto& e : ea) if (e.type == NetEvent::Message) { ++msgs; if (e.data.size() == 5000 && e.data[4999] == 0x5a) bigOk = true; } ea.clear(); return msgs >= 501; });
    CHECK(msgs == 501 && bigOk);
    a->send(aSide, encodeLeave());
    eb.clear();
    CHECK(waitFor([&] { b->poll(&eb); for (auto& e : eb) if (e.type == NetEvent::Message && msgType(e.data) == Msg::Leave) return true; return false; }));
    b->disconnect(toA);
    ea.clear();
    CHECK(waitFor([&] { a->poll(&ea); for (auto& e : ea) if (e.type == NetEvent::Disconnected) return true; return false; }));
    CHECK(b->connect("127.0.0.1", 1) != 0);  // connect() is asynchronous; the refusal arrives as a Disconnected event
    eb.clear();
    CHECK(waitFor([&] { b->poll(&eb); for (auto& e : eb) if (e.type == NetEvent::Disconnected) return true; return false; }));
  }
  {  // 10 peer full mesh on localhost: everybody listens, peer i connects to all peers j < i; a broadcast from each reaches the other nine
    constexpr int N = 10;
    std::vector<std::unique_ptr<ITransport>> t;
    for (int i = 0; i < N; ++i) { t.push_back(makeTcpTransport()); CHECK(t.back()->listen(0)); }
    std::vector<std::vector<PeerId>> links(N);
    for (int i = 0; i < N; ++i) for (int j = 0; j < i; ++j) { const PeerId id = t[size_t(i)]->connect("127.0.0.1", t[size_t(j)]->listenPort()); CHECK(id != 0); links[size_t(i)].push_back(id); }
    std::vector<int> got(N, 0);
    std::vector<std::vector<PeerId>> inLinks(N);
    auto pump = [&] {
      for (int i = 0; i < N; ++i) {
        std::vector<NetEvent> ev;
        t[size_t(i)]->poll(&ev);
        for (auto& e : ev) {
          if (e.type == NetEvent::Connected && e.incoming) inLinks[size_t(i)].push_back(e.peer);
          if (e.type == NetEvent::Message && msgType(e.data) == Msg::Ping) ++got[size_t(i)];
        }
      }
    };
    CHECK(waitFor([&] { pump(); for (int i = 0; i < N; ++i) if (int(links[size_t(i)].size() + inLinks[size_t(i)].size()) != N - 1) return false; return true; }));
    for (int i = 0; i < N; ++i) {
      Ping p; p.id = uint32_t(i);
      for (PeerId id : links[size_t(i)]) t[size_t(i)]->send(id, encode(p));
      for (PeerId id : inLinks[size_t(i)]) t[size_t(i)]->send(id, encode(p));
    }
    CHECK(waitFor([&] { pump(); for (int i = 0; i < N; ++i) if (got[size_t(i)] != N - 1) return false; return true; }));
  }
  {  // session: host + 3 joiners form a mesh, lobby edits, start, race messages, a leaving player
    std::vector<std::unique_ptr<Session>> ss;
    for (int i = 0; i < 4; ++i) ss.push_back(std::make_unique<Session>(makeTcpTransport(), 51778));
    std::string err;
    CHECK(ss[0]->host("Host", 0xabc, 0, &err));
    for (int i = 1; i < 4; ++i) CHECK(ss[size_t(i)]->join("127.0.0.1", ss[0]->listenPort(), "P" + std::to_string(i), 0xabc, &err));
    auto pump = [&] { for (auto& s : ss) s->update(0.01); };
    CHECK(waitFor([&] { pump(); for (auto& s : ss) if (s->state() != Session::State::Lobby || s->connectedPlayers().size() != 3) return false; return true; }));
    CHECK(ss[2]->myId() == 2 && ss[0]->lobby().players.size() == 4);
    ss[1]->setIntent(7, true); ss[2]->setIntent(7, true); ss[3]->setIntent(5, true);  // 2 asks for a ship that is taken
    ss[0]->hostSettings(6, 4, 2, true);
    CHECK(waitFor([&] { pump(); const Lobby& l = ss[3]->lobby(); return l.track == 6 && l.laps == 4 && l.players.size() == 4 && l.players[1].ship == 7 && l.players[3].ship == 5 && l.players[2].ship != 7; }));
    CHECK(waitFor([&] { pump(); return ss[0]->hostCanStart(); }));
    CHECK(ss[0]->hostStart(1234));
    CHECK(waitFor([&] { pump(); for (auto& s : ss) if (s->state() != Session::State::Racing) return false; return true; }));
    for (int i = 0; i < 4; ++i) {
      Start st; uint32_t at = 0;
      CHECK(ss[size_t(i)]->takeStart(&st, &at) && st.track == 6 && st.seed == 1234 && st.slots[7].playerId == 1 && st.slots[9].kind == 2);
      CHECK(std::abs(int64_t(at) - int64_t(ss[size_t(i)]->nowMs())) < 2600);
    }
    State sn; sn.slot = 5; sn.seq = 9;
    ss[3]->broadcast(encode(sn));
    std::vector<int> got;
    CHECK(waitFor([&] { pump(); for (int i = 0; i < 3; ++i) for (auto& m : ss[size_t(i)]->drain()) if (m.from == 3 && msgType(m.data) == Msg::State) got.push_back(i); return got.size() == 3; }));
    CHECK(std::abs(int(ss[2]->hostTimeMs()) - int(ss[0]->nowMs())) < 30);  // loopback clocks agree
    ss[2]->leave();
    bool left0 = false, left1 = false;
    CHECK(waitFor([&] { pump(); for (auto& e : ss[0]->drainEvents()) if (e.type == Session::Event::Left && e.player == 2) left0 = true; for (auto& e : ss[1]->drainEvents()) if (e.type == Session::Event::Left && e.player == 2) left1 = true; return left0 && left1; }));
    ss[0]->leave();  // host migration: the lowest remaining id (1) takes over, the session goes on
    bool changed = false;
    CHECK(waitFor([&] { pump(); for (auto& e : ss[3]->drainEvents()) if (e.type == Session::Event::HostChanged && e.player == 1) changed = true; return changed && ss[1]->isHost() && ss[3]->hostId() == 1; }));
    CHECK(ss[1]->state() == Session::State::Racing && ss[3]->state() == Session::State::Racing && !ss[3]->isHost());
    CHECK(waitFor([&] { pump(); return ss[3]->rttOf(1) >= 0 && ss[1]->rttOf(3) >= 0; }, 4000));  // per player ping
    ss[1]->returnToLobby(); ss[3]->returnToLobby();
    CHECK(ss[1]->hostCanStart() || ss[1]->lobby().players.size() == 1);
    ss[1]->leave();
    CHECK(waitFor([&] { pump(); return ss[3]->isHost() && ss[3]->hostId() == 3; }));
    Session h(makeTcpTransport(), 51779), j(makeTcpTransport(), 51779);  // mismatching data set is rejected
    CHECK(h.host("H", 1, 0, &err) && j.join("127.0.0.1", h.listenPort(), "J", 2, &err));
    CHECK(waitFor([&] { h.update(0.01); j.update(0.01); return j.state() == Session::State::Closed; }));
    CHECK(j.error().find("data") != std::string::npos);
  }
  {  // LAN discovery on loopback (a private port so a running game is not disturbed)
    auto host = makeLanDiscovery(51777), client = makeLanDiscovery(51777);
    SessionInfo s; s.name = "Test"; s.track = 4; s.laps = 2; s.players = 3; s.port = 51500; s.version = kProtocolVersion; s.dataHash = 42;
    CHECK(host->startAnnounce(s) && client->startBrowse());
    CHECK(waitFor([&] { host->update(0.2); client->update(0.2); return !client->sessions().empty(); }, 4000));
    const auto found = client->sessions();
    CHECK(!found.empty() && found[0].name == "Test" && found[0].track == 4 && found[0].players == 3 && found[0].port == 51500 && !found[0].host.empty());
    s.players = 5;
    host->updateAnnounce(s);
    CHECK(waitFor([&] { host->update(0.2); client->update(0.2); return !client->sessions().empty() && client->sessions()[0].players == 5; }, 4000));
    host->stopAnnounce();
    for (int i = 0; i < 40; ++i) client->update(0.2);  // announcements stop -> the entry expires
    CHECK(client->sessions().empty());
  }
  std::printf(failures ? "net: %d failure(s)\n" : "net ok\n", failures);
  return failures ? 1 : 0;
}
