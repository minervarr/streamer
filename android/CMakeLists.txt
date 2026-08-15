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
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ── ui_min_text_size.gen.h, borrowed from a desktop build ───────────────────
#
# app_shell's ui_metrics.hh includes this header, and its recipe builds and
# RUNS a host tool to measure the shipped fonts' outline geometry. Under the
# NDK toolchain that tool would be cross-compiled for arm64 and then executed
# on the build machine, which cannot work — so it is not regenerated here.
#
# Copying is correct rather than a shortcut: the output is derived from the
# font files, not from the compiler, so it is byte-identical on every platform.
#
# ANY desktop build tree will do, and it must be any of them — naming one would
# fail on a machine that happened to have built the other, and tell the
# developer to do something they had already done.
set(UI_MIN_TEXT_SIZE_CANDIDATES
    ${REPO_ROOT}/build/linux_debug/generated/ui_min_text_size.gen.h
    ${REPO_ROOT}/build/linux/generated/ui_min_text_size.gen.h
    ${REPO_ROOT}/build/generated/ui_min_text_size.gen.h)
set(UI_MIN_TEXT_SIZE_SRC "")
foreach(candidate ${UI_MIN_TEXT_SIZE_CANDIDATES})
    if(EXISTS ${candidate})
        set(UI_MIN_TEXT_SIZE_SRC ${candidate})
        break()
    endif()
endforeach()
if(NOT UI_MIN_TEXT_SIZE_SRC)
    message(FATAL_ERROR
        "ui_min_text_size.gen.h not found in any desktop build tree.\n"
        "It is generated from streamer's own fonts by a host tool that cannot "
        "run under the NDK cross-compiler, so a desktop build has to produce it "
        "first:\n"
        "  scripts/linux/build.sh --debug -DSTREAMER_GUI=ON "
        "-DVCE_SLANGC=/opt/shader-slang/bin/slangc")
endif()
message(STATUS "ui_min_text_size.gen.h taken from ${UI_MIN_TEXT_SIZE_SRC}")
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated)
configure_file(${UI_MIN_TEXT_SIZE_SRC}
               ${CMAKE_BINARY_DIR}/generated/ui_min_text_size.gen.h COPYONLY)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_BINARY_DIR}/streamer)
