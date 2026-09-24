// ---------------------------------------------------------------------------
// Main.cpp
// Application entry point for the Backrooms infinite labyrinth simulator.
//
// Usage:
//   Backrooms [seed] [--seed <n>] [--screenshot <file.bmp> [--delay <seconds>]]
//
//   seed          World seed (decimal or 0x-prefixed hex). Same seed -> same world.
//   --screenshot  Render for a few seconds, save a BMP of the frame and exit
//                 (handy for automated smoke tests / CI).
// ---------------------------------------------------------------------------
#include "Core/Engine.h"
#include "Core/GpuSelection.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

bool parseSeed(const char* text, uint64_t& out) {
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 0); // base 0: accepts 0x..., decimal
    if (end == text || *end != '\0') return false;
    out = static_cast<uint64_t>(value);
    return true;
}

void printUsage(const char* exe) {
    std::cout << "Usage: " << exe << " [seed] [--seed <n>] [--screenshot <file.bmp> [--delay <seconds>]]\n";
}

} // namespace

int main(int argc, char* argv[]) {
    // Before anything touches SDL / OpenGL: on hybrid-graphics machines the
    // GPU is chosen when the driver loads.
    gpu::preferDiscreteGpu();

    EngineOptions options;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if ((std::strcmp(arg, "--seed") == 0) && i + 1 < argc) {
            if (!parseSeed(argv[++i], options.seed)) {
                std::cerr << "Invalid seed: " << argv[i] << '\n';
                return 1;
            }
        } else if (std::strcmp(arg, "--screenshot") == 0 && i + 1 < argc) {
            options.screenshotPath = argv[++i];
        } else if (std::strcmp(arg, "--delay") == 0 && i + 1 < argc) {
            options.screenshotDelay = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else if (!parseSeed(arg, options.seed)) {
            std::cerr << "Unknown argument: " << arg << '\n';
            printUsage(argv[0]);
            return 1;
        }
    }

    Engine engine(options);
    if (!engine.init()) {
        std::cerr << "Initialisation failed.\n";
        return 1;
    }
    return engine.run();
}
