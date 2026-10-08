// Runs the actual native-PDA depth shader on D3D11 WARP, then draws a one-pixel
// checkerboard through its depth buffer to verify native UI and hand occlusion.
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr, const char* stage)
{
    if (FAILED(hr)) { std::printf("%s: 0x%08X\n", stage, unsigned(hr)); throw std::runtime_error(stage); }
}
static void Require(bool good, const char* stage) { if (!good) throw std::runtime_error(stage); }
static ComPtr<ID3DBlob> Compile(const char* source, const char* entry, const char* target)
{
    ComPtr<ID3DBlob> code, error;
    HRESULT hr = source ? D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr,
        entry, target, D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &error) :
        D3DCompileFromFile(L"extras/dlss_gamma/gamedata/shaders/r3/dlss_ui_depth.ps", nullptr,
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
    const char* shader = R"(
float4 VS(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0.5, 1);
}
float4 PS(float4 position : SV_Position) : SV_Target
{
    uint2 pixel = uint2(position.xy);
    float value = (pixel.x + pixel.y) & 1;
    return float4(value, value, value, 1);
})";
    auto vsCode = Compile(shader, "VS", "vs_5_0");
    auto uiCode = Compile(shader, "PS", "ps_5_0");
    auto depthCode = Compile(nullptr, "main", "ps_5_0");
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> depthPs, uiPs;
    Check(device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs), "VS");
    Check(device->CreatePixelShader(depthCode->GetBufferPointer(), depthCode->GetBufferSize(), nullptr, &depthPs), "Depth PS");
    Check(device->CreatePixelShader(uiCode->GetBufferPointer(), uiCode->GetBufferSize(), nullptr, &uiPs), "UI PS");
    constexpr unsigned width = 37, height = 23, depthMax = 0xffffff;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R24G8_TYPELESS; desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depth, depthRead, color, colorRead;
    Check(device->CreateTexture2D(&desc, nullptr, &depth), "Native depth");
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11DepthStencilView> dsv;
    Check(device->CreateDepthStencilView(depth.Get(), &dsvDesc, &dsv), "Native DSV");
    desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Check(device->CreateTexture2D(&desc, nullptr, &depthRead), "Depth staging");
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    Check(device->CreateTexture2D(&desc, nullptr, &colorRead), "Color staging");
    desc.BindFlags = D3D11_BIND_RENDER_TARGET; desc.Usage = D3D11_USAGE_DEFAULT; desc.CPUAccessFlags = 0;
    Check(device->CreateTexture2D(&desc, nullptr, &color), "Native color");
    ComPtr<ID3D11RenderTargetView> rtv;
    Check(device->CreateRenderTargetView(color.Get(), nullptr, &rtv), "Native RTV");
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE; dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    ComPtr<ID3D11DepthStencilState> copyState, uiState;
    Check(device->CreateDepthStencilState(&dsDesc, &copyState), "Copy depth state");
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    Check(device->CreateDepthStencilState(&dsDesc, &uiState), "UI depth state");
    D3D11_BLEND_DESC blendDesc = {};
    ComPtr<ID3D11BlendState> noColor;
    Check(device->CreateBlendState(&blendDesc, &noColor), "Depth-only color mask");
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = 16; cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> cb;
    Check(device->CreateBuffer(&cbDesc, nullptr, &cb), "Constants");
    ctx->VSSetShader(vs.Get(), nullptr, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->PSSetConstantBuffers(0, 1, cb.GetAddressOf());
    unsigned cases = 0;
    for (const auto size : {std::array<unsigned, 2>{37, 23}, {25, 15}, {19, 12}, {12, 8}, {1, 1}})
    {
        const unsigned rw = size[0], rh = size[1];
        std::vector<uint32_t> input(rw * rh);
        for (unsigned y = 0; y < rh; ++y) for (unsigned x = 0; x < rw; ++x)
        {
            const float z = (x == 0 && y == 0) ? 0.f : (x == rw - 1 && y == rh - 1) ? 1.f :
                x < rw / 2 ? 0.005f + float(y) * 0.00001f : 0.018f + float(y) * 0.00001f;
            input[y * rw + x] = uint32_t(std::round(z * depthMax)) | 0xac000000u;
        }
        desc.Width = rw; desc.Height = rh; desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{input.data(), rw * sizeof(uint32_t), 0};
        ComPtr<ID3D11Texture2D> source;
        Check(device->CreateTexture2D(&desc, &data, &source), "Source depth");
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        ComPtr<ID3D11ShaderResourceView> srv;
        Check(device->CreateShaderResourceView(source.Get(), &srvDesc, &srv), "Source SRV");
        for (const auto jitter : {std::array<float, 2>{0, 0}, {0.49f, 0.33f}, {-0.49f, -0.33f}, {0.49f, -0.49f}, {-0.49f, 0.49f}})
        {
            const std::array<float, 4> params{float(rw) / width, float(rh) / height, jitter[0], jitter[1]};
            ctx->UpdateSubresource(cb.Get(), 0, nullptr, params.data(), 0, 0);
            const float clear[4] = {0.25f, 0.25f, 0.25f, 1};
            ctx->ClearRenderTargetView(rtv.Get(), clear);
            ctx->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0, 0x5a);
            ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), dsv.Get());
            ctx->OMSetDepthStencilState(copyState.Get(), 0);
            ctx->OMSetBlendState(noColor.Get(), nullptr, ~0u);
            D3D11_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
            ctx->RSSetViewports(1, &viewport);
            ctx->PSSetShader(depthPs.Get(), nullptr, 0);
            ctx->PSSetShaderResources(0, 1, srv.GetAddressOf());
            ctx->Draw(3, 0);
            ctx->OMSetDepthStencilState(uiState.Get(), 0);
            ctx->OMSetBlendState(nullptr, nullptr, ~0u);
            viewport.MaxDepth = 0.02f; // Same compressed HUD range as rmNear.
            ctx->RSSetViewports(1, &viewport);
            ctx->PSSetShader(uiPs.Get(), nullptr, 0);
            ctx->Draw(3, 0);
            ctx->CopyResource(depthRead.Get(), depth.Get());
            ctx->CopyResource(colorRead.Get(), color.Get());
            D3D11_MAPPED_SUBRESOURCE zd, cd;
            Check(ctx->Map(depthRead.Get(), 0, D3D11_MAP_READ, 0, &zd), "Depth readback");
            Check(ctx->Map(colorRead.Get(), 0, D3D11_MAP_READ, 0, &cd), "Color readback");
            bool valid = true;
            for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x)
            {
                const int sx = std::clamp(int(std::floor((x + 0.5f) * params[0] + jitter[0])), 0, int(rw) - 1);
                const int sy = std::clamp(int(std::floor((y + 0.5f) * params[1] + jitter[1])), 0, int(rh) - 1);
                const uint32_t expected = input[sy * rw + sx] & depthMax;
                const auto actual = reinterpret_cast<const uint32_t*>(static_cast<const char*>(zd.pData) + y * zd.RowPitch)[x];
                valid &= std::abs(int(actual & depthMax) - int(expected)) <= 1 && (actual >> 24) == 0x5a;
                const auto pixel = reinterpret_cast<const float*>(static_cast<const char*>(cd.pData) + y * cd.RowPitch) + x * 4;
                const float expectedColor = double(expected) / depthMax < 0.01 ? 0.25f : float((x + y) & 1);
                valid &= pixel[0] == expectedColor && pixel[1] == expectedColor && pixel[2] == expectedColor && pixel[3] == 1;
            }
            ctx->Unmap(depthRead.Get(), 0); ctx->Unmap(colorRead.Get(), 0);
            Require(valid, "Native depth/occlusion/checkerboard mismatch");
            ++cases;
        }
    }
    for (UINT64 i = 0; i < debug->GetNumStoredMessages(); ++i)
    {
        SIZE_T size = 0; debug->GetMessage(i, nullptr, &size);
        std::vector<char> bytes(size);
        auto* message = reinterpret_cast<D3D11_MESSAGE*>(bytes.data());
        Check(debug->GetMessage(i, message, &size), "Debug message");
        if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING)
        {
            std::printf("%s\n", message->pDescription);
            throw std::runtime_error("D3D11 debug warning/error");
        }
    }
    ctx->ClearState(); ctx->Flush();
    std::printf("Native PDA depth tests passed: %u scale/jitter cases, depth/stencil, occlusion and native pixel detail.\n", cases);
}
catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
