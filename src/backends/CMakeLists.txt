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

    # rfgphys's names table: its names, struct layouts and optional layout values looked up in OpenRFG.pdb once after
    # each link of the game (its tools/names_table), written beside rfgphys.ini, so that it needs neither the PDB nor
    # DbgHelp at start-up (src/shared/openrfg64_column.h).
    add_executable(rfgphys_names ${RFGPHYS}/tools/names_table/main.cpp ${RFGPHYS}/src/rfgphys/game/layout.cpp
        ${RFGPHYS}/src/shared/openrfg64_column.cpp)
    set_target_properties(rfgphys_names PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    target_include_directories(rfgphys_names PRIVATE ${RFGPHYS}/src/rfgphys ${RFGPHYS}/src/shared)
    target_compile_definitions(rfgphys_names PRIVATE WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    add_custom_command(OUTPUT ${OPENRFG_BACKENDS_OUT}/rfgphys.names
        COMMAND rfgphys_names $<TARGET_FILE:OpenRFG> ${OPENRFG_BACKENDS_OUT}/rfgphys.names
        DEPENDS OpenRFG rfgphys_names
        COMMENT "rfgphys's names table from OpenRFG.pdb"
        VERBATIM)
    add_custom_target(rfgphys_names_table ALL DEPENDS ${OPENRFG_BACKENDS_OUT}/rfgphys.names)

    target_compile_definitions(openrfg_backends PRIVATE OPENRFG_BACKEND_PHYSX=1)
    target_link_libraries(openrfg_backends PUBLIC openrfg_backend_physx)
    add_dependencies(openrfg_backends rfgphysx_core)
elseif(OPENRFG_X64)
    message(STATUS "OpenRFG: no PhysX backend (OPENRFG_PHYSX_INSTALL is not a PhysX SDK build)")
endif()

# ---- NVIDIA's SDKs, kept out of git by their licences: the DLSS SDK in external/rfgrt/third_party/dlss (DLSS Ray
# Reconstruction for the path tracer, DLSS for the game's frame) and Streamline in external/rfgrt/third_party/streamline
# (DLSS Frame Generation). When they are missing, the path tracer's own scripts fetch them here (pinned releases, their
# hashes and NVIDIA's signatures checked). OPENRFG_FETCH_SDKS=OFF, or a fetch that fails (offline), builds without them:
# the checks below and in the path tracer then leave those features out and say so.
option(OPENRFG_FETCH_SDKS "Download NVIDIA's DLSS SDK and Streamline into external/rfgrt/third_party when missing" ON)
if(OPENRFG_X64 AND OPENRFG_FETCH_SDKS)
    set(_openrfg_sdks ${CMAKE_SOURCE_DIR}/external/rfgrt/third_party)
    set(_openrfg_sdk_files_dlss include/nvsdk_ngx.h lib/Windows_x86_64/x64/nvsdk_ngx_s.lib lib/Windows_x86_64/x64/nvsdk_ngx_d.lib
        lib/Windows_x86_64/rel/nvngx_dlssd.dll lib/Windows_x86_64/rel/nvngx_dlss.dll)
    set(_openrfg_sdk_files_streamline include/sl.h bin/x64/sl.interposer.dll bin/x64/nvngx_dlssg.dll)
    foreach(_sdk dlss streamline)
        set(_missing FALSE)
        foreach(_file IN LISTS _openrfg_sdk_files_${_sdk})
            if(NOT EXISTS ${_openrfg_sdks}/${_sdk}/${_file})
                set(_missing TRUE)
            endif()
        endforeach()
        set(_fetch ${CMAKE_SOURCE_DIR}/external/rfgrt/tools/fetch_${_sdk}.ps1)
        if(_missing AND EXISTS ${_fetch})
            message(STATUS "OpenRFG: fetching NVIDIA's ${_sdk} SDK into external/rfgrt/third_party/${_sdk} (external/rfgrt/tools/fetch_${_sdk}.ps1)")
            execute_process(COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${_fetch} RESULT_VARIABLE _rc)
            if(NOT _rc EQUAL 0)
                message(WARNING "OpenRFG: fetch_${_sdk}.ps1 failed (${_rc}): building without the ${_sdk} SDK")
            endif()
        endif()
    endforeach()
endif()

# ---- DLSS for the game's frame (dlss/dlss_frame.h), with either renderer: NVIDIA's DLSS SDK (NGX on Direct3D 11, the
# dynamic-runtime library) in external/rfgrt/third_party/dlss (out of git: the mod's tools/fetch_dlss.ps1), its runtime
# nvngx_dlss.dll in backends\dlss next to OpenRFG.exe; its motion pass compiled by the Windows SDK's fxc. Without them
# (and in the 32-bit build) the functions say DLSS is off.
target_sources(openrfg_backends PRIVATE dlss/dlss_frame.cpp)
set(OPENRFG_DLSS_SDK ${CMAKE_SOURCE_DIR}/external/rfgrt/third_party/dlss)
if(OPENRFG_X64)
    file(GLOB _openrfg_sdk_bins_dlss "C:/Program Files (x86)/Windows Kits/10/bin/10.*/x64")
    find_program(OPENRFG_FXC fxc PATHS ${_openrfg_sdk_bins_dlss} NO_DEFAULT_PATH)
endif()
if(OPENRFG_X64 AND OPENRFG_FXC AND EXISTS ${OPENRFG_DLSS_SDK}/include/nvsdk_ngx.h
   AND EXISTS ${OPENRFG_DLSS_SDK}/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib
   AND EXISTS ${OPENRFG_DLSS_SDK}/lib/Windows_x86_64/rel/nvngx_dlss.dll)
    message(STATUS "OpenRFG: DLSS for the game's frame is built (${OPENRFG_DLSS_SDK})")
    set(_openrfg_dlss_gen ${CMAKE_CURRENT_BINARY_DIR}/dlss_shaders)
    file(MAKE_DIRECTORY ${_openrfg_dlss_gen})
    set(_openrfg_dlss_headers "")
    foreach(_shader dlss_fullscreen_vs:vs_5_0 dlss_motion_ps:ps_5_0)
        string(REPLACE ":" ";" _parts ${_shader})
        list(GET _parts 0 _name)
        list(GET _parts 1 _profile)
        add_custom_command(OUTPUT ${_openrfg_dlss_gen}/${_name}.h
            COMMAND ${OPENRFG_FXC} /nologo /T ${_profile} /E main /O3 /Vn g_${_name} /Fh ${_openrfg_dlss_gen}/${_name}.h
                    ${CMAKE_CURRENT_SOURCE_DIR}/dlss/${_name}.hlsl
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/dlss/${_name}.hlsl
            COMMENT "fxc ${_name}.hlsl"
            VERBATIM)
        list(APPEND _openrfg_dlss_headers ${_openrfg_dlss_gen}/${_name}.h)
    endforeach()
    target_sources(openrfg_backends PRIVATE ${_openrfg_dlss_headers})
    target_include_directories(openrfg_backends PRIVATE ${_openrfg_dlss_gen})
    target_include_directories(openrfg_backends SYSTEM PRIVATE ${OPENRFG_DLSS_SDK}/include)
    target_compile_definitions(openrfg_backends PRIVATE OPENRFG_DLSS=1)
    target_link_libraries(openrfg_backends PUBLIC ${OPENRFG_DLSS_SDK}/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib d3d11)
    configure_file(${OPENRFG_DLSS_SDK}/lib/Windows_x86_64/rel/nvngx_dlss.dll ${OPENRFG_BACKENDS_OUT}/dlss/nvngx_dlss.dll COPYONLY)
elseif(OPENRFG_X64)
    message(STATUS "OpenRFG: no DLSS for the game's frame (the DLSS SDK with nvngx_dlss.dll and nvsdk_ngx_d.lib, or fxc, not found)")
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
