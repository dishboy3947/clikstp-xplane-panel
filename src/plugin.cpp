// CLIKSTP Panel: an X-Plane 12 plugin showing the CLIKSTP pad (its screen, connections and tools) in a native window
// inside the sim, like the toolbar panel in MSFS. It talks to the CLIKSTP app over its local web API
// (http://127.0.0.1:8780 by default); X-Plane itself is driven by the app, not by this plugin.
//
//   Plugins > CLIKSTP > Show panel / Pop out panel / Open the CLIKSTP page
//   Command clikstp/panel/toggle (bind it to a key or a joystick button)
//
// Settings: <plugin folder>/panel.ini, "port=8780" (only if the app's web.port was changed).
// The window's place and whether it was open are kept in X-Plane's Output/preferences/CLIKSTP_panel.prf.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "XPLMDataAccess.h"
#include "XPLMDefs.h"
#include "XPLMDisplay.h"
#include "XPLMMenus.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#include "api_client.h"
#include "imgui_xplm.h"
#include "panel_ui.h"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shellapi.h>
#endif

using namespace clikstp;

namespace {

const char* kVersion = "0.1.0";

std::string gPluginDir;      // .../Resources/plugins/CLIKSTP/
std::string gPrefsFile;
int gPort = 8780;

ApiClient gClient;
std::unique_ptr<XplmImGuiWindow> gWindow;
PanelMemory gMemory;

XPLMMenuID gMenu = nullptr;
int gMenuShow = -1, gMenuPopOut = -1, gMenuPage = -1;
XPLMCommandRef gToggleCommand = nullptr;
XPLMDataRef gVrEnabled = nullptr;
XPLMFlightLoopID gLoop = nullptr;

// Saved between sessions.
struct Prefs {
    bool visible = false;
    int left = 0, top = 0, right = 0, bottom = 0;   // 0 width = never placed: put it somewhere sensible
} gPrefs;

void Log(const std::string& text) { XPLMDebugString(("CLIKSTP Panel: " + text + "\n").c_str()); }

std::string DirOf(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

void ReadSettings() {
    std::ifstream in(gPluginDir + "panel.ini");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("port=", 0) == 0) {
            int p = std::atoi(line.c_str() + 5);
            if (p > 0 && p < 65536) gPort = p;
        }
    }
}

void LoadPrefs() {
    std::ifstream in(gPrefsFile);
    std::string key;
    int value;
    while (in >> key >> value) {
        if (key == "visible") gPrefs.visible = value != 0;
        else if (key == "left") gPrefs.left = value;
        else if (key == "top") gPrefs.top = value;
        else if (key == "right") gPrefs.right = value;
        else if (key == "bottom") gPrefs.bottom = value;
    }
}

void SavePrefs() {
    if (gWindow) {
        gPrefs.visible = gWindow->IsVisible();
        // Only a floating window's place makes sense next time (a popped-out one is in OS pixels, a VR one nowhere).
        if (!gWindow->IsPoppedOut() && !gWindow->IsInVR())
            gWindow->GetGeometry(gPrefs.left, gPrefs.top, gPrefs.right, gPrefs.bottom);
    }
    std::ofstream out(gPrefsFile);
    out << "visible " << (gPrefs.visible ? 1 : 0) << "\nleft " << gPrefs.left << "\ntop " << gPrefs.top << "\nright "
        << gPrefs.right << "\nbottom " << gPrefs.bottom << "\n";
}

void OpenPage() {
    std::string url = "http://127.0.0.1:" + std::to_string(gPort) + "/";
#if defined(_WIN32)
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    if (std::system(("open '" + url + "' &").c_str()) != 0) Log("couldn't open " + url);
#else
    if (std::system(("xdg-open '" + url + "' >/dev/null 2>&1 &").c_str()) != 0) Log("couldn't open " + url);
#endif
}

bool InVR() { return gVrEnabled && XPLMGetDatai(gVrEnabled) != 0; }

// The window is made the first time it's shown (X-Plane's drawing context is certainly there by then).
void EnsureWindow() {
    if (gWindow) return;
    int l, t, r, b;
    if (gPrefs.right - gPrefs.left >= 200 && gPrefs.top - gPrefs.bottom >= 200) {
        l = gPrefs.left; t = gPrefs.top; r = gPrefs.right; b = gPrefs.bottom;
    } else {
        int sl, st, sr, sb;
        XPLMGetScreenBoundsGlobal(&sl, &st, &sr, &sb);
        r = sr - 60; l = r - 340; t = st - 120; b = t - 560;   // top right, clear of X-Plane's menu bar
    }
    gWindow = std::make_unique<XplmImGuiWindow>("CLIKSTP", l, t, r, b, gPluginDir + "fonts");
    gWindow->onBuild = [] {
        static bool styled = false;
        if (!styled) { ApplyPanelStyle(); styled = true; }
        PanelState state = gClient.Snapshot();
        const auto& wf = gWindow->GetFonts();
        PanelFonts fonts{wf.ui, wf.screen, wf.small};
        PanelActions actions;
        actions.selectTool = [](const std::string& id) { gClient.SelectTool(id); };
        actions.startApp = [] { gClient.StartApp(); };
        actions.openPage = OpenPage;
        DrawPanel(state, fonts, actions, gMemory);
    };
}

