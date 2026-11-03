#include "api_client.h"

#include <cctype>
#include <chrono>

#include "http.h"
#include "nlohmann/json.hpp"

namespace clikstp {

using json = nlohmann::json;

static std::string Str(const json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

static bool Bool(const json& j, const char* key, bool fallback = false) {
    auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

bool ParseStatus(const std::string& body, PanelState& state) {
    json j = json::parse(body, nullptr, /*allow_exceptions*/ false);
    if (!j.is_object() || !j.contains("sim") || !j.contains("tools")) return false;

    PanelState s;
    s.reachable = true;
    const json& app = j.value("app", json::object());
    s.running = Bool(app, "running", true);
    const json& sim = j["sim"];
    s.simConnected = Bool(sim, "connected");
    s.simName = Str(sim, "name");
    s.aircraft = Str(sim, "aircraft");
    s.padConnected = Bool(j.value("pad", json::object()), "connected");

    const json& tool = j.value("tool", json());
    if (tool.is_object()) {
        s.currentId = Str(tool, "id");
        s.screen0 = Str(tool, "screen");
        s.screen1 = Str(tool, "readout");
    }

    for (const json& t : j["tools"]) {
        if (!t.is_object() || !Bool(t, "available")) continue;
        s.tools.push_back({Str(t, "id"), Str(t, "label")});
    }
    if (j.contains("units") && j["units"].is_array())
        for (const json& u : j["units"])
            if (u.is_object() && Bool(u, "keys")) s.keysUnit = Str(u, "label");

    s.generation = state.generation;
    state = std::move(s);
    return true;
}

// Ids are letters, digits, - and _ (the app enforces it), but encode anything else rather than trust that.
static std::string PathSegment(const std::string& id) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : id) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') out += static_cast<char>(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

void ApiClient::Start(int port) {
    if (thread_.joinable()) return;
    port_ = port;
    stop_ = false;
    HttpInit();
    thread_ = std::thread([this] { Run(); });
}

void ApiClient::Stop() {
    if (!thread_.joinable()) return;
    stop_ = true;
    wake_.notify_all();
    thread_.join();
    HttpShutdown();
}

void ApiClient::SetVisible(bool visible) {
    bool was = visible_.exchange(visible);
    if (visible && !was) wake_.notify_all();   // fresh the moment it opens
}

PanelState ApiClient::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void ApiClient::SelectTool(const std::string& id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        posts_.emplace_back("/api/tools/" + PathSegment(id) + "/select", "");
        // Show the choice at once; the next status confirms it (or puts it back).
        for (const auto& t : state_.tools)
            if (t.id == id) { state_.currentId = id; state_.screen0 = t.label; state_.screen1.clear(); ++state_.generation; }
    }
    wake_.notify_all();
}

void ApiClient::StartApp() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        posts_.emplace_back("/api/app/start", "");
    }
    wake_.notify_all();
}

void ApiClient::Run() {
    while (!stop_) {
        // Clicks first, so the status that follows shows them.
        for (;;) {
            std::pair<std::string, std::string> post;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (posts_.empty()) break;
                post = std::move(posts_.front());
                posts_.pop_front();
            }
            HttpRequest("POST", port_, post.first, post.second);
        }

        Poll();

        auto wait = visible_ ? std::chrono::milliseconds(250) : std::chrono::milliseconds(2000);
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait_for(lock, wait, [this] { return stop_.load() || !posts_.empty(); });
    }
}

void ApiClient::Poll() {
    HttpResult r = HttpRequest("GET", port_, "/api/status");
    std::lock_guard<std::mutex> lock(mutex_);
    PanelState before = state_;
    if (r.status == 200 && ParseStatus(r.body, state_)) {
        state_.error.clear();
    } else {
        state_ = PanelState{};
        state_.generation = before.generation;
        state_.error = r.status == 0 ? r.error : "the app answered " + std::to_string(r.status);
    }
    // Bump only on a real change, so the UI can tell "new" from "same again".
    bool same = before.reachable == state_.reachable && before.running == state_.running &&
                before.simConnected == state_.simConnected && before.simName == state_.simName &&
                before.aircraft == state_.aircraft && before.padConnected == state_.padConnected &&
                before.currentId == state_.currentId && before.screen0 == state_.screen0 &&
                before.screen1 == state_.screen1 && before.keysUnit == state_.keysUnit &&
                before.error == state_.error && before.tools.size() == state_.tools.size();
    if (same)
        for (size_t i = 0; i < state_.tools.size(); ++i)
            if (before.tools[i].id != state_.tools[i].id || before.tools[i].label != state_.tools[i].label) { same = false; break; }
    if (!same) ++state_.generation;
}

}  // namespace clikstp
