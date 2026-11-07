#pragma once
// DX12 executable only. No graphics types in the simulation-thread interface.
struct ID3D11Texture2D;
namespace fg
{
void BeginSimulation();
void EndSimulation();
void BeginRender();
void EndPresent();
void CaptureScene();
// source: replaces the presentation surface as the HUD-less image (r_fg_hudless_test).
void CaptureHudless(ID3D11Texture2D* source = nullptr);
// "runtime, FSR frame generation version, DLSS-G module versions", for test logs.
const char* RuntimeVersions();
void Reset();
// r__framelimit integration. With Streamline, Reflex applies the cap and the engine
// limiter must not wait as well. While frames are generated the cap is interpreted
// as displayed frames, so rendered frames are limited to half of it.
bool ReflexLimitsFrames();
float FrameLimitScale();
}
