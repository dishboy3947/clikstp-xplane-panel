# CLIKSTP Panel for X-Plane 12

A native X-Plane window showing the CLIKSTP pad, like the toolbar panel in MSFS:

- the pad's screen (the selected tool and its readout, or "Awaiting sim connection"),
- App and Pad lights, and which avionics unit the 16 keys drive,
- what needs attention (the app isn't running, it's stopped: with a **Start** button, the pad isn't plugged in),
- the tools the big encoder visits in this aircraft, in a list that scrolls under the fixed top and keeps the selected
  tool in the middle, so the next and previous ones show. Click one to select it.

It's an ordinary X-Plane window: drag it, resize it, close it with its × button, **pop it out** into its own window
(another monitor), and in **VR** it goes into the headset with the other windows.

The plugin only shows and selects: the CLIKSTP app drives X-Plane (through X-Plane's web API) and the pad. The panel
talks to the app on this computer at http://127.0.0.1:8780, the same API the CLIKSTP page uses.

## Install

1. Copy the `CLIKSTP` folder (from a build, below, or a release zip) into `X-Plane 12/Resources/plugins/`:

   ```
   X-Plane 12/Resources/plugins/CLIKSTP/
     64/win.xpl          (Windows; mac.xpl on a Mac, lin.xpl on Linux)
     fonts/Roboto-Medium.ttf, fonts/Cousine-Regular.ttf
     panel.ini.example
   ```

2. Start the CLIKSTP app (it lives in the tray) and X-Plane, in either order.
3. **Plugins > CLIKSTP > Show panel**, or bind the command **clikstp/panel/toggle** to a key or a joystick button
   (Settings > Keyboard / Joystick, search "CLIKSTP").

The panel reopens where you left it next time (X-Plane's `Output/preferences/CLIKSTP_panel.prf`). If you changed the
app's web port (settings.json, `web.port`), copy `panel.ini.example` to `panel.ini` and set `port=` to match.

Menu: **Show panel** (ticked while open) · **Pop out panel** / **Put panel back in the sim** · **Open the CLIKSTP page**
(the full page in your browser: editor, Explorer, learn mode). Its log lines go to X-Plane's `Log.txt`, prefixed
"CLIKSTP Panel:".

## Build

Everything it needs is in `third_party/` (Dear ImGui 1.91.9b, nlohmann/json 3.11.3, the X-Plane SDK 4.3 headers and
link libraries, stb_image_write for the preview tool), so it builds offline.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The plugin folder is then `build/CLIKSTP/`.

With the Ninja generator (what Visual Studio's CMake and "Open Folder" use), the build type is chosen when the build
folder is configured: `--config Release` on the build line is ignored there. A build folder configured without
`-DCMAKE_BUILD_TYPE=Release` builds Debug (`-O0 -g -D_DEBUG`, linked to the debug runtime); delete it and configure
again with the line above.

- **Windows:** Visual Studio 2022 with "Desktop development with C++" (CMake comes with it), x64. Or cross-compile
  from Linux / macOS with MinGW-w64: `cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release`.
  The MinGW build links its runtime statically: one file, no extra DLLs.
- **macOS:** Xcode command-line tools and CMake (`brew install cmake`); builds a universal (Apple Silicon + Intel)
  `mac.xpl` linked against the SDK's XPLM framework. Not yet built or tried (no Mac here).
- **Linux:** gcc or clang and the OpenGL headers (`libgl-dev`).

## How it's made

| File | |
|---|---|
| `src/plugin.cpp` | The plugin: menu, command, settings, saving the window's place, VR in and out. |
| `src/imgui_xplm.{h,cpp}` | Dear ImGui in an XPLM window: X-Plane's frame and buttons, mouse and wheel in, ImGui's triangles drawn with X-Plane's GL state. Clipping goes through X-Plane's published matrices (`sim/graphics/view/{modelview,projection}_matrix`, `viewport`), so it's right at any UI scale, popped out, or in VR, and the fonts are rasterized at the real pixel density so text stays sharp. The panel never takes keyboard focus: keys keep flying the aircraft. |
| `src/api_client.{h,cpp}` | Reads `GET /api/status` on its own thread (4 times a second while the panel is open, every 2 s while it's closed) and sends the clicks (`POST /api/tools/{id}/select`, `POST /api/app/start`). X-Plane's thread only copies the latest state: it never waits on the network. |
| `src/http.{h,cpp}` | A tiny HTTP/1.0 client for 127.0.0.1 (no libraries). |
| `src/panel_ui.{h,cpp}` | The panel itself, drawn from that state, in the CLIKSTP page's colours. No X-Plane in it. |
| `tools/preview.cpp` | Renders the panel to a PNG without X-Plane (a small software rasterizer): `cmake --build build --target clikstp_panel_preview`, then `clikstp_panel_preview out.png --demo flying` or `--port 8780` for the live app. |
| `tools/fake_xplane.cpp` | A pretend X-Plane for Linux: loads the built `lin.xpl`, provides the XPLM and GL calls it makes, opens the panel from the menu, clicks and scrolls it, and writes screenshots (at a 1.5x UI scale, to exercise the clipping maths). How the plugin was tested before it met the real sim. |

## Not yet

- Tried in the real X-Plane (built and exercised against `fake_xplane` with the real CLIKSTP app only).
- A Mac build (`mac.xpl`).
- Encoder buttons on the panel (left out on purpose, like the MSFS panel: the pad's knobs do that).
