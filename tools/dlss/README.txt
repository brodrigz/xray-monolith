Native DX11 DLSS / FSR3 Super Resolution - experimental renderer integration
=====================================================================

Target: standard xray-monolith DX11 binaries with the enabled loose-file shader
providers in D:\GAMMA\profiles\G.A.M.M.A. Co-op (audited 2026-10-07).
No Frame Generation, Streamline or DX12 bridge is included.

Status
------
The backend is connected to the renderer and builds in Release. This is an
experimental port, NOT a gameplay/image-quality sign-off. The isolated GPU test
does not validate moving game geometry, transparency, scopes or mod interactions.
Do not distribute this as a production-ready GAMMA patch.

Controls
--------
r_upscaler off|dlss|fsr3
r_fsr3_quality native_aa|quality|balanced|performance|ultra_performance
r_dlss_quality off|dlaa|quality|balanced|performance|ultra_performance
r_dlss_preset default|j|k|l|m
r_dlss_sharpness 0..1
Run vid_restart after changing method/quality/preset (or restart the engine).
The method is latched until target recreation; changing a pending console value
cannot dispatch a different backend into existing targets.
Only the selected upscaler runs. The default method is DLSS to preserve existing
DLSS-only configs, but DLSS quality defaults to Off (native rendering).
FSR3 defaults to Quality. Its quality is independent of the saved DLSS mode.
The legacy r_dlss_sharpness name now controls the SAME CAS resolve for either
method; it is labelled Upscaling sharpening in the UI. DLSS model presets only
affect DLSS. FSR3 requires no NGX initialization or NVIDIA runtime.
Sharpness applies on the next rendered frame after Apply, without vid_restart.
Default 0 disables sharpening. This is an OGSR-derived CAS filter, not NGX's
deprecated sharpening. It is fused into the existing display resolve draw;
positive strength adds neighborhood sampling, not another pass/copy/target.
Zero uses the original copy shader. Upscaling-off, fallback and scope-only frames do
not apply this filter. Display overlays/UI follow it; no DLSS history is modified.
Existing Atmospherics clarity and NV sharpening are left unchanged.
The additive modxml_dlss_options.script adds the controls to Video > Basic.
Its single method selector dynamically filters the displayed controls using
pending values: DLSS quality/preset/CAS, FSR3 quality/CAS, or none for Off.
The canonical options tree remains intact for Apply/Cancel and independent
quality persistence. Control rebuilding is deferred until after the selection
callback. Old dlss+quality=off configs display Off; selecting DLSS stages Quality
if no enabled quality exists. Returning to Off cannot accidentally enable DLSS.
Apply saves both console values and requests vid_restart; Cancel does not apply
pending changes. No full ui_options.script replacement is needed.
Default lets NVIDIA choose the model for the scaling mode. Explicit presets are
requests, not guarantees against driver overrides. Deprecated SR preset F and
reserved presets are deliberately omitted for the 310.9.1 runtime.
Native rendering is the default. Turn MSAA off before enabling either upscaler.
The matching shader override is required for either method. DLSS additionally
requires an NGX-compatible RTX GPU/driver; FSR3 requires feature level 11.1.
Initialization failure retains native resolution and the original AA path.
Evaluation failure disables the selected method until target recreation and
presents a spatial upscale, never stale temporal output.

Runtime/install layout
----------------------
Build the DX11 solution configuration, x64:
  MSBuild src\engine-vs2022.sln /t:xrEngine /p:Configuration=DX11 /p:Platform=x64 /m
This maps to ReleaseR4/Release projects. The output directory is named bin_dbg
even though this configuration uses Release compilation:
  _build\_game\bin_dbg\AnomalyDX11.exe

Supply NVIDIA's release nvngx_dlss.dll beside the executable. The local tests
use version 310.9.1.0 (latest public release verified 2026-10-07).
Official release: https://github.com/NVIDIA/DLSS/releases/tag/v310.9.1
Runtime: https://raw.githubusercontent.com/NVIDIA/DLSS/v310.9.1/lib/Windows_x86_64/rel/nvngx_dlss.dll
SHA256: 3975567B8943C53ACCE397F2B72380092F84F162D00B0D2C7D08A1025C563983
Size: 58,956,912 bytes; valid NVIDIA Authenticode signature.
Runtime DLLs are not tracked. The existing compatible NGX SDK is unchanged.
Keep the matching engine's normal dependencies. Do not add Streamline DLLs.

