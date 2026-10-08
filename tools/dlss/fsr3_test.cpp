#include "../../src/Layers/xrRenderPC_R4/Dlss/Fsr3D3D11.h"
#include <cstdio>
#include <vector>
#include <string>
#include <d3d11sdklayers.h>

using Microsoft::WRL::ComPtr;
static void Log(const char* stage, unsigned result) { std::printf("[FSR3 test] %s: 0x%08X\n", stage, result); }

static ComPtr<ID3D11Texture2D> Texture(ID3D11Device* device, dlss::Size size, DXGI_FORMAT format, const void* data, unsigned pixelBytes)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = size.width; desc.Height = size.height;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = format;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (format == DXGI_FORMAT_R8G8B8A8_UNORM) desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
    if (format == DXGI_FORMAT_R24G8_TYPELESS) desc.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
    D3D11_SUBRESOURCE_DATA initial = {data, size.width * pixelBytes, 0};
    ComPtr<ID3D11Texture2D> result;
    if (FAILED(device->CreateTexture2D(&desc, &initial, &result))) return {};
    return result;
}

int wmain(int argc, wchar_t** argv)
{
    const bool warp = argc > 1 && std::wstring(argv[1]) == L"--warp";
    const bool level110 = argc > 1 && std::wstring(argv[1]) == L"--fl11_0";
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = D3D11CreateDevice(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_DEBUG,
        level110 ? levels + 1 : levels, level110 ? 1 : 2, D3D11_SDK_VERSION,
        &device, &level, &context);
    if (FAILED(hr)) { Log("D3D11CreateDevice", hr); return 1; }
    std::printf("[FSR3 test] Device feature level: 0x%X\n", unsigned(level));
    ComPtr<ID3D11InfoQueue> debug;
    if (FAILED(device.As(&debug))) return 1;
    fsr3::D3D11Backend backend;

    const dlss::Size display{640, 360};
    if (level110)
    {
        if (level != D3D_FEATURE_LEVEL_11_0 || backend.Create(device.Get(), display, display, false, Log) ||
            backend.Ready() || backend.Output())
        { std::puts("FAIL: feature level 11.0 was not rejected cleanly"); return 1; }
        backend.Destroy();
    }
    unsigned count = 0;
    unsigned lifetimes = 0;
    for (bool hdr : {false, true})
    for (unsigned quality = 0; !level110 && quality <= 4; ++quality)
    {
        dlss::Size render;
        if (!backend.OptimalSize(quality, display, render) || !backend.Create(device.Get(), render, display, hdr, Log))
        {
            for (UINT64 i = 0; i < debug->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i)
            {
                SIZE_T length = 0;
                debug->GetMessage(i, nullptr, &length);
                std::vector<unsigned char> storage(length);
                auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
                if (SUCCEEDED(debug->GetMessage(i, message, &length)))
                    std::printf("[D3D11 creation] %s\n", message->pDescription);
            }
            return 1;
        }
        ++lifetimes;
        std::printf("[FSR3 test] quality=%u hdr=%u render=%ux%u output=%ux%u\n", unsigned(quality), unsigned(hdr), render.width, render.height, display.width, display.height);
        std::vector<unsigned> colors(size_t(render.width) * render.height, 0xff808080);
        std::vector<unsigned> depths(colors.size(), 0x00800000); std::vector<unsigned short> motion(colors.size() * 4, 0);
        auto color = Texture(device.Get(), render, DXGI_FORMAT_R8G8B8A8_UNORM, colors.data(), 4);
        auto depth = Texture(device.Get(), render, DXGI_FORMAT_R24G8_TYPELESS, depths.data(), 4);
        auto vectors = Texture(device.Get(), render, DXGI_FORMAT_R16G16B16A16_FLOAT, motion.data(), 8);
        if (!color || !depth || !vectors) return 1;
        ComPtr<ID3D11RenderTargetView> target;
        ComPtr<ID3D11ShaderResourceView> motionView;
        if (FAILED(device->CreateRenderTargetView(color.Get(), nullptr, &target)) ||
            FAILED(device->CreateShaderResourceView(vectors.Get(), nullptr, &motionView))) return 1;
        const D3D11_VIEWPORT viewport{7, 11, 123, 321, 0, 1};
        for (unsigned frame = 0; frame < 20; ++frame)
        {
            context->RSSetViewports(1, &viewport);
            ID3D11RenderTargetView* targetPtr = target.Get();
            context->OMSetRenderTargets(1, &targetPtr, nullptr);
            ID3D11ShaderResourceView* motionPtr = motionView.Get();
            context->PSSetShaderResources(3, 1, &motionPtr);
            fsr3::D3D11Backend::Frame input;
            input.color = color.Get(); input.depth = depth.Get(); input.motion = vectors.Get();
            input.jitter = dlss::Jitter(frame, render, display);
            input.cameraCut = frame == 10; if (frame == 15) backend.ResetHistory(); input.frame = ++count; input.deltaMilliseconds = 16.67f;
            if (!backend.Evaluate(input)) return 1;
            D3D11_VIEWPORT restored;
            unsigned viewports = 1;
            context->RSGetViewports(&viewports, &restored);
            if (viewports != 1 || restored.TopLeftX != 7 || restored.Height != 321)
            { std::puts("FAIL: FSR3 damaged the engine's viewport state"); return 1; }
            ComPtr<ID3D11RenderTargetView> restoredTarget;
            ComPtr<ID3D11ShaderResourceView> restoredMotion;
            context->OMGetRenderTargets(1, &restoredTarget, nullptr);
            context->PSGetShaderResources(3, 1, &restoredMotion);
            if (restoredTarget.Get() != target.Get() || restoredMotion.Get() != motionView.Get())
            { std::puts("FAIL: FSR3 damaged engine resource bindings"); return 1; }
        }
        // Force a readback: successful API results alone do not prove valid output.
        D3D11_TEXTURE2D_DESC desc;
        backend.Output()->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) return 1;
        context->CopyResource(staging.Get(), backend.Output());
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 1;
        const auto* row = reinterpret_cast<const unsigned short*>(static_cast<const unsigned char*>(mapped.pData) + mapped.RowPitch * (display.height / 2));
        const unsigned short channel = row[(display.width / 2) * 4];
        context->Unmap(staging.Get(), 0);
        // FP16 0.5 is 0x3800. Allow reconstruction rounding, reject black/NaN/stale output.
        if (channel < 0x3700 || channel > 0x3900) { std::printf("FAIL: output grey is 0x%04X\n", channel); return 1; }
        // Invalid input must fail closed, without an assertion or repeated NGX calls.
        fsr3::D3D11Backend::Frame invalid;
        if (backend.Evaluate(invalid) || backend.Ready() || backend.Evaluate(invalid))
        { std::puts("FAIL: invalid inputs did not disable FSR3"); return 1; }
        backend.Destroy(); // Simulate vid_restart and method switches.
        context->ClearState();

    }
    backend.Destroy();
    if (FAILED(device->GetDeviceRemovedReason())) { std::puts("FAIL: device removed"); return 1; }
    unsigned errors = 0;
    for (UINT64 i = 0; i < debug->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i)
    {
        SIZE_T length = 0;
        debug->GetMessage(i, nullptr, &length);
        std::vector<unsigned char> storage(length);
        auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
        if (SUCCEEDED(debug->GetMessage(i, message, &length)) &&
            message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING)
        {
            std::printf("[D3D11] severity=%u id=%u %s\n", unsigned(message->Severity), unsigned(message->ID), message->pDescription);
            if (message->Severity <= D3D11_MESSAGE_SEVERITY_ERROR) ++errors;
        }
    }
    if (errors) return 1;
    if (level110)
    {
        std::puts("FSR3 feature level 11.0 rejection passed; no shader creation errors or active output.");
        return 0;
    }
    std::printf("FSR3 backend test passed: %u evaluations, %u feature lifetimes, output readbacks, safe failure and viewport restoration; zero D3D11 errors.\n", count, lifetimes);
}
