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
# Pinned to the commit v1.3.3 points at, not the tag: a tag can be moved
# upstream, and the version we state in THIRD-PARTY-NOTICES.md has to be the
# version we actually compile in.
FetchContent_Declare(
    minhook_src
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        9fbd087432700d73fc571118d6a9697a36443d88  # v1.3.3, 2017-01-06
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
    src/Subnautica2HeadTracking/config.cpp
    src/legacy_config/legacy_config.cpp
)

target_include_directories(${MOD_NAME} PRIVATE
    src/Subnautica2HeadTracking
    src
)

target_compile_definitions(${MOD_NAME} PRIVATE
    SN2HT_MOD_VERSION="${SN2HT_MOD_VERSION}"
    SN2HT_GIT_SHA="${SN2HT_GIT_SHA}"
)

option(SN2HT_DEV_HOTKEYS "Build the discovery/tuning diagnostic hotkeys (developer aid, not for release)" OFF)
if(SN2HT_DEV_HOTKEYS)
    target_compile_definitions(${MOD_NAME} PRIVATE SN2HT_DEV_HOTKEYS=1)
    message(WARNING "SN2HT_DEV_HOTKEYS is ON. This build registers F3/F4 tooltip "
                    "follow-scale tuning, F6 mask diagnostics, F7 inject-mode cycling, "
                    "F8 standalone position toggle, F9/F10 mask compensation toggle and "
                    "mode, F11/F12 mask mark and clear, ScrollLock HUD dump, and "
                    "Insert/Delete mask-slot selection. Cycling the inject mode can "
                    "leave the view not following the head. Do not ship it.")
endif()

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

