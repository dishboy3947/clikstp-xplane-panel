// A tiny HTTP client for one job: talking to the CLIKSTP app on this computer (127.0.0.1). HTTP/1.0, so the app
// answers with a plain body and closes the connection: no chunking, no keep-alive, no TLS, nothing to link.
#pragma once

#include <string>

namespace clikstp {

struct HttpResult {
    int status = 0;          // 0 when the app couldn't be reached
    std::string body;
    std::string error;       // why it failed, for the log
};

// One request; blocks up to timeoutMs. Call from a worker thread, never from X-Plane's.
HttpResult HttpRequest(const char* method, int port, const std::string& path, const std::string& body = "",
                       int timeoutMs = 1500);

// Once per process, before the first request (Winsock wants it; elsewhere it does nothing).
void HttpInit();
void HttpShutdown();

}  // namespace clikstp
