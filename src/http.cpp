#include "http.h"

#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
using socket_t = SOCKET;
static const socket_t kBadSocket = INVALID_SOCKET;
static void CloseSocket(socket_t s) { closesocket(s); }
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
using socket_t = int;
static const socket_t kBadSocket = -1;
static void CloseSocket(socket_t s) { close(s); }
#endif

namespace clikstp {

void HttpInit() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

void HttpShutdown() {
#ifdef _WIN32
    WSACleanup();
#endif
}

static void SetTimeouts(socket_t s, int ms) {
#ifdef _WIN32
    DWORD t = static_cast<DWORD>(ms);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof t);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&t), sizeof t);
#else
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#endif
}

HttpResult HttpRequest(const char* method, int port, const std::string& path, const std::string& body, int timeoutMs) {
    HttpResult result;
    socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kBadSocket) { result.error = "no socket"; return result; }
    SetTimeouts(s, timeoutMs);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // the app only listens here
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        CloseSocket(s);
        result.error = "the CLIKSTP app isn't answering on port " + std::to_string(port);
        return result;
    }

    // Host must name the loopback address: the app refuses other host names (DNS rebinding guard). No Origin
    // header: a native client isn't a web page, and the app lets it through like curl.
    std::string req = std::string(method) + " " + path + " HTTP/1.0\r\n"
                      "Host: 127.0.0.1:" + std::to_string(port) + "\r\n"
                      "Accept: application/json\r\n"
                      "User-Agent: CLIKSTP-XPlane-Panel\r\n";
    if (!body.empty() || std::strcmp(method, "GET") != 0) {
        req += "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    }
    req += "\r\n" + body;

    size_t sent = 0;
    while (sent < req.size()) {
        int n = send(s, req.data() + sent, static_cast<int>(req.size() - sent), 0);
        if (n <= 0) { CloseSocket(s); result.error = "send failed"; return result; }
        sent += static_cast<size_t>(n);
    }

    std::string raw;
    char buf[8192];
    for (;;) {
        int n = recv(s, buf, sizeof buf, 0);
        if (n <= 0) break;   // closed (done) or timed out
        raw.append(buf, static_cast<size_t>(n));
        if (raw.size() > 8 * 1024 * 1024) break;   // nothing we ask for is this big
    }
    CloseSocket(s);

    // "HTTP/1.1 200 OK\r\n...headers...\r\n\r\nbody"
    size_t lineEnd = raw.find("\r\n");
    size_t headEnd = raw.find("\r\n\r\n");
    if (raw.compare(0, 5, "HTTP/") != 0 || lineEnd == std::string::npos || headEnd == std::string::npos) {
        result.error = raw.empty() ? "no answer" : "not an HTTP answer";
        return result;
    }
    size_t sp = raw.find(' ');
    result.status = sp != std::string::npos && sp < lineEnd ? std::atoi(raw.c_str() + sp + 1) : 0;
    result.body = raw.substr(headEnd + 4);
    return result;
}

}  // namespace clikstp
