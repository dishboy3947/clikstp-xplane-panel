#include "imgui_xplm.h"

#include <cfloat>
#include <cstdint>

#include "XPLMDataAccess.h"
#include "XPLMGraphics.h"
#include "XPLMProcessing.h"
#include "imgui.h"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <GL/gl.h>
#elif defined(__APPLE__)
#  include <OpenGL/gl.h>
#else
#  include <GL/gl.h>
#endif

namespace clikstp {

// X-Plane publishes the matrices it draws windows with, so boxels (what windows and ImGui think in) can be turned
// into the window-system pixels glScissor needs, whatever the UI scale, pop-out or VR. (XPLM 3.01 and later.)
static XPLMDataRef gModelview = nullptr, gProjection = nullptr, gViewport = nullptr;

XplmImGuiWindow::XplmImGuiWindow(const std::string& title, int left, int top, int right, int bottom,
                                 const std::string& fontDir) {
    if (!gModelview) {
        gModelview = XPLMFindDataRef("sim/graphics/view/modelview_matrix");
        gProjection = XPLMFindDataRef("sim/graphics/view/projection_matrix");
        gViewport = XPLMFindDataRef("sim/graphics/view/viewport");
    }

    // Our own ImGui context: another plugin's ImGui (in its own DLL) never sees ours.
    ImGuiContext* previous = ImGui::GetCurrentContext();
    context_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // the plugin saves the window's place itself
    io.LogFilename = nullptr;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.BackendPlatformName = "clikstp_xplm";
    io.BackendRendererName = "clikstp_xplm_gl";

    fontDir_ = fontDir;
    if (!fontDir_.empty() && fontDir_.back() != '/' && fontDir_.back() != '\\') fontDir_ += '/';
    XPLMGenerateTextureNumbers(&fontTexture_, 1);
    BuildFonts(1.0f);

    XPLMCreateWindow_t params{};
    params.structSize = sizeof params;
    params.left = left;
    params.top = top;
    params.right = right;
    params.bottom = bottom;
    params.visible = 0;
    params.drawWindowFunc = Draw;
    params.handleMouseClickFunc = Click;
    params.handleRightClickFunc = RightClick;
    params.handleKeyFunc = Key;
    params.handleCursorFunc = Cursor;
    params.handleMouseWheelFunc = Wheel;
    params.refcon = this;
    params.decorateAsFloatingWindow = xplm_WindowDecorationRoundRectangle;
    params.layer = xplm_WindowLayerFloatingWindows;
    window_ = XPLMCreateWindowEx(&params);
    XPLMSetWindowTitle(window_, title.c_str());
    XPLMSetWindowResizingLimits(window_, 240, 300, 1200, 2000);
    XPLMSetWindowPositioningMode(window_, xplm_WindowPositionFree, -1);

    lastTime_ = XPLMGetElapsedTime();
    ImGui::SetCurrentContext(previous);
}

XplmImGuiWindow::~XplmImGuiWindow() {
    if (window_) XPLMDestroyWindow(window_);
    if (fontTexture_) {
        GLuint t = static_cast<GLuint>(fontTexture_);
        glDeleteTextures(1, &t);
    }
    if (context_) ImGui::DestroyContext(context_);
}

// The fonts are rasterized at the screen's real resolution (X-Plane's UI scale, a Retina display), then drawn
// scaled back to boxels, so text stays sharp instead of being stretched from a 1x atlas.
void XplmImGuiWindow::BuildFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    fonts_.ui = io.Fonts->AddFontFromFileTTF((fontDir_ + "Roboto-Medium.ttf").c_str(), 17.0f * scale, &cfg);
    fonts_.note = io.Fonts->AddFontFromFileTTF((fontDir_ + "Roboto-Medium.ttf").c_str(), 14.0f * scale, &cfg);
    fonts_.screen = io.Fonts->AddFontFromFileTTF((fontDir_ + "Cousine-Regular.ttf").c_str(), 20.0f * scale, &cfg);
    // Missing font files (a broken install) shouldn't take the panel down: ImGui's built-in font stands in.
    if (!fonts_.ui || !fonts_.note || !fonts_.screen) {
        io.Fonts->Clear();
        ImFontConfig builtin;
        builtin.SizePixels = 13.0f * scale;
        ImFont* f = io.Fonts->AddFontDefault(&builtin);
        fonts_.ui = fonts_.note = fonts_.screen = f;
    }
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    XPLMBindTexture2d(fontTexture_, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    io.Fonts->SetTexID((ImTextureID)(intptr_t)fontTexture_);
    io.FontGlobalScale = 1.0f / scale;
    fontScale_ = scale;
}

bool XplmImGuiWindow::IsVisible() const { return window_ && XPLMGetWindowIsVisible(window_); }
void XplmImGuiWindow::SetVisible(bool visible) { if (window_) XPLMSetWindowIsVisible(window_, visible ? 1 : 0); }
void XplmImGuiWindow::BringToFront() { if (window_) XPLMBringWindowToFront(window_); }
bool XplmImGuiWindow::IsPoppedOut() const { return window_ && XPLMWindowIsPoppedOut(window_); }
bool XplmImGuiWindow::IsInVR() const { return window_ && XPLMWindowIsInVR(window_); }
void XplmImGuiWindow::PopOut() { XPLMSetWindowPositioningMode(window_, xplm_WindowPopOut, -1); }
void XplmImGuiWindow::FloatInSim() { XPLMSetWindowPositioningMode(window_, xplm_WindowPositionFree, -1); }
void XplmImGuiWindow::MoveToVR() { XPLMSetWindowPositioningMode(window_, xplm_WindowVR, -1); }

void XplmImGuiWindow::GetGeometry(int& left, int& top, int& right, int& bottom) const {
    XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);
}