void Show(bool show) {
    if (show) {
        EnsureWindow();
        gWindow->SetVisible(true);
        if (InVR()) gWindow->MoveToVR();
        gWindow->BringToFront();
    } else if (gWindow) {
        gWindow->SetVisible(false);
    }
    gClient.SetVisible(show);
}

void Toggle() { Show(!(gWindow && gWindow->IsVisible())); }

void MenuHandler(void*, void* item) {
    intptr_t which = reinterpret_cast<intptr_t>(item);
    if (which == 1) Toggle();
    else if (which == 2) {
        Show(true);
        if (gWindow->IsPoppedOut()) gWindow->FloatInSim(); else gWindow->PopOut();
    } else if (which == 3) OpenPage();
}

int ToggleCommand(XPLMCommandRef, XPLMCommandPhase phase, void*) {
    if (phase == xplm_CommandBegin) Toggle();
    return 0;   // handled
}

// Twice a second: keep the menu's tick and the client's pace in step with the window (its own close button hides
// it without telling us).
float Tick(float, float, int, void*) {
    bool visible = gWindow && gWindow->IsVisible();
    gClient.SetVisible(visible);
    if (gMenu) {
        XPLMCheckMenuItem(gMenu, gMenuShow, visible ? xplm_Menu_Checked : xplm_Menu_Unchecked);
        XPLMSetMenuItemName(gMenu, gMenuPopOut, gWindow && gWindow->IsPoppedOut() ? "Put panel back in the sim" : "Pop out panel", 0);
    }
    return 0.5f;
}

}  // namespace

PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc) {
    std::snprintf(outName, 256, "CLIKSTP Panel %s", kVersion);
    std::strcpy(outSig, "com.clikstp.xplane-panel");
    std::strcpy(outDesc, "The CLIKSTP pad's screen and tools in an X-Plane window. Needs the CLIKSTP app running.");

    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);
    char path[1024] = {0};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, path, nullptr, nullptr);
    // .../plugins/CLIKSTP/64/win.xpl -> .../plugins/CLIKSTP/
    std::string dir = DirOf(path);
    if (dir.size() > 1) dir = DirOf(dir.substr(0, dir.size() - 1));
    gPluginDir = dir;
    char prefs[1024] = {0};
    XPLMGetPrefsPath(prefs);
    gPrefsFile = DirOf(prefs) + "CLIKSTP_panel.prf";

    ReadSettings();
    LoadPrefs();
    gVrEnabled = XPLMFindDataRef("sim/graphics/VR/enabled");

    int item = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "CLIKSTP", nullptr, 0);
    gMenu = XPLMCreateMenu("CLIKSTP", XPLMFindPluginsMenu(), item, MenuHandler, nullptr);
    gMenuShow = XPLMAppendMenuItem(gMenu, "Show panel", reinterpret_cast<void*>(1), 0);
    gMenuPopOut = XPLMAppendMenuItem(gMenu, "Pop out panel", reinterpret_cast<void*>(2), 0);
    gMenuPage = XPLMAppendMenuItem(gMenu, "Open the CLIKSTP page", reinterpret_cast<void*>(3), 0);

    gToggleCommand = XPLMCreateCommand("clikstp/panel/toggle", "Show or hide the CLIKSTP panel");
    XPLMRegisterCommandHandler(gToggleCommand, ToggleCommand, 1, nullptr);

    Log(std::string("started, version ") + kVersion + ", app port " + std::to_string(gPort) + ", folder " + gPluginDir);
    return 1;
}

PLUGIN_API void XPluginStop() {
    XPLMUnregisterCommandHandler(gToggleCommand, ToggleCommand, 1, nullptr);
    if (gMenu) XPLMDestroyMenu(gMenu);
    gMenu = nullptr;
}

PLUGIN_API int XPluginEnable() {
    gClient.Start(gPort);
    XPLMCreateFlightLoop_t loop{sizeof(XPLMCreateFlightLoop_t), xplm_FlightLoop_Phase_AfterFlightModel, Tick, nullptr};
    gLoop = XPLMCreateFlightLoop(&loop);
    XPLMScheduleFlightLoop(gLoop, 1.0f, 1);
    if (gPrefs.visible) Show(true);   // open again where it was
    return 1;
}

PLUGIN_API void XPluginDisable() {
    SavePrefs();
    if (gLoop) XPLMDestroyFlightLoop(gLoop);
    gLoop = nullptr;
    gWindow.reset();
    gClient.Stop();
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int message, void*) {
#if defined(XPLM301)
    if (!gWindow || !gWindow->IsVisible()) return;
    if (message == XPLM_MSG_ENTERED_VR) gWindow->MoveToVR();
    else if (message == XPLM_MSG_EXITING_VR) gWindow->FloatInSim();
#else
    (void)message;
#endif
}
