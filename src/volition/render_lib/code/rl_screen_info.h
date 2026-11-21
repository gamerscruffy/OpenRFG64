// Original: volition/render_lib/code/rl_screen_info.h
//
// rl_screen_info, the screen and level-of-detail settings (resolution, antialiasing, LOD scales) render_lib renders
// with.
#pragma once

#include "keen/graphics/graphicssystem.hpp"
#include "volition/render_lib/code/rl_particle.h"
#include "volition/render_lib/code/rl_shadow_system.h"

#include <string.h>

enum rl_anisotropic_filtering_lod
{
    RL_AF_LOD_OFF = 0,
    RL_AF_LOD_LOW = 1,
    RL_AF_LOD_MED = 2,
    RL_AF_LOD_HIGH = 3,
    RL_NUM_AF_LODS = 4,
};

enum rl_anti_alias_level
{
    RLAA_NONE = 0,
    RLAA_2X = 1,
    RLAA_4X = 2,
    RLAA_8X = 3,
    RLAA_16X = 4,
    RL_NUM_AA_LEVELS = 5,
};

enum rl_antialias_lod
{
    RL_AA_LOD_OFF = 0,
    RL_AA_LOD_2X = 1,
    RL_AA_LOD_4X = 2,
    RL_AA_LOD_8X = 3,
    RL_AA_LOD_16X = 4,
    RL_AA_LOD_8XQ = 5,
    RL_AA_LOD_16XQ = 6,
    RL_NUM_AA_LODS = 7,
};

// size 0x1
struct new_rl_screen_info_settings
{
};

// size 0xA0
class rl_screen_info
{
public:
    rl_screen_info(); // 0x00459600 (.h:48)
    void set_screen_width(int width); // 0x004DC850 (.h:55)
    int get_screen_width() const; // 0x00458AE0 (.h:56)
    void set_screen_height(int height); // 0x004DC880 (.h:59)
    int get_screen_height() const; // 0x00458AF0 (.h:60)
    float get_aspect_ratio() const; // 0x004B6820 (.h:63)
    void set_window_mode(keen::graphics::WindowMode window_mode); // 0x004B6830 (.h:66)
    keen::graphics::WindowMode get_window_mode() const; // 0x00458B00 (.h:67)
    void set_refresh_rate(int num, int den); // 0x004B6840 (.h:70)
    void get_refresh_rate(int& num, int& den) const; // 0x00458B10 (.h:71)
    unsigned int get_monitor() const; // 0x007B9970 (.h:73)
    void set_monitor(unsigned int monitor); // 0x004B6860 (.h:74)
    bool get_vsync_enabled() const; // 0x00458B30 (.h:76)
    void set_vsync_enabled(bool enabled); // 0x004B6870 (.h:77)
private:
    void update_aspect_ratio(); // 0x00458B40 (.h:81)

    int m_current_width; // 0x00
    int m_current_height; // 0x04
    int m_rr_num; // 0x08
    int m_rr_den; // 0x0C
    float m_aspect_ratio; // 0x10
    unsigned int m_monitor; // 0x14
    keen::graphics::WindowMode m_window_mode; // 0x18
    bool m_vsync_enabled; // 0x1C
    new_rl_screen_info_settings m_new_info; // 0x1D
    unsigned char padding[127]; // 0x1E
};

// size 0x8
struct new_rl_level_of_detail_settings
{
    unsigned char m_texture_reduction; // 0x00
    rl_anisotropic_filtering_lod m_anisotropic_filtering; // 0x04
};

// size 0x94
struct rl_level_of_detail
{
    rl_level_of_detail(); // 0x00458B70 (.h:127)

