#include "stdafx.h"
#include "DlssIntegration.h"
#include "DlssD3D11.h"
#include "Fsr3D3D11.h"
#include "../../../xrEngine/igame_persistent.h"

namespace
{
dlss::D3D11Backend backend;
fsr3::D3D11Backend fsrBackend;
bool useFsr = false;
Microsoft::WRL::ComPtr<ID3D11Texture2D> sceneDepth;
Microsoft::WRL::ComPtr<ID3D11DepthStencilView> sceneDepthView;
dlss::Size renderSize;
dlss::Offset rasterJitter;
dlss::Offset previousJitter;
bool inScene = false, mainView = false, configured = false;
unsigned jitterIndex = 0;
Fvector lastPosition, lastDirection;
float lastFov = 0;
bool haveCamera = false;
bool reportedEvaluation = false;

void LogDlss(const char* stage, unsigned result) { Msg("! [DLSS] %s (0x%08X)", stage, result); }
void LogFsr(const char* stage, unsigned result) { Msg("! [FSR3] %s (0x%08X)", stage, result); }
const char* Method() { return useFsr ? "FSR3" : "DLSS"; }
void ResetHistory() { backend.ResetHistory(); fsrBackend.ResetHistory(); }
}

unsigned RenderScreenWidth() { return inScene ? dlss::RenderWidth() : Device.dwWidth; }
unsigned RenderScreenHeight() { return inScene ? dlss::RenderHeight() : Device.dwHeight; }

