// OpenRFG (VOM, new code): the selectable backends.
//
// Three settings on the display options' general page, saved in config.ini [graphics]:
// - RENDERER (renderer): STANDARD, the game's own Direct3D 11 renderer, or PATH TRACED, the game's renderer with
//   path-traced lighting (rfgrt's Direct3D 12 core in the game's process).
// - PHYSICS (physics): HAVOK, the game's physics, or PHYSX, the debris in PhysX (rfgphys, in the game's process).
//   PhysX is the default.
// - KEEP DEBRIS (keep_debris): with PHYSX, the debris stays (no fades, no load-balance deletions). -1 is automatic:
//   on with a GPU that runs PhysX, off without one (the CPU could not carry every piece of a demolition).
//
// The settings are the user's choice; what runs is the effective backend. A choice this program or this machine
// cannot run falls back (path traced to standard, PhysX to Havok), and the log says why (BACKENDS lines). The options
// menu changes them only from the main menu, where no level is loaded, and openrfg_backends_apply makes the change at
// once, without a restart. The 32-bit build has no backends (openrfg_backends_enabled is false, the rows are hidden).
//
// This first step has the settings, the hardware probe and the menu rows; no backend is built in yet, so STANDARD and
// HAVOK run whatever the choice (the log says so).
#pragma once

struct ID3D11Device;

namespace keen
{
    class FilePath;
}

enum openrfg_renderer : int
{
    OPENRFG_RENDERER_STANDARD = 0,
    OPENRFG_RENDERER_PATH_TRACED = 1,
    OPENRFG_RENDERER_COUNT = 2
};

enum openrfg_physics : int
{
    OPENRFG_PHYSICS_HAVOK = 0,
    OPENRFG_PHYSICS_PHYSX = 1,
    OPENRFG_PHYSICS_COUNT = 2
};

enum openrfg_keep_debris : int
{
    OPENRFG_KEEP_DEBRIS_AUTO = -1,
    OPENRFG_KEEP_DEBRIS_OFF = 0,
    OPENRFG_KEEP_DEBRIS_ON = 1
};

// The user's choices.
struct openrfg_backend_settings
{
    int renderer = OPENRFG_RENDERER_STANDARD;
    int physics = OPENRFG_PHYSICS_PHYSX;
    int keep_debris = OPENRFG_KEEP_DEBRIS_AUTO;
};

// What this machine can run, found once at start-up (openrfg_backends_start_probe).
struct openrfg_backend_probe
{
    bool done = false; // false: the probe did not run (the 32-bit build, or no device)
    char adapter[128] = {};
    // Path tracing needs Direct3D 11.4 (shared fences), Direct3D 12 at feature level 12_0 on the game's adapter, ray
    // tracing tier 1.1 (ray queries) and shader model 6.5.
    bool path_tracing = false;
    char path_tracing_reason[192] = {};
    // GPU PhysX needs an NVIDIA GPU of compute capability 7.5 or newer (the PhysX build's kernels); otherwise PhysX
    // runs on the CPU.
    bool physx_gpu = false;
    char physx_reason[192] = {};
};

// False in the 32-bit build: no backends, no rows.
bool openrfg_backends_enabled();

// The settings. openrfg_backends_set_settings clamps them; openrfg_backends_apply then makes them run.
openrfg_backend_settings openrfg_backends_get_settings();
void openrfg_backends_set_settings(const openrfg_backend_settings& settings);
openrfg_backend_settings openrfg_backends_default_settings();
// config.ini [graphics] renderer, physics, keep_debris (game_options.cpp's reader and writer).
void openrfg_backends_read_ini(const keen::FilePath& path);
void openrfg_backends_write_ini(const keen::FilePath& path);

// The hardware probe, on a worker thread (Application::initialize, once the game's device exists).
void openrfg_backends_start_probe(ID3D11Device* device);
// The probe's result; waits for it if it is still running.
const openrfg_backend_probe& openrfg_backends_probe();

// Whether this machine can run a choice; reason, when given, gets a short text when it cannot.
bool openrfg_backends_renderer_supported(int renderer, const char** reason = nullptr);
bool openrfg_backends_physics_supported(int physics, const char** reason = nullptr);
// KEEP DEBRIS: the automatic value (on with a GPU that runs PhysX), and a setting resolved with it.
bool openrfg_backends_keep_debris_default();
bool openrfg_backends_keep_debris_resolved(int keep_debris);

// What runs (or will, at the next openrfg_backends_apply).
int openrfg_backends_effective_renderer();
int openrfg_backends_effective_physics();
bool openrfg_backends_effective_keep_debris();

// Makes the settings run: stops what no longer runs, starts what does, and logs the result. Call only where no level
// is loaded: the main menu (main_menu_enter, the options' Accept).
void openrfg_backends_apply();
