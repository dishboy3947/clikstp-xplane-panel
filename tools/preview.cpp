// Renders the panel to a PNG without X-Plane, so its layout can be checked anywhere:
//
//   preview <out.png> [width height] [--port N] [--demo NAME]
//
// With --port it reads a running CLIKSTP app (as the plugin does); with --demo it uses a canned state ("flying",
// "stopped", "offline", "waiting"). ImGui draws as usual and a small software rasterizer turns the draw lists into
// pixels (textured, coloured, clipped triangles: all ImGui needs).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "../src/api_client.h"
#include "../src/panel_ui.h"
#include "imgui.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using namespace clikstp;

struct Image {
    int w, h;
    std::vector<float> rgb;   // linear-ish 0..1, opaque
};

static void Blend(Image& img, int x, int y, float r, float g, float b, float a) {
    float* p = &img.rgb[(static_cast<size_t>(y) * img.w + x) * 3];
    p[0] = p[0] * (1 - a) + r * a;
    p[1] = p[1] * (1 - a) + g * a;
    p[2] = p[2] * (1 - a) + b * a;
}

static void Rasterize(Image& img, const ImDrawData* dd, const unsigned char* tex, int tw, int th) {
    for (int n = 0; n < dd->CmdListsCount; n++) {
        const ImDrawList* cl = dd->CmdLists[n];
        for (const ImDrawCmd& cmd : cl->CmdBuffer) {
            int cx0 = std::max(0, (int)cmd.ClipRect.x), cy0 = std::max(0, (int)cmd.ClipRect.y);
            int cx1 = std::min(img.w, (int)std::ceil(cmd.ClipRect.z)), cy1 = std::min(img.h, (int)std::ceil(cmd.ClipRect.w));
            for (unsigned i = 0; i + 2 < cmd.ElemCount; i += 3) {
                const ImDrawVert* v[3];
                for (int k = 0; k < 3; k++) v[k] = &cl->VtxBuffer[cmd.VtxOffset + cl->IdxBuffer[cmd.IdxOffset + i + k]];
                float x0 = v[0]->pos.x, y0 = v[0]->pos.y, x1 = v[1]->pos.x, y1 = v[1]->pos.y, x2 = v[2]->pos.x, y2 = v[2]->pos.y;
                float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
                if (std::fabs(area) < 1e-6f) continue;
                int minx = std::max(cx0, (int)std::floor(std::min({x0, x1, x2})));
                int maxx = std::min(cx1 - 1, (int)std::ceil(std::max({x0, x1, x2})));
                int miny = std::max(cy0, (int)std::floor(std::min({y0, y1, y2})));
                int maxy = std::min(cy1 - 1, (int)std::ceil(std::max({y0, y1, y2})));
                for (int y = miny; y <= maxy; y++)
                    for (int x = minx; x <= maxx; x++) {
                        float px = x + 0.5f, py = y + 0.5f;
                        float w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area;
                        float w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area;
                        float w2 = 1 - w0 - w1;
                        if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
                        float u = w0 * v[0]->uv.x + w1 * v[1]->uv.x + w2 * v[2]->uv.x;
                        float vv = w0 * v[0]->uv.y + w1 * v[1]->uv.y + w2 * v[2]->uv.y;
                        int tx = std::clamp((int)(u * tw), 0, tw - 1), ty = std::clamp((int)(vv * th), 0, th - 1);
                        const unsigned char* t = &tex[(ty * tw + tx) * 4];
                        float c[4];
                        for (int k = 0; k < 4; k++) {
                            float a = ((v[0]->col >> (8 * k)) & 255) / 255.f, b = ((v[1]->col >> (8 * k)) & 255) / 255.f,
                                  d = ((v[2]->col >> (8 * k)) & 255) / 255.f;
                            c[k] = (w0 * a + w1 * b + w2 * d) * (t[k] / 255.f);
                        }
                        if (c[3] > 0) Blend(img, x, y, c[0], c[1], c[2], c[3]);
                    }
            }
        }
    }
}

