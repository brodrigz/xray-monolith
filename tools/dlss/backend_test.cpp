#include "../../src/Layers/xrRenderPC_R4/Dlss/DlssD3D11.h"
#include <cstdio>
#include <vector>
#include <string>
#include <d3d11sdklayers.h>

using Microsoft::WRL::ComPtr;
static void Log(const char* stage, unsigned result) { std::printf("[DLSS test] %s: 0x%08X\n", stage, result); }

static ComPtr<ID3D11Texture2D> Texture(ID3D11Device* device, dlss::Size size, DXGI_FORMAT format, const void* data, unsigned pixelBytes)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = size.width; desc.Height = size.height;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = format;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial = {data, size.width * pixelBytes, 0};
    ComPtr<ID3D11Texture2D> result;
    if (FAILED(device->CreateTexture2D(&desc, &initial, &result))) return {};
    return result;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) { std::puts("Usage: backend_test.exe <existing writable cache directory>"); return 2; }
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_DEBUG, levels, 2, D3D11_SDK_VERSION,
        &device, &level, &context);
    if (FAILED(hr)) { Log("D3D11CreateDevice", hr); return 1; }
    ComPtr<ID3D11InfoQueue> debug;
    if (FAILED(device.As(&debug))) return 1;
    dlss::D3D11Backend backend;
    if (!backend.Initialize(device.Get(), argv[1], Log)) return 1;
    const dlss::Size display{1280, 720};
    unsigned count = 0;
    unsigned lifetimes = 0;
    for (const auto preset : {dlss::Preset::M, dlss::Preset::L, dlss::Preset::K, dlss::Preset::J, dlss::Preset::Default})
    for (const auto quality : {dlss::Quality::Quality, dlss::Quality::Balanced, dlss::Quality::Performance,
        dlss::Quality::UltraPerformance, dlss::Quality::DLAA})
    {
        dlss::Size render;
        if (!backend.OptimalSize(quality, display, render) || !backend.Create(quality, render, display, false, preset)) return 1;
        ++lifetimes;
        std::printf("[DLSS test] quality=%u preset=%u render=%ux%u output=%ux%u\n", unsigned(quality), unsigned(preset), render.width, render.height, display.width, display.height);
        std::vector<unsigned> colors(size_t(render.width) * render.height, 0xff808080);
        std::vector<float> depths(colors.size(), 0.5f), motion(colors.size() * 2, 0.0f);
        auto color = Texture(device.Get(), render, DXGI_FORMAT_R8G8B8A8_UNORM, colors.data(), 4);
        auto depth = Texture(device.Get(), render, DXGI_FORMAT_R32_FLOAT, depths.data(), 4);
        auto vectors = Texture(device.Get(), render, DXGI_FORMAT_R32G32_FLOAT, motion.data(), 8);
        if (!color || !depth || !vectors) return 1;
        const D3D11_VIEWPORT viewport{7, 11, 123, 321, 0, 1};
        for (unsigned frame = 0; frame < 20; ++frame)
        {
            context->RSSetViewports(1, &viewport);
            dlss::D3D11Backend::Frame input;
            input.color = color.Get(); input.depth = depth.Get(); input.motion = vectors.Get();
            input.jitter = dlss::Jitter(frame, render, display);
            input.frame = ++count; input.deltaMilliseconds = 16.67f;
            if (!backend.Evaluate(input)) return 1;
            D3D11_VIEWPORT restored;
            unsigned viewports = 1;
            context->RSGetViewports(&viewports, &restored);
            if (viewports != 1 || restored.TopLeftX != 7 || restored.Height != 321)
            { std::puts("FAIL: NGX damaged the engine's viewport state"); return 1; }
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
        dlss::D3D11Backend::Frame invalid;
        if (backend.Evaluate(invalid) || backend.Ready() || backend.Evaluate(invalid))
        { std::puts("FAIL: invalid inputs did not disable DLSS"); return 1; }
        backend.ReleaseFeature(); // Simulate vid_restart: retain device NGX lifetime.
        if (!backend.Initialize(device.Get(), argv[1], Log)) return 1;
    }
    backend.Shutdown();
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
    std::printf("DLSS backend test passed: %u evaluations, %u feature lifetimes, output readbacks, safe failure and viewport restoration; zero D3D11 errors.\n", count, lifetimes);
}