void XplmImGuiWindow::SetGeometry(int left, int top, int right, int bottom) {
    XPLMSetWindowGeometry(window_, left, top, right, bottom);
}

// --- Callbacks (X-Plane's thread) --------------------------------------------------------------------

void XplmImGuiWindow::Draw(XPLMWindowID, void* refcon) { static_cast<XplmImGuiWindow*>(refcon)->DrawFrame(); }

void XplmImGuiWindow::MouseAt(int x, int y) {
    // Window callbacks get the same coordinates as XPLMGetWindowGeometry: boxels, y up. ImGui wants y down from the
    // window's top-left corner.
    int l, t, r, b;
    XPLMGetWindowGeometry(window_, &l, &t, &r, &b);
    ImGui::GetIO().AddMousePosEvent(static_cast<float>(x - l), static_cast<float>(t - y));
    mouseSeen_ = true;
}

int XplmImGuiWindow::Click(XPLMWindowID, int x, int y, XPLMMouseStatus status, void* refcon) {
    auto* self = static_cast<XplmImGuiWindow*>(refcon);
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(self->context_);
    self->MouseAt(x, y);
    if (status == xplm_MouseDown) { ImGui::GetIO().AddMouseButtonEvent(0, true); self->buttonDown_ = true; }
    else if (status == xplm_MouseUp) { ImGui::GetIO().AddMouseButtonEvent(0, false); self->buttonDown_ = false; }
    ImGui::SetCurrentContext(previous);
    return 1;   // ours: don't click the cockpit underneath
}

int XplmImGuiWindow::RightClick(XPLMWindowID, int, int, XPLMMouseStatus, void*) { return 1; }

void XplmImGuiWindow::Key(XPLMWindowID, char, XPLMKeyFlags, char, void*, int) {
    // The panel never takes keyboard focus, so keys keep flying the aircraft.
}

XPLMCursorStatus XplmImGuiWindow::Cursor(XPLMWindowID, int x, int y, void* refcon) {
    auto* self = static_cast<XplmImGuiWindow*>(refcon);
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(self->context_);
    self->MouseAt(x, y);
    ImGui::SetCurrentContext(previous);
    return xplm_CursorDefault;
}

int XplmImGuiWindow::Wheel(XPLMWindowID, int x, int y, int wheel, int clicks, void* refcon) {
    auto* self = static_cast<XplmImGuiWindow*>(refcon);
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(self->context_);
    self->MouseAt(x, y);
    if (wheel == 0) ImGui::GetIO().AddMouseWheelEvent(0, static_cast<float>(clicks));
    else ImGui::GetIO().AddMouseWheelEvent(static_cast<float>(clicks), 0);
    ImGui::SetCurrentContext(previous);
    return 1;
}

// --- One frame -------------------------------------------------------------------------------------------

