// OpenRFG (VOM, new code): DLSS Super Resolution for the game's own frame (dlss_frame.h).
#include "backends/dlss/dlss_frame.h"

#include <windows.h>
#include <d3d11.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void openrfg_log(const char* text); // crash_early.cpp

#if defined(OPENRFG_DLSS)

#include "library/modules/runtime/graphics/sources/d3d11/rendertarget_d3d11.hpp"
#include "library/modules/runtime/graphics/sources/d3d11/texturedata_d3d11.hpp"

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_d3d.h>
#include <nvsdk_ngx_params.h>

#include "dlss_fullscreen_vs.h" // fxc: g_dlss_fullscreen_vs
#include "dlss_motion_ps.h"     // fxc: g_dlss_motion_ps

#include <stdint.h>
#include <unordered_map>

namespace
{
    // The modes (backends.h openrfg_dlss).
    const int kModeOff = 0;
    const int kModeDlaa = 1;
    const int kModeCount = 6;
    const char* const kModeNames[kModeCount] = {"OFF", "DLAA", "QUALITY", "BALANCED", "PERFORMANCE", "ULTRA PERFORMANCE"};
    // NGX asks custom engines for a project id of their own (the path tracer's core has the same).
    const char kProjectId[] = "8d2f5b61-3c47-4e9a-b0d4-6a91c2e7f3b8";

    volatile LONG g_mode_wanted = kModeDlaa; // any thread
    volatile LONG g_supported = 0;
    bool g_suspended = false; // the main menu's character (openrfg_dlss_frame_set_suspended): the render thread

    // The render thread's, from here on.
    bool g_ngx_tried = false;
    bool g_ngx_ok = false; // Super Resolution available
    ID3D11Device* g_device = nullptr;
    NVSDK_NGX_Parameter* g_params = nullptr;
    NVSDK_NGX_Handle* g_feature = nullptr;
    unsigned g_feature_w = 0;
    unsigned g_feature_h = 0;
    unsigned g_feature_out_w = 0;
    unsigned g_feature_out_h = 0;
    int g_feature_mode = -1;
    // The mode DLSS failed in (its creation or an evaluation): the frame is rendered without DLSS from the next frame on,
    // until another mode is chosen.
    int g_failed_mode = -1;

    // The scene's targets.
    int g_targets_mode = -1; // -1: none made yet
    bool g_active = false;   // made for DLSS
    int g_scene_w = 0;
    int g_scene_h = 0;
    int g_out_w = 0;
    int g_out_h = 0;

    // The frame: its jitter and camera. g_in_scene is read by the scene's second thread too (rl_scene_renderer's part 2,
    // started and waited for inside the scene's draw).
    volatile bool g_in_scene = false;
    bool g_frame_camera = false; // scene_begin ran for the frame being drawn
    unsigned g_frame = 0;
    float g_jitter_px[2] = {};   // how far the picture moved this frame, in pixels (x right, y down): DLSS's jitter
    float g_jitter_proj[2] = {}; // the same in the projection's third row
    float g_clip_to_prev[16] = {};
    bool g_reset = true; // DLSS's history dropped at the next upscale
    bool g_prev_valid = false;
    double g_prev_vp[16] = {};
    double g_prev_position[3] = {};
    unsigned long long g_upscales = 0;

    // The passes.
    ID3D11Texture2D* g_color = nullptr; // the picture's sRGB bytes as UNORM: DLSS takes LDR colour that way
    ID3D11Texture2D* g_depth = nullptr; // R32_FLOAT
    ID3D11RenderTargetView* g_depth_rtv = nullptr;
    ID3D11Texture2D* g_motion = nullptr; // R16G16_FLOAT, pixels
    ID3D11RenderTargetView* g_motion_rtv = nullptr;
    ID3D11Texture2D* g_output = nullptr; // UNORM at the back buffer's size
    UINT g_tex_w = 0;
    UINT g_tex_h = 0;
    UINT g_tex_out_w = 0;
    UINT g_tex_out_h = 0;
    DXGI_FORMAT g_tex_color_format = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT g_tex_out_format = DXGI_FORMAT_UNKNOWN;
    ID3D11VertexShader* g_vs = nullptr;
    ID3D11PixelShader* g_ps = nullptr;
    ID3D11Buffer* g_constants = nullptr;
    ID3D11DepthStencilState* g_no_depth = nullptr;
    ID3D11RasterizerState* g_no_cull = nullptr;
    bool g_passes_failed = false;

    // The mip bias (openrfg_dlss_frame_sampler): set with the targets (between frames), applied while the scene is drawn.
    // The biased copies of the game's samplers, by the game's sampler (never freed at exit), and each thread's cache of
    // them (emptied when the generation changes: a new bias, a sampler gone).
    float g_mip_bias = 0.0f;
    SRWLOCK g_sampler_lock = SRWLOCK_INIT;
    std::unordered_map<ID3D11SamplerState*, ID3D11SamplerState*>* g_samplers = nullptr;
    volatile LONG g_sampler_generation = 1;
    struct SamplerCache
    {
        LONG generation = 0;
        ID3D11SamplerState* key[64] = {};
        ID3D11SamplerState* value[64] = {};
    };
    thread_local SamplerCache t_samplers;

