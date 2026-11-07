# Frame generation: laser and reticle afterimages

Status: steps 1 to 4 built; step 1 passed on FSR frame generation, DLSS-G not tested yet. The mask and fill are not tested in game yet. Decided on 2026-11-02 to do this after the 0.71 package.

## The problem

With frame generation on, fast camera moves make two dim dots orbit a laser dot or red dot reticle, whichever upscaler is active. FSR frame generation shows it most. DLSS-G has it too but hides it a little better. Frame generation off is clean.

## Cause, from AMD's source

This section only explains FSR. DLSS-G is closed source, so a similar cause there is an assumption.

- `ffx_frameinterpolation.h:102` samples the previous frame at `+motion` and the current frame at `-motion`, then blends them 50/50. Background pixels that land on the dot pick up its color, one copy from each frame at half strength. That matches the two dim dots.
- With a hudless image attached, those source frames are the hudless copies.
- `ffx_frameinterpolation_inpainting.h:116` blends the generated pixel toward the final frame wherever hudless and final differ, reaching full weight at about 10% difference in linear (`CalculateStaticContentFactor`, `ffx_frameinterpolation_common.h:199`).

The source in `sdk/FSR3` is the vendored tree (its README says 3.1.2). The runtime is the 3.1.6 provider, whose behaviour hasn't been confirmed. Step 1 below tests that.

## Options compared

| Option | Verdict |
|---|---|
| Real motion vectors | Doesn't stop background pixels reading the dot, so the ghosts stay. Medium effort: the laser dot has no stored previous position. |
| Motion vectors plus fake depth | Could suppress the ghosts from game motion vectors, but the optical flow branch can bring them back (`ffx_frameinterpolation.h:170`). Needs variants of the pack's shaders. |
| Draw them after the hudless capture, or use AMD's UI texture | Same clean result, but rejected: no bloom or grading, and it draws over gas mask and night vision. |
| Run the post chain a second time without them | Accurate, but far too expensive. |
| **Hole-fill the hudless copy** | Chosen. Removes the source, and they keep bloom and grading in the real frame. |

AMD documents three ways to handle UI, and none mention lasers or reticles. It warns that the hudless method can leave minor artifacts on semi-transparent elements.

These verdicts come from AMD's source, so they may not hold for DLSS-G. Real motion vectors in particular could behave differently there.

## Plan

1. **Rectangle test.** Add a dev toggle that fills a fixed rectangle at screen center in the hudless copy, with no mask. Point the laser into it during fast turns, on FSR frame generation and on DLSS-G. Continue for a runtime only if the ghosts inside it disappear and the real dot stays. This is the first evidence of how DLSS-G responds.
   - Built as `r_fg_hudless_test N` (DX12, not saved): the square reaches N% of the screen height from the centre, 0 is off. It needs `dlss_fg_hudless_test.ps` from the compatibility patch. The first frame logs `[FG test]` with the square's size, the runtime and the FSR frame generation and DLSS-G module versions.
   - 2026-11-06, FSR frame generation 3.1.6, RTX 4070, 1920x1080, 172 px square: inside it the afterimages were completely gone and the dot stayed. DLSS-G not tested yet.
2. **Mask.** Add a display-size color target and redraw only the lasers and reticles into it, filtered by pixel shader name (`models_laserbeam`, `models_reflex_reticle`). Glass, particles and lens contents stay out. It needs a color target rather than a single channel, so green lasers register too.
   - Lasers: redraw next to `dlss_post_effects`. `mapDlssEffects` also holds particles, so filter it.
   - Reticles: redraw next to `render_Reticle` in `phase_dlss_scope`. `mapScopeHUDSorted` also holds lenses, so filter it.
   - `r_dsgraph_render_graph_sorted` clears its list by default, so the mask draw must pass `_clear = false` or copy the items.
   - Some reticle shaders mark stencil 254 and cutouts test it, so check that the extra draw doesn't disturb that.
   - Built: `CDSGraphManager::r_dsgraph_render_fg_mask` copies the matching items and draws the copies, so the lists are untouched. The mask (`$user$fg_mask`, RGBA8, display size) is cleared on the frame's first mask draw. Beams are drawn just before the real draw in `dlss_post_effects`, with the same depth and position inputs, in both post-after modes; reticles just before `render_Reticle`. The reflex reticle shaders don't use the stencil.
   - Holo and red dot reticles without `scopelense` (most of 3DSS's `models_reflex_reticle*`) are drawn in the scene from `mapHUDSorted`, not the scope pass, so the first build missed them: the laser filled, the dot stayed. `r_dsgraph_extract_fg_reticles` now copies them at extraction, and they are drawn into the mask after the scope pass with the HUD projection, unjittered at display size, against the reprojected depth. Scope lenses (`models_scope_reticle*`) stay out, since they hold the magnified frame.
3. **Fill.** Just before `fg::CaptureHudless` in `phase_combine`, copy the frame, fill masked pixels from nearby unmasked ones, and give that image to `D3D12PresentBridge::CaptureHudless` instead of `shared11`. FSR and DLSS-G both read this capture (`inputs12[2]`), so one fill can serve both. Enable it only for the runtimes that passed step 1. Skip everything on frames with no laser or reticle.
   - Built as `dlss_fg_laser_fill.ps`: a pixel is filled when the mask, or one of its 8 neighbours, is above 3/255. In each of 8 directions it takes the pixel after the first two clear ones, within 64 px, weighted by inverse distance. Nothing clear in reach keeps the pixel. It runs on both runtimes, since DLSS-G is not tested yet. `r_fg_hudless_test` takes precedence.
4. **Checks.** Add a console toggle for comparing and a mask debug view. Test lasers on the weapon and on NPCs, 3D scope reticles, gas mask and night vision.
   - Built as `r_fg_laser_fill 0/1` (DX12, saved, default 1) and `r_fg_laser_fill_view 0/1` (not saved). The view presents the filled image itself, so the beams and reflex dots should disappear from the screen; it works without frame generation, as long as an upscaler is on. The first filled frame logs `[FG] Laser beams and reflex reticles filled` with the runtime versions.

## Cost

Estimates, not measurements:

- GPU: two small redraws, a frame copy and one fill pass, skipped on frames with no laser or reticle.
- VRAM: three display-size 32-bit targets (the mask, the frame copy and the filled copy), about 44 MB at 1440p, allocated on the first frame with a beam or reticle and frame generation on.

## Known limits

- **Faint beams:** below the 10% difference they only partly return in generated frames, so they could pulse slightly.
- **Bloom halo:** it stays in the hudless copy, so a dim, soft ghost of the glow could remain.
- **Timing:** generated frames show the dot at its current-frame position, about half a frame early.
- **Upscaler only:** the beams and reticles are only held out of the scene with an upscaler, and reticles only when the scope pass runs after it.
- **Inside scopes:** the lenses magnify the frame with the beams in it; those beams are not filled.
- **Gas mask:** its distortion could misalign the mask slightly.
- **Test builds:** frame generation only exists in `AnomalyDX12.exe`.

## Sources

- [FidelityFX Super Resolution 3.1.4: upscaling and frame generation](https://gpuopen.com/?p=69255)
- [FidelityFX SDK manual: super resolution interpolation](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-interpolation/)
