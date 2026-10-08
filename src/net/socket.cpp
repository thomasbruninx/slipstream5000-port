#include "net/socket.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace slip::net {

namespace {
void setNonBlocking(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }
void setNoDelay(int fd) { int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one); }
void setNoSigpipe(int fd) {
#ifdef SO_NOSIGPIPE
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#else
  (void)fd;
#endif
}
}  // namespace

void netInit() { signal(SIGPIPE, SIG_IGN); }

int tcpListen(uint16_t port, uint16_t* actual) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons(port);
  if (bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || listen(fd, 16) != 0) { close(fd); return -1; }
  socklen_t len = sizeof a;
  getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len);
  if (actual) *actual = ntohs(a.sin_port);
  setNonBlocking(fd);
  return fd;
}

int tcpConnectStart(const std::string& host, uint16_t port) {
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) return -1;
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) { freeaddrinfo(res); return -1; }
  setNonBlocking(fd);
  setNoDelay(fd);
  setNoSigpipe(fd);
  const int r = connect(fd, res->ai_addr, res->ai_addrlen);
  freeaddrinfo(res);
  if (r != 0 && errno != EINPROGRESS) { close(fd); return -1; }
  return fd;
}

int tcpAccept(int lfd, std::string* addr, uint16_t* port) {
  sockaddr_in a{};
  socklen_t len = sizeof a;
  const int fd = accept(lfd, reinterpret_cast<sockaddr*>(&a), &len);
  if (fd < 0) return -1;
  setNonBlocking(fd);
  setNoDelay(fd);
  setNoSigpipe(fd);
  char buf[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, &a.sin_addr, buf, sizeof buf);
  if (addr) *addr = buf;
  if (port) *port = ntohs(a.sin_port);
  return fd;
}

long sockSend(int fd, const void* data, size_t n) {
  const long r = send(fd, data, n, 0);
  if (r >= 0) return r;
  return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
}

long sockRecv(int fd, void* data, size_t n) {
  const long r = recv(fd, data, n, 0);
  if (r > 0) return r;
  if (r == 0) return -1;  // orderly shutdown
  return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
}

bool connectFinished(int fd, bool* failed) {
  int err = 0;
  socklen_t len = sizeof err;
  getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
  if (failed) *failed = err != 0;
  return true;
}

void closeSocket(int fd) { if (fd >= 0) close(fd); }

int udpOpen(uint16_t port, bool broadcast) {
  const int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return -1;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#ifdef SO_REUSEPORT
  setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
#endif
  if (broadcast) setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons(port);
  if (bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) { close(fd); return -1; }
  setNonBlocking(fd);
  return fd;
}

bool udpSendTo(int fd, const std::string& ip, uint16_t port, const void* data, size_t n) {
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  if (inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1) return false;
  return sendto(fd, data, n, 0, reinterpret_cast<sockaddr*>(&a), sizeof a) == long(n);
}

long udpRecvFrom(int fd, void* data, size_t n, std::string* fromIp, uint16_t* fromPort) {
  sockaddr_in a{};
  socklen_t len = sizeof a;
  const long r = recvfrom(fd, data, n, 0, reinterpret_cast<sockaddr*>(&a), &len);
  if (r < 0) return -1;
  char buf[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, &a.sin_addr, buf, sizeof buf);
  if (fromIp) *fromIp = buf;
  if (fromPort) *fromPort = ntohs(a.sin_port);
  return r;
}

namespace {
bool findLan(std::string* ip, std::string* bcast) {
  ifaddrs* list = nullptr;
  if (getifaddrs(&list) != 0) return false;
  bool found = false;
  for (ifaddrs* i = list; i && !found; i = i->ifa_next) {
    if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
    if (!(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_BROADCAST) || !i->ifa_broadaddr) continue;
    char b[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr, b, sizeof b);
    if (ip) *ip = b;
    inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(i->ifa_broadaddr)->sin_addr, b, sizeof b);
    if (bcast) *bcast = b;
    found = true;
  }
  freeifaddrs(list);
  return found;
}
}  // namespace

std::string localAddress() { std::string ip; return findLan(&ip, nullptr) ? ip : "127.0.0.1"; }
std::string subnetBroadcast() { std::string b; return findLan(nullptr, &b) ? b : "255.255.255.255"; }

}  // namespace slip::net
