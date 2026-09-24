// ---------------------------------------------------------------------------
// GpuSelection.cpp
// ---------------------------------------------------------------------------
#include "Core/GpuSelection.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
enum : uint32_t { kVendorNvidia = 0x10DE, kVendorAmd = 0x1002, kVendorIntel = 0x8086 };
} // namespace

// ============================================================================
// Windows
// ============================================================================
#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_6.h>

// Exported from the executable (not a DLL): the NVIDIA Optimus and AMD
// switchable-graphics drivers look these up when the process starts and
// route OpenGL to the discrete GPU.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

namespace {

struct Adapter {
    std::string name;
    uint32_t    vendor = 0;
};

/// Maps an OpenGL GL_VENDOR string to a PCI vendor id (0 if unknown).
uint32_t vendorFromGlString(const char* glVendor) {
    if (!glVendor) return 0;
    const std::string v(glVendor);
    if (v.find("NVIDIA") != std::string::npos) return kVendorNvidia;
    if (v.find("ATI") != std::string::npos || v.find("AMD") != std::string::npos) return kVendorAmd;
    if (v.find("Intel") != std::string::npos) return kVendorIntel;
    return 0;
}

std::string narrow(const wchar_t* w) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string s(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    return s;
}

/// Hardware adapters, ordered by the OS's high-performance preference when
/// DXGI 1.6 is available (Windows 10 1803+), so element 0 is the GPU Windows
/// itself considers the most powerful.
std::vector<Adapter> hardwareAdapters() {
    std::vector<Adapter> out;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return out;

    auto add = [&out](IDXGIAdapter1* adapter) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            out.push_back({narrow(desc.Description), desc.VendorId});
        }
        adapter->Release();
    };

    IDXGIFactory6* factory6 = nullptr;
    if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory6)))) {
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; SUCCEEDED(factory6->EnumAdapterByGpuPreference(
                 i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)));
             ++i) {
            add(adapter);
        }
        factory6->Release();
    } else {
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) add(adapter);
    }
    factory->Release();
    return out;
}

} // namespace

namespace gpu {

void preferDiscreteGpu() {
    // Nothing to do at runtime: the exported symbols above are the request.
    // Referencing them here also guarantees the linker keeps them.
    (void)NvOptimusEnablement;
    (void)AmdPowerXpressRequestHighPerformance;
}

void reportActiveGpu(const char* glVendor, const char* glRenderer) {
    const std::vector<Adapter> adapters = hardwareAdapters();
    if (adapters.size() < 2) return; // single GPU: nothing to choose

    const Adapter& best = adapters.front();
    if (vendorFromGlString(glVendor) == best.vendor) {
        std::cout << "[GPU] Rendering on the high-performance GPU: " << (glRenderer ? glRenderer : best.name) << '\n';
        return;
    }
    std::cout << "[GPU] WARNING: OpenGL is running on \"" << (glRenderer ? glRenderer : "?")
              << "\" instead of the high-performance GPU \"" << best.name << "\".\n"
              << "      The program requests the discrete GPU, but a per-application override is forcing\n"
              << "      power saving. To fix it, either:\n"
              << "        - Windows Settings > System > Display > Graphics: set this program to \"High performance\"\n"
              << "        - NVIDIA Control Panel > Manage 3D settings > Program Settings: choose the NVIDIA GPU\n";
}

} // namespace gpu

// ============================================================================
// Linux (and other Unix-like systems with DRM sysfs)
// ============================================================================
#elif defined(__linux__)

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

namespace fs = std::filesystem;

struct DrmGpu {
    std::string pciAddress; ///< e.g. "0000:01:00.0"
    uint32_t    vendor = 0;
    unsigned    bus = 0;
    bool        bootVga = false; ///< Drives the boot display (usually the iGPU on laptops).
    uint64_t    vramBytes = 0;   ///< Dedicated VRAM where the kernel reports it (amdgpu).
};

bool readUnsigned(const fs::path& p, uint64_t& out, int base) {
    std::ifstream f(p);
    std::string s;
    if (!(f >> s)) return false;
    char* end = nullptr;
    out = std::strtoull(s.c_str(), &end, base);
    return end != s.c_str();
}

