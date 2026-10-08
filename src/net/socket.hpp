// Thin POSIX socket helpers (IPv4, non-blocking). Kept separate so a different transport can replace them later.
#pragma once
#include <cstdint>
#include <string>

namespace slip::net {

void netInit();                                                    // ignores SIGPIPE
int tcpListen(uint16_t port, uint16_t* actualPort);                // -1 on error
int tcpConnectStart(const std::string& host, uint16_t port);       // non-blocking connect in progress, -1 on error
int tcpAccept(int listenFd, std::string* addr, uint16_t* port);    // -1 when nothing pending
long sockSend(int fd, const void* data, size_t n);                 // bytes sent, 0 when it would block, -1 on error
long sockRecv(int fd, void* data, size_t n);                       // bytes read, 0 when it would block, -1 on closed / error
bool connectFinished(int fd, bool* failed);                        // after POLLOUT
void closeSocket(int fd);

int udpOpen(uint16_t port, bool broadcast);                        // bound, non-blocking, address reuse (several instances per machine)
bool udpSendTo(int fd, const std::string& ip, uint16_t port, const void* data, size_t n);
long udpRecvFrom(int fd, void* data, size_t n, std::string* fromIp, uint16_t* fromPort);  // -1 when nothing pending
std::string localAddress();                                        // best guess of the LAN address ("127.0.0.1" fallback)
std::string subnetBroadcast();                                     // directed broadcast of the LAN interface ("255.255.255.255" fallback)

}  // namespace slip::net