# Behaviour lock for the lean boundary, the config tests and the config
# differential test. Separate executables linking none of the hook machinery,
# so `pixi run test` runs anywhere the mod builds, with no game installed.
option(SN2HT_BUILD_TESTS "Build the position behaviour tests and the config tests" ON)
if(SN2HT_BUILD_TESTS)
    enable_testing()

    add_executable(sn2ht_position_tests tests/position_tests.cpp)
    target_include_directories(sn2ht_position_tests PRIVATE
        src/Subnautica2HeadTracking
        cameraunlock-core/cpp/include
    )
    target_link_libraries(sn2ht_position_tests PRIVATE cameraunlock)
    add_test(NAME sn2ht_position_tests COMMAND sn2ht_position_tests)

    # The committed config against the table's fresh render, and the saves.
    # `sn2ht_config_tests --render-config <path>` is pixi run render-config.
    add_executable(sn2ht_config_tests
        tests/config_tests.cpp
        src/Subnautica2HeadTracking/config.cpp
        src/legacy_config/legacy_config.cpp
    )
    target_include_directories(sn2ht_config_tests PRIVATE
        src
        src/Subnautica2HeadTracking
        cameraunlock-core/cpp/include
    )
    target_compile_definitions(sn2ht_config_tests PRIVATE
        SN2HT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(sn2ht_config_tests PRIVATE cameraunlock)
    if(MSVC)
        target_compile_options(sn2ht_config_tests PRIVATE /W4 /WX /EHsc)
    endif()
    add_test(NAME sn2ht_config_tests COMMAND sn2ht_config_tests)

    # The canonical config conversion's differential test: v0.6.2's reader and
    # startup code (the oracle, transcribed in tests/config_differential/oracle
    # beside byte copies of the v0.6.2 headers it includes) against the frozen
    # reader in src/legacy_config/, and that against the config owner's
    # migration into CameraUnlock.ini.
    set(SN2HT_DIFFERENTIAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    add_executable(sn2ht_config_differential
        ${SN2HT_DIFFERENTIAL_DIR}/differential_tests.cpp
        ${SN2HT_DIFFERENTIAL_DIR}/oracle/oracle_reader.cpp
        src/Subnautica2HeadTracking/config.cpp
        src/legacy_config/legacy_config.cpp
    )
    target_include_directories(sn2ht_config_differential PRIVATE
        src
        src/Subnautica2HeadTracking
        ${SN2HT_DIFFERENTIAL_DIR}
        cameraunlock-core/cpp/include
    )
    target_compile_definitions(sn2ht_config_differential PRIVATE
        SN2HT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(sn2ht_config_differential PRIVATE cameraunlock)
    if(MSVC)
        target_compile_options(sn2ht_config_differential PRIVATE /W4 /WX /EHsc)
    endif()
    add_test(NAME sn2ht_config_differential COMMAND sn2ht_config_differential)
    # Core's canonical config lint over the committed file and every file the
    # differential test migrated, which it writes beside itself under migrated\.
    add_test(NAME sn2ht_config_lint COMMAND node
             ${SN2HT_DIFFERENTIAL_DIR}/lint-migrated.mjs
             $<TARGET_FILE_DIR:sn2ht_config_differential>/migrated)
    set_tests_properties(sn2ht_config_lint PROPERTIES DEPENDS sn2ht_config_differential)

    # What the differential test compiles is pinned by hash, line endings aside:
    # the oracle's byte copies must stay v0.6.2's (git show
    # v0.6.2:src/Subnautica2HeadTracking/<file>), the core sources both readers
    # compile must stay the ones v0.6.2 was built with (core bd22895), and the
    # frozen reader is never edited.
    function(sn2ht_require_hash path expected)
        file(READ "${path}" text)
        string(REPLACE "\r" "" text "${text}")
        string(SHA256 actual "${text}")
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR "${path} has changed (SHA-256 of its text ${actual}, pinned "
                                "${expected}). The config differential test compares against "
                                "exactly this file; see CMakeLists.txt.")
        endif()
    endfunction()
    set(SN2HT_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cameraunlock-core/cpp)
    foreach(pinned
            "${SN2HT_DIFFERENTIAL_DIR}/oracle/src/position_boundary.h|c292ea3287c594fe1706e8e035e6a5c968f9fcf28edee09093227fa7268e89af"
            "${SN2HT_DIFFERENTIAL_DIR}/oracle/src/logging.h|71e3d4f18b0324310c1e2357e282716d7d1b49e516b5cc46e720c7453fd014bf"
            "${SN2HT_CORE_DIR}/include/cameraunlock/config/ini_reader.h|a7ffb44210ff59672fa97e8e5feaa2cb3e81938fcc0334a384c68bc371b3857a"
            "${SN2HT_CORE_DIR}/src/config/ini_reader.cpp|e01515c2656aaf533bae4350dc45b702c3e3d4043935743dcc9bd5ea581fbe1c"
            "${SN2HT_CORE_DIR}/include/cameraunlock/data/position_settings.h|b24dceb8e25475aebc5a468a5c7362a4a4e64204d183d1408525345f32f547f5"
            "${SN2HT_CORE_DIR}/include/cameraunlock/math/smoothing_utils.h|fc2146f8c585e5f610c7234e302f59de4945679cfa28ff479ca47477ec073f22"
            "${SN2HT_CORE_DIR}/include/cameraunlock/protocol/udp_receiver.h|7e452809960190720ddff64c4a33ac41d43695fae66d37d4ddecc06dd0f9c4d8"
            "${SN2HT_CORE_DIR}/include/cameraunlock/processing/position_processor.h|9963726117bdbc354170b8c77f2d95cf9337af8de55435eae97a0f33c47fcc87"
            "${SN2HT_CORE_DIR}/include/cameraunlock/logging/file_log.h|43bdd2ef8554c78e5f440333463750c13b95110fe672b0b6e273244df9e7d169"
            "${SN2HT_CORE_DIR}/src/logging/file_log.cpp|73c53c2baa06bbfebe8211f62678aa2b60cb95f604743d3686951ba56b87ea47"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy_config/legacy_config.h|af6049e1aa5985625cfc03fb358ce797b2bbf11b84a00c078cbe3fc380851445"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy_config/legacy_config.cpp|b69eae157a3536021e9141a64e20b8da711a6ff92f7d16e2bb9c9b5f240a4408")
        string(REPLACE "|" ";" pair "${pinned}")
        list(GET pair 0 pinned_path)
        list(GET pair 1 pinned_hash)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${pinned_path}")
        sn2ht_require_hash("${pinned_path}" "${pinned_hash}")
    endforeach()
endif()
