// Force-included into every C++ file (root CMakeLists.txt). The original layouts were recovered from the 32-bit
// rfg.exe, so "sizeof(X) == N" checks cannot hold on a 64-bit build. In 64-bit they are skipped (the port is
// experimental); in 32-bit they are the normal static_assert.
#pragma once
#ifdef __cplusplus
#ifdef OPENRFG_X64
#define OPENRFG_SIZE_ASSERT(...)
#else
#define OPENRFG_SIZE_ASSERT(...) static_assert(__VA_ARGS__)
#endif
#endif

// OpenRFG: zones loaded at once, and the scale of the pools of objects placed in zones. The terrain layer streams 3x3
// zones (511 m cells); on x64 the Terrain Distance option streams up to 5x5, so the zone, terrain and placed-object
// pools are sized for 25 zones (x3, the original pools are sized for 9).
#ifdef OPENRFG_X64
#define OPENRFG_MAX_ZONES 25
#define OPENRFG_ZONE_POOL(n) ((n) * 3)
#else
#define OPENRFG_MAX_ZONES 9
#define OPENRFG_ZONE_POOL(n) (n)
#endif
