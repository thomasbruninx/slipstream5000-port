#include "net/socket.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
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
#endif

#include <cerrno>
#include <cstring>

namespace slip::net {

#ifdef _WIN32
// Winsock: SOCKET handles are small integers in practice, the rest of the code keeps `int`.
namespace {
using SockLen = int;
using OptPtr = const char*;
#define SLIP_SOCK(fd) (SOCKET(fd))
void setNonBlocking(int fd) { u_long on = 1; ioctlsocket(SLIP_SOCK(fd), FIONBIO, &on); }
int closeFd(int fd) { return closesocket(SLIP_SOCK(fd)); }
bool wouldBlock() { const int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINTR; }
bool inProgress() { const int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
}  // namespace
#else
namespace {
using SockLen = socklen_t;
using OptPtr = const void*;
#define SLIP_SOCK(fd) (fd)
void setNonBlocking(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }
int closeFd(int fd) { return close(fd); }
bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
bool inProgress() { return errno == EINPROGRESS; }
}  // namespace
#endif

namespace {
void setNoDelay(int fd) { int one = 1; setsockopt(SLIP_SOCK(fd), IPPROTO_TCP, TCP_NODELAY, OptPtr(&one), sizeof one); }
void setNoSigpipe(int fd) {
#ifdef SO_NOSIGPIPE
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#else
  (void)fd;
#endif
}
}  // namespace

void netInit() {
#ifdef _WIN32
  static bool done = false;
  if (!done) { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); done = true; }
#else
  signal(SIGPIPE, SIG_IGN);
#endif
}

int tcpListen(uint16_t port, uint16_t* actual) {
  const int fd = int(socket(AF_INET, SOCK_STREAM, 0));
  if (fd < 0) return -1;
  int one = 1;
  setsockopt(SLIP_SOCK(fd), SOL_SOCKET, SO_REUSEADDR, OptPtr(&one), sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons(port);
  if (bind(SLIP_SOCK(fd), reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || listen(SLIP_SOCK(fd), 16) != 0) { closeFd(fd); return -1; }
  SockLen len = sizeof a;
  getsockname(SLIP_SOCK(fd), reinterpret_cast<sockaddr*>(&a), &len);
  if (actual) *actual = ntohs(a.sin_port);
  setNonBlocking(fd);
  return fd;
}

int tcpConnectStart(const std::string& host, uint16_t port) {
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) return -1;
  const int fd = int(socket(AF_INET, SOCK_STREAM, 0));
  if (fd < 0) { freeaddrinfo(res); return -1; }
  setNonBlocking(fd);
  setNoDelay(fd);
  setNoSigpipe(fd);
  const int r = connect(SLIP_SOCK(fd), res->ai_addr, int(res->ai_addrlen));
  freeaddrinfo(res);
  if (r != 0 && !inProgress()) { closeFd(fd); return -1; }
  return fd;
}

int tcpAccept(int lfd, std::string* addr, uint16_t* port) {
  sockaddr_in a{};
  SockLen len = sizeof a;
  const int fd = int(accept(SLIP_SOCK(lfd), reinterpret_cast<sockaddr*>(&a), &len));
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
  const long r = long(send(SLIP_SOCK(fd), static_cast<const char*>(data), int(n), 0));
  if (r >= 0) return r;
  return wouldBlock() ? 0 : -1;
}

long sockRecv(int fd, void* data, size_t n) {
  const long r = long(recv(SLIP_SOCK(fd), static_cast<char*>(data), int(n), 0));
  if (r > 0) return r;
  if (r == 0) return -1;  // orderly shutdown
  return wouldBlock() ? 0 : -1;
}

bool connectFinished(int fd, bool* failed) {
  int err = 0;
  SockLen len = sizeof err;
  getsockopt(SLIP_SOCK(fd), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
  if (failed) *failed = err != 0;
  return true;
}

void closeSocket(int fd) { if (fd >= 0) closeFd(fd); }

int udpOpen(uint16_t port, bool broadcast) {
  const int fd = int(socket(AF_INET, SOCK_DGRAM, 0));
  if (fd < 0) return -1;
  int one = 1;
  setsockopt(SLIP_SOCK(fd), SOL_SOCKET, SO_REUSEADDR, OptPtr(&one), sizeof one);
#ifdef SO_REUSEPORT
  setsockopt(SLIP_SOCK(fd), SOL_SOCKET, SO_REUSEPORT, OptPtr(&one), sizeof one);
#endif
  if (broadcast) setsockopt(SLIP_SOCK(fd), SOL_SOCKET, SO_BROADCAST, OptPtr(&one), sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons(port);
  if (bind(SLIP_SOCK(fd), reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) { closeFd(fd); return -1; }
  setNonBlocking(fd);
  return fd;
}

bool udpSendTo(int fd, const std::string& ip, uint16_t port, const void* data, size_t n) {
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  if (inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1) return false;
  return sendto(SLIP_SOCK(fd), static_cast<const char*>(data), int(n), 0, reinterpret_cast<sockaddr*>(&a), sizeof a) == long(n);
}

long udpRecvFrom(int fd, void* data, size_t n, std::string* fromIp, uint16_t* fromPort) {
  sockaddr_in a{};
  SockLen len = sizeof a;
  const long r = long(recvfrom(SLIP_SOCK(fd), static_cast<char*>(data), int(n), 0, reinterpret_cast<sockaddr*>(&a), &len));
  if (r < 0) return -1;
  char buf[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, &a.sin_addr, buf, sizeof buf);
  if (fromIp) *fromIp = buf;
  if (fromPort) *fromPort = ntohs(a.sin_port);
  return r;
}

namespace {
#ifdef _WIN32
bool findLan(std::string* ip, std::string* bcast) {  // the first up IPv4 adapter that is not the loopback: address | ~netmask
  ULONG size = 0;
  GetAdaptersInfo(nullptr, &size);
  if (size == 0) return false;
  std::string buf(size, '\0');
  auto* info = reinterpret_cast<IP_ADAPTER_INFO*>(buf.data());
  if (GetAdaptersInfo(info, &size) != NO_ERROR) return false;
  for (IP_ADAPTER_INFO* a = info; a; a = a->Next) {
    for (IP_ADDR_STRING* s = &a->IpAddressList; s; s = s->Next) {
      const unsigned long addr = inet_addr(s->IpAddress.String), mask = inet_addr(s->IpMask.String);
      if (addr == 0 || addr == INADDR_NONE || (addr & 0xff) == 127) continue;
      if (ip) *ip = s->IpAddress.String;
      if (bcast) { in_addr b; b.s_addr = addr | ~mask; *bcast = inet_ntoa(b); }
      return true;
    }
  }
  return false;
}
#else
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

#endif
}  // namespace

std::string localAddress() { std::string ip; return findLan(&ip, nullptr) ? ip : "127.0.0.1"; }
std::string subnetBroadcast() { std::string b; return findLan(nullptr, &b) ? b : "255.255.255.255"; }

}  // namespace slip::net
