#include "panel_ui.h"

#include <algorithm>
#include <cstdio>

#include "imgui.h"

namespace clikstp {

// The CLIKSTP page's colours (app.css, dark theme).
namespace col {
static const ImVec4 Panel = ImVec4(0x2a / 255.f, 0x2f / 255.f, 0x35 / 255.f, 1);
static const ImVec4 Raised = ImVec4(0x33 / 255.f, 0x39 / 255.f, 0x40 / 255.f, 1);
static const ImVec4 Line = ImVec4(0x48 / 255.f, 0x50 / 255.f, 0x5a / 255.f, 1);
static const ImVec4 Text = ImVec4(0xe8 / 255.f, 0xeb / 255.f, 0xee / 255.f, 1);
static const ImVec4 Muted = ImVec4(0x9b / 255.f, 0xa4 / 255.f, 0xae / 255.f, 1);
static const ImVec4 Amber = ImVec4(0xe8 / 255.f, 0xa3 / 255.f, 0x3d / 255.f, 1);
static const ImVec4 AmberInk = ImVec4(0x1f / 255.f, 0x16 / 255.f, 0x08 / 255.f, 1);
static const ImVec4 Green = ImVec4(0x3f / 255.f, 0xbf / 255.f, 0x7f / 255.f, 1);
static const ImVec4 GreenInk = ImVec4(0x06 / 255.f, 0x14 / 255.f, 0x0c / 255.f, 1);
static const ImVec4 Red = ImVec4(0xe5 / 255.f, 0x53 / 255.f, 0x4b / 255.f, 1);
static const ImVec4 Unlit = ImVec4(0x3b / 255.f, 0x42 / 255.f, 0x49 / 255.f, 1);
static const ImVec4 UnlitText = ImVec4(0x7d / 255.f, 0x87 / 255.f, 0x91 / 255.f, 1);
static const ImVec4 Glass = ImVec4(0, 0, 0, 1);
static const ImVec4 Pixel = ImVec4(0xdc / 255.f, 0xeb / 255.f, 0xff / 255.f, 1);
}  // namespace col

void ApplyPanelStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowPadding = ImVec2(12, 10);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(8, 8);
    s.WindowRounding = 0;
    s.ChildRounding = 6;
    s.FrameRounding = 4;
    s.ScrollbarSize = 12;
    s.ScrollbarRounding = 6;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = col::Panel;
    c[ImGuiCol_ChildBg] = col::Panel;
    c[ImGuiCol_Border] = col::Line;
    c[ImGuiCol_Text] = col::Text;
    c[ImGuiCol_TextDisabled] = col::Muted;
    c[ImGuiCol_Button] = col::Raised;
    c[ImGuiCol_ButtonHovered] = col::Line;
    c[ImGuiCol_ButtonActive] = col::Unlit;
    c[ImGuiCol_Header] = ImVec4(col::Amber.x, col::Amber.y, col::Amber.z, 0.22f);
    c[ImGuiCol_HeaderHovered] = col::Raised;
    c[ImGuiCol_HeaderActive] = col::Line;
    c[ImGuiCol_ScrollbarBg] = col::Panel;
    c[ImGuiCol_ScrollbarGrab] = col::Line;
    c[ImGuiCol_ScrollbarGrabHovered] = col::Muted;
    c[ImGuiCol_Separator] = col::Line;
}

// A lit annunciator, like the page's (green on, grey off, red fault).
static void Annunciator(const char* label, bool on, bool fault, const PanelFonts& f) {
    ImGui::PushFont(f.small);
    ImVec2 text = ImGui::CalcTextSize(label);
    ImVec2 size(std::max(56.f, text.x + 20), text.y + 10);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 bg = fault ? col::Red : on ? col::Green : col::Unlit;
    ImVec4 fg = fault ? ImVec4(1, 1, 1, 1) : on ? col::GreenInk : col::UnlitText;
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(bg), 3);
    dl->AddText(ImVec2(p.x + (size.x - text.x) / 2, p.y + (size.y - text.y) / 2), ImGui::GetColorU32(fg), label);
    ImGui::Dummy(size);
    ImGui::PopFont();
}

