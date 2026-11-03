// A pretend X-Plane, for testing the built plugin (lin.xpl) on Linux without the sim. It provides the XPLM and
// OpenGL functions the plugin calls, loads the .xpl, opens the panel from the menu, draws frames into a software
// framebuffer (with X-Plane's boxel-to-pixel matrices at a 1.5x UI scale, so the scissor maths is exercised), clicks
// where it's told, and writes PNG screenshots.
//
//   fake_xplane <path/to/CLIKSTP/64/lin.xpl> <out-dir> [script...]
//   script steps: shot:NAME   frames:N   sleep:MS   click:X,Y (window-local, y down)   wheel:X,Y,CLICKS
//                 hide   show   menu:N   disable
#include <dlfcn.h>

#include <GL/gl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMMenus.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// --- The pretend desktop: 1920x1080 boxels, drawn at 1.5 pixels per boxel ---
static const int kW = 1920, kH = 1080;
static const float kScale = 1.5f;
static const int kPW = int(kW * kScale), kPH = int(kH * kScale);

static std::string gPluginPath, gOutDir;
static auto gStart = std::chrono::steady_clock::now();

struct Window {
    XPLMCreateWindow_t p;
    bool visible = false;
    int mode = xplm_WindowPositionFree;
    std::string title;
};
static std::vector<Window*> gWindows;

struct Menu { std::string name; XPLMMenuHandler_f handler; void* ref; std::vector<std::pair<std::string, void*>> items; };
static std::vector<Menu*> gMenus;
static std::map<std::string, std::pair<XPLMCommandCallback_f, void*>> gCommands;
static std::vector<XPLMCreateFlightLoop_t> gLoops;

// Framebuffer and GL state.
static std::vector<float> gFb(size_t(kPW) * kPH * 3, 0.35f);
static std::map<int, std::vector<unsigned char>> gTex;
static std::map<int, std::pair<int, int>> gTexSize;
static int gBound = 0, gNextTex = 1;
static bool gScissorOn = false;
static int gSc[4] = {0, 0, kPW, kPH};
struct V { float x, y, u, v; unsigned char c[4]; };
static std::vector<V> gTri;
static unsigned char gCol[4] = {255, 255, 255, 255};
static float gUV[2] = {0, 0};
static int gTriangles = 0, gScissors = 0;

// ---------------------------------------------------------------------------------------------------------
extern "C" {

void XPLMDebugString(const char* s) { std::fputs(s, stdout); }
void XPLMEnableFeature(const char*, int) {}
XPLMPluginID XPLMGetMyID(void) { return 1; }
void XPLMGetPluginInfo(XPLMPluginID, char* name, char* path, char* sig, char* desc) {
    if (name) std::strcpy(name, "plugin");
    if (path) std::strcpy(path, gPluginPath.c_str());
    if (sig) *sig = 0;
    if (desc) *desc = 0;
}
void XPLMGetPrefsPath(char* out) { std::strcpy(out, (gOutDir + "/X-Plane.prf").c_str()); }
float XPLMGetElapsedTime(void) {
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - gStart).count();
}

// Datarefs: the three drawing matrices, and VR off.
XPLMDataRef XPLMFindDataRef(const char* name) { return (XPLMDataRef)strdup(name); }
int XPLMGetDatai(XPLMDataRef) { return 0; }
int XPLMGetDatavf(XPLMDataRef ref, float* out, int off, int max) {
    std::string n = (const char*)ref;
    float m[16] = {0};
    if (n == "sim/graphics/view/modelview_matrix") { m[0] = m[5] = m[10] = m[15] = 1; }
    else if (n == "sim/graphics/view/projection_matrix") {   // boxels 0..W, 0..H -> -1..1
        m[0] = 2.f / kW; m[5] = 2.f / kH; m[10] = -1; m[12] = -1; m[13] = -1; m[15] = 1;
    }
    for (int i = 0; i < max && off + i < 16; i++) out[i] = m[off + i];
    return 16;
}
int XPLMGetDatavi(XPLMDataRef ref, int* out, int off, int max) {
    int vp[4] = {0, 0, kPW, kPH};
    for (int i = 0; i < max && off + i < 4; i++) out[i] = vp[off + i];
    (void)ref;
    return 4;
}

