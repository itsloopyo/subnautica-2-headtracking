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
set(CAMERAUNLOCK_BUILD_UNREAL ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp)

set(MOD_NAME Subnautica2HeadTracking)

add_library(${MOD_NAME} SHARED
    src/Subnautica2HeadTracking/dllmain.cpp
    src/Subnautica2HeadTracking/headtracking_mod.cpp
    src/Subnautica2HeadTracking/aim_projection.cpp
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
endif()

target_link_libraries(${MOD_NAME} PRIVATE
    cameraunlock
    cameraunlock_hooks
    cameraunlock_unreal
    minhook
    ws2_32
    psapi
)

set_target_properties(${MOD_NAME} PROPERTIES
    SUFFIX ".dll"
    OUTPUT_NAME "dxgi"
)
