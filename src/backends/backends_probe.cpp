// OpenRFG (VOM, new code): what this machine can run (backends.h), found once at start-up on a worker thread.
//
// Path tracing: Direct3D 11.4 on the game's device (the shared fence between the two devices), then a Direct3D 12
// device on the same adapter at feature level 12_0 with ray tracing tier 1.1 and shader model 6.5 (the core's ray
// queries). The test device is released again; d3d12.dll is loaded at run time, so the program starts without it.
// GPU PhysX: the CUDA driver (nvcuda.dll, loaded at run time) with a device of compute capability 7.5 or newer, the
// game's adapter preferred (matched by LUID). PhysX initializes the same driver when it starts, so the probe costs it
// nothing more.

#include "backends/backends.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

#if defined(OPENRFG_BACKENDS)
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi.h>
#endif

void openrfg_log(const char* text); // crash_early.cpp

namespace
{
    openrfg_backend_probe g_probe;
    HANDLE g_probe_thread = nullptr;

#if defined(OPENRFG_BACKENDS)
    // The adapter's description, filled by probe_path_tracing for probe_physx_gpu.
    struct adapter_info
    {
        bool valid = false;
        LUID luid = {};
        unsigned int vendor = 0;
    };

    void probe_path_tracing(ID3D11Device* device, openrfg_backend_probe& probe, adapter_info& adapter_out)
    {
        IDXGIDevice* dxgi_device = nullptr;
        IDXGIAdapter* adapter = nullptr;
        if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_device)) && SUCCEEDED(dxgi_device->GetAdapter(&adapter)))
        {
            DXGI_ADAPTER_DESC desc = {};
            if (SUCCEEDED(adapter->GetDesc(&desc)))
            {
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, probe.adapter, (int)sizeof(probe.adapter), nullptr, nullptr);
                adapter_out.valid = true;
                adapter_out.luid = desc.AdapterLuid;
                adapter_out.vendor = desc.VendorId;
            }
        }
        if (dxgi_device != nullptr)
        {
            dxgi_device->Release();
        }

        ID3D11Device5* device5 = nullptr;
        if (FAILED(device->QueryInterface(__uuidof(ID3D11Device5), (void**)&device5)))
        {
            snprintf(probe.path_tracing_reason, sizeof(probe.path_tracing_reason), "no Direct3D 11.4 on the game's device (shared fences)");
        }
        else
        {
            device5->Release();
            HMODULE d3d12 = LoadLibraryW(L"d3d12.dll");
            PFN_D3D12_CREATE_DEVICE create = d3d12 != nullptr ? (PFN_D3D12_CREATE_DEVICE)(void*)GetProcAddress(d3d12, "D3D12CreateDevice") : nullptr;
            ID3D12Device* device12 = nullptr;
            if (create == nullptr || adapter == nullptr)
            {
                snprintf(probe.path_tracing_reason, sizeof(probe.path_tracing_reason), "no Direct3D 12");
            }
            else if (FAILED(create(adapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&device12)))
            {
                snprintf(probe.path_tracing_reason, sizeof(probe.path_tracing_reason), "no Direct3D 12 at feature level 12_0 on the game's adapter");
            }
            else
            {
                D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
                D3D12_FEATURE_DATA_SHADER_MODEL shader_model = {D3D_SHADER_MODEL_6_5};
                const bool have_options5 = SUCCEEDED(device12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)));
                const bool have_sm65 = SUCCEEDED(device12->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model))) && shader_model.HighestShaderModel >= D3D_SHADER_MODEL_6_5;
                if (!have_options5 || options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1)
                {
                    const int tier = have_options5 ? (int)options5.RaytracingTier : 0;
                    snprintf(probe.path_tracing_reason, sizeof(probe.path_tracing_reason), "no hardware ray tracing tier 1.1 (tier %d.%d)", tier / 10, tier % 10);
                }
                else if (!have_sm65)
                {
                    snprintf(probe.path_tracing_reason, sizeof(probe.path_tracing_reason), "no shader model 6.5");
                }
                else
                {
                    probe.path_tracing = true;
                    snprintf(probe.path_tracing_reason, sizeof(probe.path_tracing_reason), "Direct3D 12, ray tracing tier %d.%d, shader model 6.5", (int)options5.RaytracingTier / 10, (int)options5.RaytracingTier % 10);
                }
                device12->Release();
            }
        }
        if (adapter != nullptr)
        {
            adapter->Release();
        }
    }

    // The CUDA driver API, the few entry points the probe needs (CUresult is an int; CUdevice an int).
    typedef int(__stdcall* cu_init_fn)(unsigned int flags);
    typedef int(__stdcall* cu_device_get_count_fn)(int* count);
    typedef int(__stdcall* cu_device_get_fn)(int* device, int ordinal);
    typedef int(__stdcall* cu_device_get_attribute_fn)(int* value, int attribute, int device);
    typedef int(__stdcall* cu_device_get_name_fn)(char* name, int length, int device);
    typedef int(__stdcall* cu_device_get_luid_fn)(char* luid, unsigned int* node_mask, int device);
    const int kCuComputeCapabilityMajor = 75; // CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR
    const int kCuComputeCapabilityMinor = 76; // CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR

    void probe_physx_gpu(const adapter_info& adapter, openrfg_backend_probe& probe)
    {
        HMODULE cuda = LoadLibraryW(L"nvcuda.dll");
        if (cuda == nullptr)
        {
            snprintf(probe.physx_reason, sizeof(probe.physx_reason), "no NVIDIA CUDA driver: PhysX runs on the CPU");
            return;
        }
        cu_init_fn cu_init = (cu_init_fn)(void*)GetProcAddress(cuda, "cuInit");
        cu_device_get_count_fn cu_device_get_count = (cu_device_get_count_fn)(void*)GetProcAddress(cuda, "cuDeviceGetCount");
        cu_device_get_fn cu_device_get = (cu_device_get_fn)(void*)GetProcAddress(cuda, "cuDeviceGet");
        cu_device_get_attribute_fn cu_device_get_attribute = (cu_device_get_attribute_fn)(void*)GetProcAddress(cuda, "cuDeviceGetAttribute");
        cu_device_get_name_fn cu_device_get_name = (cu_device_get_name_fn)(void*)GetProcAddress(cuda, "cuDeviceGetName");
        cu_device_get_luid_fn cu_device_get_luid = (cu_device_get_luid_fn)(void*)GetProcAddress(cuda, "cuDeviceGetLuid");
        if (cu_init == nullptr || cu_device_get_count == nullptr || cu_device_get == nullptr || cu_device_get_attribute == nullptr)
        {
            snprintf(probe.physx_reason, sizeof(probe.physx_reason), "the CUDA driver lacks its entry points: PhysX runs on the CPU");
            return;
        }
        const int init_result = cu_init(0);
        int count = 0;
        if (init_result != 0 || cu_device_get_count(&count) != 0 || count <= 0)
        {
            snprintf(probe.physx_reason, sizeof(probe.physx_reason), "no CUDA device (cuInit %d): PhysX runs on the CPU", init_result);
            return;
        }
        // The game's adapter if CUDA has it, else the most capable device.
        int best_capability = -1;
        char best_name[128] = {};
        bool best_is_game_adapter = false;
        for (int ordinal = 0; ordinal < count; ordinal++)
        {
            int device = 0;
            int major = 0;
            int minor = 0;
            if (cu_device_get(&device, ordinal) != 0 || cu_device_get_attribute(&major, kCuComputeCapabilityMajor, device) != 0 || cu_device_get_attribute(&minor, kCuComputeCapabilityMinor, device) != 0)
            {
                continue;
            }
            bool is_game_adapter = false;
            char luid[8] = {};
            unsigned int node_mask = 0;
            if (adapter.valid && cu_device_get_luid != nullptr && cu_device_get_luid(luid, &node_mask, device) == 0)
            {
                is_game_adapter = memcmp(luid, &adapter.luid, sizeof(luid)) == 0;
            }
            const int capability = major * 10 + minor;
            if (best_capability < 0 || (is_game_adapter && !best_is_game_adapter) || (is_game_adapter == best_is_game_adapter && capability > best_capability))
            {
                best_capability = capability;
                best_is_game_adapter = is_game_adapter;
                best_name[0] = '\0';
                if (cu_device_get_name != nullptr)
                {
                    cu_device_get_name(best_name, (int)sizeof(best_name), device);
                }
            }
        }
        if (best_capability < 0)
        {
            snprintf(probe.physx_reason, sizeof(probe.physx_reason), "no usable CUDA device: PhysX runs on the CPU");
            return;
        }
        if (best_capability < 75)
        {
            snprintf(probe.physx_reason, sizeof(probe.physx_reason), "%s, compute capability %d.%d (GPU PhysX needs 7.5): PhysX runs on the CPU", best_name, best_capability / 10, best_capability % 10);
            return;
        }
        probe.physx_gpu = true;
        snprintf(probe.physx_reason, sizeof(probe.physx_reason), "%s, compute capability %d.%d%s", best_name, best_capability / 10, best_capability % 10, best_is_game_adapter ? "" : " (not the game's adapter)");
    }

    DWORD WINAPI probe_thread(void* param)
    {
        ID3D11Device* device = (ID3D11Device*)param;
        adapter_info adapter;
        probe_path_tracing(device, g_probe, adapter);
        probe_physx_gpu(adapter, g_probe);
        device->Release();
        char msg[768];
        snprintf(msg, sizeof(msg), "BACKENDS probe: adapter \"%s\"; path tracing %s (%s); GPU PhysX %s (%s); keep debris by default %s", g_probe.adapter, g_probe.path_tracing ? "yes" : "no", g_probe.path_tracing_reason, g_probe.physx_gpu ? "yes" : "no", g_probe.physx_reason, g_probe.physx_gpu ? "on" : "off");
        openrfg_log(msg);
        g_probe.done = true;
        return 0;
    }
#endif
}

void openrfg_backends_start_probe(ID3D11Device* device)
{
#if defined(OPENRFG_BACKENDS)
    if (device == nullptr || g_probe_thread != nullptr)
    {
        return;
    }
    device->AddRef();
    g_probe_thread = CreateThread(nullptr, 0, probe_thread, device, 0, nullptr);
    if (g_probe_thread == nullptr)
    {
        device->Release();
    }
#else
    (void)device;
#endif
}

const openrfg_backend_probe& openrfg_backends_probe()
{
    // The probe takes a fraction of a second; the menus that ask come seconds later. The wait also orders the thread's
    // writes before the reads (it returns at once once the thread has ended).
    if (g_probe_thread != nullptr)
    {
        WaitForSingleObject(g_probe_thread, 10000);
    }
    return g_probe;
}
