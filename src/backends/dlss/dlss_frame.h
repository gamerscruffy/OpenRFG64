// OpenRFG (VOM, new code): DLSS Super Resolution for the game's own frame (research/docs/03-dlss-framegen-plan.md,
// phase 5), with either renderer.
//
// With the DLSS row at QUALITY down to ULTRA PERFORMANCE the scene and its post-processing are rendered at NVIDIA's
// render size for the mode (the scene renderer's targets made at that size: rl_renderer::updateResourceSizes), the main
// camera's projection jittered each frame (a Halton sequence, rl_camera::setup_perspective), the post effects' last pass
// written into DLSS's input instead of the back buffer, FXAA left out. Once the scene's commands ran (the end of
// rl_scene_renderer::render, on the immediate context) DLSS upscales that picture into the back buffer, with the motion
// made from the scene's depth and the camera's last two frames; the HUD is drawn over it at full size. DLAA is the same at
// full size (DLSS as anti-aliasing); OFF renders as the game always did.
//
// NVIDIA's DLSS SDK (NGX on Direct3D 11) and its runtime nvngx_dlss.dll in backends\dlss next to OpenRFG.exe. Built
// only in the 64-bit build with the SDK (OPENRFG_DLSS); otherwise every function says DLSS is off. Without an NVIDIA
// RTX GPU (or a driver too old) DLSS is unavailable and the game renders at full size.
//
// While the scene is drawn for DLSS its textures are sampled with a lower mip bias (NVIDIA's: log2(scene / screen) - 1), as
// sharp as at full size: keen's pixel shader sampler binds go through openrfg_dlss_frame_sampler.
//
// The motion is the camera's alone (the depth reprojected with the previous frame's camera): moving objects (vehicles,
// people) count as still, DLSS's own checks keep them from ghosting much.
#pragma once

struct ID3D11Device;
struct ID3D11SamplerState;

namespace keen
{
    struct RenderTarget;
    struct TextureData;
}

// The DLSS row's mode (backends.h, openrfg_dlss: OFF, DLAA, QUALITY .. ULTRA PERFORMANCE). Any thread; the next frame
// makes the scene's targets again at the new size.
void openrfg_dlss_frame_set_mode(int mode);
// Whether DLSS can run on this machine: NVIDIA's runtime answered (known after the game's first frame), and false
// before that.
bool openrfg_dlss_frame_supported();

// The scene's size for a screen of out_w x out_h with the mode wanted now: the render size with DLSS upscaling, else the
// screen's; from then on the frames are rendered for DLSS (or not). The render thread, as the targets are made again
// (rl_renderer::updateResourceSizes); NGX starts at the first call.
void openrfg_dlss_frame_targets(ID3D11Device* device, int out_w, int out_h, int* scene_w, int* scene_h);
// The mode wanted is not the one the targets were made for, or DLSS has not sized them yet (the game's first ones,
// made at start-up): rl_render_lib::check_device_status then asks for new targets.
bool openrfg_dlss_frame_targets_stale();
// The targets are made for DLSS: the post effects end in DLSS's input, FXAA left out, the camera jittered.
bool openrfg_dlss_frame_active();

// The main scene's draw (game_render.cpp's render_frame): begun with the main camera's unjittered transform (its
// right, up and forward axes and its position, 3 floats each) and projection (rl_camera's, 16 floats, row vectors),
// then the camera's projection is jittered until the end.
void openrfg_dlss_frame_scene_begin(const float* right, const float* up, const float* forward, const float* position,
                                    const float* projection);
void openrfg_dlss_frame_scene_end();
// While the main scene is drawn: this frame's jitter, to add to a perspective projection's third row (x and y);
// false (and 0) otherwise.
bool openrfg_dlss_frame_jitter(float* x, float* y);

// keen's pixel shader sampler binds (any thread): while the main scene is drawn for DLSS, a copy of the sampler with DLSS's
// mip bias added (the same sampler for comparison filters and samplers without mipmaps); the sampler itself otherwise.
ID3D11SamplerState* openrfg_dlss_frame_sampler(ID3D11SamplerState* state);
// A sampler about to be released (keen's destroySamplerState): its copy goes too.
void openrfg_dlss_frame_sampler_destroyed(ID3D11SamplerState* state);

// After the scene's commands ran on the immediate context (rl_scene_renderer::render): DLSS upscales color (the scene's
// finished picture, sRGB, at the scene's size) into output (the back buffer's target), with depth (the scene's depth,
// its view range 0..depth_max). False when it could not (the back buffer then keeps what it had).
bool openrfg_dlss_frame_upscale(const keen::TextureData* color, const keen::TextureData* depth, const keen::RenderTarget* output,
                                float depth_max);
