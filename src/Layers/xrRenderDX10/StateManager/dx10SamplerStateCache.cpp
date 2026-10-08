#include "stdafx.h"
#include "dx10SamplerStateCache.h"

#include "../dx10StateUtils.h"

using dx10StateUtils::operator==;

dx10SamplerStateCache SSManager;

dx10SamplerStateCache::dx10SamplerStateCache():
	m_uiMaxAnisotropy(1), m_uiMipLODBias(0.0f)
{
	static const int iMaxRSStates = 10;
	m_StateArray.reserve(iMaxRSStates);
	ResetDeviceState();
}

dx10SamplerStateCache::~dx10SamplerStateCache()
{
	ClearStateArray();
}

dx10SamplerStateCache::SHandle dx10SamplerStateCache::GetState(D3D_SAMPLER_DESC& desc)
{
	SHandle hResult;

	//	MaxAnisitropy is reset by ValidateState if not aplicable
	//	to the filter mode used.
	desc.MaxAnisotropy = m_uiMaxAnisotropy;
	desc.MipLODBias = m_uiMipLODBias;
	dx10StateUtils::ValidateState(desc);

	u32 crc = dx10StateUtils::GetHash(desc);

	hResult = FindState(desc, crc);

	if (hResult == hInvalidHandle)
	{
		StateRecord rec;
		rec.m_crc = crc;
		rec.m_usesAnisotropy = desc.Filter == D3D_FILTER_ANISOTROPIC ||
			desc.Filter == D3D_FILTER_COMPARISON_ANISOTROPIC;
		rec.m_variants[0].m_mipLODBias = desc.MipLODBias;
		rec.m_variants[0].m_maxAnisotropy = desc.MaxAnisotropy;
		CreateState(desc, &rec.m_variants[0].m_pState);
		hResult = m_StateArray.size();
		m_StateArray.push_back(rec);
	}

	return hResult;
}

void dx10SamplerStateCache::CreateState(StateDecs desc, IDeviceState** ppIState)
{
	CHK_DX(HW.pDevice->CreateSamplerState( &desc, ppIState));
}

dx10SamplerStateCache::SHandle dx10SamplerStateCache::FindState(const StateDecs& desc, u32 StateCRC)
{
	u32 res = 0xffffffff;
	u32 i = 0;
	for (; i < m_StateArray.size(); ++i)
	{
		if (m_StateArray[i].m_crc == StateCRC)
		{
			StateDecs descCandidate;
			m_StateArray[i].m_variants[0].m_pState->GetDesc(&descCandidate);
			if (descCandidate == desc)
				//return i;
				//	TEST
			{
				//return i;
				res = i;
				break;
			}
			//else
			//{
			//	VERIFY(0);
			//}
		}
	}

	return res != 0xffffffff ? i : (u32)hInvalidHandle;
}

void dx10SamplerStateCache::ClearStateArray()
{
	for (u32 i = 0; i < m_StateArray.size(); ++i)
	{
		for (auto& variant : m_StateArray[i].m_variants)
			_RELEASE(variant.m_pState);
	}

	m_StateArray.clear_not_free();
}

void dx10SamplerStateCache::PrepareSamplerStates(
	HArray& samplers,
	ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT],
	SHandle pCurrentState[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT],
	u32& uiMin,
	u32& uiMax
) const
{
	//	It seems that sizeof pSS is 4 wor win32!
	ZeroMemory(pSS, sizeof(pSS[0])*D3D_COMMONSHADER_SAMPLER_SLOT_COUNT);

	for (u32 i = 0; i < samplers.size(); ++i)
	{
		if (samplers[i] != hInvalidHandle)
		{
			VERIFY(samplers[i]<m_StateArray.size());
			pSS[i] = m_StateArray[samplers[i]].m_variants[0].m_pState;
		}
	}

	uiMin = 0;
	uiMax = D3D_COMMONSHADER_SAMPLER_SLOT_COUNT - 1;
}

void dx10SamplerStateCache::VSApplySamplers(HArray& samplers)
{
	ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];
	u32 uiMin;
	u32 uiMax;
	PrepareSamplerStates(samplers, pSS, m_aVSSamplers, uiMin, uiMax);
	HW.pContext->VSSetSamplers(uiMin, uiMax - uiMin + 1, &pSS[uiMin]);
}

void dx10SamplerStateCache::PSApplySamplers(HArray& samplers)
{
	ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];
	u32 uiMin;
	u32 uiMax;
	PrepareSamplerStates(samplers, pSS, m_aPSSamplers, uiMin, uiMax);
	HW.pContext->PSSetSamplers(uiMin, uiMax - uiMin + 1, &pSS[uiMin]);
}