    rl_shadow_lod m_shadow_lod; // 0x00
    rl_antialias_lod m_aa_lod; // 0x04
    rl_particle_lod m_particle_lod; // 0x08
    bool m_soft_shadows; // 0x0C
    bool m_bloom_enabled; // 0x0D
    bool m_motion_blur_enabled; // 0x0E
    bool m_depth_of_field_enabled; // 0x0F
    bool m_distortion_enabled; // 0x10
    bool m_ssao_enabled; // 0x11
    bool m_sun_shafts_enabled; // 0x12
    new_rl_level_of_detail_settings m_new_data; // 0x14
    // OpenRFG: SSAO quality 1 low (the original), 2 medium, 3 high, 4 very high; off is m_ssao_enabled false. Taken
    // from the first padding byte (layout unchanged). Read it through rl_lod_get_ssao_level.
    unsigned char m_ssao_quality; // 0x1C
    // OpenRFG: the Dynamic Lights level plus one (0 = never set, counts as medium), the second padding byte. Read it
    // through rl_lod_get_dynamic_lights_level.
    unsigned char m_dynamic_lights; // 0x1D
    // OpenRFG: the World Detail Distance level plus one (0 = never set, counts as normal), the third padding byte.
    // Read it through rl_lod_get_world_detail_level.
    unsigned char m_world_detail; // 0x1E
    // OpenRFG: the LOD Detail and Draw Distance levels plus one (0 = never set, counts as normal), the fourth and fifth
    // padding bytes. Read them through rl_lod_get_lod_detail_level / rl_lod_get_draw_distance_level.
    unsigned char m_lod_detail; // 0x1F
    unsigned char m_draw_distance; // 0x20
    // OpenRFG: the Terrain Distance level plus one (0 = never set, counts as normal), the sixth padding byte. Read it
    // through rl_lod_get_terrain_distance_level.
    unsigned char m_terrain_distance; // 0x21
    // OpenRFG: the Shadow Distance in steps of 25 % of the original (0 = never set, counts as 100 %), the seventh
    // padding byte. Read it through rl_lod_get_shadow_distance_percent.
    unsigned char m_shadow_distance; // 0x22
    unsigned char padding[113]; // 0x23
};

// OpenRFG: the SSAO level of the options menu: 0 off, 1 low (the original game's SSAO), 2 medium, 3 high, 4 very
// high. A quality byte out of range (old saved data) counts as low.
inline int rl_lod_get_ssao_level(const rl_level_of_detail& lod)
{
    if (!lod.m_ssao_enabled)
    {
        return 0;
    }
    const int quality = lod.m_ssao_quality;
    return (quality < 1 || quality > 4) ? 1 : quality;
}

inline void rl_lod_set_ssao_level(rl_level_of_detail& lod, int level)
{
    lod.m_ssao_enabled = level > 0;
    if (level > 0)
    {
        lod.m_ssao_quality = (unsigned char)(level > 4 ? 4 : level);
    }
}

// OpenRFG: the Dynamic Lights level of the options menu: 0 off, 1 low, 2 medium, 3 high, 4 very high (rl_dynamic_lights.h).
// An unset or out of range byte counts as medium.
inline int rl_lod_get_dynamic_lights_level(const rl_level_of_detail& lod)
{
    const int stored = lod.m_dynamic_lights;
    return (stored < 1 || stored > 5) ? 2 : stored - 1;
}

inline void rl_lod_set_dynamic_lights_level(rl_level_of_detail& lod, int level)
{
    if (level < 0 || level > 4)
    {
        level = 2;
    }
    lod.m_dynamic_lights = (unsigned char)(level + 1);
}

// OpenRFG: the number of levels of the World Detail Distance, LOD Detail, Draw Distance and Terrain Distance options
// (the menu lists hold one entry per level). The byte of each keeps level + 1, so 0 (unset) counts as the first level.
#define RL_WORLD_DETAIL_LEVELS 6
#define RL_LOD_DETAIL_LEVELS 7
#define RL_DRAW_DISTANCE_LEVELS 7
#define RL_TERRAIN_DISTANCE_LEVELS 7

inline int rl_lod_stored_level(unsigned char stored, int count)
{
    return (stored < 1 || stored > count) ? 0 : stored - 1;
}

inline unsigned char rl_lod_level_to_store(int level, int count)
{
    return (unsigned char)((level < 0 || level >= count ? 0 : level) + 1);
}

// OpenRFG: the World Detail Distance level of the options menu: how far buildings stream at full detail. 0 is the
// original 5x5 building cells, each level adds a cell on every side up to 15x15 (64-bit only;
// stream_world_detail_level_radius).
inline int rl_lod_get_world_detail_level(const rl_level_of_detail& lod)
{
    return rl_lod_stored_level(lod.m_world_detail, RL_WORLD_DETAIL_LEVELS);
}