extras\dlss_gamma is a separate MO2 override mod: its gamedata folder must win
shader conflicts over the audited SSS/Atmospherics/NVG versions. These are
profile-specific overrides, not universal replacements for all SSS releases.
Do not overwrite the original mods. Installation/enabling in the live profile
is a separate, explicit step; development tests use isolated staging instead.

Integration contract
--------------------
Device dimensions stay display-sized; physically smaller scene RTs, viewport,
shader screen_res and shader-readable D24 depth use NGX's optimal dimensions.
PDA, second-viewport output and menu presentation remain display-sized.

SSS 23 writes non-jittered currentUV-minus-previousUV vectors. NGX receives
negative render-width/render-height motion scales; SSS's own buffer is unchanged.
One Halton sample is used for geometry and NGX, in pixels positive right/down.
Depth reconstruction accounts for the raster offset. SSS AO/IL/shadow/reflection
history sampling includes the previous-minus-current jitter offset.
Grass jitters uniformly under DLSS; the flat material variant exports its MVs.

Only main-camera frames advance DLSS history/jitter. Secondary scope frames use
spatial upscaling. Menu/loading, target recreation, large position/direction/FOV
changes invalidate history. SSS TAA is suppressed while DLSS targets are in use.

DLSS runs on scene color before the display-sized gas-mask, NV, heat-vision and
fake-scope overlay passes. Display overlays have separate source/destination RTs;
they cannot feed back into DLSS. Final postprocess and 2D UI follow.
IMPORTANT: the current scene resolve still follows bloom/DOF/scene combine,
including distortion and 3D shader-reticle rendering. Those paths need temporal
quality validation and may need further pass separation. HDR10 is not validated.

NGX initialization lasts for the real D3D11 device lifetime. vid_restart releases
the feature/targets, not global NGX. Shutdown occurs before device destruction.
A private D3D11.1 context state isolates NGX and restores engine bindings.
Production evaluation has no staging readbacks or explicit GPU waits.

Diagnostics
-----------
[DLSS] quality=... requested preset=... scene=... display=... confirms target creation.
[DLSS] First scene evaluation succeeded: ... confirms an actual game evaluation.
Failures include stage/result and report the spatial fallback. Logging is bounded.

Tests
-----
  MSBuild tools\dlss\temporal_test.vcxproj /p:Configuration=Release /p:Platform=x64
  MSBuild tools\dlss\backend_test.vcxproj /p:Configuration=Release /p:Platform=x64
  MSBuild tools\dlss\lua_runner.vcxproj /p:Configuration=Release /p:Platform=x64
  MSBuild tools\dlss\sharpen_test.vcxproj /p:Configuration=Release /p:Platform=x64
  _build\dlss_tests\temporal_test.exe
  _build\dlss_tests\backend_test.exe <existing-writable-cache-directory>
  _build\dlss_tests\lua_runner.exe tools\dlss\ui_test.lua
  _build\dlss_tests\sharpen_test.exe

Put nvngx_dlss.dll beside backend_test.exe. It requires an RTX adapter and the
D3D11 debug layer and creates no window. Tests cover five scaling modes times five
render presets, 500 evaluations, 25 feature lifetimes on one device/NGX lifetime,
gray readbacks, viewport
restoration and intentional invalid-input rejection; unsupported hardware fails.
Locally passed with runtime 310.9.1 and zero D3D11 errors. These are backend tests
only; they do not establish the exact model ultimately chosen by the driver.
The intentional invalid-input rejection logs are expected, not test failures.
The Lua runner uses the engine's built Release lua51.lib (build the engine first).
UI tests cover command tokens, insertion, rebuild/idempotence, unsupported
renderers and deferred Apply metadata. They do not replace an in-game UI test.
The CAS test executes the production HLSL using D3D11 WARP with the debug layer:
28 draws across four strengths; verifies zero bypass, constant black/grey/white/
HDR, alpha preservation, borders, finite extremes and monotonic sharpening.
Passed locally with zero D3D11 errors. The real GAMMA shader also compiled using
fxc ps_5_0. This does not replace subjective in-game sharpening evaluation.

