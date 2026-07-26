#!/usr/bin/env bash
# streamer desktop build (Linux). Builds the CLI always, and the
# Vulkan/Wayland GUI (gui/) when a slangc shader compiler is found.
#
# Dependencies (Arch: pacman / Debian: apt names in parens):
#   cmake g++ ninja curl                        — toolchain (curl for
#                                                  CMake FetchContent)
#   openssl                                      — CLI/GUI networking
#   wayland wayland-protocols libxkbcommon       — GUI only
#     (libwayland-dev wayland-protocols libxkbcommon-dev)
#   vulkan-icd-loader vulkan-headers             — GUI only
#     (libvulkan-dev)
#   slangc — Slang shader compiler, GUI only: a Vulkan SDK install, or a
#   standalone release from https://github.com/shader-slang/slang/releases
#   extracted anywhere and pointed to via -DVCE_SLANGC=/path/to/slangc or
#   $VULKAN_SDK/bin/slangc.
#
# First time: git submodule update --init --recursive
set -e

root="$(cd "$(dirname "$0")/../.." && pwd)"
build="$root/build"

# slangc autodetect: env override > VULKAN_SDK > PATH.
slangc="${VCE_SLANGC:-}"
if [ -z "$slangc" ]; then
    for c in "${VULKAN_SDK:+$VULKAN_SDK/bin/slangc}" "$(command -v slangc || true)"; do
        if [ -n "$c" ] && [ -x "$c" ]; then slangc="$c"; break; fi
    done
fi

extra=()
if [ -n "$slangc" ]; then
    extra+=("-DVCE_SLANGC=$slangc" "-DSTREAMER_GUI=ON")
    echo "==> slangc: $slangc (GUI enabled)"
else
    echo "==> slangc not found — building without the GUI"
fi

cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release "${extra[@]}"
cmake --build "$build" -j"$(nproc)"

echo
echo "Done: $build/streamer"
if [ -n "$slangc" ]; then
    echo "GUI:  $build/gui/streamer_gui"
fi
