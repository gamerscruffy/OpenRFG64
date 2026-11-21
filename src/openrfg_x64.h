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
// zones (511 m cells); on x64 the Terrain Distance option streams up to 15x15 (the 16x16 world almost whole), so the
// zone, terrain and placed-object pools are sized for 225 zones (x25, the original pools are sized for 9).
// OPENRFG_ZONE_LIST scales the local collection lists (nearby roads, action nodes) less: they hold what is near a
// point, live on the stack, and stop adding when full.
// OPENRFG_ZONE_HASH_INDEX is the index type of the zone-sized hash tables (one byte holds at most 255 entries).
// OPENRFG_OBJECT_INDEX is the type of the world's live-object indices (object::all_index, world::type_objects): 16 bits
// in the original, which caps the live objects at 65535; that many zones of objects can pass it.
#ifdef OPENRFG_X64
#define OPENRFG_MAX_ZONES 225
// Capped at 65000: object_pool links its entries with 16-bit indices (index_link).
#define OPENRFG_ZONE_POOL(n) ((n) * 25 > 65000 ? 65000 : (n) * 25)
#define OPENRFG_ZONE_LIST(n) ((n) * 3)
// render_lib factory pools (pool_list, no 16-bit links): x50, twice the zone ratio, since a full 15x15 load already
// needed every slot of x25 and the terrain and roads of the outer zones went missing.
#define OPENRFG_RL_POOL(n) ((n) * 50)
#define OPENRFG_ZONE_HASH_INDEX unsigned short
#define OPENRFG_OBJECT_INDEX unsigned int
#else
#define OPENRFG_MAX_ZONES 9
#define OPENRFG_ZONE_POOL(n) (n)
#define OPENRFG_ZONE_LIST(n) (n)
#define OPENRFG_RL_POOL(n) (n)
#define OPENRFG_ZONE_HASH_INDEX unsigned char
#define OPENRFG_OBJECT_INDEX unsigned short
#endif