void dx10SamplerStateCache::GSApplySamplers(HArray& samplers)
{
	ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];
	u32 uiMin;
	u32 uiMax;
	PrepareSamplerStates(samplers, pSS, m_aGSSamplers, uiMin, uiMax);
	HW.pContext->GSSetSamplers(uiMin, uiMax - uiMin + 1, &pSS[uiMin]);
}

#ifdef USE_DX11
void dx10SamplerStateCache::HSApplySamplers(HArray& samplers)
{
	ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];
	u32 uiMin;
	u32 uiMax;
	PrepareSamplerStates(samplers, pSS, m_aHSSamplers, uiMin, uiMax);
	HW.pContext->HSSetSamplers(uiMin, uiMax - uiMin + 1, &pSS[uiMin]);
}

void dx10SamplerStateCache::DSApplySamplers(HArray& samplers)
{
	ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];
	u32 uiMin;
	u32 uiMax;
	PrepareSamplerStates(samplers, pSS, m_aDSSamplers, uiMin, uiMax);
	HW.pContext->DSSetSamplers(uiMin, uiMax - uiMin + 1, &pSS[uiMin]);
}

void dx10SamplerStateCache::CSApplySamplers(HArray& samplers)
{
	ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];
	u32 uiMin;
	u32 uiMax;
	PrepareSamplerStates(samplers, pSS, m_aCSSamplers, uiMin, uiMax);
	HW.pContext->CSSetSamplers(uiMin, uiMax - uiMin + 1, &pSS[uiMin]);
}
#endif


void dx10SamplerStateCache::SetMaxAnisotropy(u32 uiMaxAniso)
{
	clamp(uiMaxAniso, (u32)1, (u32)16);

	if (m_uiMaxAnisotropy == uiMaxAniso)
		return;

	m_uiMaxAnisotropy = uiMaxAniso;

	for (auto& rec : m_StateArray)
		SelectVariant(rec);
}

void dx10SamplerStateCache::SetMipLODBias(float uiMipLODBias)
{
    if (m_uiMipLODBias == uiMipLODBias)
        return;

    m_uiMipLODBias = uiMipLODBias;

    for (auto& rec : m_StateArray)
        SelectVariant(rec);
}

void dx10SamplerStateCache::SelectVariant(StateRecord& rec)
{
    // Match ValidateState's effective AF. Point/linear samplers must not gain
    // redundant variants when the renderer enables/disables anisotropy.
    const u32 anisotropy = rec.m_usesAnisotropy ? m_uiMaxAnisotropy : 1;
    const u32 count = sizeof(rec.m_variants) / sizeof(rec.m_variants[0]);
    for (u32 i = 0; i < count; ++i)
    {
        const StateVariant selected = rec.m_variants[i];
        if (selected.m_pState && selected.m_mipLODBias == m_uiMipLODBias &&
            selected.m_maxAnisotropy == anisotropy)
        {
            // Transfer our owned references without AddRef/Release on hits.
            for (u32 j = i; j > 0; --j) rec.m_variants[j] = rec.m_variants[j - 1];
            rec.m_variants[0] = selected;
            return;
        }
    }

    StateDecs desc;
    rec.m_variants[0].m_pState->GetDesc(&desc);
    desc.MipLODBias = m_uiMipLODBias;
    desc.MaxAnisotropy = anisotropy;
    dx10StateUtils::ValidateState(desc);

    // Create before eviction. Cache hits perform no descriptor queries or
    // device calls; misses keep at most four owned states per stable handle.
    StateVariant next;
    next.m_mipLODBias = m_uiMipLODBias;
    next.m_maxAnisotropy = anisotropy;
    CreateState(desc, &next.m_pState);
    _RELEASE(rec.m_variants[count - 1].m_pState);
    for (u32 j = count - 1; j > 0; --j) rec.m_variants[j] = rec.m_variants[j - 1];
    rec.m_variants[0] = next;
}


void dx10SamplerStateCache::ResetDeviceState()
{
	for (int i = 0; i < sizeof(m_aPSSamplers) / sizeof(m_aPSSamplers[0]); ++i)
	{
		m_aPSSamplers[i] = (SHandle)hInvalidHandle;
		m_aVSSamplers[i] = (SHandle)hInvalidHandle;
		m_aGSSamplers[i] = (SHandle)hInvalidHandle;
#ifdef USE_DX11
		m_aHSSamplers[i] = (SHandle)hInvalidHandle;
		m_aDSSamplers[i] = (SHandle)hInvalidHandle;
		m_aCSSamplers[i] = (SHandle)hInvalidHandle;
#endif
	}
}