/// Enumerates PCI GPUs exposed by the kernel DRM subsystem.
std::vector<DrmGpu> enumerateGpus() {
    std::vector<DrmGpu> gpus;
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator("/sys/class/drm", ec)) {
        const std::string name = e.path().filename().string();
        // Only primary nodes ("card0"), not connectors ("card0-HDMI-A-1").
        if (name.rfind("card", 0) != 0 || name.find('-') != std::string::npos) continue;

        const fs::path dev = e.path() / "device";
        uint64_t vendor = 0;
        if (!readUnsigned(dev / "vendor", vendor, 16)) continue; // not a PCI GPU (e.g. simpledrm)

        DrmGpu g;
        g.vendor = static_cast<uint32_t>(vendor);
        g.pciAddress = fs::canonical(dev, ec).filename().string();
        const size_t c1 = g.pciAddress.find(':'), c2 = g.pciAddress.find(':', c1 + 1);
        if (c1 != std::string::npos && c2 != std::string::npos) {
            g.bus = static_cast<unsigned>(std::strtoul(g.pciAddress.substr(c1 + 1, c2 - c1 - 1).c_str(), nullptr, 16));
        }
        uint64_t v = 0;
        g.bootVga = readUnsigned(dev / "boot_vga", v, 10) && v == 1;
        if (readUnsigned(dev / "mem_info_vram_total", v, 10)) g.vramBytes = v;
        gpus.push_back(g);
    }
    return gpus;
}

bool envSet(const char* name) {
    const char* v = std::getenv(name);
    return v && *v;
}

std::string g_linuxDecision; ///< Human-readable summary printed by reportActiveGpu().

} // namespace

namespace gpu {

void preferDiscreteGpu() {
    // Respect any explicit choice (e.g. `prime-run`, `DRI_PRIME=...`).
    if (envSet("DRI_PRIME") || envSet("__NV_PRIME_RENDER_OFFLOAD") || envSet("__GLX_VENDOR_LIBRARY_NAME")) {
        g_linuxDecision = "using the GPU selected by the environment (DRI_PRIME / PRIME offload variables)";
        return;
    }

    const std::vector<DrmGpu> gpus = enumerateGpus();
    if (gpus.size() < 2) return; // single GPU: nothing to choose

    const DrmGpu* boot = &gpus.front();
    for (const DrmGpu& g : gpus) {
        if (g.bootVga) boot = &g;
    }
    if (boot->vendor == kVendorNvidia) return; // display already on the discrete GPU

    // Case 1: NVIDIA proprietary driver in a hybrid setup -> PRIME render offload
    // (exactly the variables NVIDIA documents for GLX and EGL applications).
    bool hasNvidia = false;
    for (const DrmGpu& g : gpus) hasNvidia = hasNvidia || g.vendor == kVendorNvidia;
    std::error_code ec;
    if (hasNvidia && fs::exists("/proc/driver/nvidia/version", ec)) {
        setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 0);
        setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 0);
        g_linuxDecision = "requested NVIDIA PRIME render offload (hybrid graphics detected)";
        return;
    }

    // Case 2: Mesa drivers. Only switch when the boot GPU is clearly integrated,
    // and name the target explicitly so the choice is never ambiguous.
    const DrmGpu* target = nullptr;
    for (const DrmGpu& g : gpus) {
        if (&g == boot) continue;
        const bool discrete = g.vendor == kVendorNvidia ||                             // nouveau
                              (g.vendor == kVendorIntel && g.bus != 0) ||              // Intel Arc dGPU
                              (g.vendor == kVendorAmd && g.vramBytes > boot->vramBytes); // AMD dGPU
        if (discrete) target = &g;
    }
    const bool bootIntegrated =
        (boot->vendor == kVendorIntel && boot->bus == 0) || // Intel iGPUs always sit at 00:02.0
        (boot->vendor == kVendorAmd && target && target->vramBytes > 2 * boot->vramBytes); // APU carve-out
    if (bootIntegrated && target) {
        std::string tag = "pci-" + target->pciAddress; // Mesa ID_PATH_TAG form: pci-0000_03_00_0
        for (char& ch : tag) {
            if (ch == ':' || ch == '.') ch = '_';
        }
        setenv("DRI_PRIME", tag.c_str(), 0);
        g_linuxDecision = "requested discrete GPU " + target->pciAddress + " via DRI_PRIME";
    }
}

void reportActiveGpu(const char* glVendor, const char* glRenderer) {
    (void)glVendor;
    if (!g_linuxDecision.empty()) {
        std::cout << "[GPU] " << g_linuxDecision << "; rendering on " << (glRenderer ? glRenderer : "?") << '\n';
    }
}

} // namespace gpu

// ============================================================================
// Other platforms: the default GPU is used.
// ============================================================================
#else

namespace gpu {
void preferDiscreteGpu() {}
void reportActiveGpu(const char*, const char*) {}
} // namespace gpu

#endif
