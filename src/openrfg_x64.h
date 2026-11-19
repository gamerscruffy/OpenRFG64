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
