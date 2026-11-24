# OpenRFG (VOM, new code): the path tracer, rfgrt (external/rfgrt, the path tracing mod's repository as a git subtree).
# Its x64 plugin is built as the mod builds it, a DLL with the ray tracing core inside (RFGRT_LOCAL_CORE), into
# backends\rfgrt\ next to OpenRFG.exe; the renderer backend loads it (rt_backend.cpp). It stays a DLL of its own: it and
# rfgphys share names (hooks, rfg, session, Config: they were separate plugins).
#
# The game's directory settings (the forced include of openrfg_x64.h, its definitions, warnings and stub headers) are
# not the plugin's: they are cleared here, before the plugin's own CMakeLists sets its own.
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")
set_property(DIRECTORY PROPERTY COMPILE_DEFINITIONS "")
set_property(DIRECTORY PROPERTY INCLUDE_DIRECTORIES "")
set_property(DIRECTORY PROPERTY LINK_OPTIONS "")
add_subdirectory(${CMAKE_SOURCE_DIR}/external/rfgrt ${CMAKE_BINARY_DIR}/external/rfgrt EXCLUDE_FROM_ALL)

set_target_properties(rfgrt PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${OPENRFG_BACKENDS_OUT}/rfgrt)

# rfgrt's names table: its values looked up in OpenRFG.pdb once after each link of the game (the mod's
# tools/names_table), written beside the plugin, so that it needs neither the PDB nor DbgHelp at start-up
# (src/shared/openrfg64_column.h).
set(OPENRFG_RFGRT_NAMES ${OPENRFG_BACKENDS_OUT}/rfgrt/rfgrt.names)
add_custom_command(OUTPUT ${OPENRFG_RFGRT_NAMES}
    COMMAND rfgrt_names $<TARGET_FILE:OpenRFG> ${OPENRFG_RFGRT_NAMES}
    DEPENDS OpenRFG rfgrt_names
    COMMENT "rfgrt's names table from OpenRFG.pdb"
    VERBATIM)
add_custom_target(rfgrt_names_table ALL DEPENDS ${OPENRFG_RFGRT_NAMES})
configure_file(rfgrt.ini ${OPENRFG_BACKENDS_OUT}/rfgrt/rfgrt.ini COPYONLY)
# DLSS Ray Reconstruction (GI.Denoiser=2): the SDK in external/rfgrt/third_party/dlss (out of git: the mod's
# tools/fetch_dlss.ps1); its runtime goes next to the plugin, where the core looks for it.
set(OPENRFG_DLSS_RUNTIME ${CMAKE_SOURCE_DIR}/external/rfgrt/third_party/dlss/lib/Windows_x86_64/rel/nvngx_dlssd.dll)
if(EXISTS ${OPENRFG_DLSS_RUNTIME})
    configure_file(${OPENRFG_DLSS_RUNTIME} ${OPENRFG_BACKENDS_OUT}/rfgrt/nvngx_dlssd.dll COPYONLY)
endif()
