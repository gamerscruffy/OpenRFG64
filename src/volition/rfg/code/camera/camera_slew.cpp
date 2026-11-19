// Original: volition/rfg/code/camera/camera_slew.cpp
//
// The slew camera mode (stub_impl.md S5): a debug camera its controls move (control_debug_slew); the mode only sets
// the fov. Compiled in rfg_camera_unity.cpp.

#include "volition/rfg/code/camera/camera_slew.h"

#include "volition/rfg/code/camera/rfg_camera.h"
#include "volition/rfg/code/gamewide/game_trace.h"

static float Camera_slew_fov = 45.0f; // 0x016488D8

// 0x006C7040 (:80)
void camera_slew_mode_init()
{
    Rfg_camera.ideal_fov = Camera_slew_fov;
}

// 0x006C7060 (:90)
void camera_slew_set_fov(float fov_deg)
{
    Camera_slew_fov = fov_deg;
    if (Rfg_camera.mode == CAMERA_SLEW_MODE || Camera_third_person_lock)
    {
        Rfg_camera.ideal_fov = fov_deg;
    }
}

// 0x006C7090 (:99)
// Empty in rfg.exe: the slew controls move the camera directly.
extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int vKey);

extern int Mouse_dx;
extern int Mouse_dy;

// OpenRFG debug free camera (F10): mouse = look, arrows = move/strafe, PageUp/PageDown = up/down, Shift = 5x faster.
void camera_slew_process(float step_time, bool last_step_of_frame)
{
    #define SLEWK(vk) ((GetAsyncKeyState(vk) & 0x8000) != 0)
    const float speed = SLEWK(0x10) ? 100.0f : 20.0f; // m/s
    const float d = speed * step_time;
    const float sens = 0.003f; // rad per mouse pixel
    Rfg_camera.ideal_orient.rotate_around_local_y((float)Mouse_dx * sens);
    Rfg_camera.ideal_orient.rotate_around_local_x((float)Mouse_dy * sens);
    Rfg_camera.ideal_orient.orthogonalize();
    const vector f = Rfg_camera.ideal_orient.fvec;
    const vector r = Rfg_camera.ideal_orient.rvec;
    float mx = 0.0f, my = 0.0f, mz = 0.0f;
    if (SLEWK(0x57)) { mx += f.x * d; my += f.y * d; mz += f.z * d; } // W
    if (SLEWK(0x53)) { mx -= f.x * d; my -= f.y * d; mz -= f.z * d; } // S
    if (SLEWK(0x44)) { mx += r.x * d; my += r.y * d; mz += r.z * d; } // D
    if (SLEWK(0x41)) { mx -= r.x * d; my -= r.y * d; mz -= r.z * d; } // A
    if (SLEWK(0x21) || SLEWK(0x45)) my += d; // E or page up
    if (SLEWK(0x22) || SLEWK(0x51)) my -= d; // Q or page down
    #undef SLEWK
    Rfg_camera.ideal_pos.x += mx;
    Rfg_camera.ideal_pos.y += my;
    Rfg_camera.ideal_pos.z += mz;
    // No smoothing lag: the smoothing step blends from last_* to ideal_*.
    Rfg_camera.last_pos = Rfg_camera.ideal_pos;
    Rfg_camera.last_orient = Rfg_camera.ideal_orient;
    Rfg_camera.real_pos = Rfg_camera.ideal_pos;
    Rfg_camera.real_orient = Rfg_camera.ideal_orient;
    { static int n = 0; if ((n++ % 120) == 0) { GAME_TRACE("debug", "slew pos=(%.1f %.1f %.1f) mouse=%d,%d", Rfg_camera.ideal_pos.x, Rfg_camera.ideal_pos.y, Rfg_camera.ideal_pos.z, Mouse_dx, Mouse_dy); } }
}
