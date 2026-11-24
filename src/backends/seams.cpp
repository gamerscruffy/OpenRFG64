// OpenRFG (VOM, new code): the seams' registry (seams.h).

#include "backends/seams.h"

#include <string.h>

thread_local bool openrfg_seam_skip = false;
thread_local const void* openrfg_seam_caller = nullptr;

namespace
{
    // Constant-initialized: the seams register themselves while the game's files are initialized, in any order.
    openrfg_seam* g_seams = nullptr;
}

void openrfg_seam_register(openrfg_seam* seam)
{
    seam->next = g_seams;
    g_seams = seam;
}

openrfg_seam* openrfg_seam_find(const char* name, const char* signature)
{
    for (openrfg_seam* s = g_seams; s != nullptr; s = s->next)
    {
        if (strcmp(s->name, name) == 0 && (s->signature == nullptr || (signature != nullptr && strcmp(s->signature, signature) == 0)))
        {
            return s;
        }
    }
    return nullptr;
}

void openrfg_seam_attach(openrfg_seam* seam, void* handler, void** original)
{
    *original = seam->attached != nullptr ? seam->attached : seam->original;
    seam->attached = handler;
}

void openrfg_seams_enable()
{
    for (openrfg_seam* s = g_seams; s != nullptr; s = s->next)
    {
        s->handler = s->attached;
    }
}