    // The motion pass's constants (dlss_motion_ps.hlsl).
    struct MotionConstants
    {
        float clip_to_prev[16];
        float size[2];
        float jitter[2];
        float depth_max;
        float padding[3];
    };

    void logf(const char* format, ...)
    {
        char line[512] = "DLSS ";
        va_list args;
        va_start(args, format);
        vsnprintf(line + 5, sizeof(line) - 5, format, args);
        va_end(args);
        openrfg_log(line);
    }

    template <typename T> void release(T*& p)
    {
        if (p != nullptr)
        {
            p->Release();
            p = nullptr;
        }
    }

    int clamp_mode(LONG mode)
    {
        return mode >= kModeOff && mode < kModeCount ? (int)mode : kModeDlaa;
    }

    // The mode the targets should be made for: the row's, OFF while suspended.
    int effective_mode()
    {
        return g_suspended ? kModeOff : clamp_mode(InterlockedCompareExchange(&g_mode_wanted, 0, 0));
    }

    NVSDK_NGX_PerfQuality_Value quality_value(int mode)
    {
        switch (mode)
        {
        case 2: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
        case 3: return NVSDK_NGX_PerfQuality_Value_Balanced;
        case 4: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
        case 5: return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
        default: return NVSDK_NGX_PerfQuality_Value_DLAA;
        }
    }

    void NVSDK_CONV ngx_log(const char* message, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature)
    {
        if (message == nullptr)
        {
            return;
        }
        char line[400];
        size_t n = 0;
        for (const char* c = message; *c != 0 && n + 1 < sizeof(line); ++c)
        {
            if (*c != '\n' && *c != '\r')
            {
                line[n++] = *c;
            }
        }
        line[n] = 0;
        logf("NGX: %s", line);
    }

