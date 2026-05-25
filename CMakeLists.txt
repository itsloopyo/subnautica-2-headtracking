cmake_minimum_required(VERSION 3.20)
project(Subnautica2HeadTracking LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single source of truth for the mod version is pixi.toml. Parse it here and
# inject as SN2HT_MOD_VERSION so the bootstrap log can print it - that string
# is what users paste into crash reports, so getting it from a real artifact
# (not a hand-maintained define) is what keeps reports accurate.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/pixi.toml" _pixi_toml)
if(_pixi_toml MATCHES "(^|\n)version[ \t]*=[ \t]*\"([^\"]+)\"")
    set(SN2HT_MOD_VERSION "${CMAKE_MATCH_2}")
else()
    message(FATAL_ERROR "Could not parse version from pixi.toml")
endif()

# Short git SHA, captured at configure time. Lets two nightlies built off
# the same pixi.toml version be distinguished in crash reports. Captured
# at configure (not build) so it doesn't bust incremental builds; CI does
# a fresh configure on every push, so artifacts there are always accurate.
# Local dev builds may show a stale SHA after commits without reconfiguring,
# which is acceptable - the SHA is for matching reporter logs to releases,
# not for proving local dev state. Appended "-dirty" if the working tree
# has uncommitted changes at configure time.
find_package(Git QUIET)
if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE SN2HT_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _git_sha_rc
    )
    if(NOT _git_sha_rc EQUAL 0)
        set(SN2HT_GIT_SHA "unknown")
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE _git_dirty
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT _git_dirty STREQUAL "")
        set(SN2HT_GIT_SHA "${SN2HT_GIT_SHA}-dirty")
    endif()
else()
    set(SN2HT_GIT_SHA "unknown")
endif()
message(STATUS "Subnautica2HeadTracking version: ${SN2HT_MOD_VERSION} (${SN2HT_GIT_SHA})")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /MP)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

include(FetchContent)
FetchContent_Declare(
    minhook_src
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.3
)
FetchContent_MakeAvailable(minhook_src)

add_library(minhook STATIC
    ${minhook_src_SOURCE_DIR}/src/hook.c
    ${minhook_src_SOURCE_DIR}/src/buffer.c
    ${minhook_src_SOURCE_DIR}/src/trampoline.c
    ${minhook_src_SOURCE_DIR}/src/hde/hde64.c
    ${minhook_src_SOURCE_DIR}/src/hde/hde32.c
)
target_include_directories(minhook PUBLIC ${minhook_src_SOURCE_DIR}/include)
if(MSVC)
    target_compile_options(minhook PRIVATE /wd4267 /wd4244 /wd4127 /wd4131)
endif()

set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp)

# Kiero - vtable hooking helper for DXGI / D3D11 / D3D12.
FetchContent_Declare(
    kiero_src
    GIT_REPOSITORY https://github.com/Rebzzel/kiero.git
    # Pinned to master tip (1.2.12 is the last tag but predates several D3D12
    # fixes). Bump deliberately if the upstream API moves.
    GIT_TAG        5539f06a03154ad3c30702792f88a220ac54958d
)
FetchContent_MakeAvailable(kiero_src)

# kiero.h hardcodes every backend + minhook to 0, so kiero::init(D3D12) falls
# through to NotSupportedError and kiero::bind is a no-op. Patch the header at
# configure time to enable the D3D12 hook and minhook. Idempotent.
file(READ "${kiero_src_SOURCE_DIR}/kiero.h" _kiero_h)
string(REGEX REPLACE "KIERO_INCLUDE_D3D12([ \t]+)0" "KIERO_INCLUDE_D3D12\\11" _kiero_h "${_kiero_h}")
string(REGEX REPLACE "KIERO_USE_MINHOOK([ \t]+)0"   "KIERO_USE_MINHOOK\\11"   _kiero_h "${_kiero_h}")
file(WRITE "${kiero_src_SOURCE_DIR}/kiero.h" "${_kiero_h}")

add_library(kiero STATIC
    ${kiero_src_SOURCE_DIR}/kiero.cpp
    ${kiero_src_SOURCE_DIR}/minhook/src/buffer.c
    ${kiero_src_SOURCE_DIR}/minhook/src/hook.c
    ${kiero_src_SOURCE_DIR}/minhook/src/trampoline.c
    ${kiero_src_SOURCE_DIR}/minhook/src/hde/hde32.c
    ${kiero_src_SOURCE_DIR}/minhook/src/hde/hde64.c
)
target_include_directories(kiero PUBLIC
    ${kiero_src_SOURCE_DIR}
    ${kiero_src_SOURCE_DIR}/minhook/include
)
if(MSVC)
    target_compile_options(kiero PRIVATE /wd4267 /wd4244 /wd4127 /wd4131 /wd4996)
    # Kiero's kiero.cpp uses free() without including <cstdlib>; force-include
    # it on that one file only (not the bundled minhook .c sources).
    set_source_files_properties(${kiero_src_SOURCE_DIR}/kiero.cpp
        PROPERTIES COMPILE_OPTIONS "/FI;cstdlib")
endif()

# Dear ImGui with Win32 + D3D12 backends.
FetchContent_Declare(
    imgui_src
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    # >= v1.92 for the ImGui_ImplDX12_InitInfo struct cameraunlock-core uses.
    GIT_TAG        v1.92.8
)
FetchContent_MakeAvailable(imgui_src)
add_library(imgui STATIC
    ${imgui_src_SOURCE_DIR}/imgui.cpp
    ${imgui_src_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_src_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_src_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_src_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_src_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_src_SOURCE_DIR}
    ${imgui_src_SOURCE_DIR}/backends
)

set(MOD_NAME Subnautica2HeadTracking)

add_library(${MOD_NAME} SHARED
    src/Subnautica2HeadTracking/dllmain.cpp
    src/Subnautica2HeadTracking/headtracking_mod.cpp
    src/Subnautica2HeadTracking/logging.cpp
    src/Subnautica2HeadTracking/crash_handler.cpp
    src/Subnautica2HeadTracking/reticle_overlay.cpp
    src/Subnautica2HeadTracking/ue_runtime.cpp
    src/Subnautica2HeadTracking/builds/build_registry.cpp
    src/Subnautica2HeadTracking/builds/steam_offsets.cpp
    src/Subnautica2HeadTracking/builds/gdk_offsets.cpp
)

target_include_directories(${MOD_NAME} PRIVATE
    src/Subnautica2HeadTracking
)

target_compile_definitions(${MOD_NAME} PRIVATE
    SN2HT_MOD_VERSION="${SN2HT_MOD_VERSION}"
    SN2HT_GIT_SHA="${SN2HT_GIT_SHA}"
)

if(MSVC)
    target_compile_options(${MOD_NAME} PRIVATE /W4 /WX /wd4324)
    # reticle_overlay.cpp inlines third-party headers (cameraunlock-core's
    # dx12_overlay.h, ImGui, Kiero) whose narrowing conversions trip /WX.
    # Suppress narrowing/sign warnings on that one file.
    set_source_files_properties(
        src/Subnautica2HeadTracking/reticle_overlay.cpp
        PROPERTIES COMPILE_OPTIONS "/wd4244;/wd4267;/wd4458;/wd4459;/wd4127")
endif()

target_link_libraries(${MOD_NAME} PRIVATE
    cameraunlock
    cameraunlock_hooks
    minhook
    kiero
    imgui
    ws2_32
    psapi
    d3d12
    dxgi
)

set_target_properties(${MOD_NAME} PROPERTIES
    SUFFIX ".dll"
    OUTPUT_NAME "dxgi"
)
