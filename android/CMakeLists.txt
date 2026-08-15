cmake_minimum_required(VERSION 3.22.1)
project(streamer_android LANGUAGES C CXX)

# The Android build is the *same* project, not a fork of it. The repo root
# already knows what to leave out here — its `if(NOT ANDROID)` blocks skip the
# vendored curl/TagLib (ae_net and ae_tag cross-compile their own, with mbedTLS
# for the missing NDK TLS) and the CLI binary (no argv, no terminal) — and
# gui/CMakeLists.txt builds libstreamer_gui.so instead of an executable.
#
# So there is one source of truth for how streamer is built, and Android is a
# branch inside it rather than a parallel description that drifts.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_BINARY_DIR}/streamer)
