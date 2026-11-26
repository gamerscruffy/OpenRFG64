// OpenRFG (VOM, new code): DLSS's motion vectors and depth from the game's depth (dlss_frame.cpp), at the scene's size.
//
// Each pixel's depth is reprojected with the camera's previous frame: the motion, in pixels, from where the pixel is to
// where it was (DLSS's convention: x right, y down), the jitter left out (the pixel's centre without this frame's
// jitter). The depth is copied as well: the game's depth texture is typeless, DLSS takes an R32_FLOAT one. The scene's
// depth spans 0..DepthMax (the rest is the landmarks' and the sky's): those, and the sky, count as the far plane.
cbuffer Constants : register(b0)
{
    row_major float4x4 ClipToPrevClip; // this frame's clip space (unjittered) to the previous frame's
    float2 Size;                       // the scene's size in pixels
    float2 Jitter;                     // this frame's jitter in pixels (how far the picture moved: x right, y down)
    float DepthMax;
    float3 Padding;
};

Texture2D<float> DepthTex : register(t0);

struct Output
{
    float2 motion : SV_Target0;
    float depth : SV_Target1;
};

Output main(float4 pos : SV_Position)
{
    Output o;
    const float d = DepthTex.Load(int3(int2(pos.xy), 0));
    o.depth = d;
    const float2 uv = (pos.xy - Jitter) / Size;
    const float2 clip = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const float z = min(d / DepthMax, 1.0);
    const float4 prev = mul(float4(clip, z, 1.0), ClipToPrevClip);
    float2 motion = float2(0.0, 0.0);
    if (prev.w > 1e-6)
    {
        motion = (prev.xy / prev.w - clip) * float2(0.5, -0.5) * Size;
    }
    o.motion = motion;
    return o;
}
