#ifndef MONOLITH_DLSS_CONTRACT
#define MONOLITH_DLSS_CONTRACT 1
// Paired with the feat/dlss-sr renderer. Keep SSS vectors in their original
// current-minus-previous UV convention; the engine supplies NGX's negative scale.
uniform float4 dlss_params; // active, render width, render height, reserved
uniform float4 dlss_temporal_jitter; // current raster UV offset, previous raster UV offset
float2 dlss_history_offset()
{
    return dlss_params.x > 0.5f ? dlss_temporal_jitter.zw - dlss_temporal_jitter.xy : float2(0, 0);
}
float2 dlss_reproject(float2 uv) { return uv + dlss_history_offset(); }
#endif
