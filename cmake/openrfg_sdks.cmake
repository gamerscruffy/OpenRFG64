# OpenRFG64: where the NVIDIA SDKs are (DLSS, Streamline, a PhysX build). They are not part of the repository and
# normally sit next to it, in OpenRFG-main's parent folder, so syncs of the project folder never pick them up:
#   ../DLSS-<version>/DLSS-<version>/      the DLSS SDK (include/, lib/Windows_x86_64/)
#   ../streamline-sdk-v<version>/           the Streamline SDK (include/, bin/x64/)
#   ../physx-5.12/physx-build/release-gpu/install/   PhysX built by external/rfgphys/tools/build_physx.cmd
# A copy inside the tree (external/rfgrt/third_party/dlss, .../streamline, the mod's fetch scripts) is used first.
# Each cache variable below overrides the search; empty means search. The results are the *_RESOLVED variables
# (empty when not found), read by src/backends and external/rfgrt.

set(OPENRFG_DLSS_SDK_DIR "" CACHE PATH "NVIDIA DLSS SDK folder (has include/nvsdk_ngx.h); empty: search the tree, then OpenRFG-main's parent")
set(OPENRFG_STREAMLINE_SDK_DIR "" CACHE PATH "NVIDIA Streamline SDK folder (has include/sl.h); empty: search the tree, then OpenRFG-main's parent")
set(OPENRFG_PHYSX_INSTALL "" CACHE PATH "The PhysX SDK built with rfgphys's patch (its release-gpu/install folder); empty: search OpenRFG-main's parent")

get_filename_component(_openrfg_sdk_parent "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)

# _openrfg_find_sdk(<out> <explicit> <marker> <candidate>...): the explicit folder, else the first candidate that has
# <marker>. A candidate may be a glob; its matches are tried newest version first, each also one level down (the DLSS
# archive unpacks to DLSS-x/DLSS-x).
function(_openrfg_find_sdk out explicit marker)
    set(result "")
    if(explicit)
        if(EXISTS "${explicit}/${marker}")
            set(result "${explicit}")
        else()
            message(WARNING "OpenRFG: ${explicit} has no ${marker}")
        endif()
    else()
        foreach(pattern IN LISTS ARGN)
            # CONFIGURE_DEPENDS: a build reconfigures when an SDK appears (the PhysX build lands after the first configure).
            file(GLOB matches LIST_DIRECTORIES true CONFIGURE_DEPENDS "${pattern}")
            list(SORT matches COMPARE NATURAL ORDER DESCENDING)
            foreach(dir IN LISTS matches)
                get_filename_component(name "${dir}" NAME)
                foreach(candidate "${dir}" "${dir}/${name}")
                    if(NOT result AND EXISTS "${candidate}/${marker}")
                        set(result "${candidate}")
                    endif()
                endforeach()
            endforeach()
        endforeach()
    endif()
    set(${out} "${result}" PARENT_SCOPE)
endfunction()

_openrfg_find_sdk(OPENRFG_DLSS_SDK_RESOLVED "${OPENRFG_DLSS_SDK_DIR}" include/nvsdk_ngx.h
    "${CMAKE_SOURCE_DIR}/external/rfgrt/third_party/dlss"
    "${_openrfg_sdk_parent}/DLSS-*")
_openrfg_find_sdk(OPENRFG_STREAMLINE_SDK_RESOLVED "${OPENRFG_STREAMLINE_SDK_DIR}" include/sl.h
    "${CMAKE_SOURCE_DIR}/external/rfgrt/third_party/streamline"
    "${_openrfg_sdk_parent}/streamline-sdk-*")
_openrfg_find_sdk(OPENRFG_PHYSX_INSTALL_RESOLVED "${OPENRFG_PHYSX_INSTALL}" lib/PhysX_static_64.lib
    "${CMAKE_SOURCE_DIR}/external/rfgphys/third_party/physx-build/release-gpu/install"
    "${_openrfg_sdk_parent}/physx-*/physx-build/release-gpu/install")

if(OPENRFG_X64)
    foreach(_sdk DLSS_SDK STREAMLINE_SDK PHYSX_INSTALL)
        if(OPENRFG_${_sdk}_RESOLVED)
            message(STATUS "OpenRFG: ${_sdk}: ${OPENRFG_${_sdk}_RESOLVED}")
        else()
            message(STATUS "OpenRFG: ${_sdk}: not found")
        endif()
    endforeach()
endif()
