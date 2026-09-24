#pragma once
// ---------------------------------------------------------------------------
// GpuSelection.h
// Makes hybrid-graphics machines (integrated + discrete GPU, e.g. NVIDIA
// Optimus laptops) run the renderer on the discrete, high-performance GPU.
// Integrated graphics are only used when they are the only option.
//
// OpenGL has no API for choosing a GPU, so the choice must be made before the
// driver loads:
//   * Windows: the executable exports NvOptimusEnablement and
//     AmdPowerXpressRequestHighPerformance, which the NVIDIA and AMD drivers
//     read at process start (defined in GpuSelection.cpp, always linked).
//   * Linux: PRIME render offload environment variables are set when a
//     hybrid setup is detected (never overriding the user's own settings).
// ---------------------------------------------------------------------------

namespace gpu {

/// Requests the discrete GPU. Must run at the very start of main(), before
/// SDL or OpenGL are initialised (Linux drivers read the environment then).
void preferDiscreteGpu();

/// Reports which GPU the OpenGL context actually landed on, and prints
/// instructions if a discrete GPU exists but was not used (e.g. because the
/// user or OS forced "power saving" for this program).
void reportActiveGpu(const char* glVendor, const char* glRenderer);

} // namespace gpu