inline void rl_lod_set_world_detail_level(rl_level_of_detail& lod, int level)
{
    lod.m_world_detail = rl_lod_level_to_store(level, RL_WORLD_DETAIL_LEVELS);
}

// OpenRFG: the LOD Detail level of the options menu. How far buildings, objects and the full detail terrain are drawn
// (the camera's high LOD far clip; rl_lod_detail_distance).
inline int rl_lod_get_lod_detail_level(const rl_level_of_detail& lod)
{
    return rl_lod_stored_level(lod.m_lod_detail, RL_LOD_DETAIL_LEVELS);
}

inline void rl_lod_set_lod_detail_level(rl_level_of_detail& lod, int level)
{
    lod.m_lod_detail = rl_lod_level_to_store(level, RL_LOD_DETAIL_LEVELS);
}

// Magic numbers: 320 m is the game's own (rfg_camera_system_init); 400 and 490 stay inside the 7x7 World Detail
// Distance radius (510 m); the rest stay inside the 9x9, 11x11, 13x13 and 15x15 radii (681, 851, 1021, 1192 m), so the
// full detail edge is loaded before it is drawn when both options are raised together.
inline float rl_lod_detail_distance(int level)
{
    static const float distances[RL_LOD_DETAIL_LEVELS] = {320.0f, 400.0f, 490.0f, 660.0f, 830.0f, 1000.0f, 1170.0f};
    return distances[(level < 0 || level >= RL_LOD_DETAIL_LEVELS) ? 0 : level];
}

// OpenRFG: the Draw Distance level of the options menu. The camera's far clip, which ends the low detail terrain
// (landmark LODs have their own 10 km frustum; rl_draw_distance).
inline int rl_lod_get_draw_distance_level(const rl_level_of_detail& lod)
{
    return rl_lod_stored_level(lod.m_draw_distance, RL_DRAW_DISTANCE_LEVELS);
}

inline void rl_lod_set_draw_distance_level(rl_level_of_detail& lod, int level)
{
    lod.m_draw_distance = rl_lod_level_to_store(level, RL_DRAW_DISTANCE_LEVELS);
}

// Magic numbers: 475 m is the game's own (rfg_camera_system_init); 1000 m is its unused LOD 0 far clip
// (game_render_set_lod_level). 1500 m reaches the edge of 5x5 zones of Terrain Distance; 2000 to 4000 m reach 7x7 up
// to 15x15 (the zone ring is 511 m further each level, the corners further still).
inline float rl_draw_distance(int level)
{
    static const float distances[RL_DRAW_DISTANCE_LEVELS] = {475.0f, 1000.0f, 1500.0f, 2000.0f, 2500.0f, 3000.0f, 4000.0f};
    return distances[(level < 0 || level >= RL_DRAW_DISTANCE_LEVELS) ? 0 : level];
}

// OpenRFG: the Terrain Distance level of the options menu: 0 the original 3x3 zones, each level adds a zone on every
// side up to 15x15 (64-bit only; stream_terrain_distance_radius). Each zone is a 511 m terrain cell with its low detail
// terrain and its objects.
inline int rl_lod_get_terrain_distance_level(const rl_level_of_detail& lod)
{
    return rl_lod_stored_level(lod.m_terrain_distance, RL_TERRAIN_DISTANCE_LEVELS);
}

inline void rl_lod_set_terrain_distance_level(rl_level_of_detail& lod, int level)
{
    lod.m_terrain_distance = rl_lod_level_to_store(level, RL_TERRAIN_DISTANCE_LEVELS);
}

// OpenRFG: the Shadow Distance slider of the options menu, a percentage of the game's own shadow distances (the sun's
// shadow cascades end 100 m out, the terrain-only extension at 240 m). Steps of 25 from 100 to 500 percent.
#define RL_SHADOW_DISTANCE_MIN_PCT 100
#define RL_SHADOW_DISTANCE_MAX_PCT 500
#define RL_SHADOW_DISTANCE_STEP_PCT 25

inline int rl_lod_get_shadow_distance_percent(const rl_level_of_detail& lod)
{
    const int pct = lod.m_shadow_distance * RL_SHADOW_DISTANCE_STEP_PCT;
    return (pct < RL_SHADOW_DISTANCE_MIN_PCT || pct > RL_SHADOW_DISTANCE_MAX_PCT) ? RL_SHADOW_DISTANCE_MIN_PCT : pct;
}