    // NGX on the game's device, once: whether Super Resolution can run here (an NVIDIA RTX GPU, a driver new enough, the
    // runtime nvngx_dlss.dll in backends\dlss).
    bool ngx_start(ID3D11Device* device)
    {
        if (g_ngx_tried)
        {
            return g_ngx_ok;
        }
        g_ngx_tried = true;
        g_device = device;
        g_device->AddRef();
        wchar_t dir[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, dir, MAX_PATH);
        wchar_t* slash = length > 0 && length < MAX_PATH ? wcsrchr(dir, L'\\') : nullptr;
        if (slash == nullptr)
        {
            logf("off: the program's folder is unknown");
            return false;
        }
        slash[1] = 0;
        wcscat_s(dir, L"backends\\dlss");
        const wchar_t* paths[] = {dir};
        NVSDK_NGX_FeatureCommonInfo info = {};
        info.PathListInfo.Path = paths;
        info.PathListInfo.Length = 1;
        info.LoggingInfo.LoggingCallback = &ngx_log;
        info.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_OFF; // its errors still come back as results
        info.LoggingInfo.DisableOtherLoggingSinks = true;
        NVSDK_NGX_Result r = NVSDK_NGX_D3D11_Init_with_ProjectID(kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0", dir, device, &info, NVSDK_NGX_Version_API);
        if (NVSDK_NGX_FAILED(r))
        {
            logf("off: NGX could not start (0x%08X; not an NVIDIA GPU, or its driver too old)", (unsigned)r);
            return false;
        }
        r = NVSDK_NGX_D3D11_GetCapabilityParameters(&g_params);
        if (NVSDK_NGX_FAILED(r) || g_params == nullptr)
        {
            logf("off: NGX's capabilities are unavailable (0x%08X)", (unsigned)r);
            g_params = nullptr;
            return false;
        }
        int available = 0;
        int needs_driver = 0;
        unsigned major = 0;
        unsigned minor = 0;
        NVSDK_NGX_Parameter_GetI(g_params, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
        NVSDK_NGX_Parameter_GetI(g_params, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs_driver);
        NVSDK_NGX_Parameter_GetUI(g_params, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &major);
        NVSDK_NGX_Parameter_GetUI(g_params, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minor);
        if (!available)
        {
            int init_result = 0;
            NVSDK_NGX_Parameter_GetI(g_params, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &init_result);
            if (needs_driver)
            {
                logf("off: Super Resolution is not available (0x%08X; it needs driver %u.%u)", (unsigned)init_result, major, minor);
            }
            else
            {
                logf("off: Super Resolution is not available (0x%08X; nvngx_dlss.dll missing from backends\\dlss, or not an RTX GPU)", (unsigned)init_result);
            }
            return false;
        }
        g_ngx_ok = true;
        InterlockedExchange(&g_supported, 1);
        logf("Super Resolution available (NGX on Direct3D 11)");
        return true;
    }

    // The scene's size for an output of out_w x out_h: NVIDIA's for the mode (out_w x out_h for DLAA).
    void render_size(int mode, unsigned out_w, unsigned out_h, unsigned& w, unsigned& h)
    {
        w = out_w;
        h = out_h;
        if (mode <= kModeDlaa)
        {
            return;
        }
        unsigned optimal_w = 0;
        unsigned optimal_h = 0;
        unsigned max_w = 0;
        unsigned max_h = 0;
        unsigned min_w = 0;
        unsigned min_h = 0;
        float sharpness = 0.0f;
        const NVSDK_NGX_Result r = NGX_DLSS_GET_OPTIMAL_SETTINGS(g_params, out_w, out_h, quality_value(mode), &optimal_w, &optimal_h, &max_w, &max_h, &min_w, &min_h, &sharpness);
        if (NVSDK_NGX_FAILED(r) || optimal_w == 0 || optimal_h == 0 || optimal_w > out_w || optimal_h > out_h)
        {
            static const double kScale[kModeCount] = {1.0, 1.0, 2.0 / 3.0, 0.58, 0.5, 1.0 / 3.0}; // NVIDIA's, per axis
            optimal_w = (unsigned)lround(out_w * kScale[mode]);
            optimal_h = (unsigned)lround(out_h * kScale[mode]);
            logf("no render size from NGX for %s at %ux%u (0x%08X): NVIDIA's scale, %ux%u", kModeNames[mode], out_w, out_h, (unsigned)r, optimal_w, optimal_h);
        }
        w = optimal_w > 0 ? optimal_w : 1;
        h = optimal_h > 0 ? optimal_h : 1;
    }

    // The copies dropped (a new bias): the frames that used them are done (between frames).
    void release_samplers()
    {
        AcquireSRWLockExclusive(&g_sampler_lock);
        if (g_samplers != nullptr)
        {
            for (auto& entry : *g_samplers)
            {
                if (entry.second != entry.first)
                {
                    entry.second->Release();
                }
            }
            g_samplers->clear();
        }
        InterlockedIncrement(&g_sampler_generation);
        ReleaseSRWLockExclusive(&g_sampler_lock);
    }

    // The sampler with DLSS's mip bias added: the sampler itself for comparison, minimum and maximum filters (shadow maps)
    // and for samplers without mipmaps.
    ID3D11SamplerState* make_biased(ID3D11SamplerState* state)
    {
        D3D11_SAMPLER_DESC desc = {};
        state->GetDesc(&desc);
        if ((unsigned)desc.Filter >= 0x80u || !(desc.MaxLOD > desc.MinLOD))
        {
            return state;
        }
        float bias = desc.MipLODBias + g_mip_bias;
        bias = bias < -16.0f ? -16.0f : (bias > 15.99f ? 15.99f : bias);
        desc.MipLODBias = bias;
        ID3D11Device* device = nullptr;
        state->GetDevice(&device);
        ID3D11SamplerState* biased = nullptr;
        const HRESULT hr = device != nullptr ? device->CreateSamplerState(&desc, &biased) : E_FAIL;
        release(device);
        return SUCCEEDED(hr) && biased != nullptr ? biased : state;
    }

    void release_feature()
    {
        if (g_feature != nullptr)
        {
            NVSDK_NGX_D3D11_ReleaseFeature(g_feature);
            g_feature = nullptr;
        }
        g_feature_mode = -1;
    }

    void release_textures()
    {
        release(g_color);
        release(g_depth_rtv);
        release(g_depth);
        release(g_motion_rtv);
        release(g_motion);
        release(g_output);
        g_tex_w = g_tex_h = g_tex_out_w = g_tex_out_h = 0;
    }

    // The bytes of an sRGB format read as they are (DLSS's LDR input and output: sRGB values, not linear).
    DXGI_FORMAT unorm_of(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
        default: return format;
        }
    }

    ID3D11Texture2D* make_texture(UINT w, UINT h, DXGI_FORMAT format, UINT bind, const char* what)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = bind;
        ID3D11Texture2D* texture = nullptr;
        const HRESULT hr = g_device->CreateTexture2D(&desc, nullptr, &texture);
        if (FAILED(hr))
        {
            logf("the %s texture (%ux%u, format %d) could not be made (0x%08lX)", what, w, h, (int)format, (unsigned long)hr);
            return nullptr;
        }
        return texture;
    }

    bool ensure_textures(UINT w, UINT h, DXGI_FORMAT color_format, UINT out_w, UINT out_h, DXGI_FORMAT out_format)
    {
        if (g_color != nullptr && g_tex_w == w && g_tex_h == h && g_tex_out_w == out_w && g_tex_out_h == out_h && g_tex_color_format == color_format && g_tex_out_format == out_format)
        {
            return true;
        }
        release_textures();
        g_color = make_texture(w, h, unorm_of(color_format), D3D11_BIND_SHADER_RESOURCE, "input");
        g_depth = make_texture(w, h, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, "depth");
        g_motion = make_texture(w, h, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, "motion");
        g_output = make_texture(out_w, out_h, unorm_of(out_format), D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, "output");
        if (g_color == nullptr || g_depth == nullptr || g_motion == nullptr || g_output == nullptr ||
            FAILED(g_device->CreateRenderTargetView(g_depth, nullptr, &g_depth_rtv)) || FAILED(g_device->CreateRenderTargetView(g_motion, nullptr, &g_motion_rtv)))
        {
            release_textures();
            return false;
        }
        g_tex_w = w;
        g_tex_h = h;
        g_tex_out_w = out_w;
        g_tex_out_h = out_h;
        g_tex_color_format = color_format;
        g_tex_out_format = out_format;
        return true;
    }

