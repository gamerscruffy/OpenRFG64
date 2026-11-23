// OpenRFG (VOM, new code): the backend settings, what runs, and the change between them (backends.h).

#include "backends/backends.h"

#include "keen/base/inifile.hpp"

#include <stdio.h>

void openrfg_log(const char* text); // crash_early.cpp

namespace
{
    openrfg_backend_settings g_settings;

    // What runs, as last applied (-1: nothing applied yet).
    int g_applied_renderer = -1;
    int g_applied_physics = -1;
    int g_applied_keep_debris = -1;

    // Built into this program. The path tracer and PhysX come in later steps; until then a choice of them falls back.
    bool renderer_built(int renderer)
    {
        return renderer == OPENRFG_RENDERER_STANDARD;
    }

    bool physics_built(int physics)
    {
        return physics == OPENRFG_PHYSICS_HAVOK;
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
        return physics_built(physics) ? nullptr : "not built into this program yet";
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
        snprintf(renderer_note, sizeof(renderer_note), " (chosen: %s; %s)", renderer_name(g_settings.renderer), renderer_fallback_reason(g_settings.renderer));
    }
    char physics_note[256] = {};
    if (physics != g_settings.physics)
    {
        snprintf(physics_note, sizeof(physics_note), " (chosen: %s; %s)", physics_name(g_settings.physics), physics_fallback_reason(g_settings.physics));
    }
    const char* keep_choice = g_settings.keep_debris == OPENRFG_KEEP_DEBRIS_AUTO ? "automatic" : (g_settings.keep_debris == OPENRFG_KEEP_DEBRIS_ON ? "on" : "off");
    char msg[768];
    snprintf(msg, sizeof(msg), "BACKENDS renderer %s%s, physics %s%s, keep debris %s (chosen: %s)", renderer_name(renderer), renderer_note, physics_name(physics), physics_note, keep_debris ? "on" : "off", keep_choice);
    openrfg_log(msg);
}
