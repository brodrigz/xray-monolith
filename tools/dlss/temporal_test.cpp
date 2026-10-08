#include "../../src/Layers/xrRenderPC_R4/Dlss/DlssTemporal.h"
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
    std::puts("DLSS temporal contract tests passed.");
}