namespace dlss
{
void InitializeTargets()
{
    ReleaseTargets();
    renderSize = {Device.dwWidth, Device.dwHeight};
    if (!ps_r_upscaler || (ps_r_upscaler == 1 && !ps_r_dlss_quality)) return;
    useFsr = ps_r_upscaler == 2;
    string_path marker;
    if (!RImplementation.o.ssfx_motionvectors || !FS.exist(marker, "$game_shaders$", "r3\\dlss_contract.h"))
    {
        Msg("! [%s] Disabled: the matching SSS 23 temporal-upscaling shader override is required.", Method());
        return;
    }
    if (RImplementation.o.dx10_msaa)
    {
        Msg("! [%s] Disabled: turn MSAA off and run vid_restart.", Method());
        return;
    }
    Size size;
    const Size display{Device.dwWidth, Device.dwHeight};
    if (useFsr)
    {
        // Release NGX as well when switching methods; FSR does not require NVIDIA.
        backend.Shutdown();
        if (!fsr3::D3D11Backend::OptimalSize(ps_r_fsr3_quality, display, size) ||
            !fsrBackend.Create(HW.pDevice, size, display, RImplementation.o.dx11_hdr10, LogFsr)) return;
    }
    else
    {
        string_path cache;
        FS.update_path(cache, "$app_data_root$", "");
        wchar_t wideCache[MAX_PATH] = {};
        if (!MultiByteToWideChar(CP_ACP, 0, cache, -1, wideCache, MAX_PATH)) return;
        if (!backend.Initialize(HW.pDevice, wideCache, LogDlss)) return;
        const auto quality = static_cast<Quality>(ps_r_dlss_quality);
        if (!backend.OptimalSize(quality, display, size) ||
            !backend.Create(quality, size, display, RImplementation.o.dx11_hdr10, static_cast<Preset>(ps_r_dlss_preset))) return;
    }

    D3D11_TEXTURE2D_DESC depth = {};
    depth.Width = size.width;
    depth.Height = size.height;
    depth.MipLevels = depth.ArraySize = depth.SampleDesc.Count = 1;
    depth.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depth.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    D3D11_DEPTH_STENCIL_VIEW_DESC view = {};
    view.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    view.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    HRESULT result = HW.pDevice->CreateTexture2D(&depth, nullptr, &sceneDepth);
    if (SUCCEEDED(result)) result = HW.pDevice->CreateDepthStencilView(sceneDepth.Get(), &view, &sceneDepthView);
    if (FAILED(result))
    {
        (useFsr ? LogFsr : LogDlss)("Scene depth allocation failed; retaining native resolution", result);
        ReleaseTargets();
        return;
    }
    renderSize = size;
    configured = true;
    if (useFsr)
        Msg("[FSR3] 3.1.2 DX11 upscaler: quality=%u scene=%ux%u display=%ux%u; changes require vid_restart",
            ps_r_fsr3_quality, size.width, size.height, display.width, display.height);
    else
        Msg("[DLSS] quality=%u requested preset=%u scene=%ux%u display=%ux%u; changes require vid_restart",
            ps_r_dlss_quality, ps_r_dlss_preset, size.width, size.height, display.width, display.height);
}

void ReleaseTargets()
{
    inScene = mainView = configured = false;
    jitterIndex = 0;
    haveCamera = false;
    reportedEvaluation = false;
    rasterJitter = {};
    previousJitter = {};
    backend.ReleaseFeature();
    fsrBackend.Destroy();
    useFsr = false;
    sceneDepthView.Reset();
    sceneDepth.Reset();
    renderSize = {};
}

void ShutdownDevice() { ReleaseTargets(); backend.Shutdown(); }
unsigned RenderWidth() { return renderSize.Valid() ? renderSize.width : Device.dwWidth; }
unsigned RenderHeight() { return renderSize.Valid() ? renderSize.height : Device.dwHeight; }
bool Configured() { return configured; }
bool Active() { return configured && inScene && mainView && (useFsr ? fsrBackend.Ready() : backend.Ready()); }
Offset RasterJitter() { return rasterJitter; }
Offset PreviousRasterJitter() { return previousJitter; }
ID3DDepthStencilView* SceneDepth() { return inScene && configured ? sceneDepthView.Get() : HW.pBaseZB; }
ID3D11Texture2D* Output() { return useFsr ? fsrBackend.Output() : backend.Output(); }

void BeginScene()
{
    inScene = true;
    if (configured)
    {
        const D3D11_VIEWPORT viewport{0, 0, float(RenderWidth()), float(RenderHeight()), 0, 1};
        HW.pContext->RSSetViewports(1, &viewport);
    }
    // Scope frames render spatially at the selected scene resolution. They must
    // neither consume a jitter sample nor overwrite the main camera's history.
    mainView = !Device.m_SecondViewport.IsSVPFrame();
    if (Active())
    {
        if (haveCamera && (lastPosition.distance_to_sqr(Device.vCameraPosition) > 64.0f * 64.0f ||
            lastDirection.dotproduct(Device.vCameraDirection) < 0.25f || _abs(lastFov - Device.fFOV) > 10.0f))
        {
            ResetHistory();
            jitterIndex = 0;
        }
        lastPosition = Device.vCameraPosition;
        lastDirection = Device.vCameraDirection;
        lastFov = Device.fFOV;
        haveCamera = true;
        SSManager.SetMipLODBias(ps_r__tf_Mipbias + log2f(float(RenderWidth()) / Device.dwWidth));
    }
    rasterJitter = Active() ? Jitter(jitterIndex++, renderSize, {Device.dwWidth, Device.dwHeight}) : Offset{};
}

void EndScene(bool invalidateHistory)
{
    if (configured && inScene && mainView) previousJitter = rasterJitter;
    if (configured && inScene) SSManager.SetMipLODBias(ps_r__tf_Mipbias);
    inScene = mainView = false;
    rasterJitter = {};
    if (invalidateHistory) { ResetHistory(); jitterIndex = 0; haveCamera = false; previousJitter = {}; }
}

bool Evaluate(ID3D11Texture2D* color, ID3D11Texture2D* motion)
{
    if (!Active()) return false;
    fsr3::D3D11Backend::Frame frame;
    frame.color = color;
    frame.depth = sceneDepth.Get();
    frame.motion = motion;
    frame.jitter = rasterJitter;
    frame.deltaMilliseconds = Device.fTimeDelta * 1000.0f;
    frame.frame = Device.dwFrame;
    // Derive the actual main-view near/far/FOV from its projection, including
    // camera overrides, rather than assuming the environment's nominal range.
    frame.cameraNear = -Device.mProject._43 / Device.mProject._33;
    frame.cameraFar = -Device.mProject._43 / (Device.mProject._33 - 1.0f);
    frame.verticalFov = 2.0f * atanf(1.0f / Device.mProject._22);
    const bool success = useFsr ? fsrBackend.Evaluate(frame) : backend.Evaluate(frame);
    if (success && !reportedEvaluation)
    {
        Msg("[%s] First scene evaluation succeeded: frame=%u, scene=%ux%u, jitter=(%.4f,%.4f), MV scale=(-%u,-%u)",
            Method(), Device.dwFrame, RenderWidth(), RenderHeight(), rasterJitter.x, rasterJitter.y, RenderWidth(), RenderHeight());
        reportedEvaluation = true;
    }
    else if (!success)
        Msg("! [%s] Evaluation disabled until vid_restart; using spatial upscale (no stale temporal output).", Method());
    return success;
}
}
