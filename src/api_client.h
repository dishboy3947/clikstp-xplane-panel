// Keeps a PanelState up to date from the CLIKSTP app, on its own thread, and sends the panel's clicks back.
// X-Plane's thread only ever takes a snapshot (a quick copy under a lock) and queues requests: it never waits on
// the network.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "panel_state.h"

namespace clikstp {

class ApiClient {
public:
    ApiClient() = default;
    ~ApiClient() { Stop(); }
    ApiClient(const ApiClient&) = delete;
    ApiClient& operator=(const ApiClient&) = delete;

    void Start(int port);
    void Stop();

    // While the panel is on screen it asks four times a second; hidden, every two seconds (just to know whether
    // the app is there, so it's right the moment it opens).
    void SetVisible(bool visible);

    PanelState Snapshot() const;

    // Queued; sent from the worker thread. The next status shows the result.
    void SelectTool(const std::string& id);
    void StartApp();

    int Port() const { return port_; }

private:
    void Run();
    void Poll();

    int port_ = 8780;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> visible_{false};

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    PanelState state_;
    std::deque<std::pair<std::string, std::string>> posts_;   // (path, body)
};

}  // namespace clikstp
