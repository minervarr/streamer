# gui/ — the new cross-platform desktop GUI (Vulkan/vk_canvas), replacing
# streamer-gui/ (already removed) now that the port is complete. Linux/Wayland
# and Windows share this same target; the platform skin lives in os/.
#
# Skeleton + skins: gui_main.cc is the portable app (controllers, frame loop,
# drawing); the platform host behind src/host.hh is picked per OS below.

# Anchored on this directory, not CMAKE_SOURCE_DIR: the Android build's top
# level is android/CMakeLists.txt, which add_subdirectory()s the repo root, so
# CMAKE_SOURCE_DIR points at android/ there and every asset path built from it
# silently resolves to a file that does not exist.
set(STREAMER_ROOT   ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(VK_CANVAS_ROOT  ${STREAMER_ROOT}/framework/Vk_Canvas_Lb_LAW)
set(APP_SHELL_ROOT  ${STREAMER_ROOT}/framework/app_shell)

add_subdirectory(${VK_CANVAS_ROOT}/core ${CMAKE_BINARY_DIR}/vk_canvas_core)

# ── app_shell: the application shell ────────────────────────────────────────
#
# vk_canvas makes DRAWING portable; this makes the APPLICATION portable — the
# window, the pump, the input, where files live, how big the text is, which way
# round the screen is. streamer used to carry its own copy of all of that in
# src/host.hh + src/os/ (now gui/src/legacy_host/, out of the build); this is
# the same seam, shared with Matrix Player and better tested on a phone.
#
# It looks for vk_canvas at ../vk_canvas, which is Matrix's directory name.
# Ours is Vk_Canvas_Lb_LAW, so the cache variable is set here rather than the
# submodule being renamed — renaming would touch four CMakeLists and android/.
set(VK_CANVAS_DIR ${VK_CANVAS_ROOT} CACHE PATH "" FORCE)

# The names app_shell needs from its consumer. APP_SHELL_APP_NAME is a bare
# identifier (log file, logcat tag); WINDOW_TITLE and APP_ID are what the user
# and the compositor see.
set(APP_SHELL_APP_NAME     "streamer"        CACHE STRING "" FORCE)
set(APP_SHELL_WIN_CLASS    "StreamerMain"    CACHE STRING "" FORCE)
set(APP_SHELL_WINDOW_TITLE "streamer"        CACHE STRING "" FORCE)
set(APP_SHELL_APP_ID       "io.nava.streamer" CACHE STRING "" FORCE)
if(ANDROID)
    # native_app_glue ships as SOURCE inside the NDK, so only we know where it
    # is; app_shell hard-fails without it rather than guessing.
    set(APP_SHELL_NATIVE_APP_GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue
        CACHE PATH "" FORCE)
endif()

add_subdirectory(${APP_SHELL_ROOT} ${CMAKE_BINARY_DIR}/app_shell_build)

# ── The text-size floor, generated from OUR fonts ───────────────────────────
#
# app_shell's ui_metrics pins its smallest type role to a legibility floor
# derived from the shipped faces' own outline geometry, so the number is a
# property of THIS application's fonts and cannot be a library constant. The
# recipe is app_shell's; the list is ours, and it must be exactly what
# gui/src/fonts.cc opens — the emitted floor is the WORST case across them, so
# a face left out here could end up rendering below it.
#
# Cannot run under a cross-compiler (it builds and runs a host tool), so the
# Android build copies the result out of a desktop build tree instead — see
# android/CMakeLists.txt.
if(NOT ANDROID)
    include(AppShellMinTextSize)
    app_shell_generate_min_text_size(
        ${STREAMER_ROOT}/assets/fonts/newcomputermodern/NewCM10-Book.otf
        ${STREAMER_ROOT}/assets/fonts/newcomputermodern/NewCM10-Bold.otf
        ${STREAMER_ROOT}/assets/fonts/newcomputermodern/NewCM10-BookItalic.otf
        ${STREAMER_ROOT}/assets/fonts/fandol/FandolSong-Regular.otf
        ${STREAMER_ROOT}/assets/fonts/fandol/FandolSong-Bold.otf
        ${STREAMER_ROOT}/assets/fonts/haranoaji/HaranoAjiMincho-Regular.otf
        ${STREAMER_ROOT}/assets/fonts/haranoaji/HaranoAjiMincho-Bold.otf
        ${STREAMER_ROOT}/assets/fonts/unfonts-core/UnBatang.ttf
        ${STREAMER_ROOT}/assets/fonts/unfonts-core/UnBatangBold.ttf)
endif()

# Shaders next to the exe: FileAssetReader reads <exe dir>/assets/. Same split
# as every consumer of this engine: shared shaders come from the font
# engine's shaders_src, vk_canvas's own from its shaders_src.
include(${VK_CANVAS_ROOT}/cmake/VceShaders.cmake)

# Where the app's assets are read from at runtime, and therefore where they
# must be written at build time. Desktop: next to the exe, because
# FileAssetReader reads <exe dir>/assets/. Android: into the Gradle module's
# source tree, because AndroidAssetReader reads them out of the APK and AGP
# only packages what is under src/main/assets at assemble time.
if(ANDROID)
    set(GUI_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../android/app/src/main/assets)
else()
    set(GUI_ASSETS_DIR ${CMAKE_CURRENT_BINARY_DIR}/assets)
endif()

vce_compile_slang(gui_shared_shaders
    ${GUI_ASSETS_DIR}/shaders
    ${VK_CANVAS_ROOT}/first_party/vulkan_font_engine/shaders_src
    composite_vert
    composite_frag
    tiling
    coverage
    msdf_vert
    msdf_frag
)
vce_compile_slang(gui_shaders
    ${GUI_ASSETS_DIR}/shaders
    ${VK_CANVAS_ROOT}/shaders_src
    overlay_vert
    overlay_frag
    image_vert
    image_frag
    shape_vert
    shape_frag
)

# The Host implementations themselves now come from app_shell (one library per
# platform, linked below). What stays here is the vk_canvas SUBSTRATE each of
# them is built on, which app_shell deliberately does not compile: it includes
# those headers but leaves the translation units to the consumer, so a build
# tree has exactly one copy of them however many things use them.
if(ANDROID)
    # native_app_glue turns NativeActivity's callback-driven lifecycle into the
    # android_main() + ALooper event queue app_shell's AndroidHost pumps. It
    # ships as source with the NDK, not as a prebuilt library.
    set(NATIVE_APP_GLUE ${ANDROID_NDK}/sources/android/native_app_glue)
    set(_gui_host_sources
        # android_main() — the entry point NativeActivity starts. It is
        # streamer's rather than app_shell's because the android_app* it is
        # handed is the ingredient AndroidHost needs, and because everything
        # else it does (where the library goes, the CA bundle, the language)
        # is application knowledge.
        src/os/android_main.cc
        ${VK_CANVAS_ROOT}/platform/android/android_platform.cc
        ${NATIVE_APP_GLUE}/android_native_app_glue.c
    )
elseif(WIN32)
    set(_gui_host_sources
        ${VK_CANVAS_ROOT}/platform/windows/win32_platform.cc
    )
else()
    # The engine's raw-Wayland backend library (xdg-shell glue generated by
    # wayland-scanner inside; links wayland-client/wayland-cursor/xkbcommon).
    add_subdirectory(${VK_CANVAS_ROOT}/platform/linux ${CMAKE_BINARY_DIR}/vk_canvas_wayland)
    set(_gui_host_sources)
endif()

# FileAssetReader reads <exe dir>/assets/, and the shaders above compile into
# ${CMAKE_CURRENT_BINARY_DIR}/assets — so the exe must land there too. The
# root CMakeLists.txt pins CMAKE_RUNTIME_OUTPUT_DIRECTORY(_<CONFIG>) to the
# top build dir for the `streamer` CLI; override all config variants locally
# so a single-config generator (Ninja) with CMAKE_BUILD_TYPE set doesn't
# prefer the top-level default over a plain RUNTIME_OUTPUT_DIRECTORY property.
foreach(_cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY${_cfg} ${CMAKE_CURRENT_BINARY_DIR})
endforeach()

# WIN32: without it, win32_host.cc's own window still opens fine, but the
# console subsystem also allocates a visible console window behind it (a
# plain `int main()` needs no WinMain change on MinGW/Clang — the toolchain's
# own CRT startup already dispatches WinMain into main()). No-op on Linux.
set(_gui_sources
    src/gui_main.cc
    src/streamer_app.cc
    src/fonts.cc
    src/library_controller.cc
    src/library_view.cc
    src/query_dsl.cc
    src/search_controller.cc
    src/settings_controller.cc
    src/views.cc
    ${_gui_host_sources}
)
if(ANDROID)
    # An Android app has no executable of its own: the process is started by
    # the system and NativeActivity dlopen()s this .so, whose name must match
    # android.app.lib_name in AndroidManifest.xml.
    add_library(streamer_gui SHARED ${_gui_sources})
else()
    add_executable(streamer_gui WIN32 ${_gui_sources})
endif()
add_dependencies(streamer_gui gui_shared_shaders gui_shaders)

target_link_libraries(streamer_gui PRIVATE vk_canvas_core streamer_core app_shell)

# src/ on the include path, not just implicitly via each file's own directory:
# src/os/android_main.cc includes streamer_app.hh, which is a directory up.
target_include_directories(streamer_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# app_shell's ui_metrics.cc includes the header generated below, so it has to
# wait for it. Stated here and not in app_shell's own CMakeLists because the
# target does not exist yet when CMake reads that file — the consumer owns the
# font list, so the consumer owns the ordering.
if(NOT ANDROID)
    add_dependencies(app_shell generate_ui_min_text_size)
endif()

if(ANDROID)
    target_include_directories(streamer_gui PRIVATE
        ${VK_CANVAS_ROOT}/platform/android
        ${NATIVE_APP_GLUE}
    )
    target_link_libraries(streamer_gui PRIVATE app_shell_android android log vulkan)
    # ANativeActivity_onCreate lives in native_app_glue and is called by the
    # system through dlsym, so nothing in our code references it. Without this
    # the linker garbage-collects it and the app dies at launch with a black
    # screen and "Unable to find native entry point" — no build-time hint.
    target_link_options(streamer_gui PRIVATE -u ANativeActivity_onCreate)
elseif(WIN32)
    target_include_directories(streamer_gui PRIVATE ${VK_CANVAS_ROOT}/platform/windows)
    target_link_libraries(streamer_gui PRIVATE app_shell_win32 user32 ole32 shell32)
    if(MSVC)
        target_compile_options(streamer_gui PRIVATE /utf-8)
    endif()
else()
    target_link_libraries(streamer_gui PRIVATE app_shell_wayland vk_canvas_wayland)
endif()

# The CA bundle, Android only. libcurl there is built with CURL_CA_BUNDLE=none
# (no system trust store is readable by an app), so without this every HTTPS
# request fails certificate verification and nothing — login, search, download
# — works. Reused from KawusapiCC rather than added again, so there is one
# bundle to keep current, not two.
if(ANDROID)
    add_custom_command(TARGET streamer_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${STREAMER_ROOT}/first_party/KawusapiCC/platform/android/kobuzapi/src/main/assets/cacert.pem
            ${GUI_ASSETS_DIR}/cacert.pem
    )
endif()

# Fonts alongside the shaders, wherever those went (see GUI_ASSETS_DIR).
add_custom_command(TARGET streamer_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${GUI_ASSETS_DIR}/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${STREAMER_ROOT}/assets/fonts/newcomputermodern/NewCM10-Book.otf
        ${STREAMER_ROOT}/assets/fonts/newcomputermodern/NewCM10-Bold.otf
        ${STREAMER_ROOT}/assets/fonts/newcomputermodern/NewCM10-BookItalic.otf
        ${STREAMER_ROOT}/assets/fonts/fandol/FandolSong-Regular.otf
        ${STREAMER_ROOT}/assets/fonts/fandol/FandolSong-Bold.otf
        ${STREAMER_ROOT}/assets/fonts/haranoaji/HaranoAjiMincho-Regular.otf
        ${STREAMER_ROOT}/assets/fonts/haranoaji/HaranoAjiMincho-Bold.otf
        ${STREAMER_ROOT}/assets/fonts/unfonts-core/UnBatang.ttf
        ${STREAMER_ROOT}/assets/fonts/unfonts-core/UnBatangBold.ttf
        ${GUI_ASSETS_DIR}/fonts/
)

# streamer_gui_capture — headless, native-resolution UI screenshot tool (dev
# tooling, see gui/tools/capture/main.cc). Debug-only, precedent set by
# Matrix_Player's matrix_ui_capture (framework/Vk_Canvas_Lb_LAW/CLAUDE.md's
# "gate the consumer's capture target on a Debug build"). Linux-only for now
# — needs FileAssetReader from vk_canvas_wayland, no window/compositor
# required otherwise.
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT WIN32 AND NOT ANDROID)
    # gui_main.cc and streamer_app.cc are HERE on purpose: this tool drives the
    # real application rather than replicating it, so it compiles the real
    # frame loop and the real AppView. That is the property that makes a
    # capture evidence — the old tool rebuilt the layout by hand and drifted.
    add_executable(streamer_gui_capture
        tools/capture/main.cc
        src/gui_main.cc
        src/streamer_app.cc
        src/fonts.cc
        src/library_controller.cc
        src/library_view.cc
        src/query_dsl.cc
        src/search_controller.cc
        src/settings_controller.cc
        src/views.cc
    )
    add_dependencies(streamer_gui_capture gui_shared_shaders gui_shaders)
    target_include_directories(streamer_gui_capture PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${VK_CANVAS_ROOT}/core/capture
    )
    # app_shell but NOT app_shell_wayland: the portable half supplies Host,
    # AppView and FrameInputView, while the Wayland half also brings a main()
    # of its own, which would collide with this tool's. vk_canvas_wayland stays
    # for FileAssetReader — no compositor is contacted, only the filesystem.
    target_link_libraries(streamer_gui_capture PRIVATE
        vk_canvas_core vk_canvas_wayland streamer_core app_shell
    )
    add_dependencies(streamer_gui_capture generate_ui_min_text_size)

    add_custom_command(TARGET streamer_gui_capture POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            ${CMAKE_CURRENT_BINARY_DIR}/assets/fonts
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${STREAMER_ROOT}/assets/fonts/newcomputermodern/NewCM10-Book.otf
            ${STREAMER_ROOT}/assets/fonts/newcomputermodern/NewCM10-Bold.otf
            ${STREAMER_ROOT}/assets/fonts/newcomputermodern/NewCM10-BookItalic.otf
            # The CJK chain too. Without it a capture cannot show Chinese,
            # Japanese or Korean text no matter how correct the code is —
            # which makes the capture tool useless for exactly the bug it is
            # best placed to verify.
            ${STREAMER_ROOT}/assets/fonts/fandol/FandolSong-Regular.otf
            ${STREAMER_ROOT}/assets/fonts/fandol/FandolSong-Bold.otf
            ${STREAMER_ROOT}/assets/fonts/haranoaji/HaranoAjiMincho-Regular.otf
            ${STREAMER_ROOT}/assets/fonts/haranoaji/HaranoAjiMincho-Bold.otf
            ${STREAMER_ROOT}/assets/fonts/unfonts-core/UnBatang.ttf
            ${STREAMER_ROOT}/assets/fonts/unfonts-core/UnBatangBold.ttf
            ${CMAKE_CURRENT_BINARY_DIR}/assets/fonts/
    )
endif()