An isolated Quality-mode gameplay smoke test with runtime 310.4 succeeded:
the engine logged its first NGX evaluation and the tester reported no obvious
visual problems. This is not exhaustive image-quality validation.

The effective profile's staged grass/material/SSS temporal shaders and the
display-overlay variants compile with fxc ps_5_0/vs_5_0. Existing upstream shader
warnings remain. This is not exhaustive shader-variant coverage.

Gameplay checks before release
------------------------------
Compare Off/DLAA/all scaling presets during camera motion and moving objects.
Check terrain, trees/grass, skinning, HUD weapons, sky, particles/transparency,
distortion, NV, masks, 3D/fake/secondary scopes, PDA and menus.
Exercise loading, camera cuts, vid_restart, fullscreen and unsupported runtime.
Compare DLSS-off against unchanged binaries. Validate HDR separately.
No visual-quality or performance claim follows from successful compilation.

Dependencies and provenance
---------------------------
sdk/FSR3 vendors OGSR's FSR 3.1.2/community DX11 backend and DXBC shaders.
No DX12 bridge, DLL download or Frame Generation. See its README.txt for
provenance, licenses and the constant-buffer/sampler corrections.
Both methods share the SSS shader contract, depth, UV-space motion, jitter,
scene dimensions, history invalidation, scope spatial fallback and CAS resolve.
FSR receives actual camera near/far/vertical FOV from the main projection.
Reactive/transparency masks are currently omitted (as in OGSR); particles,
transparent surfaces and disocclusions still need gameplay-quality evaluation.

Build tools/dlss/fsr3_test.vcxproj with Release|x64 and run
_build/dlss_tests/fsr3_test.exe (no NGX DLL needed). It checks all five FSR
qualities in SDR/HDR, 200 evaluations, 10 context lifetimes, D24S8 depth and
RGBA16F motion, output readbacks, camera cuts, explicit history reset,
invalid-input latching and engine viewport/resource-state restoration.
This does not replace GPU-vendor or in-game image-quality testing.
2026-10-08: FSR test passed on RTX 4070 and with --warp (200 evaluations each),
zero D3D11 errors/warnings. Existing DLSS (500 evaluations), temporal, CAS
(28 WARP draws) and menu tests also passed. Release DX11 engine built and
deployed to the isolated test launcher. After the feature-level correction below,
the gameplay log confirms FSR3 evaluation in Balanced, Quality and Native AA,
including renderer restarts. The tester reported normal visuals. Broader
image-quality, mod-interaction and cross-vendor validation is still pending.

FSR3 requires a D3D feature level 11.1 device (its precompiled compute shaders
use UAV slots beyond the eight available at 11.0). The engine now requests
11.1 before 11.0 and logs the selected level; older runtimes retry 11.0 when
the feature-level list is rejected. Engine shader profiles remain SM5 at 11.1.
On 11.0 hardware, FSR3 logs an explicit unsupported message and stays native.
The original engine requested only 11.0, causing FSR context creation to fail
with 0x8000000D even on capable GPUs. This was reproduced with the debug layer.
fsr3_test.exe --fl11_0 now verifies clean rejection without shader API errors;
normal hardware/WARP tests verify all five modes at 11.1. These tests passed
after the correction, together with DLSS, temporal and dynamic menu tests.

sdk/DLSS contains SDK headers, release /MD x64 nvsdk_ngx_d.lib and NVIDIA's
LICENSE.txt from the OGSR reference; no local OGSR path is needed to build.
Review NVIDIA's license before redistributing SDK/runtime files.
Shader overrides retain their original author/license notices. See
extras\dlss_gamma\README.txt for the exact audited providers.
