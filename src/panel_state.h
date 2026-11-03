// What the panel shows: the CLIKSTP app's status, boiled down. Plain data; the API client fills it from
// GET /api/status, the UI draws it. Kept apart from X-Plane and from the network so both can be tested alone.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace clikstp {

struct PanelTool {
    std::string id;
    std::string label;
};

struct PanelState {
    bool reachable = false;      // the app answered
    bool running = true;         // the app's controller isn't stopped (tray Stop)
    bool simConnected = false;
    std::string simName;         // "X-Plane 12.4.4", or what it's waiting for
    std::string aircraft;        // the loaded .acf, relative to X-Plane's folder
    bool padConnected = false;

    // The pad's screen: line 0 is the tool, line 1 its readout. Empty tool = nothing selected.
    std::string currentId;
    std::string screen0;
    std::string screen1;

    std::vector<PanelTool> tools;   // the tools the big encoder visits right now, in order
    std::string keysUnit;           // the avionics unit the 16 keys drive ("GNS 1"), or empty

    std::string error;           // why the app couldn't be reached, when it couldn't
    uint64_t generation = 0;     // bumps whenever any of the above changes
};

// Fills a state from the body of GET /api/status. False (and state untouched) when it isn't readable.
bool ParseStatus(const std::string& json, PanelState& state);

}  // namespace clikstp
