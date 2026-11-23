// OpenRFG (VOM, new code): the backend settings, what runs, and the change between them (backends.h).

#include "backends/backends.h"

#include "keen/base/inifile.hpp"

#if defined(OPENRFG_BACKEND_PHYSX)
#include "backends/physx/physx_backend.h"
#endif

#if defined(OPENRFG_X64)
#include "volition/vlib/code/util/endian_type.h"
#endif

#include <stdio.h>

void openrfg_log(const char* text); // crash_early.cpp

#if defined(OPENRFG_X64)
// The target of a self-relative 32-bit offset field of the game's data (et_ptr_offset). On x64 such a field may hold an
// index into the far table (a target more than 2 GB away, endian_type.inl), which code outside the game's own cannot
// read: rfgphys (the PhysX backend, and the plugin in OpenRFG64) finds this export with GetProcAddress on the program.
// It lives in this file because the program always links it (an export alone would be dropped).
extern "C" __declspec(dllexport) const void* openrfg_et_ptr_target(const void* field)
{
    const int offset = *static_cast<const int*>(field);
    if (offset >= et_far::Base)
    {
        return et_far::far_target(offset);
    }
    return static_cast<const char*>(field) + offset;
}
#endif

namespace
{
    openrfg_backend_settings g_settings;

    // What runs, as last applied (-1: nothing applied yet).
    int g_applied_renderer = -1;
    int g_applied_physics = -1;
    int g_applied_keep_debris = -1;

    // PhysX started (it keeps running until the game ends: stopping it is a later step), or it failed to start.
    bool g_physx_running = false;
    bool g_physx_failed = false;

    // Built into this program. The path tracer comes in a later step; until then a choice of it falls back.
    bool renderer_built(int renderer)
    {
        return renderer == OPENRFG_RENDERER_STANDARD;
    }

    bool physics_built(int physics)
    {
#if defined(OPENRFG_BACKEND_PHYSX)
        return true;
#else
        return physics == OPENRFG_PHYSICS_HAVOK;
#endif
    }

    int clamp_choice(int value, int count, int fallback)
    {
        return (value < 0 || value >= count) ? fallback : value;
    }

    const char* renderer_name(int renderer)
    {
        return renderer == OPENRFG_RENDERER_PATH_TRACED ? "path traced" : "standard";
    }

    const char* physics_name(int physics)
    {
        return physics == OPENRFG_PHYSICS_PHYSX ? "PhysX" : "Havok";
    }

    // Why a choice does not run: the machine cannot, or this program does not have it yet.
    const char* renderer_fallback_reason(int renderer)
    {
        const char* reason = nullptr;
        if (!openrfg_backends_renderer_supported(renderer, &reason))
        {
            return reason;
        }
        return renderer_built(renderer) ? nullptr : "not built into this program yet";
    }

    const char* physics_fallback_reason(int physics)
    {
        const char* reason = nullptr;
        if (!openrfg_backends_physics_supported(physics, &reason))
        {
            return reason;
        }
        if (!physics_built(physics))
        {
            return "not built into this program (no PhysX SDK given to CMake)";
        }
        return physics == OPENRFG_PHYSICS_PHYSX && g_physx_failed ? "PhysX did not start (backends rfgphys.log says why)" : nullptr;
    }
}

bool openrfg_backends_enabled()
{
#if defined(OPENRFG_BACKENDS)
    return true;
#else
    return false;
#endif
}

openrfg_backend_settings openrfg_backends_get_settings()
{
    return g_settings;
}

void openrfg_backends_set_settings(const openrfg_backend_settings& settings)
{
    g_settings.renderer = clamp_choice(settings.renderer, OPENRFG_RENDERER_COUNT, OPENRFG_RENDERER_STANDARD);
    g_settings.physics = clamp_choice(settings.physics, OPENRFG_PHYSICS_COUNT, OPENRFG_PHYSICS_PHYSX);
    g_settings.keep_debris = (settings.keep_debris < OPENRFG_KEEP_DEBRIS_AUTO || settings.keep_debris > OPENRFG_KEEP_DEBRIS_ON) ? OPENRFG_KEEP_DEBRIS_AUTO : settings.keep_debris;
}

openrfg_backend_settings openrfg_backends_default_settings()
{
    return openrfg_backend_settings();
}

void openrfg_backends_read_ini(const keen::FilePath& path)
{
    openrfg_backend_settings settings;
    settings.renderer = keen::readIniFileInt(path, "graphics", "renderer", g_settings.renderer);
    settings.physics = keen::readIniFileInt(path, "graphics", "physics", g_settings.physics);
    settings.keep_debris = keen::readIniFileInt(path, "graphics", "keep_debris", g_settings.keep_debris);
    openrfg_backends_set_settings(settings);
}