inline void rl_lod_set_shadow_distance_percent(rl_level_of_detail& lod, int pct)
{
    if (pct < RL_SHADOW_DISTANCE_MIN_PCT)
    {
        pct = RL_SHADOW_DISTANCE_MIN_PCT;
    }
    if (pct > RL_SHADOW_DISTANCE_MAX_PCT)
    {
        pct = RL_SHADOW_DISTANCE_MAX_PCT;
    }
    lod.m_shadow_distance = (unsigned char)((pct + RL_SHADOW_DISTANCE_STEP_PCT / 2) / RL_SHADOW_DISTANCE_STEP_PCT);
}

// ---- inline members (no .cpp home)

// 0x00459600 (.h:48)
// Magic numbers: the default screen is 1280x720, windowed. m_vsync_enabled is left as is.
inline rl_screen_info::rl_screen_info()
{
    m_rr_num = 0;
    m_monitor = 0;
    m_window_mode = keen::graphics::WindowMode_Windowed;
    m_current_width = 1280;
    m_current_height = 720;
    m_rr_den = 1;
    m_new_info = new_rl_screen_info_settings();
    memset(padding, 0, sizeof(padding));
    m_aspect_ratio = (float)m_current_width / (float)m_current_height;
}

// 0x004DC850 (.h:55)
inline void rl_screen_info::set_screen_width(int width)
{
    m_current_width = width;
    update_aspect_ratio();
}

// 0x00458AE0 (.h:56)
inline int rl_screen_info::get_screen_width() const
{
    return m_current_width;
}

// 0x004DC880 (.h:59)
inline void rl_screen_info::set_screen_height(int height)
{
    m_current_height = height;
    update_aspect_ratio();
}

// 0x00458AF0 (.h:60)
inline int rl_screen_info::get_screen_height() const
{
    return m_current_height;
}

// 0x004B6820 (.h:63)
inline float rl_screen_info::get_aspect_ratio() const
{
    return m_aspect_ratio;
}

// 0x004B6830 (.h:66)
inline void rl_screen_info::set_window_mode(keen::graphics::WindowMode window_mode)
{
    m_window_mode = window_mode;
}

// 0x00458B00 (.h:67)
inline keen::graphics::WindowMode rl_screen_info::get_window_mode() const
{
    return m_window_mode;
}

// 0x004B6840 (.h:70)
inline void rl_screen_info::set_refresh_rate(int num, int den)
{
    m_rr_num = num;
    m_rr_den = den;
}

// 0x00458B10 (.h:71)
inline void rl_screen_info::get_refresh_rate(int& num, int& den) const
{
    num = m_rr_num;
    den = m_rr_den;
}

// 0x007B9970 (.h:73)
inline unsigned int rl_screen_info::get_monitor() const
{
    return m_monitor;
}

// 0x004B6860 (.h:74)
inline void rl_screen_info::set_monitor(unsigned int monitor)
{
    m_monitor = monitor;
}

// 0x00458B30 (.h:76)
inline bool rl_screen_info::get_vsync_enabled() const
{
    return m_vsync_enabled;
}

// 0x004B6870 (.h:77)
inline void rl_screen_info::set_vsync_enabled(bool enabled)
{
    m_vsync_enabled = enabled;
}

// 0x00458B40 (.h:81)
inline void rl_screen_info::update_aspect_ratio()
{
    m_aspect_ratio = (float)m_current_width / (float)m_current_height;
}

// 0x00458B70 (.h:127)
// The padding is left as is.
inline rl_level_of_detail::rl_level_of_detail()
{
    m_shadow_lod = RL_SHADOW_LOD_MED;
    m_aa_lod = RL_AA_LOD_OFF;
    m_particle_lod = RL_PARTICLE_LOD_LOW;
    m_soft_shadows = true;
    m_bloom_enabled = true;
    m_motion_blur_enabled = true;
    m_depth_of_field_enabled = true;
    m_distortion_enabled = true;
    m_ssao_enabled = true;
    m_ssao_quality = 2; // OpenRFG
    m_dynamic_lights = 3; // OpenRFG: medium
    m_sun_shafts_enabled = true;
    m_new_data.m_texture_reduction = 0;
    m_new_data.m_anisotropic_filtering = RL_AF_LOD_OFF;
}
