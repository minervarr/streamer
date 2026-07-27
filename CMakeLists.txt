cmake_minimum_required(VERSION 3.22)
project(streamer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# A bare `cmake -B build` used to configure with no CMAKE_BUILD_TYPE at all,
# which on single-config generators means *no* optimization flags — not Debug,
# not Release, just -O0 with no -g. Default to Release.
get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT CMAKE_BUILD_TYPE AND NOT _multi_config)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                 Debug Release RelWithDebInfo MinSizeRel)
endif()

# ── Release optimization ────────────────────────────────────────────────────
# Everything here is set before the first add_subdirectory() so the vendored
# curl / taglib / sqlite3 / KawusapiCC targets inherit it too: a download tool
# spends most of its cycles inside libcurl and TagLib, not inside src/, so
# optimizing only our own translation units would miss the hot code.

# Whole-program optimization (GCC/Clang LTO, MSVC /GL+/LTCG): lets the linker
# inline and dead-strip across translation units instead of stopping at each
# object boundary. Read at target-creation time, hence before the targets
# exist. Costs link time; Release only.
option(STREAMER_LTO "Link-time optimization for Release builds" ON)
if(STREAMER_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_why)
    if(_ipo_ok)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    else()
        message(STATUS "LTO unavailable, continuing without it: ${_ipo_why}")
    endif()
endif()

# Optional x86-64 psABI microarchitecture level (v2/v3/v4), "native", or any
# -march name the compiler knows (znver4, …). Empty (the default) keeps the
# generic x86-64 baseline: a v4 binary SIGILLs on a CPU without AVX-512, and
# -march=x86-64-v2/v3/v4 needs GCC 11+ / Clang 12+, so this is opt-in per
# build rather than something a plain configure turns on:
#   cmake -B build -DSTREAMER_ARCH_LEVEL=v3
# Appended to CMAKE_{C,CXX}_FLAGS rather than add_compile_options() so it
# survives into any nested cmake invocation that forwards those variables.
set(STREAMER_ARCH_LEVEL "" CACHE STRING
    "x86-64 target: v2/v3/v4 (psABI level), native, or an -march name. Empty = portable.")
if(STREAMER_ARCH_LEVEL AND NOT MSVC)
    if(STREAMER_ARCH_LEVEL MATCHES "^v[1234]$")
        set(_arch_flag "-march=x86-64-${STREAMER_ARCH_LEVEL}")
    else()
        set(_arch_flag "-march=${STREAMER_ARCH_LEVEL}")
    endif()
    include(CheckCXXCompilerFlag)
    # The result variable is cached, so it must be named after the flag being
    # tested — a fixed name would answer for -march=v3 on a later reconfigure
    # that asked for -march=nonsense and let the bad flag through.
    string(MAKE_C_IDENTIFIER "streamer_arch_ok_${_arch_flag}" _arch_flag_ok_var)
    check_cxx_compiler_flag("${_arch_flag}" ${_arch_flag_ok_var})
    if(NOT ${_arch_flag_ok_var})
        message(FATAL_ERROR "Compiler does not support ${_arch_flag} "
                            "(x86-64-v2/v3/v4 need GCC 11+ or Clang 12+). "
                            "Leave STREAMER_ARCH_LEVEL unset to build portably.")
    endif()
    string(APPEND CMAKE_C_FLAGS   " ${_arch_flag}")
    string(APPEND CMAKE_CXX_FLAGS " ${_arch_flag}")
elseif(STREAMER_ARCH_LEVEL AND MSVC)
    message(WARNING "STREAMER_ARCH_LEVEL is a GCC/Clang flag with no MSVC "
                    "equivalent (MSVC tops out at /arch:AVX2) — ignored.")
endif()

