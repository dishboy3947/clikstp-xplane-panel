// Dear ImGui in a native X-Plane window: an XPLM "modern" window (X-Plane's own frame, title bar, close and pop-out
// buttons) whose contents ImGui draws. It works floating in the sim window, popped out into its own OS window, and
// in VR, because everything goes through the window's own geometry and X-Plane's drawing state.
#pragma once

#include <functional>
#include <string>

#include "XPLMDisplay.h"

struct ImGuiContext;
struct ImFont;

namespace clikstp {

class XplmImGuiWindow {
public:
    struct Fonts {
        ImFont* ui = nullptr;
        ImFont* small = nullptr;
        ImFont* screen = nullptr;
    };

    // left/top/right/bottom in global desktop boxels. fontDir holds Roboto-Medium.ttf and Cousine-Regular.ttf.
    XplmImGuiWindow(const std::string& title, int left, int top, int right, int bottom, const std::string& fontDir);
    ~XplmImGuiWindow();
    XplmImGuiWindow(const XplmImGuiWindow&) = delete;
    XplmImGuiWindow& operator=(const XplmImGuiWindow&) = delete;

    // Called inside each frame, between ImGui::NewFrame() and ImGui::Render(), to draw the contents.
    std::function<void()> onBuild;

    const Fonts& GetFonts() const { return fonts_; }
    XPLMWindowID Id() const { return window_; }

    bool IsVisible() const;
    void SetVisible(bool visible);
    void BringToFront();

    // Where it is: floating in the sim, its own OS window, or in VR.
    bool IsPoppedOut() const;
    bool IsInVR() const;
    void PopOut();
    void FloatInSim();
    void MoveToVR();

    // Floating geometry (global boxels), for saving and restoring between flights.
    void GetGeometry(int& left, int& top, int& right, int& bottom) const;
    void SetGeometry(int left, int top, int right, int bottom);

private:
    static void Draw(XPLMWindowID, void* refcon);
    static int Click(XPLMWindowID, int x, int y, XPLMMouseStatus status, void* refcon);
    static int RightClick(XPLMWindowID, int x, int y, XPLMMouseStatus status, void* refcon);
    static void Key(XPLMWindowID, char key, XPLMKeyFlags flags, char vkey, void* refcon, int losingFocus);
    static XPLMCursorStatus Cursor(XPLMWindowID, int x, int y, void* refcon);
    static int Wheel(XPLMWindowID, int x, int y, int wheel, int clicks, void* refcon);

    void DrawFrame();
    void RenderDrawData();
    void MouseAt(int x, int y);
    void BoxelsToPixels(float bx, float by, int& px, int& py) const;
    void BuildFonts(float pixelsPerBoxel);

    XPLMWindowID window_ = nullptr;
    ImGuiContext* context_ = nullptr;
    Fonts fonts_;
    int fontTexture_ = 0;
    std::string fontDir_;
    float fontScale_ = 0;      // pixels per boxel the font atlas was made for
    int left_ = 0, top_ = 0, right_ = 0, bottom_ = 0;   // geometry of the frame being drawn
    float lastTime_ = 0;
    bool mouseSeen_ = false;   // the cursor callback ran since the last frame
    bool buttonDown_ = false;
};

}  // namespace clikstp
