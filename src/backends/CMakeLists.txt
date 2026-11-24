# OpenRFG (VOM, new code): the selectable backends (backends.h): the settings, the hardware probe, what runs.
# The game reaches them from its options (rfg, rfg_ui) and its start-up (rfg_app), all of which link rfg.
add_library(openrfg_backends STATIC
    backends.cpp
    backends_probe.cpp
    seams.cpp)
target_link_libraries(openrfg_backends PUBLIC keen_base)
# Only the 64-bit build has backends (the 32-bit build keeps the settings code, without rows or probe).
if(OPENRFG_X64)
    target_compile_definitions(openrfg_backends PRIVATE OPENRFG_BACKENDS=1)
endif()
target_link_libraries(rfg PUBLIC openrfg_backends)
# KEEP DEBRIS (keep_debris.h) uses the game's types: it is compiled into the rfg library itself.
target_sources(rfg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/keep_debris.cpp)

# The backends' files next to OpenRFG.exe: backends/rfgphys.ini, backends/rfgphysx/, backends/rfgrt/.
set(OPENRFG_BACKENDS_OUT ${CMAKE_BINARY_DIR}/src/openrfg/backends)

# ---- PhysX: rfgphys (external/rfgphys, the PhysX mod's repository as a git subtree).
# Its game code is built into the game; its core (the PhysX host's code with PhysX's static libraries, which have
# their own C runtime) is a DLL that steps PhysX on a thread of the game: backends\rfgphysx\rfgphysx_core.dll next to
# OpenRFG.exe, with PhysX's GPU module and backends\rfgphys.ini. Needs the PhysX SDK built with rfgphys's patch (the
# PhysX repository's tools/build_physx.cmd): its release-gpu/install folder.
set(OPENRFG_PHYSX_INSTALL "" CACHE PATH "The PhysX SDK built with rfgphys's patch (its release-gpu/install folder); empty: no PhysX backend")
if(OPENRFG_X64 AND OPENRFG_PHYSX_INSTALL AND EXISTS "${OPENRFG_PHYSX_INSTALL}/lib/PhysX_static_64.lib")
    set(RFGPHYS ${CMAKE_SOURCE_DIR}/external/rfgphys)
    message(STATUS "OpenRFG: the PhysX backend is built (PhysX SDK ${OPENRFG_PHYSX_INSTALL})")

    # rfgphys's game code: the plugin without its ASI entry (main.cpp), C++20, the game's runtime.
    add_library(openrfg_backend_physx STATIC
        physx/physx_backend.cpp
        ${RFGPHYS}/src/rfgphys/core/startup.cpp
        ${RFGPHYS}/src/rfgphys/core/log.cpp
        ${RFGPHYS}/src/rfgphys/core/config.cpp
        ${RFGPHYS}/src/rfgphys/core/build_check.cpp
        ${RFGPHYS}/src/shared/openrfg64_column.cpp
        ${RFGPHYS}/src/rfgphys/core/session.cpp
        ${RFGPHYS}/src/rfgphys/core/gameloop.cpp
        ${RFGPHYS}/src/rfgphys/core/contacts.cpp
        ${RFGPHYS}/src/rfgphys/core/game_lock.cpp
        ${RFGPHYS}/src/rfgphys/core/thread_dump.cpp
        ${RFGPHYS}/src/rfgphys/core/hang_watch.cpp
        ${RFGPHYS}/src/rfgphys/core/address_space.cpp
        ${RFGPHYS}/src/rfgphys/dev/fly_camera.cpp
        ${RFGPHYS}/src/rfgphys/hooks/hooks.cpp
        ${RFGPHYS}/src/rfgphys/game/rfg.cpp
        ${RFGPHYS}/src/rfgphys/game/layout.cpp
        ${RFGPHYS}/src/rfgphys/telemetry/telemetry.cpp
        ${RFGPHYS}/src/rfgphys/telemetry/object_census.cpp
        ${RFGPHYS}/src/rfgphys/telemetry/render_census.cpp
        ${RFGPHYS}/src/rfgphys/telemetry/render_timeline.cpp
        ${RFGPHYS}/src/rfgphys/telemetry/profiler.cpp
        ${RFGPHYS}/src/rfgphys/test/automation.cpp
        ${RFGPHYS}/src/rfgphys/test/memory_census.cpp
        ${RFGPHYS}/src/rfgphys/test/restore_building.cpp
        ${RFGPHYS}/src/rfgphys/test/stress_pieces.cpp
        ${RFGPHYS}/src/rfgphys/test/vehicle_push.cpp
        ${RFGPHYS}/src/rfgphys/test/chunk_fall.cpp
        ${RFGPHYS}/src/rfgphys/test/stream_report.cpp
        ${RFGPHYS}/src/rfgphys/test/view_shots.cpp
        ${RFGPHYS}/src/rfgphys/test/viewpoints.cpp
        ${RFGPHYS}/src/rfgphys/test/visit.cpp
        ${RFGPHYS}/src/rfgphys/test/zone_cycle.cpp
        ${RFGPHYS}/src/rfgphys/experiments/debris_collision.cpp
        ${RFGPHYS}/src/rfgphys/experiments/big_buildings.cpp
        ${RFGPHYS}/src/rfgphys/experiments/render_tweaks.cpp
        ${RFGPHYS}/src/rfgphys/experiments/havok_audit.cpp
        ${RFGPHYS}/src/rfgphys/experiments/havok_mt.cpp
        ${RFGPHYS}/src/rfgphys/experiments/keep_debris.cpp
        ${RFGPHYS}/src/rfgphys/harness/harness.cpp
        ${RFGPHYS}/src/rfgphys/harness/rec_file.cpp
        ${RFGPHYS}/src/rfgphys/harness/checkpoint.cpp
        ${RFGPHYS}/src/rfgphys/harness/rng_guard.cpp
        ${RFGPHYS}/src/rfgphys/harness/screenshot.cpp
        ${RFGPHYS}/src/rfgphys/harness/track.cpp
        ${RFGPHYS}/src/rfgphys/physx/bridge.cpp
        ${RFGPHYS}/src/rfgphys/physx/collision_survey.cpp
        ${RFGPHYS}/src/rfgphys/physx/debris_offload.cpp
        ${RFGPHYS}/src/rfgphys/physx/havok_shapes.cpp
        ${RFGPHYS}/src/rfgphys/physx/render_routing.cpp
        ${RFGPHYS}/src/rfgphys/physx/test_scene.cpp
        ${RFGPHYS}/src/rfgphys/physx/world_mirror.cpp
        ${RFGPHYS}/third_party/minhook/src/buffer.c
        ${RFGPHYS}/third_party/minhook/src/hook.c
        ${RFGPHYS}/third_party/minhook/src/trampoline.c
        ${RFGPHYS}/third_party/minhook/src/hde/hde64.c)
    set_target_properties(openrfg_backend_physx PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    target_include_directories(openrfg_backend_physx PRIVATE
        ${RFGPHYS}/src/rfgphys ${RFGPHYS}/src/shared ${RFGPHYS}/third_party/minhook/include)
    target_compile_definitions(openrfg_backend_physx PRIVATE WIN32_LEAN_AND_MEAN UNICODE _UNICODE RFGPHYS_VERSION="0.5.0-vom")
    target_link_libraries(openrfg_backend_physx PRIVATE psapi)

    # The PhysX core: the host's code as a DLL, with PhysX's static libraries and their static runtime.
    add_library(rfgphysx_core SHARED ${RFGPHYS}/host/src/host_main.cpp)
    set_target_properties(rfgphysx_core PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
        MSVC_RUNTIME_LIBRARY MultiThreaded
        RUNTIME_OUTPUT_DIRECTORY ${OPENRFG_BACKENDS_OUT}/rfgphysx)
    target_include_directories(rfgphysx_core PRIVATE ${RFGPHYS}/src/shared ${OPENRFG_PHYSX_INSTALL}/include)
    target_compile_definitions(rfgphysx_core PRIVATE WIN32_LEAN_AND_MEAN UNICODE _UNICODE PX_PHYSX_STATIC_LIB RFGPHYSX_CORE_DLL)
    target_link_libraries(rfgphysx_core PRIVATE
        ${OPENRFG_PHYSX_INSTALL}/lib/PhysXExtensions_static_64.lib
        ${OPENRFG_PHYSX_INSTALL}/lib/PhysX_static_64.lib
        ${OPENRFG_PHYSX_INSTALL}/lib/PhysXPvdSDK_static_64.lib
        ${OPENRFG_PHYSX_INSTALL}/lib/PhysXCommon_static_64.lib
        ${OPENRFG_PHYSX_INSTALL}/lib/PhysXFoundation_static_64.lib
        dbghelp)
    # PhysX loads its GPU module by name (the core loads it first from its own folder): the build calls it PhysXGpu.dll.
    add_custom_command(TARGET rfgphysx_core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OPENRFG_PHYSX_INSTALL}/bin/PhysXGpu.dll ${OPENRFG_BACKENDS_OUT}/rfgphysx/PhysXGpu_64.dll)
    configure_file(physx/rfgphys.ini ${OPENRFG_BACKENDS_OUT}/rfgphys.ini COPYONLY)

    target_compile_definitions(openrfg_backends PRIVATE OPENRFG_BACKEND_PHYSX=1)
    target_link_libraries(openrfg_backends PUBLIC openrfg_backend_physx)
    add_dependencies(openrfg_backends rfgphysx_core)
