/**
 * @ Version: SCREEN SPACE SHADERS - UPDATE 23
 * @ Description: Motion Vectors - Common
 * @ Modified time: 2025-04-20 07:36:37
 * @ Author: https://www.moddb.com/members/ascii1457
 * @ Mod: https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders
 */

#ifndef SSFX_MV_LOADED

	#define SSFX_MV_LOADED
	#include "dlss_contract.h"

	uniform float4x4 m_wvp_prev;
	uniform float4x4 m_vp_prev;
	uniform float4 ssfx_jitter;

	float4 ssfx_mv_calc(float4 current, float4 previous, float IsHUD, float TAAMask)
	{
		// Motion vectors
		// A newly visible surface may not have usable history. Avoid NaN/Inf MVs.
		if (abs(previous.w) < 0.00001f) previous = current;
		float2 motion_vectors = (current.xy / max(abs(current.w), 0.00001f) * sign(current.w))
			- (previous.xy / max(abs(previous.w), 0.00001f) * sign(previous.w));
		
		return float4(float2(motion_vectors.x, -motion_vectors.y) * 0.5f, IsHUD, TAAMask);
	}

	float2 ssfx_taa_jitter(float4 hpos)
	{
		return float2( hpos.xy + ssfx_jitter.xy * hpos.w );
	}

#endif