if(NOT MSVC)
    # CMake's Release default is only "-O3 -DNDEBUG". Add the rest: split every
    # function and variable into its own section so the linker can garbage
    # collect what LTO proved unreachable, and let the compiler assume no
    # symbol interposition (these are all static libraries linked into one
    # executable — nothing can be replaced at load time).
    add_compile_options(
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-ffunction-sections>
        $<$<CONFIG:Release>:-fdata-sections>
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE)
        add_compile_options($<$<CONFIG:Release>:-fno-semantic-interposition>)
        # --gc-sections drops the collected sections; -O1 is the *linker's*
        # own optimization level (hash table layout etc.), unrelated to -O
        # for the compiler; --as-needed keeps DT_NEEDED down to what is used.
        add_link_options(
            $<$<CONFIG:Release>:LINKER:--gc-sections>
            $<$<CONFIG:Release>:LINKER:-O1>
            $<$<CONFIG:Release>:LINKER:--as-needed>
        )
    endif()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}")

# Paths into Kobuzapi++
set(KOBUZAPI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/first_party/KawusapiCC")

# ── bundled curl (third_party/curl) ─────────────────────────────────────────
set(BUILD_CURL_EXE        OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS     OFF CACHE BOOL "" FORCE)
set(CURL_STATICLIB        ON  CACHE BOOL "" FORCE)
set(HTTP_ONLY             ON  CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP     ON  CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAPS    ON  CACHE BOOL "" FORCE)
set(CURL_DISABLE_TELNET   ON  CACHE BOOL "" FORCE)
set(CURL_DISABLE_DICT     ON  CACHE BOOL "" FORCE)
set(CURL_DISABLE_FILE     ON  CACHE BOOL "" FORCE)
set(CURL_DISABLE_TFTP     ON  CACHE BOOL "" FORCE)
set(CURL_DISABLE_FTP      ON  CACHE BOOL "" FORCE)
set(CURL_DISABLE_IMAP     ON  CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMTP     ON  CACHE BOOL "" FORCE)
set(CURL_DISABLE_POP3     ON  CACHE BOOL "" FORCE)
set(CURL_DISABLE_GOPHER   ON  CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMB      ON  CACHE BOOL "" FORCE)
set(CURL_DISABLE_RTSP     ON  CACHE BOOL "" FORCE)
set(CURL_DISABLE_MQTT     ON  CACHE BOOL "" FORCE)
if(WIN32)
    set(CURL_USE_SCHANNEL ON  CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL  OFF CACHE BOOL "" FORCE)
    set(CURL_USE_MBEDTLS  OFF CACHE BOOL "" FORCE)
endif()
set(CURL_USE_LIBPSL       OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2      OFF CACHE BOOL "" FORCE)
set(USE_LIBIDN2           OFF CACHE BOOL "" FORCE)
set(CURL_ZLIB             OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/curl)

# ── bundled taglib (third_party/taglib) ─────────────────────────────────────
set(BUILD_SHARED_LIBS     OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING         OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES        OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/taglib)

# taglib's source headers are in subdirectories (toolkit/, mpeg/, …) but public API
# uses <taglib/tbytevector.h> style (flat layout, matching the install tree). Create
# a flat include directory at configure time so consumers work without an install step.
set(_taglib_flat "${CMAKE_BINARY_DIR}/taglib_flat_include/taglib")
file(MAKE_DIRECTORY "${_taglib_flat}")
file(GLOB_RECURSE _taglib_all_hdrs
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/taglib/taglib/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/taglib/taglib/*.tcc")
foreach(_hdr ${_taglib_all_hdrs})
    get_filename_component(_hdr_name "${_hdr}" NAME)
    configure_file("${_hdr}" "${_taglib_flat}/${_hdr_name}" COPYONLY)
endforeach()
# taglib_config.h is generated by add_subdirectory(third_party/taglib) above
configure_file(
    "${CMAKE_BINARY_DIR}/third_party/taglib/taglib_config.h"
    "${_taglib_flat}/taglib_config.h"
    COPYONLY)

# ── Kobuzapi++ (KawusapiCC/core: kobuzapi_core, ae_util, ae_net, ae_tag) ────
# CURL::libcurl and `tag` (TagLib) targets above must exist before this —
# KawusapiCC/core's ae_net/ae_tag CMakeLists.txt link against them directly
# instead of streamer re-implementing http.cc/tag.cc compilation itself.
add_subdirectory(${KOBUZAPI_ROOT}/core ${CMAKE_BINARY_DIR}/kobuzapi_core)
target_include_directories(ae_tag PRIVATE
    "${CMAKE_BINARY_DIR}/taglib_flat_include"   # flat <taglib/...> layout
)

# ── FetchContent dependencies ────────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.4.2
)
FetchContent_MakeAvailable(CLI11)

FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

# SQLite3 amalgamation (downloaded at configure time)
FetchContent_Declare(sqlite3_amalg
    URL https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(sqlite3_amalg)
add_library(sqlite3_lib STATIC ${sqlite3_amalg_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3_lib PUBLIC ${sqlite3_amalg_SOURCE_DIR})
target_compile_definitions(sqlite3_lib PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_DEFAULT_MEMSTATUS=0
)

# ── streamer_core (backend logic shared by the CLI and the GUI) ─────────────
add_library(streamer_core STATIC
    src/config.cpp
    src/history.cpp
    src/library.cpp
    src/download.cpp
    src/search.cpp
    src/inspect.cpp
    src/url.cpp
    src/extras.cpp
    src/i18n.cpp
    src/service_factory.cpp
)
target_include_directories(streamer_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(streamer_core PUBLIC
    kobuzapi_core
    sqlite3_lib
    tomlplusplus::tomlplusplus
)
if(WIN32)
    target_link_libraries(streamer_core PUBLIC ws2_32 wldap32 crypt32 normaliz)
endif()
if(MSVC)
    # /O2 /Ob2 were unconditional here, so a Debug build was compiled
    # optimized and single-stepped through inlined-away code. Config-guarded.
    target_compile_options(streamer_core PRIVATE /MP /W3 /utf-8
        $<$<CONFIG:Release>:/O2> $<$<CONFIG:Release>:/Ob2>
        $<$<CONFIG:Release>:/arch:AVX2>)
endif()

# ── streamer executable (CLI) ────────────────────────────────────────────────
add_executable(streamer src/main.cpp)
target_link_libraries(streamer PRIVATE streamer_core CLI11::CLI11)
if(MSVC)
    target_compile_options(streamer PRIVATE /MP /W3 /utf-8
        $<$<CONFIG:Release>:/O2> $<$<CONFIG:Release>:/Ob2>
        $<$<CONFIG:Release>:/arch:AVX2>)
endif()

# ── legacy GUI (Windows only; preserved as reference until the new gui/ port
#    is complete) ─────────────────────────────────────────────────────────────
if(WIN32)
    add_subdirectory(streamer-gui)
endif()

# ── new cross-platform GUI (Vulkan/vk_canvas) ───────────────────────────────
# Needs a Slang shader compiler (Vulkan SDK / shader-slang) at build time, so
# a box without one must still configure. Override with -DSTREAMER_GUI=ON/OFF
# and -DVCE_SLANGC=/path/to/slangc.
if(WIN32)
    set(_gui_default ON)
elseif(UNIX)
    set(_gui_default OFF)
    if(VCE_SLANGC OR DEFINED ENV{VULKAN_SDK})
        set(_gui_default ON)
    else()
        find_program(_slangc_probe slangc)
        if(_slangc_probe)
            set(_gui_default ON)
        endif()
    endif()
endif()
option(STREAMER_GUI "Build the new Vulkan/vk_canvas GUI" ${_gui_default})
if(STREAMER_GUI)
    add_subdirectory(gui)
else()
    message(STATUS "gui skipped (no slangc found; pass -DSTREAMER_GUI=ON + -DVCE_SLANGC=/path/to/slangc)")
endif()
