# OpenRFG: reimplementation of rfg.exe (Red Faction Guerrilla Re-Mars-tered).
#
# 32-bit x86 is the reference build (every recovered layout matches the original binary); 64-bit is experimental.
# Configure from an "x86 Native Tools" prompt (or with vcvars32), for example:
#   cmake -S . -B build/x86 -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
#   cmake --build build/x86
cmake_minimum_required(VERSION 3.20)
project(OpenRFG C CXX)

# 64-bit port (experimental): the original is 32-bit, so a 64-bit build is expected to hit compile errors until the
# code is ported. The prebuilt platform DLLs/import libs in lib/win32 are 32-bit only, so a 64-bit build must use the
# stand-ins (OPENRFG_STUB_SWAPI/BINK/DISCORD/GFESDK, forced ON below).
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(OPENRFG_X64 ON)
    message(WARNING "OpenRFG: building 64-bit (experimental). Platform DLLs are replaced by stand-ins.")
    add_compile_definitions(OPENRFG_X64=1)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/FI${CMAKE_SOURCE_DIR}/src/openrfg_x64.h>)
else()
    set(OPENRFG_X64 OFF)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/FI${CMAKE_SOURCE_DIR}/src/openrfg_x64.h>)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OPENRFG_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(OPENRFG_RUNTIME ${OPENRFG_SRC}/library/modules/runtime)

add_compile_definitions(WIN32 _WIN32 _CRT_SECURE_NO_WARNINGS NOMINMAX)
if(MSVC)
    add_compile_options(/W4 /utf-8 $<$<COMPILE_LANGUAGE:CXX>:/EHsc$<SEMICOLON>/permissive-$<SEMICOLON>/Zc:__cplusplus>)
	add_compile_options(/wd4100) # warning C4100: unreferenced formal parameter
	add_compile_options(/wd4127) # warning C4127: conditional expression is constant
	add_compile_options(/wd4201) # warning C4201: nonstandard extension used: nameless struct/union
	add_compile_options(/wd4324) # warning C4324: structure was padded due to alignment specifier
	add_compile_options(/wd4471) # warning C4471: a forward declaration of an unscoped enumeration must have an underlying type
	add_compile_options(/wd4505) # warning C4505: unreferenced function with internal linkage has been removed
	# add_compile_options(/wd) # 
    # Volition code relies on char being signed and on MSVC's x86 struct packing; keep the defaults.
endif()

add_subdirectory(external)
add_subdirectory(src)
# OpenRFG (VOM): the selectable rendering and physics backends (src/backends/backends.h).
add_subdirectory(src/backends)

# The stand-ins OpenRFG shares with the test programs (src/stubs, new code): the OPENRFG_STUBS_* source lists by
# scope. Their header (stubs.h) is found from every test directory.
set(OPENRFG_STUBS ${OPENRFG_SRC}/stubs)
include(${OPENRFG_STUBS}/stubs.cmake)
include_directories(${OPENRFG_STUBS})

add_subdirectory(src/openrfg)

# The test programs' own stand-ins (tests/stubs, stub_impl.md S1): the OPENRFG_TEST_STUBS_* lists by scope and their
# header (test_stubs.h). OpenRFG links none of them.
option(OPENRFG_BUILD_TESTS "Build the test programs in tests/ (headless, hkdiff)" OFF)
if(OPENRFG_BUILD_TESTS)
    set(OPENRFG_TEST_STUBS ${CMAKE_SOURCE_DIR}/tests/stubs)
    include(${OPENRFG_TEST_STUBS}/stubs.cmake)
    include_directories(${OPENRFG_TEST_STUBS})
    add_subdirectory(tests/headless)
    add_subdirectory(tests/havok)
endif()

# The platform DLLs (stub_impl.md S2, 4.1): every program that links one of the imported platform_* targets
# (src/CMakeLists.txt) gets the DLLs it imports copied next to it after the link, as rfg.exe's install has them.
function(openrfg_copy_platform_dlls dir)
    get_property(targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target ${targets})
        get_target_property(type ${target} TYPE)
        if(type STREQUAL "EXECUTABLE")
            # A target of its own (a POST_BUILD command can only be added in the program's own directory), built with
            # every build after the program.
            add_custom_target(${target}_platform_dlls ALL
                COMMAND ${CMAKE_COMMAND} "-DDLLS=$<JOIN:$<TARGET_RUNTIME_DLLS:${target}>,|>" -DSTEAM_API=${OPENRFG_STEAM_API_DLL}
                    -DDEST=$<TARGET_FILE_DIR:${target}> -P ${CMAKE_SOURCE_DIR}/cmake/copy_platform_dlls.cmake
                VERBATIM)
            add_dependencies(${target}_platform_dlls ${target})
        endif()
    endforeach()
    get_property(subdirs DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
    foreach(subdir ${subdirs})
        openrfg_copy_platform_dlls(${subdir})
    endforeach()
endfunction()

openrfg_copy_platform_dlls(${CMAKE_SOURCE_DIR})
