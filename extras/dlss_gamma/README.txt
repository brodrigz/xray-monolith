Experimental DLSS / FSR3 shader override for the audited GAMMA profile
=================================================================

Place this folder as its own MO2 mod, with priority after the original shader
mods, only when explicitly installing/testing the matching DLSS engine.
Do not replace the original mods. Disable this override when reverting engines.
These overrides have new engine constants and must not be used with stock bins.

Audited enabled loose-file providers in G.A.M.M.A. Co-op, 2026-10-07:
  190- Screen Space Shaders 23 - Ascii1457:
    screenspace_mvectors.h, deffer_grass.vs,
    ssfx_ssr.ps, ssfx_sss.ps, ssfx_water_ssr.ps
  Atmospherics 2.69 RC7.5:
    deffer_impl_flat.ps, ssfx_ao.ps, ssfx_il.ps, ssfx_sss_ext.ps
  189- Beef's NVG - theRealBeef:
    night_vision.h
  New integration files:
    dlss_contract.h, dlss_copy.ps, dlss_cas.h, dlss_sharpen.ps, dlss_ui_depth.ps
    scripts/modxml_dlss_options.script
    configs/text/eng/st_monolith_dlss.xml

Settings > Video > Basic includes an Off / NVIDIA DLSS / AMD FSR 3 selector.
The other controls change immediately with the pending method selection:
Off hides them; DLSS shows quality, model preset and sharpening; FSR3 shows
quality and sharpening. Switching preserves each method's pending quality.
Only Apply changes console settings/restarts the renderer; Cancel discards them.
The dual-method UI has no second Off entry in the DLSS quality dropdown.
FSR3 quality: Native AA/Quality/Balanced/Performance/Ultra Performance.
Only the selected method runs. FSR3 uses OGSR's native DX11 FSR 3.1.2 backend;
it requires D3D feature level 11.1, but no NVIDIA runtime or DX12 bridge.
No Frame Generation. The engine logs its negotiated feature level at startup.
The same shader override supplies both methods; no second SSS patch is needed.
Method/quality changes apply on vid_restart, never halfway through a frame.
Selecting NVIDIA DLSS shows quality and render preset dropdowns.
Quality: DLAA/Quality/Balanced/Performance/Ultra Performance.
Preset: Default/J/K/L/M (SR preset F is deprecated in DLSS 310.9.1).
Apply saves the settings and restarts the renderer. Turn MSAA off before enabling.
The shared Upscaling sharpening (CAS) slider ranges from 0 (off, default) to 1 (maximum).
Unlike mode/preset changes, sharpness alone applies without restarting the
renderer. It filters reconstructed DLSS/DLAA/FSR3 color before display overlays/UI,
inside the existing resolve draw. Atmospherics clarity/NV effects are unchanged.
The UI hook is additive; it does not replace GAMMA's ui_options.script.
English strings also provide the engine's localization fallback.
Use NVIDIA's release nvngx_dlss.dll 310.9.1 beside AnomalyDX11.exe; source and
checksum are recorded in tools/dlss/README.txt. No Frame Generation is included.

Changes: non-jittered MV convention retained; bounded homogeneous division;
uniform DLSS grass jitter and previous wind motion; flat-material MV output;
jitter-aware SSS history reprojection; NV depth loads use physical depth size.
Existing material/weather/NV customization and attribution remain in place.

The matching engine draws the handheld PDA screen after reconstruction, at
display resolution. dlss_ui_depth.ps supplies unjittered occlusion depth so
hands/casing still hide the screen. Update BOTH binaries and this patch; older
patches without that shader are rejected with an explicit upscaler log message.
The flat 2D PDA/menu already renders after the scene at display resolution.

This is a profile-specific experimental override, not a generic SSS distribution.
Other versions or higher-priority overrides require re-merging and testing.
See tools/dlss/README.txt in the engine repository for controls and limitations.