void XPLMGetScreenBoundsGlobal(int* l, int* t, int* r, int* b) {
    if (l) *l = 0; if (t) *t = kH; if (r) *r = kW; if (b) *b = 0;
}
XPLMWindowID XPLMCreateWindowEx(XPLMCreateWindow_t* p) {
    auto* w = new Window{*p};
    w->visible = p->visible;
    gWindows.push_back(w);
    return w;
}
void XPLMDestroyWindow(XPLMWindowID id) {
    gWindows.erase(std::remove(gWindows.begin(), gWindows.end(), (Window*)id), gWindows.end());
    delete (Window*)id;
}
void XPLMSetWindowTitle(XPLMWindowID id, const char* t) { ((Window*)id)->title = t; }
void XPLMSetWindowResizingLimits(XPLMWindowID, int, int, int, int) {}
void XPLMSetWindowPositioningMode(XPLMWindowID id, XPLMWindowPositioningMode m, int) { ((Window*)id)->mode = m; }
int XPLMWindowIsPoppedOut(XPLMWindowID id) { return ((Window*)id)->mode == xplm_WindowPopOut; }
int XPLMWindowIsInVR(XPLMWindowID id) { return ((Window*)id)->mode == xplm_WindowVR; }
void XPLMGetWindowGeometry(XPLMWindowID id, int* l, int* t, int* r, int* b) {
    auto& p = ((Window*)id)->p;
    if (l) *l = p.left; if (t) *t = p.top; if (r) *r = p.right; if (b) *b = p.bottom;
}
void XPLMSetWindowGeometry(XPLMWindowID id, int l, int t, int r, int b) {
    auto& p = ((Window*)id)->p;
    p.left = l; p.top = t; p.right = r; p.bottom = b;
}
int XPLMGetWindowIsVisible(XPLMWindowID id) { return ((Window*)id)->visible; }
void XPLMSetWindowIsVisible(XPLMWindowID id, int v) { ((Window*)id)->visible = v != 0; }
void XPLMBringWindowToFront(XPLMWindowID) {}

XPLMMenuID XPLMFindPluginsMenu(void) { return (XPLMMenuID)1; }
int XPLMAppendMenuItem(XPLMMenuID menu, const char* name, void* ref, int) {
    if (menu == (XPLMMenuID)1) return 0;
    auto* m = (Menu*)menu;
    m->items.push_back({name, ref});
    return int(m->items.size() - 1);
}
XPLMMenuID XPLMCreateMenu(const char* name, XPLMMenuID, int, XPLMMenuHandler_f h, void* ref) {
    auto* m = new Menu{name, h, ref, {}};
    gMenus.push_back(m);
    return m;
}
void XPLMDestroyMenu(XPLMMenuID) {}
void XPLMCheckMenuItem(XPLMMenuID, int, XPLMMenuCheck) {}
void XPLMSetMenuItemName(XPLMMenuID menu, int i, const char* name, int) { ((Menu*)menu)->items[i].first = name; }

XPLMCommandRef XPLMCreateCommand(const char* name, const char*) { return (XPLMCommandRef)strdup(name); }
void XPLMRegisterCommandHandler(XPLMCommandRef c, XPLMCommandCallback_f h, int, void* ref) { gCommands[(const char*)c] = {h, ref}; }
void XPLMUnregisterCommandHandler(XPLMCommandRef c, XPLMCommandCallback_f, int, void*) { gCommands.erase((const char*)c); }

XPLMFlightLoopID XPLMCreateFlightLoop(XPLMCreateFlightLoop_t* p) { gLoops.push_back(*p); return (XPLMFlightLoopID)gLoops.size(); }
void XPLMScheduleFlightLoop(XPLMFlightLoopID, float, int) {}
void XPLMDestroyFlightLoop(XPLMFlightLoopID) {}

void XPLMGenerateTextureNumbers(int* ids, int n) { for (int i = 0; i < n; i++) ids[i] = gNextTex++; }
void XPLMBindTexture2d(int t, int) { gBound = t; }
void XPLMSetGraphicsState(int, int, int, int, int, int, int) {}