void XplmImGuiWindow::DrawFrame() {
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();

    XPLMGetWindowGeometry(window_, &left_, &top_, &right_, &bottom_);

    // How many pixels a boxel is right now (it changes with the UI scale, and when popped out onto another screen).
    int x0, y0, x1, y1;
    BoxelsToPixels(static_cast<float>(left_), static_cast<float>(bottom_), x0, y0);
    BoxelsToPixels(static_cast<float>(left_ + 100), static_cast<float>(bottom_), x1, y1);
    float scale = (x1 - x0) / 100.0f;
    if (scale < 0.5f || scale > 4.0f) scale = 1.0f;   // matrices not what we expect: don't guess
    scale = static_cast<float>(static_cast<int>(scale * 4 + 0.5f)) / 4;   // quarter steps: no rebuilds on jitter
    if (scale != fontScale_) BuildFonts(scale);

    io.DisplaySize = ImVec2(static_cast<float>(right_ - left_), static_cast<float>(top_ - bottom_));
    io.DisplayFramebufferScale = ImVec2(1, 1);
    float now = XPLMGetElapsedTime();
    io.DeltaTime = now > lastTime_ ? now - lastTime_ : 1.0f / 60.0f;
    lastTime_ = now;

    // No cursor callback since the last frame: the mouse has left the window (unless a button is held, as when
    // dragging the list's scrollbar out of the window).
    if (!mouseSeen_ && !buttonDown_) io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    mouseSeen_ = false;

    ImGui::NewFrame();
    if (onBuild) onBuild();
    ImGui::Render();
    RenderDrawData();

    ImGui::SetCurrentContext(previous);
}

void XplmImGuiWindow::BoxelsToPixels(float bx, float by, int& px, int& py) const {
    float mv[16], pr[16];
    int vp[4];
    XPLMGetDatavf(gModelview, mv, 0, 16);
    XPLMGetDatavf(gProjection, pr, 0, 16);
    XPLMGetDatavi(gViewport, vp, 0, 4);
    // Column-major 4x4, as OpenGL keeps them.
    float eye[4], clip[4];
    for (int i = 0; i < 4; i++) eye[i] = mv[i] * bx + mv[4 + i] * by + mv[12 + i];
    for (int i = 0; i < 4; i++) clip[i] = pr[i] * eye[0] + pr[4 + i] * eye[1] + pr[8 + i] * eye[2] + pr[12 + i] * eye[3];
    float w = clip[3] != 0 ? clip[3] : 1;
    px = static_cast<int>(vp[0] + (clip[0] / w + 1) * 0.5f * vp[2] + 0.5f);
    py = static_cast<int>(vp[1] + (clip[1] / w + 1) * 0.5f * vp[3] + 0.5f);
}

void XplmImGuiWindow::RenderDrawData() {
    ImDrawData* data = ImGui::GetDrawData();
    if (!data || data->CmdListsCount == 0) return;

    // X-Plane's way to set the fixed-function state: no fog, one texture unit, no lighting, no alpha test, blending
    // on, no depth.
    XPLMSetGraphicsState(0, 1, 0, 0, 1, 0, 0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);

    const float L = static_cast<float>(left_), T = static_cast<float>(top_);
    for (int n = 0; n < data->CmdListsCount; n++) {
        const ImDrawList* list = data->CmdLists[n];
        const ImDrawVert* vtx = list->VtxBuffer.Data;
        const ImDrawIdx* idx = list->IdxBuffer.Data;
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) { cmd.UserCallback(list, &cmd); continue; }
            if (cmd.ClipRect.z <= cmd.ClipRect.x || cmd.ClipRect.w <= cmd.ClipRect.y) continue;

            // The clip rectangle, from window-local (y down) to boxels (y up) to pixels.
            int x0, y0, x1, y1;
            BoxelsToPixels(L + cmd.ClipRect.x, T - cmd.ClipRect.w, x0, y0);
            BoxelsToPixels(L + cmd.ClipRect.z, T - cmd.ClipRect.y, x1, y1);
            if (x1 <= x0 || y1 <= y0) continue;
            glScissor(x0, y0, x1 - x0, y1 - y0);

            XPLMBindTexture2d((int)(intptr_t)cmd.GetTexID(), 0);
            glBegin(GL_TRIANGLES);
            for (unsigned i = 0; i < cmd.ElemCount; i++) {
                const ImDrawVert& v = vtx[cmd.VtxOffset + idx[cmd.IdxOffset + i]];
                glColor4ub(static_cast<GLubyte>(v.col), static_cast<GLubyte>(v.col >> 8), static_cast<GLubyte>(v.col >> 16),
                           static_cast<GLubyte>(v.col >> 24));
                glTexCoord2f(v.uv.x, v.uv.y);
                glVertex2f(L + v.pos.x, T - v.pos.y);
            }
            glEnd();
        }
    }
    glDisable(GL_SCISSOR_TEST);
}

}  // namespace clikstp