static PanelState Demo(const std::string& name) {
    PanelState s;
    s.reachable = name != "offline";
    s.running = name != "stopped";
    s.simConnected = name == "flying";
    s.simName = s.simConnected ? "X-Plane 12.4.4" : "X-Plane 12";
    s.padConnected = name != "nopad";
    if (name == "nopad") { s.simConnected = true; s.simName = "X-Plane 12.4.4"; }
    if (s.simConnected) {
        s.aircraft = "Aircraft/Laminar Research/Cessna 172 SP/Cessna_172SP_G1000.acf";
        const char* labels[] = {"COM 1", "COM 2", "NAV 1", "NAV 2", "XPDR", "Altimeter", "G1000", "Panel light",
                                "Avionics Lights", "Pedestal Lights", "Standby Lights", "Glareshield Lights"};
        for (int i = 0; i < 12; i++) s.tools.push_back({"t" + std::to_string(i), labels[i]});
        s.currentId = "t6";
        s.screen0 = "G1000";
        s.screen1 = "";
        s.keysUnit = "G1000";
        if (name == "flying") { s.currentId = "t0"; s.screen0 = "COM 1"; s.screen1 = "121.500 / 118.000"; }
    }
    return s;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: preview out.png [w h] [--port N | --demo NAME]\n"); return 2; }
    std::string out = argv[1];
    int W = 340, H = 560, port = 0;
    std::string demo = "flying";
    for (int i = 2; i < argc; i++) {
        if (!std::strcmp(argv[i], "--port") && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--demo") && i + 1 < argc) demo = argv[++i];
        else if (i + 1 < argc && std::isdigit((unsigned char)argv[i][0])) { W = std::atoi(argv[i]); H = std::atoi(argv[++i]); }
    }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2((float)W, (float)H);
    io.DeltaTime = 1 / 30.f;
    std::string fonts = std::string(CLIKSTP_FONT_DIR) + "/";
    PanelFonts f;
    f.ui = io.Fonts->AddFontFromFileTTF((fonts + "Roboto-Medium.ttf").c_str(), 17);
    f.small = io.Fonts->AddFontFromFileTTF((fonts + "Roboto-Medium.ttf").c_str(), 14);
    f.screen = io.Fonts->AddFontFromFileTTF((fonts + "Cousine-Regular.ttf").c_str(), 20);
    if (!f.ui || !f.small || !f.screen) { std::fprintf(stderr, "fonts not found in %s\n", fonts.c_str()); return 1; }
    unsigned char* tex; int tw, th;
    io.Fonts->GetTexDataAsRGBA32(&tex, &tw, &th);
    ApplyPanelStyle();

    PanelState state;
    ApiClient client;
    if (port) {
        client.Start(port);
        client.SetVisible(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        state = client.Snapshot();
    } else state = Demo(demo);

    PanelMemory memory;
    PanelActions actions;
    for (int frame = 0; frame < 4; frame++) {   // a few frames: scrolling settles after the first
        ImGui::NewFrame();
        DrawPanel(state, f, actions, memory);
        ImGui::Render();
    }
    Image img{W, H, std::vector<float>(static_cast<size_t>(W) * H * 3, 0.f)};
    Rasterize(img, ImGui::GetDrawData(), tex, tw, th);
    std::vector<unsigned char> px(static_cast<size_t>(W) * H * 3);
    for (size_t i = 0; i < px.size(); i++) px[i] = (unsigned char)std::clamp((int)(img.rgb[i] * 255 + 0.5f), 0, 255);
    stbi_write_png(out.c_str(), W, H, 3, px.data(), W * 3);
    if (port) client.Stop();
    ImGui::DestroyContext();
    std::printf("wrote %s (%dx%d, %s)\n", out.c_str(), W, H, port ? "live" : demo.c_str());
    return 0;
}