// --- OpenGL, the calls the plugin makes ---
void glTexParameteri(GLenum, GLenum, GLint) {}
void glPixelStorei(GLenum, GLint) {}
void glTexImage2D(GLenum, GLint, GLint, GLsizei w, GLsizei h, GLint, GLenum, GLenum, const GLvoid* px) {
    gTex[gBound].assign((const unsigned char*)px, (const unsigned char*)px + size_t(w) * h * 4);
    gTexSize[gBound] = {w, h};
}
void glDeleteTextures(GLsizei, const GLuint*) {}
void glBlendFunc(GLenum, GLenum) {}
void glEnable(GLenum cap) { if (cap == GL_SCISSOR_TEST) gScissorOn = true; }
void glDisable(GLenum cap) { if (cap == GL_SCISSOR_TEST) gScissorOn = false; }
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h) { gSc[0] = x; gSc[1] = y; gSc[2] = w; gSc[3] = h; gScissors++; }
void glBegin(GLenum) { gTri.clear(); }
void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) { gCol[0] = r; gCol[1] = g; gCol[2] = b; gCol[3] = a; }
void glTexCoord2f(GLfloat u, GLfloat v) { gUV[0] = u; gUV[1] = v; }
void glVertex2f(GLfloat x, GLfloat y) {
    V v{x * kScale, y * kScale, gUV[0], gUV[1], {gCol[0], gCol[1], gCol[2], gCol[3]}};
    gTri.push_back(v);
}
void glEnd(void) {
    auto& tex = gTex[gBound];
    auto [tw, th] = gTexSize[gBound];
    for (size_t i = 0; i + 2 < gTri.size(); i += 3) {
        const V* v[3] = {&gTri[i], &gTri[i + 1], &gTri[i + 2]};
        gTriangles++;
        float area = (v[1]->x - v[0]->x) * (v[2]->y - v[0]->y) - (v[2]->x - v[0]->x) * (v[1]->y - v[0]->y);
        if (std::fabs(area) < 1e-6f) continue;
        int x0 = std::max(0, (int)std::floor(std::min({v[0]->x, v[1]->x, v[2]->x})));
        int x1 = std::min(kPW - 1, (int)std::ceil(std::max({v[0]->x, v[1]->x, v[2]->x})));
        int y0 = std::max(0, (int)std::floor(std::min({v[0]->y, v[1]->y, v[2]->y})));
        int y1 = std::min(kPH - 1, (int)std::ceil(std::max({v[0]->y, v[1]->y, v[2]->y})));
        if (gScissorOn) {
            x0 = std::max(x0, gSc[0]); x1 = std::min(x1, gSc[0] + gSc[2] - 1);
            y0 = std::max(y0, gSc[1]); y1 = std::min(y1, gSc[1] + gSc[3] - 1);
        }
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = ((v[1]->x - px) * (v[2]->y - py) - (v[2]->x - px) * (v[1]->y - py)) / area;
                float w1 = ((v[2]->x - px) * (v[0]->y - py) - (v[0]->x - px) * (v[2]->y - py)) / area;
                float w2 = 1 - w0 - w1;
                if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
                float u = w0 * v[0]->u + w1 * v[1]->u + w2 * v[2]->u, vv = w0 * v[0]->v + w1 * v[1]->v + w2 * v[2]->v;
                const unsigned char* t = tex.empty() ? nullptr
                    : &tex[(size_t(std::clamp(int(vv * th), 0, th - 1)) * tw + std::clamp(int(u * tw), 0, tw - 1)) * 4];
                float c[4];
                for (int k = 0; k < 4; k++)
                    c[k] = (w0 * v[0]->c[k] + w1 * v[1]->c[k] + w2 * v[2]->c[k]) / 255.f * (t ? t[k] / 255.f : 1.f);
                float* p = &gFb[(size_t(y) * kPW + x) * 3];
                for (int k = 0; k < 3; k++) p[k] = p[k] * (1 - c[3]) + c[k] * c[3];
            }
    }
}

}  // extern "C"

// ---------------------------------------------------------------------------------------------------------

static void Frame() {
    std::fill(gFb.begin(), gFb.end(), 0.35f);
    for (auto& l : gLoops) l.callbackFunc(0.05f, 0.05f, 1, l.refcon);
    for (Window* w : gWindows)
        if (w->visible) w->p.drawWindowFunc(w, w->p.refcon);
}

