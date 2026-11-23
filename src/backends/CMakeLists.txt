# OpenRFG (VOM, new code): the selectable backends (backends.h): the settings, the hardware probe, what runs.
# The game reaches them from its options (rfg, rfg_ui) and its start-up (rfg_app), all of which link rfg.
add_library(openrfg_backends STATIC
    backends.cpp
    backends_probe.cpp)
target_link_libraries(openrfg_backends PUBLIC keen_base)
# Only the 64-bit build has backends (the 32-bit build keeps the settings code, without rows or probe).
if(OPENRFG_X64)
    target_compile_definitions(openrfg_backends PRIVATE OPENRFG_BACKENDS=1)
endif()
target_link_libraries(rfg PUBLIC openrfg_backends)
