#pragma once
// Test-only engine glue. The test compiles the actual sampler-cache .cpp,
// uses real D3D11 sampler objects, and counts calls made to CreateSamplerState.
#define NOMINMAX
#define USE_DX11
#define dx10StateUtils_included
#include <d3d11.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>
using u32 = unsigned;
using ID3DSamplerState = ID3D11SamplerState;
using D3D_SAMPLER_DESC = D3D11_SAMPLER_DESC;
constexpr unsigned D3D_COMMONSHADER_SAMPLER_SLOT_COUNT = D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;
template<class T> struct xr_vector : std::vector<T> { void clear_not_free() { this->clear(); } };
template<class T> void clamp(T& value, T lo, T hi) { value = std::clamp(value, lo, hi); }
#define VERIFY(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
#define CHK_DX(x) do { if (FAILED(x)) throw std::runtime_error(#x); } while (false)
#define _RELEASE(x) do { if (x) { (x)->Release(); (x) = nullptr; } } while (false)
struct CountingDevice
{
    ID3D11Device* device = nullptr;
    unsigned creates = 0;
    HRESULT CreateSamplerState(const D3D11_SAMPLER_DESC* desc, ID3D11SamplerState** out)
    {
        ++creates;
        return device->CreateSamplerState(desc, out);
    }
};
struct TestHardware { CountingDevice* pDevice; ID3D11DeviceContext* pContext; };
extern TestHardware HW;
namespace dx10StateUtils
{
    // Deliberately collide hashes to exercise descriptor matching, not the hash.
    inline u32 GetHash(const D3D11_SAMPLER_DESC&) { return 0; }
    inline bool operator==(D3D11_SAMPLER_DESC a, D3D11_SAMPLER_DESC b)
    {
        a.MipLODBias = b.MipLODBias = 0;
        a.MaxAnisotropy = b.MaxAnisotropy = 1;
        return std::memcmp(&a, &b, sizeof(a)) == 0;
    }
    // Same sampler normalization as the engine; other state utilities are unused.
    inline void ValidateState(D3D11_SAMPLER_DESC& desc)
    {
        if (desc.AddressU != D3D11_TEXTURE_ADDRESS_BORDER &&
            desc.AddressV != D3D11_TEXTURE_ADDRESS_BORDER &&
            desc.AddressW != D3D11_TEXTURE_ADDRESS_BORDER)
            for (float& value : desc.BorderColor) value = 0;
        if (desc.Filter != D3D11_FILTER_ANISOTROPIC &&
            desc.Filter != D3D11_FILTER_COMPARISON_ANISOTROPIC)
            desc.MaxAnisotropy = 1;
    }
}
