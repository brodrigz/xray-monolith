Texture2D<float4> s_image : register(t0);
SamplerState smp_rtlinear : register(s0);
#include "../../extras/dlss_gamma/gamedata/shaders/r3/dlss_cas.h"

float4 VS(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 PS(float4 position : SV_Position) : SV_Target
{
    return DlssCas(position.xy / 16.0);
}