// The pad's 21-column screen: what it would show right now.
static void Screen(const PanelState& s, const PanelFonts& f) {
    const char* l0;
    const char* l1;
    std::string a, b;
    if (!s.reachable) { l0 = "CLIKSTP not running"; l1 = ""; }
    else if (!s.running) { l0 = "Stopped"; l1 = ""; }
    else if (s.screen0.empty() && !s.simConnected) { l0 = "Awaiting sim"; l1 = "connection"; }
    else { a = s.screen0.substr(0, 21); b = s.screen1.substr(0, 21); l0 = a.c_str(); l1 = b.c_str(); }

    ImGui::PushFont(f.screen);
    float line = ImGui::GetTextLineHeight();
    float width = ImGui::GetContentRegionAvail().x;
    float height = line * 2 + 22;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height + 12), ImGui::GetColorU32(ImVec4(0x14 / 255.f, 0x17 / 255.f, 0x1a / 255.f, 1)), 8);
    dl->AddRectFilled(ImVec2(p.x + 6, p.y + 6), ImVec2(p.x + width - 6, p.y + height + 6), ImGui::GetColorU32(col::Glass), 4);
    ImU32 pixel = ImGui::GetColorU32(s.padConnected || !s.reachable ? col::Pixel : ImVec4(col::Pixel.x, col::Pixel.y, col::Pixel.z, 0.55f));
    dl->AddText(ImVec2(p.x + 16, p.y + 14), pixel, l0);
    dl->AddText(ImVec2(p.x + 16, p.y + 14 + line), pixel, l1);
    ImGui::Dummy(ImVec2(width, height + 12));
    ImGui::PopFont();
}

static void Hint(const char* text, const ImVec4& colour, const PanelFonts& f) {
    ImGui::PushFont(f.small);
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void DrawPanel(const PanelState& s, const PanelFonts& f, const PanelActions& actions, PanelMemory& memory) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("CLIKSTP", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushFont(f.ui);

    // --- The fixed top: screen, connections, what needs attention ---
    Screen(s, f);
    Annunciator("App", s.reachable, false, f);
    ImGui::SameLine();
    Annunciator("Pad", s.reachable && s.padConnected, false, f);
    if (s.reachable && !s.keysUnit.empty()) {
        ImGui::SameLine();
        ImGui::PushFont(f.small);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Keys: %s", s.keysUnit.c_str());
        ImGui::PopFont();
    }

    if (!s.reachable) {
        Hint("Can't reach the CLIKSTP app. Start it on this computer (it lives in the tray); this panel connects on its own.",
             col::Red, f);
    } else if (!s.running) {
        Hint("Stopped: CLIKSTP has let go of the pad and the sim.", col::Amber, f);
        ImGui::PushStyleColor(ImGuiCol_Button, col::Amber);
        ImGui::PushStyleColor(ImGuiCol_Text, col::AmberInk);
        if (ImGui::Button("Start") && actions.startApp) actions.startApp();
        ImGui::PopStyleColor(2);
    } else if (!s.simConnected) {
        std::string t = "Awaiting sim connection (" + s.simName + ").";
        Hint(t.c_str(), col::Muted, f);
    } else if (!s.padConnected) {
        Hint("The pad isn't connected. The tools below still work from here.", col::Amber, f);
    }

    // --- The tools, scrolling under the fixed top; the selected one kept in view ---
    float footer = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("tools", ImVec2(0, -footer), ImGuiChildFlags_Borders);
    if (s.tools.empty()) {
        ImGui::PushFont(f.small);
        ImGui::TextDisabled("%s", s.reachable && s.running && s.simConnected ? "No tools for this aircraft."
                                                                                 : "The tools show here once X-Plane is connected.");
        ImGui::PopFont();
    }
    for (const PanelTool& t : s.tools) {
        bool current = t.id == s.currentId;
        ImGui::PushID(t.id.c_str());
        if (current) ImGui::PushStyleColor(ImGuiCol_Text, col::Amber);
        // Tall rows: easy to hit in VR and on a small popped-out window.
        if (ImGui::Selectable(t.label.c_str(), current, 0, ImVec2(0, ImGui::GetTextLineHeight() + 10)) && !current &&
            actions.selectTool)
            actions.selectTool(t.id);
        if (current) ImGui::PopStyleColor();
        // The big encoder moved: scroll the new tool into the middle, so its neighbours (next / previous) show.
        if (current && memory.scrolledTo != t.id) {
            ImGui::SetScrollHereY(0.5f);
            memory.scrolledTo = t.id;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    // --- Footer ---
    ImGui::PushFont(f.small);
    if (ImGui::Button("Open the CLIKSTP page") && actions.openPage) actions.openPage();
    if (s.reachable && s.simConnected && !s.aircraft.empty()) {
        std::string name = s.aircraft;
        size_t slash = name.find_last_of("/\\");
        if (slash != std::string::npos) name = name.substr(slash + 1);
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".acf") == 0) name.resize(name.size() - 4);
        float w = ImGui::CalcTextSize(name.c_str()).x;
        float x = ImGui::GetWindowContentRegionMax().x - w;
        ImGui::SameLine();
        if (x > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", name.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.aircraft.c_str());
    }
    ImGui::PopFont();

    ImGui::PopFont();
    ImGui::End();
    memory.seenGeneration = s.generation;
}

}  // namespace clikstp