void openrfg_backends_write_ini(const keen::FilePath& path)
{
    keen::writeIniFileInt(path, "graphics", "renderer", g_settings.renderer);
    keen::writeIniFileInt(path, "graphics", "physics", g_settings.physics);
    keen::writeIniFileInt(path, "graphics", "keep_debris", g_settings.keep_debris);
}

bool openrfg_backends_renderer_supported(int renderer, const char** reason)
{
    if (renderer == OPENRFG_RENDERER_STANDARD)
    {
        return true;
    }
    if (!openrfg_backends_enabled())
    {
        if (reason != nullptr)
        {
            *reason = "the 32-bit build has no backends";
        }
        return false;
    }
    const openrfg_backend_probe& probe = openrfg_backends_probe();
    if (!probe.path_tracing)
    {
        if (reason != nullptr)
        {
            *reason = probe.done ? probe.path_tracing_reason : "the hardware check did not run";
        }
        return false;
    }
    return true;
}

bool openrfg_backends_physics_supported(int physics, const char** reason)
{
    if (physics == OPENRFG_PHYSICS_HAVOK)
    {
        return true;
    }
    if (!openrfg_backends_enabled())
    {
        if (reason != nullptr)
        {
            *reason = "the 32-bit build has no backends";
        }
        return false;
    }
    // PhysX runs on the CPU where the GPU cannot.
    return true;
}

bool openrfg_backends_keep_debris_default()
{
    return openrfg_backends_enabled() && openrfg_backends_probe().physx_gpu;
}

bool openrfg_backends_keep_debris_resolved(int keep_debris)
{
    if (keep_debris == OPENRFG_KEEP_DEBRIS_AUTO)
    {
        return openrfg_backends_keep_debris_default();
    }
    return keep_debris == OPENRFG_KEEP_DEBRIS_ON;
}

int openrfg_backends_effective_renderer()
{
    return renderer_fallback_reason(g_settings.renderer) == nullptr ? g_settings.renderer : OPENRFG_RENDERER_STANDARD;
}

int openrfg_backends_effective_physics()
{
    // A running PhysX cannot be stopped yet: it runs until the game ends, whatever the choice since.
    if (g_physx_running)
    {
        return OPENRFG_PHYSICS_PHYSX;
    }
    return physics_fallback_reason(g_settings.physics) == nullptr ? g_settings.physics : OPENRFG_PHYSICS_HAVOK;
}

bool openrfg_backends_effective_keep_debris()
{
    return openrfg_backends_effective_physics() == OPENRFG_PHYSICS_PHYSX && openrfg_backends_keep_debris_resolved(g_settings.keep_debris);
}

void openrfg_backends_apply()
{
    if (!openrfg_backends_enabled())
    {
        return;
    }
#if defined(OPENRFG_BACKEND_PHYSX)
    // PhysX starts here, where no level is loaded (at boot, or later from the options).
    if (openrfg_backends_effective_physics() == OPENRFG_PHYSICS_PHYSX && !g_physx_running && !g_physx_failed)
    {
        if (openrfg_physx_start())
        {
            g_physx_running = true;
        }
        else
        {
            g_physx_failed = true;
        }
    }
#endif
    const int renderer = openrfg_backends_effective_renderer();
    const int physics = openrfg_backends_effective_physics();
    const int keep_debris = openrfg_backends_effective_keep_debris() ? 1 : 0;
    if (renderer == g_applied_renderer && physics == g_applied_physics && keep_debris == g_applied_keep_debris)
    {
        return;
    }
    // Here the backends that no longer run stop and the chosen ones start (no level is loaded). None is built in yet.
    g_applied_renderer = renderer;
    g_applied_physics = physics;
    g_applied_keep_debris = keep_debris;

    char renderer_note[256] = {};
    if (renderer != g_settings.renderer)
    {
        const char* why = renderer_fallback_reason(g_settings.renderer);
        snprintf(renderer_note, sizeof(renderer_note), " (chosen: %s; %s)", renderer_name(g_settings.renderer), why != nullptr ? why : "?");
    }
    char physics_note[256] = {};
    if (physics != g_settings.physics)
    {
        const char* why = physics_fallback_reason(g_settings.physics);
        if (why == nullptr && g_physx_running)
        {
            why = "PhysX runs until the game restarts (stopping it comes later)";
        }
        snprintf(physics_note, sizeof(physics_note), " (chosen: %s; %s)", physics_name(g_settings.physics), why != nullptr ? why : "?");
    }
    const char* keep_choice = g_settings.keep_debris == OPENRFG_KEEP_DEBRIS_AUTO ? "automatic" : (g_settings.keep_debris == OPENRFG_KEEP_DEBRIS_ON ? "on" : "off");
    char msg[768];
    snprintf(msg, sizeof(msg), "BACKENDS renderer %s%s, physics %s%s, keep debris %s (chosen: %s)", renderer_name(renderer), renderer_note, physics_name(physics), physics_note, keep_debris ? "on" : "off", keep_choice);
    openrfg_log(msg);
}