    bool ensure_passes()
    {
        if (g_ps != nullptr)
        {
            return true;
        }
        if (g_passes_failed)
        {
            return false;
        }
        g_passes_failed = true;
        HRESULT hr = g_device->CreateVertexShader(g_dlss_fullscreen_vs, sizeof(g_dlss_fullscreen_vs), nullptr, &g_vs);
        if (SUCCEEDED(hr))
        {
            hr = g_device->CreatePixelShader(g_dlss_motion_ps, sizeof(g_dlss_motion_ps), nullptr, &g_ps);
        }
        if (SUCCEEDED(hr))
        {
            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth = sizeof(MotionConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            hr = g_device->CreateBuffer(&desc, nullptr, &g_constants);
        }
        if (SUCCEEDED(hr))
        {
            D3D11_DEPTH_STENCIL_DESC desc = {};
            desc.DepthEnable = FALSE;
            desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
            hr = g_device->CreateDepthStencilState(&desc, &g_no_depth);
        }
        if (SUCCEEDED(hr))
        {
            D3D11_RASTERIZER_DESC desc = {};
            desc.FillMode = D3D11_FILL_SOLID;
            desc.CullMode = D3D11_CULL_NONE;
            desc.DepthClipEnable = TRUE;
            hr = g_device->CreateRasterizerState(&desc, &g_no_cull);
        }
        if (FAILED(hr))
        {
            logf("off: its motion pass could not be made (0x%08lX)", (unsigned long)hr);
            release(g_vs);
            release(g_ps);
            release(g_constants);
            release(g_no_depth);
            release(g_no_cull);
            return false;
        }
        g_passes_failed = false;
        return true;
    }

    // The immediate context's state the motion pass changes, put back after it (the game's command buffers keep their own
    // view of what is bound).
    struct SavedState
    {
        ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        ID3D11DepthStencilView* dsv = nullptr;
        ID3D11BlendState* blend = nullptr;
        FLOAT blend_factor[4] = {};
        UINT sample_mask = 0;
        ID3D11DepthStencilState* depth = nullptr;
        UINT stencil_ref = 0;
        ID3D11RasterizerState* raster = nullptr;
        D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
        UINT viewport_count = 0;
        ID3D11InputLayout* layout = nullptr;
        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        ID3D11VertexShader* vs = nullptr;
        ID3D11GeometryShader* gs = nullptr;
        ID3D11PixelShader* ps = nullptr;
        ID3D11Buffer* ps_constants = nullptr;
        ID3D11ShaderResourceView* ps_srv = nullptr;

        void capture(ID3D11DeviceContext* c)
        {
            c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
            c->OMGetBlendState(&blend, blend_factor, &sample_mask);
            c->OMGetDepthStencilState(&depth, &stencil_ref);
            c->RSGetState(&raster);
            viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
            c->RSGetViewports(&viewport_count, viewports);
            c->IAGetInputLayout(&layout);
            c->IAGetPrimitiveTopology(&topology);
            c->VSGetShader(&vs, nullptr, nullptr);
            c->GSGetShader(&gs, nullptr, nullptr);
            c->PSGetShader(&ps, nullptr, nullptr);
            c->PSGetConstantBuffers(0, 1, &ps_constants);
            c->PSGetShaderResources(0, 1, &ps_srv);
        }

        void restore(ID3D11DeviceContext* c)
        {
            c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, dsv);
            c->OMSetBlendState(blend, blend_factor, sample_mask);
            c->OMSetDepthStencilState(depth, stencil_ref);
            c->RSSetState(raster);
            c->RSSetViewports(viewport_count, viewports);
            c->IASetInputLayout(layout);
            c->IASetPrimitiveTopology(topology);
            c->VSSetShader(vs, nullptr, 0);
            c->GSSetShader(gs, nullptr, 0);
            c->PSSetShader(ps, nullptr, 0);
            c->PSSetConstantBuffers(0, 1, &ps_constants);
            c->PSSetShaderResources(0, 1, &ps_srv);
            for (ID3D11RenderTargetView*& rtv : rtvs)
            {
                release(rtv);
            }
            release(dsv);
            release(blend);
            release(depth);
            release(raster);
            release(layout);
            release(vs);
            release(gs);
            release(ps);
            release(ps_constants);
            release(ps_srv);
        }
    };

