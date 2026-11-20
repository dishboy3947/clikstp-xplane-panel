// The panel itself, drawn with Dear ImGui from a PanelState. No X-Plane here: the plugin calls it inside its window,
// and tools/preview.cpp calls it headless to render screenshots.
#pragma once

#include <functional>
#include <string>

#include "panel_state.h"

struct ImFont;

namespace clikstp {

struct PanelFonts {
    ImFont* ui = nullptr;       // Roboto, for everything else
    ImFont* screen = nullptr;   // Cousine (monospace), for the pad's screen
    ImFont* note = nullptr;    // Roboto, smaller: indicators and notes
};

struct PanelActions {
    std::function<void(const std::string&)> selectTool;
    std::function<void()> startApp;
    std::function<void()> openPage;   // the CLIKSTP page in a browser
};

// What the UI remembers between frames (which tool it last scrolled to).
struct PanelMemory {
    std::string scrolledTo;
    uint64_t seenGeneration = ~0ull;
};

// Adds the default style (dark, like the CLIKSTP page's panel mode) to the current ImGui context.
void ApplyPanelStyle();

// Draws the whole panel into the current ImGui frame, filling the display.
void DrawPanel(const PanelState& state, const PanelFonts& fonts, const PanelActions& actions, PanelMemory& memory);

}  // namespace clikstp
