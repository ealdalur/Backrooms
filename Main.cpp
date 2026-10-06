// ---------------------------------------------------------------------------
// Main.cpp
// Application entry point for the Backrooms infinite labyrinth simulator.
//
// Usage:
//   Backrooms [seed] [options]
//
//   Backrooms --help            every option
//   Backrooms --help demo       the developer scenes, and what --type does in each
//   Backrooms --help <scene>    one scene
//   Backrooms --help puzzle     the stages --puzzle can start at
//
// The options, the scenes and the stages are all listed in Core/CommandLine.cpp.
// ---------------------------------------------------------------------------
#include "Core/CommandLine.h"
#include "Core/ConsoleLog.h"
#include "Core/Engine.h"
#include "Core/GpuSelection.h"

#include <cstdio>

int main(int argc, char* argv[]) {
    // Unbuffered log output: nothing is lost if the process dies, even when
    // stdout is redirected to a file.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // Before anything touches SDL / OpenGL: on hybrid-graphics machines the
    // GPU is chosen when the driver loads.
    gpu::preferDiscreteGpu();

    con::init();
    con::banner();

    EngineOptions options;
    int exitCode = 0;
    if (!parseCommandLine(argc, argv, options, exitCode)) return exitCode;

    Engine engine(options);
    if (!engine.init()) {
        con::line("ENGINE", "Initialisation failed.", con::Level::Error);
        return 1;
    }
    return engine.run();
}