    // The motion (and the depth as R32_FLOAT) at the scene's size.
    void motion_pass(ID3D11DeviceContext* c, ID3D11ShaderResourceView* depth, float depth_max, const float* clip_to_prev, const float* jitter)
    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(c->Map(g_constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            return;
        }
        MotionConstants constants = {};
        memcpy(constants.clip_to_prev, clip_to_prev, sizeof(constants.clip_to_prev));
        constants.size[0] = (float)g_tex_w;
        constants.size[1] = (float)g_tex_h;
        constants.jitter[0] = jitter[0];
        constants.jitter[1] = jitter[1];
        constants.depth_max = depth_max > 0.5f && depth_max <= 1.0f ? depth_max : 1.0f;
        memcpy(mapped.pData, &constants, sizeof(constants));
        c->Unmap(g_constants, 0);
        SavedState saved;
        saved.capture(c);
        ID3D11RenderTargetView* targets[2] = {g_motion_rtv, g_depth_rtv};
        c->OMSetRenderTargets(2, targets, nullptr);
        c->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        c->OMSetDepthStencilState(g_no_depth, 0);
        c->RSSetState(g_no_cull);
        const D3D11_VIEWPORT viewport = {0.0f, 0.0f, (float)g_tex_w, (float)g_tex_h, 0.0f, 1.0f};
        c->RSSetViewports(1, &viewport);
        c->IASetInputLayout(nullptr);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(g_vs, nullptr, 0);
        c->GSSetShader(nullptr, nullptr, 0);
        c->PSSetShader(g_ps, nullptr, 0);
        c->PSSetConstantBuffers(0, 1, &g_constants);
        c->PSSetShaderResources(0, 1, &depth);
        c->Draw(3, 0);
        ID3D11ShaderResourceView* none = nullptr;
        c->PSSetShaderResources(0, 1, &none);
        saved.restore(c);
    }

    // DLSS at the scene's size, mode and output size: made again when one of them changed.
    bool ensure_feature(ID3D11DeviceContext* c, unsigned w, unsigned h, unsigned out_w, unsigned out_h, int mode)
    {
        if (g_feature != nullptr && g_feature_w == w && g_feature_h == h && g_feature_out_w == out_w && g_feature_out_h == out_h && g_feature_mode == mode)
        {
            return true;
        }
        release_feature();
        NVSDK_NGX_DLSS_Create_Params create = {};
        create.Feature.InWidth = w;
        create.Feature.InHeight = h;
        create.Feature.InTargetWidth = out_w;
        create.Feature.InTargetHeight = out_h;
        create.Feature.InPerfQualityValue = quality_value(mode);
        // LDR colour (the game's tone-mapped picture, its sRGB bytes), the motion at the scene's size, without jitter.
        create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
        create.InEnableOutputSubrects = false;
        const NVSDK_NGX_Result r = NGX_D3D11_CREATE_DLSS_EXT(c, &g_feature, g_params, &create);
        if (NVSDK_NGX_FAILED(r) || g_feature == nullptr)
        {
            logf("Super Resolution could not be made at %ux%u to %ux%u (%s, 0x%08X): the game's frame without DLSS from the next frame", w, h, out_w, out_h, kModeNames[mode], (unsigned)r);
            g_feature = nullptr;
            g_failed_mode = mode;
            return false;
        }
        g_feature_w = w;
        g_feature_h = h;
        g_feature_out_w = out_w;
        g_feature_out_h = out_h;
        g_feature_mode = mode;
        g_reset = true;
        logf("Super Resolution made: %s, the scene at %ux%u, out at %ux%u", kModeNames[mode], w, h, out_w, out_h);
        return true;
    }

    double halton(unsigned index, unsigned base)
    {
        double result = 0.0;
        double f = 1.0;
        while (index > 0)
        {
            f /= base;
            result += f * (index % base);
            index /= base;
        }
        return result;
    }

    // Row vectors: c = a * b.
    void multiply(const double* a, const double* b, double* c)
    {
        for (int i = 0; i < 4; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                double sum = 0.0;
                for (int k = 0; k < 4; ++k)
                {
                    sum += a[i * 4 + k] * b[k * 4 + j];
                }
                c[i * 4 + j] = sum;
            }
        }
    }

    // Gauss-Jordan with partial pivoting; false when singular.
    bool invert(const double* m, double* out)
    {
        double a[4][8];
        for (int i = 0; i < 4; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                a[i][j] = m[i * 4 + j];
                a[i][j + 4] = i == j ? 1.0 : 0.0;
            }
        }
        for (int col = 0; col < 4; ++col)
        {
            int pivot = col;
            for (int row = col + 1; row < 4; ++row)
            {
                if (fabs(a[row][col]) > fabs(a[pivot][col]))
                {
                    pivot = row;
                }
            }
            if (fabs(a[pivot][col]) < 1e-12)
            {
                return false;
            }
            if (pivot != col)
            {
                for (int j = 0; j < 8; ++j)
                {
                    const double t = a[col][j];
                    a[col][j] = a[pivot][j];
                    a[pivot][j] = t;
                }
            }
            const double inv = 1.0 / a[col][col];
            for (int j = 0; j < 8; ++j)
            {
                a[col][j] *= inv;
            }
            for (int row = 0; row < 4; ++row)
            {
                if (row != col)
                {
                    const double f = a[row][col];
                    for (int j = 0; j < 8; ++j)
                    {
                        a[row][j] -= f * a[col][j];
                    }
                }
            }
        }
        for (int i = 0; i < 4; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                out[i * 4 + j] = a[i][j + 4];
            }
        }
        return true;
    }
}

