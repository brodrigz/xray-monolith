#include "../../src/Layers/xrRenderPC_R4/Dlss/DlssTemporal.h"
#include "../../src/Layers/xrRenderPC_R4/Dlss/DlssNativeUi.h"
#include <vector>
#include <cstdio>
#include <cstdlib>

static void Require(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main()
{
    using namespace dlss;
    const Size render{1280, 720}, display{1920, 1080};
    Require(ValidPreset(Preset::Default) && ValidPreset(Preset::J) && ValidPreset(Preset::K) &&
        ValidPreset(Preset::L) && ValidPreset(Preset::M), "supported SR presets");
    Require(!ValidPreset(static_cast<Preset>(6)) && !ValidPreset(static_cast<Preset>(14)), "deprecated/reserved presets rejected");
    const auto scale = MotionScale(render);
    Require(scale.x == -1280 && scale.y == -720, "SSS current-minus-previous to NGX previous-minus-current");
    Require(0.125f * scale.x == -160, "rightward object motion reprojects left");
    for (unsigned frame = 0; frame < 4096; ++frame)
    {
        const auto pixel = Jitter(frame, render, display);
        const auto clip = ClipJitter(pixel, render);
        Require(std::abs(pixel.x) <= 0.5f && std::abs(pixel.y) <= 0.5f, "subpixel jitter bounded");
        Require(std::abs(clip.x * render.width / 2 - pixel.x) < 1e-6f, "horizontal raster offset matches NGX");
        Require(std::abs(-clip.y * render.height / 2 - pixel.y) < 1e-6f, "vertical raster offset matches NGX");
    }
    Require(Jitter(0, {}, display).x == 0, "invalid dimensions disable jitter");
    Require(ClipJitter({1, 1}, {}).y == 0, "invalid dimensions cannot divide by zero");
    History history;
    Require(history.Begin(10, false), "first frame resets");
    history.Commit(10);
    Require(!history.Begin(12, false), "skipped scope frame does not advance or reset main-view history");
    Require(history.Begin(12, true), "camera cut resets");
    history.Commit(12);
    Require(history.Begin(12, false), "duplicate frame resets");
    Require(history.Begin(1, false), "restarted frame sequence resets");
    history.Invalidate();
    Require(history.Begin(20, false), "vid_restart and failed evaluations reset history");
    struct Draw { int pVisual, pMatrix; bool ui; };
    std::vector<Draw> scene{{1, 10, false}, {2, 20, true}, {3, 30, false}, {4, 40, true}};
    std::vector<Draw> native{{99, 99, true}};
    ExtractNativeUi(scene, native, [](const Draw& draw) { return draw.ui; });
    Require(scene.size() == 2 && scene[0].pVisual == 1 && scene[1].pVisual == 3,
        "non-PDA draws remain in scene order");
    Require(native.size() == 2 && native[0].pVisual == 2 && native[1].pVisual == 4,
        "PDA screen packets move once and stale native packets are discarded");
    std::vector<Draw> emissive{{2, 20, true}, {2, 21, false}, {3, 30, false}, {4, 40, true}};
    RemoveNativeUiDuplicates(emissive, native);
    Require(emissive.size() == 2 && emissive[0].pMatrix == 21 && emissive[1].pVisual == 3,
        "remove scene duplicates without removing another instance of the same mesh");
    ExtractNativeUi(scene, native, [](const Draw& draw) { return draw.ui; });
    Require(native.empty() && scene.size() == 2, "closing PDA leaves no stale native UI");
    std::puts("DLSS temporal contract tests passed.");
}
