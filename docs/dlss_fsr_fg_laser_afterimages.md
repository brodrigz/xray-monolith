# FSR frame generation: laser and reticle afterimages

Status: planned, nothing built. Decided on 2026-11-02 to do this after the 0.71 package.

## The problem

With FSR frame generation on, fast camera moves make two dim dots orbit a laser dot or red dot reticle. DLSS-G and frame generation off are clean, whichever upscaler is active.

## Cause, from AMD's source

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

## Plan

1. **Rectangle test.** Add a dev toggle that fills a fixed rectangle at screen center in the hudless copy, with no mask. Point the laser into it during fast turns. Continue only if the ghosts inside it disappear and the real dot stays.
2. **Mask.** Add a display-size color target and redraw only the lasers and reticles into it, filtered by pixel shader name (`models_laserbeam`, `models_reflex_reticle`). Glass, particles and lens contents stay out. It needs a color target rather than a single channel, so green lasers register too.
   - Lasers: redraw next to `dlss_post_effects`. `mapDlssEffects` also holds particles, so filter it.
   - Reticles: redraw next to `render_Reticle` in `phase_dlss_scope`. `mapScopeHUDSorted` also holds lenses, so filter it.
   - `r_dsgraph_render_graph_sorted` clears its list by default, so the mask draw must pass `_clear = false` or copy the items.
   - Some reticle shaders mark stencil 254 and cutouts test it, so check that the extra draw doesn't disturb that.
3. **Fill.** Just before `fg::CaptureHudless` in `phase_combine`, copy the frame, fill masked pixels from nearby unmasked ones, and give that image to `D3D12PresentBridge::CaptureHudless` instead of `shared11`. Only for `PresentRuntime::Fsr`, so DLSS-G is unchanged. Skip everything on frames with no laser or reticle.
4. **Checks.** Add a console toggle for comparing and a mask debug view. Test lasers on the weapon and on NPCs, 3D scope reticles, gas mask and night vision.

## Cost

Estimates, not measurements:

- GPU: two small redraws, a frame copy and one fill pass, skipped on frames with no laser or reticle.
- VRAM: two display-size 8-bit targets, about 30 MB at 1440p, allocated only with FSR frame generation on.

## Known limits

- **Faint beams:** below the 10% difference they only partly return in generated frames, so they could pulse slightly.
- **Bloom halo:** it stays in the hudless copy, so a dim, soft ghost of the glow could remain.
- **Timing:** generated frames show the dot at its current-frame position, about half a frame early.
- **Post-after off:** the first version only covers post-after on, because the laser redraw relies on that path's display-size position copy.
- **Gas mask:** its distortion could misalign the mask slightly.
- **Test builds:** frame generation only exists in `AnomalyDX12.exe`.

## Sources

- [FidelityFX Super Resolution 3.1.4: upscaling and frame generation](https://gpuopen.com/?p=69255)
- [FidelityFX SDK manual: super resolution interpolation](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-interpolation/)