elseif(OPENRFG_X64)
    message(STATUS "OpenRFG: no PhysX backend (OPENRFG_PHYSX_INSTALL is not a PhysX SDK build)")
endif()

# ---- The path tracer: rfgrt (rt/CMakeLists.txt). Needs the Windows SDK's shader compilers (fxc, dxc).
option(OPENRFG_PATH_TRACER "Build the path tracing backend (external/rfgrt)" ON)
if(OPENRFG_X64 AND OPENRFG_PATH_TRACER)
    file(GLOB _openrfg_sdk_bins "C:/Program Files (x86)/Windows Kits/10/bin/10.*/x64")
    find_program(OPENRFG_FXC fxc PATHS ${_openrfg_sdk_bins} NO_DEFAULT_PATH)
    find_program(OPENRFG_DXC dxc PATHS ${_openrfg_sdk_bins} NO_DEFAULT_PATH)
    if(OPENRFG_FXC AND OPENRFG_DXC)
        message(STATUS "OpenRFG: the path tracing backend is built (external/rfgrt)")
        add_subdirectory(rt)
        target_sources(openrfg_backends PRIVATE rt/rt_backend.cpp)
        target_compile_definitions(openrfg_backends PRIVATE OPENRFG_BACKEND_RT=1)
        add_dependencies(openrfg_backends rfgrt)
    else()
        message(STATUS "OpenRFG: no path tracing backend (the Windows SDK's fxc and dxc were not found)")
    endif()
endif()