void openrfg_dlss_frame_set_mode(int mode)
{
    InterlockedExchange(&g_mode_wanted, (LONG)clamp_mode(mode));
}

bool openrfg_dlss_frame_supported()
{
    return InterlockedCompareExchange(&g_supported, 0, 0) != 0;
}

void openrfg_dlss_frame_set_suspended(bool suspended)
{
    g_suspended = suspended;
}

void openrfg_dlss_frame_targets(ID3D11Device* device, int out_w, int out_h, int* scene_w, int* scene_h)
{
    const int wanted = clamp_mode(InterlockedCompareExchange(&g_mode_wanted, 0, 0));
    const int mode = effective_mode();
    const bool was_active = g_active;
    const int was_w = g_scene_w;
    const int was_h = g_scene_h;
    const int was_mode = g_targets_mode;
    *scene_w = out_w;
    *scene_h = out_h;
    g_targets_mode = mode;
    g_active = false;
    if (device != nullptr && out_w > 0 && out_h > 0)
    {
        ngx_start(device); // also with OFF: the options show DLSS only where it runs
    }
    if (mode != kModeOff && g_ngx_ok && mode != g_failed_mode && out_w > 0 && out_h > 0)
    {
        unsigned w = 0;
        unsigned h = 0;
        render_size(mode, (unsigned)out_w, (unsigned)out_h, w, h);
        *scene_w = (int)w;
        *scene_h = (int)h;
        g_active = true;
    }
    g_scene_w = *scene_w;
    g_scene_h = *scene_h;
    g_out_w = out_w;
    g_out_h = out_h;
    // The textures' mip bias (NVIDIA's): -1 for DLAA, -2 for PERFORMANCE.
    const float mip_bias = g_active && out_w > 0 ? (float)(log2((double)g_scene_w / (double)out_w) - 1.0) : 0.0f;
    if (mip_bias != g_mip_bias)
    {
        release_samplers();
        g_mip_bias = mip_bias;
    }
    // New targets: DLSS starts over.
    g_reset = true;
    g_prev_valid = false;
    if (!g_active)
    {
        release_feature();
        release_textures();
    }
    if (g_active != was_active || g_scene_w != was_w || g_scene_h != was_h || mode != was_mode)
    {
        if (g_active)
        {
            logf("%s: the scene at %dx%d, upscaled to %dx%d", kModeNames[mode], g_scene_w, g_scene_h, out_w, out_h);
        }
        else
        {
            const char* why = g_suspended && wanted != kModeOff ? ", while the main menu shows its character" : (mode == kModeOff ? "" : (!g_ngx_ok ? ", unavailable" : ", failed"));
            logf("off for the game's frame (%s%s): the scene at %dx%d", kModeNames[wanted], why, out_w, out_h);
        }
    }
}

bool openrfg_dlss_frame_targets_stale()
{
    // (also until DLSS has sized them once: the game makes its first targets at start-up, and makes them again only when
    // the window's size or mode changes)
    return g_targets_mode < 0 || effective_mode() != g_targets_mode || (g_active && g_failed_mode == g_targets_mode);
}

bool openrfg_dlss_frame_active()
{
    return g_active;
}

void openrfg_dlss_frame_scene_begin(const float* right, const float* up, const float* forward, const float* position, const float* projection)
{
    g_frame_camera = false;
    if (!g_active || g_scene_w <= 0 || g_scene_h <= 0)
    {
        return;
    }
    // This frame's jitter: Halton (2, 3), its phases by the upscale (NVIDIA: at least 8 x its square).
    const double scale = (double)g_out_w / (double)g_scene_w;
    double phases = ceil(8.0 * scale * scale);
    phases = phases < 8.0 ? 8.0 : (phases > 128.0 ? 128.0 : phases);
    const unsigned index = g_frame % (unsigned)phases + 1;
    ++g_frame;
    g_jitter_px[0] = (float)(halton(index, 2) - 0.5);
    g_jitter_px[1] = (float)(halton(index, 3) - 0.5);
    // The picture moves by the jitter: clip x right, clip y up.
    g_jitter_proj[0] = 2.0f * g_jitter_px[0] / (float)g_scene_w;
    g_jitter_proj[1] = -2.0f * g_jitter_px[1] / (float)g_scene_h;

    // The camera's world-to-clip transform without the jitter (row vectors: the camera's axes in the view's columns).
    const double p[3] = {position[0], position[1], position[2]};
    const double view[16] = {
        right[0], up[0], forward[0], 0.0,
        right[1], up[1], forward[1], 0.0,
        right[2], up[2], forward[2], 0.0,
        -(right[0] * p[0] + right[1] * p[1] + right[2] * p[2]), -(up[0] * p[0] + up[1] * p[1] + up[2] * p[2]), -(forward[0] * p[0] + forward[1] * p[1] + forward[2] * p[2]), 1.0};
    double proj[16];
    for (int i = 0; i < 16; ++i)
    {
        proj[i] = projection[i];
    }
    double vp[16];
    multiply(view, proj, vp);
    g_frame_camera = true;
    g_in_scene = true;
    double inv_vp[16];
    if (!invert(vp, inv_vp))
    {
        // (a singular camera: no motion this frame, DLSS starts over)
        static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        memcpy(g_clip_to_prev, kIdentity, sizeof(g_clip_to_prev));
        g_prev_valid = false;
        g_reset = true;
        return;
    }
    // A cut (no last frame, or the camera 50 m away from it: a teleport, a cutscene's cut): DLSS starts over.
    double moved = 0.0;
    for (int k = 0; k < 3; ++k)
    {
        const double d = p[k] - g_prev_position[k];
        moved += d * d;
    }
    const bool cut = !g_prev_valid || moved > 50.0 * 50.0;
    if (cut)
    {
        g_reset = true;
    }
    double clip_to_prev[16];
    multiply(inv_vp, cut ? vp : g_prev_vp, clip_to_prev);
    for (int i = 0; i < 16; ++i)
    {
        g_clip_to_prev[i] = (float)clip_to_prev[i];
    }
    memcpy(g_prev_vp, vp, sizeof(g_prev_vp));
    memcpy(g_prev_position, p, sizeof(g_prev_position));
    g_prev_valid = true;
}