static void Shot(const std::string& name) {
    Window* w = gWindows.empty() ? nullptr : gWindows.front();
    if (!w) { std::printf("shot %s: no window\n", name.c_str()); return; }
    // The window's rectangle in pixels (y flipped: the framebuffer's row 0 is the bottom).
    int l = int(w->p.left * kScale), r = int(w->p.right * kScale), t = int(w->p.top * kScale), b = int(w->p.bottom * kScale);
    int W = r - l, H = t - b;
    std::vector<unsigned char> px(size_t(W) * H * 3);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            for (int k = 0; k < 3; k++)
                px[(size_t(y) * W + x) * 3 + k] = (unsigned char)std::clamp(int(gFb[(size_t(t - 1 - y) * kPW + l + x) * 3 + k] * 255 + .5f), 0, 255);
    std::string path = gOutDir + "/" + name + ".png";
    stbi_write_png(path.c_str(), W, H, 3, px.data(), W * 3);
    std::printf("shot %s: %dx%d px, %d triangles, %d scissors\n", path.c_str(), W, H, gTriangles, gScissors);
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: fake_xplane lin.xpl outdir [steps]\n"); return 2; }
    gPluginPath = argv[1];
    gOutDir = argv[2];
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { std::fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    auto start = (int (*)(char*, char*, char*))dlsym(lib, "XPluginStart");
    auto enable = (int (*)())dlsym(lib, "XPluginEnable");
    auto disable = (void (*)())dlsym(lib, "XPluginDisable");
    auto stop = (void (*)())dlsym(lib, "XPluginStop");
    char name[256], sig[256], desc[256];
    if (!start(name, sig, desc)) return 1;
    std::printf("plugin: %s (%s)\n", name, sig);
    enable();
    bool disabled = false;

    for (int i = 3; i < argc; i++) {
        std::string s = argv[i];
        auto arg = s.substr(s.find(':') + 1);
        if (s.rfind("menu:", 0) == 0) { Menu* m = gMenus.at(0); int n = std::stoi(arg); m->handler(m->ref, m->items.at(n).second); }
        else if (s == "show" || s == "hide" || s == "toggle") { auto& c = gCommands.at("clikstp/panel/toggle"); c.first(nullptr, xplm_CommandBegin, c.second); }
        else if (s.rfind("frames:", 0) == 0) { for (int k = std::stoi(arg); k > 0; k--) { Frame(); std::this_thread::sleep_for(std::chrono::milliseconds(16)); } }
        else if (s.rfind("sleep:", 0) == 0) std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(arg)));
        else if (s.rfind("shot:", 0) == 0) { Frame(); Shot(arg); }
        else if (s.rfind("click:", 0) == 0 || s.rfind("wheel:", 0) == 0) {
            Window* w = gWindows.at(0);
            int x, y, clicks = 0;
            std::sscanf(arg.c_str(), "%d,%d,%d", &x, &y, &clicks);
            int gx = w->p.left + x, gy = w->p.top - y;   // window-local (y down) -> global boxels (y up)
            if (s[0] == 'c') {
                w->p.handleCursorFunc(w, gx, gy, w->p.refcon);
                Frame();
                w->p.handleMouseClickFunc(w, gx, gy, xplm_MouseDown, w->p.refcon);
                Frame();
                w->p.handleMouseClickFunc(w, gx, gy, xplm_MouseUp, w->p.refcon);
                Frame();
            } else {
                w->p.handleCursorFunc(w, gx, gy, w->p.refcon);
                w->p.handleMouseWheelFunc(w, gx, gy, 0, clicks, w->p.refcon);
                Frame();
            }
            std::printf("%s at %d,%d\n", s[0] == 'c' ? "click" : "wheel", x, y);
        }
        else if (s == "disable") { disable(); disabled = true; }
        else if (s.rfind("menuname:", 0) == 0) { Menu* m = gMenus.at(0); std::printf("menu item %s: %s\n", arg.c_str(), m->items.at(std::stoi(arg)).first.c_str()); }
        else if (s == "state") {
            Window* w = gWindows.empty() ? nullptr : gWindows.front();
            std::printf("window: %s, visible %d, mode %d, geometry %d %d %d %d\n", w ? w->title.c_str() : "-", w ? w->visible : 0, w ? w->mode : -1,
                        w ? w->p.left : 0, w ? w->p.top : 0, w ? w->p.right : 0, w ? w->p.bottom : 0);
        }
    }
    if (!disabled) disable();
    stop();
    dlclose(lib);
    return 0;
}
