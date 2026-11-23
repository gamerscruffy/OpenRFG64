// OpenRFG (VOM, new code): the backend settings, what runs, and the change between them (backends.h).

#include "backends/backends.h"

#include "keen/base/inifile.hpp"

#if defined(OPENRFG_BACKEND_PHYSX)
#include "backends/physx/physx_backend.h"
#endif

#if defined(OPENRFG_X64)
#include "volition/vlib/code/util/endian_type.h"
#endif

#include <windows.h>

#include <stdio.h>
#include <string.h>

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

// The width of the world's object indices (OPENRFG_OBJECT_INDEX: 32-bit on x64, where rfg.exe's are 16-bit and cap the
// live objects at 65,535). rfgphys reads the world's index lists with it (rfg::object_index, GetProcAddress).
extern "C" __declspec(dllexport) const int openrfg_object_index_size = (int)sizeof(OPENRFG_OBJECT_INDEX);
#endif

namespace
{
    openrfg_backend_settings g_settings;

    // What runs, as last applied (-1: nothing applied yet).
    int g_applied_renderer = -1;
    int g_applied_physics = -1;
    int g_applied_keep_debris = -1;

    // Keep debris as chosen (the choice resolved against the hardware): it applies while PhysX runs. Set where waiting
    // for the hardware check is fine (the main menu, the install thread); read by the game's code every frame.
    volatile bool g_keep_debris_wanted = false;

#if defined(OPENRFG_BACKEND_PHYSX)
    // rfgphys installs once, on a worker thread, never on the game's: its install reads OpenRFG.pdb for seconds, and a
    // game thread held that long is a hang (the watchdog reports one after 6 s). It installs its hooks and modules, and
    // with PHYSX chosen it starts the PhysX core; rfgphys attaches whenever it is ready (its world mirror arms at
    // gameplay, and the debris offload takes the pieces made from then on). After that the core starts and stops on the
    // game's simulation thread at the main menu (physx_reconcile), where no level is loaded.
    // g_install_result: 0 not done, 1 installed, 2 failed (written by the install thread, read from any thread).
    HANDLE g_install_thread = nullptr;
    volatile LONG g_install_result = 0;
    bool g_install_begun = false;

    DWORD WINAPI physx_install_thread(void* launch)
    {
        const bool ok = openrfg_physx_install(launch != nullptr);
        if (ok)
        {
            g_keep_debris_wanted = openrfg_backends_keep_debris_resolved(g_settings.keep_debris);
        }
        InterlockedExchange(&g_install_result, ok ? 1 : 2);
        if (!ok)
        {
            openrfg_log("BACKENDS PhysX did not install: Havok only (backends rfgphys.log says why)");
        }
        else
        {
            openrfg_log(launch != nullptr ? "BACKENDS PhysX installed and starting (backends rfgphys.log has the details)" : "BACKENDS PhysX installed, not started (Havok chosen)");
        }
        return 0;
    }

    void physx_begin_install(bool launch)
    {
        if (g_install_begun)
        {
            return;
        }
        g_install_begun = true;
        g_install_thread = CreateThread(nullptr, 0, physx_install_thread, launch ? (void*)1 : nullptr, 0, nullptr);
        if (g_install_thread == nullptr)
        {
            InterlockedExchange(&g_install_result, 2);
        }
    }

    bool physx_installed()
    {
        return g_install_result == 1;
    }

    bool physx_installing()
    {
        return g_install_begun && g_install_result == 0;
    }

    int physx_state()
    {
        return physx_installed() ? openrfg_physx_state() : OPENRFG_PHYSX_OFF;
    }

    // PhysX steps the debris, or is coming up to.
    bool physx_active()
    {
        const int state = physx_state();
        return state == OPENRFG_PHYSX_STARTING || state == OPENRFG_PHYSX_RUNNING;
    }

    // The unattended tests run rfgphys's record/replay harness, which installs with rfgphys: with Havok chosen too.
    bool harness_requested()
    {
        char mode[32];
        const DWORD n = GetEnvironmentVariableA("RFGPHYS_HARNESS_MODE", mode, sizeof(mode));
        return n > 0 && n < sizeof(mode) && _stricmp(mode, "off") != 0 && strcmp(mode, "0") != 0;
    }
#endif

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
#if defined(OPENRFG_BACKEND_PHYSX)
        if (physics == OPENRFG_PHYSICS_PHYSX)
        {
            if (g_install_result == 2)
            {
                return "PhysX did not install (backends rfgphys.log says why)";
            }
            const int state = physx_state();
            if (state == OPENRFG_PHYSX_FAILED || state == OPENRFG_PHYSX_LOST)
            {
                return "PhysX stopped working (backends rfgphys.log says why); it starts again at the main menu";
            }
        }
#endif
        return nullptr;
    }

