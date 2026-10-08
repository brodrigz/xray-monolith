#include "sampler_test_support/stdafx.h"
#include "../../src/Layers/xrRenderDX10/StateManager/dx10SamplerStateCache.h"
#include <d3d11sdklayers.h>
#include <wrl/client.h>
#include <cstdio>
using Microsoft::WRL::ComPtr;
TestHardware HW{};
static D3D11_SAMPLER_DESC Description(D3D11_FILTER filter)
{
    D3D11_SAMPLER_DESC desc{};
    desc.Filter = filter;
    desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.MaxAnisotropy = 1;
    desc.ComparisonFunc = filter == D3D11_FILTER_COMPARISON_ANISOTROPIC ?
        D3D11_COMPARISON_LESS_EQUAL : D3D11_COMPARISON_NEVER;
    desc.MinLOD = 0;
    desc.MaxLOD = D3D11_FLOAT32_MAX;
    return desc;
}
static void CheckBindings(dx10SamplerStateCache& cache, dx10SamplerStateCache::HArray& handles,
    const std::vector<D3D11_FILTER>& filters, float bias, unsigned anisotropy)
{
    cache.VSApplySamplers(handles); cache.PSApplySamplers(handles); cache.GSApplySamplers(handles);
    cache.HSApplySamplers(handles); cache.DSApplySamplers(handles); cache.CSApplySamplers(handles);
    for (unsigned stage = 0; stage < 6; ++stage)
    for (unsigned i = 0; i < handles.size(); ++i)
    {
        ComPtr<ID3D11SamplerState> sampler;
        switch (stage)
        {
        case 0: HW.pContext->VSGetSamplers(i, 1, &sampler); break;
        case 1: HW.pContext->PSGetSamplers(i, 1, &sampler); break;
        case 2: HW.pContext->GSGetSamplers(i, 1, &sampler); break;
        case 3: HW.pContext->HSGetSamplers(i, 1, &sampler); break;
        case 4: HW.pContext->DSGetSamplers(i, 1, &sampler); break;
        case 5: HW.pContext->CSGetSamplers(i, 1, &sampler); break;
        }
        VERIFY(sampler);
        D3D11_SAMPLER_DESC actual{};
        sampler->GetDesc(&actual);
        auto expected = Description(filters[i]);
        expected.MipLODBias = bias;
        expected.MaxAnisotropy = anisotropy;
        dx10StateUtils::ValidateState(expected);
        // Compare with an uncached reference state: WARP canonicalizes unused
        // descriptor fields (e.g. anisotropy=0 on point/linear samplers).
        // This bypasses CountingDevice, which counts only production-cache calls.
        ComPtr<ID3D11SamplerState> reference;
        CHK_DX(HW.pDevice->device->CreateSamplerState(&expected, &reference));
        reference->GetDesc(&expected);
        if (std::memcmp(&actual, &expected, sizeof(actual)) != 0)
        {
            std::printf("stage=%u sampler=%u filter=%u/%u bias=%g/%g aniso=%u/%u compare=%u/%u LOD=%g,%g/%g,%g\n",
                stage, i, unsigned(actual.Filter), unsigned(expected.Filter), actual.MipLODBias, expected.MipLODBias,
                actual.MaxAnisotropy, expected.MaxAnisotropy, unsigned(actual.ComparisonFunc), unsigned(expected.ComparisonFunc),
                actual.MinLOD, actual.MaxLOD, expected.MinLOD, expected.MaxLOD);
            VERIFY(false);
        }
        auto lookup = Description(filters[i]);
        VERIFY(cache.GetState(lookup) == handles[i]); // Bias/aniso never change handles.
    }
}
int main()
try
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    CHK_DX(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_DEBUG,
        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
    ComPtr<ID3D11InfoQueue> debug;
    CHK_DX(device.As(&debug));
    CountingDevice counted{device.Get()};
    HW = {&counted, context.Get()};
    const std::vector<D3D11_FILTER> originalFilters = {
        D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_FILTER_MIN_MAG_MIP_LINEAR,
        D3D11_FILTER_ANISOTROPIC, D3D11_FILTER_COMPARISON_ANISOTROPIC};
    unsigned warmSwitches = 0;
    {
        dx10SamplerStateCache cache;
        for (unsigned lifetime = 0; lifetime < 3; ++lifetime)
        {
            cache.SetMipLODBias(0);
            cache.SetMaxAnisotropy(4);
            auto filters = originalFilters;
            dx10SamplerStateCache::HArray handles;
            for (auto filter : filters)
            {
                auto desc = Description(filter);
                handles.push_back(cache.GetState(desc));
            }
            CheckBindings(cache, handles, filters, 0, 4);
            unsigned count = counted.creates;
            cache.SetMipLODBias(0);
            VERIFY(counted.creates == count); // Native/DLAA no-op.
            cache.SetMipLODBias(-0.585f);
            VERIFY(counted.creates == count + handles.size());
            count = counted.creates;
            for (unsigned frame = 0; frame < 1000; ++frame)
            {
                cache.SetMipLODBias(0); CheckBindings(cache, handles, filters, 0, 4);
                cache.SetMipLODBias(-0.585f); CheckBindings(cache, handles, filters, -0.585f, 4);
                warmSwitches += 2;
            }
            VERIFY(counted.creates == count);
            // A sampler first requested while the scene bias is active.
            filters.push_back(D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR);
            auto desc = Description(filters.back());
            handles.push_back(cache.GetState(desc));
            count = counted.creates;
            cache.SetMipLODBias(0);
            VERIFY(counted.creates == count + 1);
            CheckBindings(cache, handles, filters, 0, 4);
            count = counted.creates;
            cache.SetMipLODBias(-0.585f);
            VERIFY(counted.creates == count);
            // Invalidate both variants when anisotropy changes.
            cache.SetMaxAnisotropy(16);
            CheckBindings(cache, handles, filters, -0.585f, 16);
            count = counted.creates;
            cache.SetMipLODBias(0);
            VERIFY(counted.creates == count + handles.size());
            CheckBindings(cache, handles, filters, 0, 16);
            count = counted.creates;
            cache.SetMipLODBias(-0.585f);
            VERIFY(counted.creates == count);
            CheckBindings(cache, handles, filters, -0.585f, 16);
            // Third bias evicts the older alternate instead of retaining a third.
            cache.SetMipLODBias(0.25f);
            CheckBindings(cache, handles, filters, 0.25f, 16);
            count = counted.creates;
            cache.SetMipLODBias(-0.585f);
            VERIFY(counted.creates == count);
            cache.SetMipLODBias(0);
            VERIFY(counted.creates == count + handles.size());
            CheckBindings(cache, handles, filters, 0, 16);
            cache.ClearStateArray();
            cache.ResetDeviceState();
            context->ClearState();
        }
        // Destruction with two populated variants must release both.
        auto desc = Description(D3D11_FILTER_ANISOTROPIC);
        cache.GetState(desc);
        cache.SetMipLODBias(-1);
    }
    context->ClearState();
    ComPtr<ID3D11Debug> deviceDebug;
    CHK_DX(device.As(&deviceDebug));
    CHK_DX(deviceDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL));
    for (UINT64 i = 0; i < debug->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i)
    {
        SIZE_T bytes = 0;
        debug->GetMessage(i, nullptr, &bytes);
        std::vector<unsigned char> data(bytes);
        auto* message = reinterpret_cast<D3D11_MESSAGE*>(data.data());
        CHK_DX(debug->GetMessage(i, message, &bytes));
        if (message->Severity <= D3D11_MESSAGE_SEVERITY_ERROR ||
            message->ID == D3D11_MESSAGE_ID_LIVE_SAMPLER)
        {
            std::printf("D3D11: %s\n", message->pDescription);
            return 1;
        }
    }
    VERIFY(SUCCEEDED(device->GetDeviceRemovedReason()));
    std::printf("Sampler cache passed: %u warmed bias switches with zero creation calls; "
        "six shader stages, stable handles, late samplers, anisotropy invalidation, "
        "bounded eviction, three resets and destruction; no live samplers or D3D11 errors.\n", warmSwitches);
    HW = {};
    return 0;
}
catch (const std::exception& error)
{
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