void openrfg_dlss_frame_scene_end()
{
    g_in_scene = false;
}

bool openrfg_dlss_frame_jitter(float* x, float* y)
{
    if (!g_in_scene)
    {
        *x = 0.0f;
        *y = 0.0f;
        return false;
    }
    *x = g_jitter_proj[0];
    *y = g_jitter_proj[1];
    return true;
}

ID3D11SamplerState* openrfg_dlss_frame_sampler(ID3D11SamplerState* state)
{
    if (state == nullptr || !g_in_scene || g_mip_bias == 0.0f)
    {
        return state;
    }
    SamplerCache& cache = t_samplers;
    const LONG generation = g_sampler_generation;
    if (cache.generation != generation)
    {
        memset(cache.key, 0, sizeof(cache.key));
        cache.generation = generation;
    }
    const size_t slot = ((uintptr_t)state >> 4) & 63;
    if (cache.key[slot] == state)
    {
        return cache.value[slot];
    }
    ID3D11SamplerState* biased = nullptr;
    AcquireSRWLockShared(&g_sampler_lock);
    if (g_samplers != nullptr)
    {
        const auto found = g_samplers->find(state);
        if (found != g_samplers->end())
        {
            biased = found->second;
        }
    }
    ReleaseSRWLockShared(&g_sampler_lock);
    if (biased == nullptr)
    {
        biased = make_biased(state);
        AcquireSRWLockExclusive(&g_sampler_lock);
        if (g_samplers == nullptr)
        {
            g_samplers = new std::unordered_map<ID3D11SamplerState*, ID3D11SamplerState*>();
        }
        const auto inserted = g_samplers->emplace(state, biased);
        if (!inserted.second)
        {
            // (another thread made it first)
            if (biased != state)
            {
                biased->Release();
            }
            biased = inserted.first->second;
        }
        ReleaseSRWLockExclusive(&g_sampler_lock);
    }
    cache.key[slot] = state;
    cache.value[slot] = biased;
    return biased;
}

void openrfg_dlss_frame_sampler_destroyed(ID3D11SamplerState* state)
{
    if (state == nullptr || g_samplers == nullptr)
    {
        return;
    }
    AcquireSRWLockExclusive(&g_sampler_lock);
    const auto found = g_samplers->find(state);
    if (found != g_samplers->end())
    {
        if (found->second != state)
        {
            found->second->Release();
        }
        g_samplers->erase(found);
        InterlockedIncrement(&g_sampler_generation);
    }
    ReleaseSRWLockExclusive(&g_sampler_lock);
}

