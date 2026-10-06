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

#include <cstdint>
#include <string>
#include <vector>

namespace gpu {

/// The GPU the OpenGL context landed on, for the start-up log.
struct ActiveGpu {
    std::string name;      ///< The renderer, tidied ("NVIDIA GeForce RTX 4070 Laptop GPU").
    std::string kind;      ///< "discrete", "integrated", "software" - or empty if it cannot be told.
    uint64_t    vram = 0;  ///< Dedicated video memory in bytes, where known (discrete GPUs).
    std::string note;      ///< How it was chosen, if that is worth saying (Linux PRIME offload).
    /// If a discrete GPU exists but OpenGL is not on it (the user or OS forced
    /// "power saving" for this program): what happened, then how to fix it, one line each.
    std::vector<std::string> warning;
};

/// Requests the discrete GPU. Must run at the very start of main(), before
/// SDL or OpenGL are initialised (Linux drivers read the environment then).
void preferDiscreteGpu();

/// Which GPU the OpenGL context actually landed on (from GL_VENDOR / GL_RENDERER),
/// with instructions if a discrete GPU exists but was not used.
ActiveGpu describeActiveGpu(const char* glVendor, const char* glRenderer);

} // namespace gpu