#if defined(OPENRFG_BACKEND_PHYSX)
    // Brings the PhysX core in line with the choice (simulation thread, no level loaded). retry: also starts again one
    // that failed or was lost (on entering the main menu and on Accept; each main menu frame only takes a finished
    // install and a changed choice, so a core that fails at once is not started every frame).
    void physx_reconcile(bool retry)
    {
        if (g_install_thread != nullptr && g_install_result != 0)
        {
            CloseHandle(g_install_thread);
            g_install_thread = nullptr;
        }
        const bool want = g_settings.physics == OPENRFG_PHYSICS_PHYSX && openrfg_backends_physics_supported(OPENRFG_PHYSICS_PHYSX, nullptr) && physics_built(OPENRFG_PHYSICS_PHYSX);
        if (!g_install_begun)
        {
            if (want)
            {
                physx_begin_install(true);
            }
            return;
        }
        if (!physx_installed())
        {
            return; // installing (it started the core if PHYSX was chosen then; settled here once it is done) or failed
        }
        const int state = openrfg_physx_state();
        if (want)
        {
            // A start asked for while the last core stops follows once it has ended (pxbridge::start).
            if (state == OPENRFG_PHYSX_OFF || (retry && (state == OPENRFG_PHYSX_STOPPING || state == OPENRFG_PHYSX_FAILED || state == OPENRFG_PHYSX_LOST)))
            {
                const bool ok = openrfg_physx_launch();
                openrfg_log(!ok ? "BACKENDS PhysX cannot start now (backends rfgphys.log says why)"
                                : (state == OPENRFG_PHYSX_STOPPING ? "BACKENDS PhysX starts again once it has stopped" : "BACKENDS PhysX starting"));
            }
        }
        else if (state == OPENRFG_PHYSX_STARTING || state == OPENRFG_PHYSX_RUNNING)
        {
            openrfg_physx_stop();
            openrfg_log("BACKENDS PhysX stopping (Havok chosen): the core releases PhysX");
        }
    }
#endif
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

bool openrfg_backends_keep_debris_now()
{
#if defined(OPENRFG_BACKEND_PHYSX)
    // Live: off as soon as PhysX stops or is lost (the game's own limits on debris apply again).
    return g_keep_debris_wanted && physx_active();
#else
    return false;
#endif
}

bool openrfg_backends_effective_keep_debris()
{
    return openrfg_backends_effective_physics() == OPENRFG_PHYSICS_PHYSX && openrfg_backends_keep_debris_resolved(g_settings.keep_debris);
}

void openrfg_backends_start_early()
{
#if defined(OPENRFG_BACKEND_PHYSX)
    if (!openrfg_backends_enabled())
    {
        return;
    }
    if (openrfg_backends_effective_physics() == OPENRFG_PHYSICS_PHYSX)
    {
        physx_begin_install(true);
    }
    else if (harness_requested())
    {
        physx_begin_install(false);
    }
#endif
}

void openrfg_backends_main_menu_frame()
{
#if defined(OPENRFG_BACKEND_PHYSX)
    if (openrfg_backends_enabled())
    {
        physx_reconcile(false);
    }
#endif
}

void openrfg_backends_apply()
{
    if (!openrfg_backends_enabled())
    {
        return;
    }
    g_keep_debris_wanted = openrfg_backends_keep_debris_resolved(g_settings.keep_debris);
#if defined(OPENRFG_BACKEND_PHYSX)
    // The PhysX core starts or stops (an install not begun yet begins on its worker thread); nothing here waits.
    physx_reconcile(true);
    const int px = physx_state();
    const char* physx_note = physx_installing() || px == OPENRFG_PHYSX_STARTING ? " (starting)" : (px == OPENRFG_PHYSX_STOPPING ? " (PhysX stopping)" : "");
#else
    const char* physx_note = "";
#endif
    const int renderer = openrfg_backends_effective_renderer();
    const int physics = openrfg_backends_effective_physics();
    const int keep_debris = openrfg_backends_effective_keep_debris() ? 1 : 0;
    if (renderer == g_applied_renderer && physics == g_applied_physics && keep_debris == g_applied_keep_debris)
    {
        return;
    }
    // Here the backends that no longer run stop and the chosen ones start (no level is loaded): PhysX above; the path
    // tracer is not built in yet.
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
        snprintf(physics_note, sizeof(physics_note), " (chosen: %s; %s)", physics_name(g_settings.physics), why != nullptr ? why : "?");
    }
    const char* keep_choice = g_settings.keep_debris == OPENRFG_KEEP_DEBRIS_AUTO ? "automatic" : (g_settings.keep_debris == OPENRFG_KEEP_DEBRIS_ON ? "on" : "off");
    char msg[768];
    snprintf(msg, sizeof(msg), "BACKENDS renderer %s%s, physics %s%s%s, keep debris %s (chosen: %s)", renderer_name(renderer), renderer_note, physics_name(physics), physx_note, physics_note, keep_debris ? "on" : "off", keep_choice);
    openrfg_log(msg);
}
