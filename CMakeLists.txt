# CLIKSTP Panel: the X-Plane 12 plugin (C++ / Dear ImGui). Everything it needs is in third_party/, so it builds
# offline on any of the three platforms:
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release
#
# Windows: Visual Studio 2022 (x64), or MinGW-w64 (see README). macOS: Xcode command-line tools (universal
# arm64 + x86_64). Linux: gcc/clang and the OpenGL headers (libgl-dev).
#
# The result is build/CLIKSTP/: copy that folder into X-Plane 12/Resources/plugins/.
cmake_minimum_required(VERSION 3.16)
project(clikstp_xplane_panel VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" FORCE)
endif()

set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/third_party/xplane-sdk)
set(IMGUI ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
set(OUT ${CMAKE_BINARY_DIR}/CLIKSTP)

# --- The parts that don't need X-Plane (also used by the preview tool) ---
add_library(panel_core STATIC
  src/api_client.cpp src/http.cpp src/panel_ui.cpp
  ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_widgets.cpp ${IMGUI}/imgui_tables.cpp)
target_include_directories(panel_core PUBLIC ${IMGUI} ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
# No IMGUI_DISABLE_OBSOLETE_*: keep the classic font-atlas path the plugin's renderer uses.
find_package(Threads REQUIRED)
target_link_libraries(panel_core PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(panel_core PUBLIC ws2_32)
endif()

# --- The plugin: CLIKSTP/64/{win,mac,lin}.xpl ---
add_library(clikstp_panel MODULE src/plugin.cpp src/imgui_xplm.cpp)
target_link_libraries(clikstp_panel PRIVATE panel_core)
target_include_directories(clikstp_panel PRIVATE ${SDK}/CHeaders/XPLM)
target_compile_definitions(clikstp_panel PRIVATE XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM302=1 XPLM303=1 XPLM400=1)
set_target_properties(clikstp_panel PROPERTIES PREFIX "" SUFFIX ".xpl"
  LIBRARY_OUTPUT_DIRECTORY ${OUT}/64 LIBRARY_OUTPUT_DIRECTORY_RELEASE ${OUT}/64 LIBRARY_OUTPUT_DIRECTORY_DEBUG ${OUT}/64
  RUNTIME_OUTPUT_DIRECTORY ${OUT}/64 RUNTIME_OUTPUT_DIRECTORY_RELEASE ${OUT}/64 RUNTIME_OUTPUT_DIRECTORY_DEBUG ${OUT}/64)

if(WIN32)
  # NOMINMAX: windows.h's min/max macros. _CRT_SECURE_NO_WARNINGS: the SDK's XPluginStart fills its name buffers
  # with strcpy, which MSVC's headers flag as deprecated.
  target_compile_definitions(clikstp_panel PRIVATE IBM=1 APL=0 LIN=0 NOMINMAX _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(clikstp_panel PRIVATE ${SDK}/Libraries/Win/XPLM_64.lib opengl32 shell32)
  set_target_properties(clikstp_panel PROPERTIES OUTPUT_NAME win)
  if(MINGW)
    # One file, no MinGW runtime DLLs to ship next to it.
    target_link_options(clikstp_panel PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
elseif(APPLE)
  target_compile_definitions(clikstp_panel PRIVATE IBM=0 APL=1 LIN=0 GL_SILENCE_DEPRECATION)
  target_link_options(clikstp_panel PRIVATE -F${SDK}/Libraries/Mac -framework XPLM -framework OpenGL)
  set_target_properties(clikstp_panel PROPERTIES OUTPUT_NAME mac)
else()
  target_compile_definitions(clikstp_panel PRIVATE IBM=0 APL=0 LIN=1)
  # X-Plane provides the XPLM and OpenGL symbols when it loads the plugin; export only the XPlugin* entry points.
  target_link_options(clikstp_panel PRIVATE -static-libgcc -static-libstdc++ -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/exports.map)
  set_target_properties(clikstp_panel PROPERTIES OUTPUT_NAME lin)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT APPLE)
  target_link_options(clikstp_panel PRIVATE $<$<CONFIG:Release>:-s>)   # strip: a third of the size
endif()

# Fonts and a settings example next to the plugin.
add_custom_command(TARGET clikstp_panel POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${OUT}/fonts
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Roboto-Medium.ttf ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Cousine-Regular.ttf ${OUT}/fonts
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/fonts/LICENSE.txt ${OUT}/fonts
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/panel.ini.example ${OUT})

# --- Preview: renders the panel to a PNG without X-Plane (not built by default) ---
add_executable(clikstp_panel_preview EXCLUDE_FROM_ALL tools/preview.cpp)
target_link_libraries(clikstp_panel_preview PRIVATE panel_core)
target_include_directories(clikstp_panel_preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
target_compile_definitions(clikstp_panel_preview PRIVATE CLIKSTP_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fonts")
