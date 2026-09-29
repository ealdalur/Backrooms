// ---------------------------------------------------------------------------
// Main.cpp
// Application entry point for the Backrooms infinite labyrinth simulator.
//
// Usage:
//   Backrooms [seed] [--seed <n>] [--level <n>] [--no-entities] [--dump-sounds <dir>]
//             [--demo <scene> [--type <text>]] [--screenshot <file.bmp> [--delay <seconds>]]
//             [--size <width>x<height>]
//
//   seed          World seed (decimal or 0x-prefixed hex). Same seed -> same world.
//   --level       Storey to start on (0 = the classic floor; negative = below).
//   --demo        Developer scene: stairs, stairs-top, stairs-sign (a closed
//                 entrance and its sign; --type upper for the storey above),
//                 climb / descend (scripted walk up / down the nearest
//                 stairwell), stalker, ambush (it creeps up from
//                 behind, then the view whips round), caught (...and it never
//                 does), wanderer, terminal, doom (sits down and plays the
//                 hidden terminal game), phone (picks up the nearest desk
//                 phone; --type <digits> dials them), explore (a long scripted walk with
//                 the Stalker off; --type <n> picks the route), idle (just
//                 logs entity activity), cabinet (searches the nearest filing
//                 cabinet with a Tesla gun part in it; --type take: takes it;
//                 --type swap-persist: swaps a flat one in, leaves until the
//                 chunk unloads, comes back and checks), part (walks up to
//                 the nearest part on a desk or chair),
//                 assemble (all four parts, put together), tesla /
//                 tesla-stalker (the assembled gun fired at the Wanderer /
//                 the Stalker until it is vaporised; --type <percent> sets
//                 the battery's charge).
//   --type        Text typed into the terminal in the terminal scene (keys
//                 dialled in the phone scene, "upper" in the stairs-sign
//                 scene, a route number in explore).
//   --no-entities Disable the Stalker and the Wanderer.
//   --dump-sounds Write every procedurally synthesised sound to <dir> as WAV.
//   --screenshot  Render for a few seconds, save a BMP of the frame and exit
//                 (handy for automated smoke tests / CI).
//   --size        Initial window size in logical pixels, e.g. 1280x720.
// ---------------------------------------------------------------------------
#include "Core/Engine.h"
#include "Core/GpuSelection.h"

#include <cstdio>
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
    std::cout << "Usage: " << exe << " [seed] [--seed <n>] [--level <n>] [--no-entities] [--dump-sounds <dir>]\n"
              << "       [--demo <scene> [--type <text>]] [--screenshot <file.bmp> [--delay <seconds>]] [--size <w>x<h>]\n"
              << "  demo scenes: stairs, stairs-top, stairs-sign, climb, descend, stalker, ambush, caught, wanderer, terminal, doom, phone,\n"
              << "               explore, idle, cabinet, part, assemble, tesla, tesla-stalker\n";
}

} // namespace

int main(int argc, char* argv[]) {
    // Unbuffered log output: nothing is lost if the process dies, even when
    // stdout is redirected to a file.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

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
        } else if (std::strcmp(arg, "--level") == 0 && i + 1 < argc) {
            options.startLevel = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--demo") == 0 && i + 1 < argc) {
            options.demo = argv[++i];
        } else if (std::strcmp(arg, "--type") == 0 && i + 1 < argc) {
            options.demoInput = argv[++i];
        } else if (std::strcmp(arg, "--no-entities") == 0) {
            options.noEntities = true;
        } else if (std::strcmp(arg, "--dump-sounds") == 0 && i + 1 < argc) {
            options.dumpSoundsDir = argv[++i];
        } else if (std::strcmp(arg, "--screenshot") == 0 && i + 1 < argc) {
            options.screenshotPath = argv[++i];
        } else if (std::strcmp(arg, "--size") == 0 && i + 1 < argc) {
            char* end = nullptr;
            const long w = std::strtol(argv[++i], &end, 10);
            const long h = (end && (*end == 'x' || *end == 'X')) ? std::strtol(end + 1, &end, 10) : 0;
            if (w < 64 || h < 64 || *end != '\0') {
                std::cerr << "Invalid size: " << argv[i] << " (expected e.g. 1280x720)\n";
                return 1;
            }
            options.windowWidth = static_cast<int>(w);
            options.windowHeight = static_cast<int>(h);
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
