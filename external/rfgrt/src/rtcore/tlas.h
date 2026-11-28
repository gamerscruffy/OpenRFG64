#pragma once
// The frame's top-level acceleration structure (research/docs/03, phase 2 step 2): built every frame on the frame
// queue from the plugin's snapshot (frame_scene.h). Each instance names a mesh and a submesh; the scene's BLAS for it
// and the submesh's offset (applied before the instance's transform, as the game draws it) make one instance desc.
// Instances whose mesh or BLAS is not ready (yet) are left out and counted.
#include <d3d12.h>

#include <cstdint>

#include "frame_scene.h"
#include "scene.h"

namespace rtcore {

class Tlas {
public:
    // `fast_trace`: built PREFER_FAST_TRACE (Mirror.TlasFastTrace), else PREFER_FAST_BUILD.
    bool init(ID3D12Device5* dev, uint32_t max_instances, int ring, LogFn log, bool fast_trace = true);
    // Fills ring slot `slot`'s instance descs from `in` and records the build into `cl` (a UAV barrier follows).
    // `posed` (may be null): per instance of `in`, a posed BLAS that replaces its mesh's (skinning.h), 0 for none.
    // `screens`: the frame's particles that give light (ScreenQuads below), one more instance in a mask of its own (0x04),
    // 0 for none. Returns the number of instances in the TLAS.
    uint32_t record(ID3D12GraphicsCommandList4* cl, int slot, const rfgrt_frame::Instance* in, uint32_t count, const Scene& scene,
                    const D3D12_GPU_VIRTUAL_ADDRESS* posed = nullptr, D3D12_GPU_VIRTUAL_ADDRESS screens = 0);
    // Ring slot `slot`'s TLAS: each frame in flight has its own, so that a build never rewrites the one a previous
    // frame's pass may still be tracing.
    D3D12_GPU_VIRTUAL_ADDRESS address(int slot) const { return result_[slot] ? result_[slot]->GetGPUVirtualAddress() : 0; }
    // Ring slot `slot`'s instance table: per InstanceID (the frame scene's instance index) a uint4 of the geometry pool
    // page, the byte offsets of the positions and of the indices (bit 31: 16-bit indices) and of the geometry table
    // (Scene::GeoRef); page 0xFFFFFFFF: none. A StructuredBuffer<uint4> for the shaders.
    D3D12_GPU_VIRTUAL_ADDRESS instance_table(int slot) const {
        return geo_ ? geo_->GetGPUVirtualAddress() + uint64_t(slot) * max_ * 16 : 0;
    }
    void release();

    struct Stats {
        uint64_t frames = 0, instances = 0, missing_mesh = 0, missing_blas = 0, submesh_beyond = 0, posed = 0;
        uint64_t glass_whole = 0, glass_partial = 0;  // instances all transparent (non-opaque panes), some (non-opaque)
        uint64_t glass_whole_out = 0;                 // of the first, with more than 23 geometries (out of the rays' mask)
        uint64_t glass_translucent = 0;               // with translucent glass (non-opaque: a share of the rays through)
        uint64_t glass_by_mesh = 0;                   // of them, classed by their mesh's materials (kMeshMaterials)
        uint64_t no_materials[6] = {};                // instances whose mesh has no materials (kMeshMaterials), by kind
        uint32_t glass_meshes[6] = {}, glass_whole_bits = 0, glass_mesh_count = 0;  // the first few meshes of each, for the log
        // one of each, for the log: mesh id, submesh, the mesh's submesh count (beyond) or the instance's kind
        uint32_t sample_beyond[3] = {}, sample_blas[3] = {};
        double nearest_sum = 0;  // distance from the camera to the nearest instance origin, summed over frames
        uint64_t screen_frames = 0;  // frames with the particles that give light in the TLAS
    } stats;
    void set_camera(const float origin[3]) {
        camera_[0] = origin[0];
        camera_[1] = origin[1];
        camera_[2] = origin[2];
    }

private:
    ID3D12Device5* dev_ = nullptr;
    uint32_t max_ = 0;
    int ring_ = 0;
    ID3D12Resource* descs_ = nullptr;  // upload: ring_ x max_ instance descs
    uint8_t* descs_cpu_ = nullptr;
    ID3D12Resource* geo_ = nullptr;    // upload: ring_ x max_ instance table entries (uint4)
    uint8_t* geo_cpu_ = nullptr;
    static constexpr int kMaxRing = 4;
    ID3D12Resource* result_[kMaxRing] = {};
    ID3D12Resource* scratch_[kMaxRing] = {};
    float camera_[3] = {};
    LogFn log_ = nullptr;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags_ = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
};

// The particles that give light (frame_scene.h ScreenQuad, GI.ScreenLight): every frame their quads in a small BLAS, non-opaque
// so that a query meets each one it crosses (once: no duplicate candidates), the TLAS's instance in mask 0x04 (no other
// ray's mask has it), and per quad what the shaders need (trace_common.hlsli screens_along): a float4 of its bitmap's
// rectangle, a float4 of its colour x alpha x GI.ScreenLight and its bitmap's id + 1 (the bits).
class ScreenQuads {
public:
    bool init(ID3D12Device5* dev, int ring, LogFn log);
    // Ring slot `slot`'s quads from the frame's slot header, the BLAS's build recorded into `cl` (a UAV barrier
    // follows); `scale`: GI.ScreenLight (0: none). Returns the BLAS, 0 for none this frame.
    D3D12_GPU_VIRTUAL_ADDRESS record(ID3D12GraphicsCommandList4* cl, int slot, const rfgrt_frame::SlotHeader* h, float scale);
    uint32_t count(int slot) const { return slot >= 0 && slot < kMaxRing ? count_[slot] : 0; }
    // Ring slot `slot`'s quads for the shaders: StructuredBuffer<float4> t8, two a quad (0: none made).
    D3D12_GPU_VIRTUAL_ADDRESS data(int slot) const;
    void release();

    struct Stats {
        uint64_t frames = 0, quads = 0, most = 0, skipped = 0;  // frames with some, their quads, the most, non-finite left out
    } stats;

private:
    static constexpr int kMaxRing = 4;
    ID3D12Device5* dev_ = nullptr;
    int ring_ = 0;
    LogFn log_ = nullptr;
    ID3D12Resource* upload_ = nullptr;   // per slot: the corners (float3, 4 a quad), then the shaders' data (2 float4 a quad)
    uint8_t* upload_cpu_ = nullptr;
    ID3D12Resource* indices_ = nullptr;  // 16-bit, 6 a quad: (0 1 2) (2 1 3)
    ID3D12Resource* result_[kMaxRing] = {};
    ID3D12Resource* scratch_[kMaxRing] = {};
    uint32_t count_[kMaxRing] = {};
    uint64_t slot_bytes_ = 0;
};

}  // namespace rtcore