// The upscale itself (openrfg_dlss_frame_upscale): DLSS's input copied, the motion made, DLSS evaluated, its output
// copied into the back buffer.
static bool upscale(ID3D11Resource* color_resource, ID3D11ShaderResourceView* depth_srv, ID3D11RenderTargetView* output_rtv, float depth_max,
                    const float* clip_to_prev, const float* jitter)
{
    bool done = false;
    ID3D11Texture2D* color_texture = nullptr;
    ID3D11Resource* back_resource = nullptr;
    ID3D11Texture2D* back_texture = nullptr;
    output_rtv->GetResource(&back_resource);
    if (back_resource != nullptr && SUCCEEDED(color_resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&color_texture)) &&
        SUCCEEDED(back_resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&back_texture)))
    {
        D3D11_TEXTURE2D_DESC color_desc = {};
        D3D11_TEXTURE2D_DESC back_desc = {};
        color_texture->GetDesc(&color_desc);
        back_texture->GetDesc(&back_desc);
        if (color_desc.SampleDesc.Count == 1 && back_desc.SampleDesc.Count == 1 && color_desc.Width <= back_desc.Width && color_desc.Height <= back_desc.Height &&
            ensure_textures(color_desc.Width, color_desc.Height, color_desc.Format, back_desc.Width, back_desc.Height, back_desc.Format))
        {
            ID3D11DeviceContext* c = nullptr;
            g_device->GetImmediateContext(&c);
            if (ensure_feature(c, color_desc.Width, color_desc.Height, back_desc.Width, back_desc.Height, g_targets_mode))
            {
                c->CopyResource(g_color, color_texture);
                motion_pass(c, depth_srv, depth_max, clip_to_prev, jitter);
                NVSDK_NGX_D3D11_DLSS_Eval_Params eval = {};
                eval.Feature.pInColor = g_color;
                eval.Feature.pInOutput = g_output;
                eval.pInDepth = g_depth;
                eval.pInMotionVectors = g_motion;
                eval.InJitterOffsetX = jitter[0];
                eval.InJitterOffsetY = jitter[1];
                eval.InRenderSubrectDimensions.Width = color_desc.Width;
                eval.InRenderSubrectDimensions.Height = color_desc.Height;
                eval.InReset = g_reset ? 1 : 0;
                eval.InMVScaleX = 1.0f;
                eval.InMVScaleY = 1.0f;
                const NVSDK_NGX_Result r = NGX_D3D11_EVALUATE_DLSS_EXT(c, g_feature, g_params, &eval);
                if (NVSDK_NGX_FAILED(r))
                {
                    logf("Super Resolution's evaluation failed (0x%08X): the game's frame without DLSS from the next frame", (unsigned)r);
                    g_failed_mode = g_targets_mode;
                }
                else
                {
                    // The bytes as they are: sRGB in, sRGB out.
                    c->CopyResource(back_texture, g_output);
                    g_reset = false;
                    done = true;
                    if (++g_upscales == 1)
                    {
                        logf("the first frame upscaled: %ux%u to %ux%u", color_desc.Width, color_desc.Height, back_desc.Width, back_desc.Height);
                    }
                }
            }
            release(c);
        }
    }
    release(color_texture);
    release(back_texture);
    release(back_resource);
    return done;
}

bool openrfg_dlss_frame_upscale(const keen::TextureData* color, const keen::TextureData* depth, const keen::RenderTarget* output, float depth_max)
{
    if (!g_active || !g_ngx_ok || color == nullptr || depth == nullptr || output == nullptr)
    {
        return false;
    }
    // The frame's motion and jitter; a scene drawn without scene_begin (not jittered, no camera) is upscaled without
    // motion, DLSS starting over.
    static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    static const float kNoJitter[2] = {0.0f, 0.0f};
    const bool have_camera = g_frame_camera;
    g_frame_camera = false;
    const float* clip_to_prev = have_camera ? g_clip_to_prev : kIdentity;
    const float* jitter = have_camera ? g_jitter_px : kNoJitter;
    if (!have_camera)
    {
        g_reset = true;
        g_prev_valid = false;
    }
    ID3D11Resource* color_resource = color->pTexture;
    ID3D11ShaderResourceView* depth_srv = depth->pShaderView;
    ID3D11RenderTargetView* output_rtv = output->colorBufferCount > 0 ? output->renderTargetViews[0] : nullptr;
    bool done = false;
    if (color_resource != nullptr && depth_srv != nullptr && output_rtv != nullptr && ensure_passes())
    {
        done = upscale(color_resource, depth_srv, output_rtv, depth_max, clip_to_prev, jitter);
    }
    // The scene went to DLSS's input, not to the back buffer: without DLSS from the next frame on (rather than without
    // the scene), until another mode is chosen.
    if (!done && g_failed_mode != g_targets_mode)
    {
        g_failed_mode = g_targets_mode;
        logf("the frame could not be upscaled: the game's frame without DLSS from the next frame");
    }
    return done;
}

#else

void openrfg_dlss_frame_set_mode(int)
{
}

void openrfg_dlss_frame_set_suspended(bool)
{
}

bool openrfg_dlss_frame_supported()
{
    return false;
}

void openrfg_dlss_frame_targets(ID3D11Device*, int out_w, int out_h, int* scene_w, int* scene_h)
{
    *scene_w = out_w;
    *scene_h = out_h;
}

bool openrfg_dlss_frame_targets_stale()
{
    return false;
}

bool openrfg_dlss_frame_active()
{
    return false;
}

void openrfg_dlss_frame_scene_begin(const float*, const float*, const float*, const float*, const float*)
{
}

void openrfg_dlss_frame_scene_end()
{
}

bool openrfg_dlss_frame_jitter(float* x, float* y)
{
    *x = 0.0f;
    *y = 0.0f;
    return false;
}

ID3D11SamplerState* openrfg_dlss_frame_sampler(ID3D11SamplerState* state)
{
    return state;
}

void openrfg_dlss_frame_sampler_destroyed(ID3D11SamplerState*)
{
}

bool openrfg_dlss_frame_upscale(const keen::TextureData*, const keen::TextureData*, const keen::RenderTarget*, float)
{
    return false;
}

#endif
