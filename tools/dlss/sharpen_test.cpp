// Exercises the production CAS HLSL without a game window or NGX dependency.
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr, const char* stage)
{
    if (FAILED(hr)) { std::printf("%s: 0x%08X\n", stage, unsigned(hr)); throw std::runtime_error(stage); }
}
static ComPtr<ID3DBlob> Compile(const char* entry, const char* target)
{
    ComPtr<ID3DBlob> code, error;
    HRESULT hr = D3DCompileFromFile(L"tools/dlss/sharpen_test.hlsl", nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target, D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &error);
    if (error) std::printf("%s", static_cast<const char*>(error->GetBufferPointer()));
    Check(hr, "Compile");
    return code;
}
int main()
try
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> ctx;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_DEBUG,
        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &ctx), "Device");
    ComPtr<ID3D11InfoQueue> debug;
    Check(device.As(&debug), "Debug layer");
    auto vsCode = Compile("VS", "vs_5_0"), psCode = Compile("PS", "ps_5_0");
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    Check(device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs), "VS");
    Check(device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &ps), "PS");
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = desc.Height = 16; desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> input, output, staging;
    Check(device->CreateTexture2D(&desc, nullptr, &input), "Input");
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    Check(device->CreateTexture2D(&desc, nullptr, &output), "Output");
    desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Check(device->CreateTexture2D(&desc, nullptr, &staging), "Staging");
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11RenderTargetView> rtv;
    Check(device->CreateShaderResourceView(input.Get(), nullptr, &srv), "SRV");
    Check(device->CreateRenderTargetView(output.Get(), nullptr, &rtv), "RTV");
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler;
    Check(device->CreateSamplerState(&samplerDesc, &sampler), "Sampler");
    D3D11_BUFFER_DESC constantsDesc = {};
    constantsDesc.ByteWidth = 16; constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> constants;
    Check(device->CreateBuffer(&constantsDesc, nullptr, &constants), "Constants");
    D3D11_VIEWPORT viewport{0, 0, 16, 16, 0, 1};
    ctx->RSSetViewports(1, &viewport);
    ctx->VSSetShader(vs.Get(), nullptr, 0);
    ctx->PSSetShader(ps.Get(), nullptr, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->PSSetShaderResources(0, 1, srv.GetAddressOf());
    ctx->PSSetSamplers(0, 1, sampler.GetAddressOf());
    ctx->PSSetConstantBuffers(0, 1, constants.GetAddressOf());
    ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
    using Pixel = std::array<float, 4>;
    std::array<Pixel, 256> pixels;
    unsigned draws = 0;
    for (unsigned pattern = 0; pattern < 7; ++pattern)
    {
        for (unsigned y = 0; y < 16; ++y) for (unsigned x = 0; x < 16; ++x)
        {
            float value = pattern == 0 ? 0.f : pattern == 1 ? 0.5f : pattern == 2 ? 1.f :
                pattern == 3 ? 10000.f : pattern == 4 ? (x == 8 && y == 8 ? 0.6f : 0.3f) :
                pattern == 5 ? float(x) / 15.f : ((x + y) % 2 ? 0.f : 65504.f);
            pixels[y * 16 + x] = {value, value, value, float(x + 1) / 16.f};
        }
        ctx->UpdateSubresource(input.Get(), 0, nullptr, pixels.data(), 16 * sizeof(Pixel), 0);
        float previousDelta = -1.f;
        for (float strength : {0.f, 0.25f, 0.5f, 1.f})
        {
            const Pixel data{strength, 0, 0, 0};
            ctx->UpdateSubresource(constants.Get(), 0, nullptr, data.data(), 0, 0);
            ctx->Draw(3, 0);
            ctx->CopyResource(staging.Get(), output.Get());
            D3D11_MAPPED_SUBRESOURCE mapped;
            Check(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Readback");
            float centerDelta = 0;
            bool valid = true;
            for (unsigned y = 0; y < 16; ++y)
            {
                const auto* row = reinterpret_cast<const Pixel*>(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch);
                for (unsigned x = 0; x < 16; ++x)
                {
                    const auto& original = pixels[y * 16 + x];
                    for (unsigned c = 0; c < 4; ++c)
                    {
                        const float v = row[x][c];
                        valid &= std::isfinite(v) && v >= 0 && v <= 65504;
                        if (!strength || c == 3 || pattern < 4)
                            valid &= std::abs(v - original[c]) <= 0.002f * std::fmax(1.f, original[c]);
                    }
                    if (x == 8 && y == 8) centerDelta = row[x][0] - original[0];
                }
            }
            ctx->Unmap(staging.Get(), 0);
            if (!valid) throw std::runtime_error("non-finite/bypass/flat/alpha/border check");
            if (pattern == 4)
            {
                if (centerDelta < previousDelta || (strength > 0 && centerDelta <= 0))
                    throw std::runtime_error("slider must monotonically sharpen the bright detail");
                previousDelta = centerDelta;
            }
            ++draws;
        }
    }
    ctx->ClearState();
    for (UINT64 n = 0; n < debug->GetNumStoredMessagesAllowedByRetrievalFilter(); ++n)
    {
        SIZE_T size = 0;
        debug->GetMessage(n, nullptr, &size);
        std::vector<char> bytes(size);
        auto* message = reinterpret_cast<D3D11_MESSAGE*>(bytes.data());
        Check(debug->GetMessage(n, message, &size), "Debug message");
        if (message->Severity <= D3D11_MESSAGE_SEVERITY_ERROR)
            throw std::runtime_error(message->pDescription);
    }
    std::printf("CAS shader passed: %u WARP draws, zero bypass, flat black/grey/white/HDR, alpha, borders, finite extremes and monotonic strength; zero D3D11 errors.\n", draws);
    return 0;
}
catch (const std::exception& e) { std::printf("FAIL: %s\n", e.what()); return 1; }
