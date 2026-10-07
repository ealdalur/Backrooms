// ---------------------------------------------------------------------------
// CommandLine.cpp
// ---------------------------------------------------------------------------
#include "Core/CommandLine.h"

#include "Core/ConsoleLog.h"
#include "Core/Engine.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// ---- What there is to say ----------------------------------------------------------------

struct OptionHelp {
    const char* usage;
    const char* about;
};

const OptionHelp kOptions[] = {
    {"<seed>", "World seed, decimal or 0x hex: same seed, same world."},
    {"--seed <n>", "The same, as an option."},
    {"--level <n>", "Storey to start on (0 = the classic floor)."},
    {"--fullscreen", "Start full screen (the default on a discrete GPU)."},
    {"--windowed", "Start in a window (the default on an integrated GPU)."},
    {"--size <w>x<h>", "The window's size in logical pixels, e.g. 1280x720."},
    {"--no-title", "Straight into the game, without the title screen."},
    {"--no-entities", "Without the Stalker and the Wanderer."},
    {"--screenshot <file.bmp>", "Save the frame after --delay seconds, and exit."},
    {"--delay <seconds>", "When --screenshot takes its picture (default 3)."},
    {"--dump-sounds <dir>", "Write every synthesised sound to <dir> as WAV."},
    {"--verbose", "Developer log: also print the game's secrets."},
    {"--demo <scene>", "A scripted developer scene. Spoilers: --help demo"},
    {"--type <text>", "Tunes a scene: what it types, dials or picks."},
    {"--puzzle <stage>", "Skip ahead in the way out. Spoilers: --help puzzle"},
    {"--help [topic]", "This; or a topic: demo, puzzle or a scene's name."},
};

/// A value --type takes in a scene, and what it does there.
struct TypeHelp {
    const char* value;
    const char* about;
};

struct Scene {
    const char*           name;
    const char*           about;
    std::vector<TypeHelp> types;
};

struct SceneGroup {
    const char*        title;
    std::vector<Scene> scenes;
};

/// Every developer scene (Core/Engine.cpp setupDemo / updateDemo, Core/EnginePuzzle.cpp).
const std::vector<SceneGroup>& sceneGroups() {
    static const std::vector<SceneGroup> kGroups = {
        {"STAIRS",
         {
             {"stairs", "At the foot of the nearest stairwell's flight, its entrances open.", {}},
             {"stairs-top", "In the nearest stairwell's upper lobby, looking down the shaft.", {}},
             {"stairs-sign", "A few metres back from a closed stairwell entrance and its sign.",
              {{"upper", "the entrance on the storey above"}}},
             {"climb", "Walks up the nearest stairwell.",
              {{"door", "opens the shut entrance from outside first, and reports which way it swung"}}},
             {"descend", "Walks down the nearest stairwell.", {{"door", "the same, from the storey above"}}},
             {"stairs-chase", "Climbs the nearest stairwell, shutting the doors behind: the Stalker has to follow on foot.",
              {{"wanderer", "the Wanderer follows instead"},
               {"wanderer-far", "the Wanderer, out of earshot: only the fallback brings it"}}},
         }},
        {"ENTITIES",
         {
             {"stalker", "The Stalker, a few metres ahead.", {}},
             {"ambush", "The Stalker creeps up from behind; the view whips round once it is close.", {}},
             {"caught", "The Stalker creeps up from behind - and the player never looks.", {}},
             {"stalker-stare", "The player keeps turning to face the Stalker: stared down three times, it bolts.", {}},
             {"stalker-door", "The Stalker hunts the player from behind a closed door.", {}},
             {"wanderer", "The Wanderer, a few metres ahead; a moment later a door nearby opens, to make a noise.", {}},
             {"explore", "A long scripted walk through the rooms (the Stalker off): how often is the Wanderer met?",
              {{"<n>", "the route (default 1)"}}},
             {"idle", "Nothing staged: the entities' activity is just logged.", {}},
         }},
        {"TERMINALS AND PHONES",
         {
             {"terminal", "Sits down at the nearest terminal.",
              {{"<command>", "typed in and run, then the screen is printed (e.g. --type map)"}}},
             {"terminal-pause", "Pauses a terminal's live log, leaves, sits down again and resumes it: does the stream come back?",
              {}},
             {"doom", "Sits down at the nearest terminal and plays DOOM (scripted).", {}},
             {"phone", "Picks up the nearest desk phone and logs the line.",
              {{"<keys>", "dialled one by one: 0-9 * #, h hangs up, M presses the message lamp (and picks a phone with a "
                          "message waiting)"}}},
             {"ringer", "A phone nearby starts ringing within a second.",
              {{"look", "stands 3 m in front of it, looking at it"}, {"look-far", "the same from 8 m (as far as the room allows)"}}},
         }},
        {"CABINETS AND THE TESLA GUN",
         {
             {"cabinet", "Searches the nearest filing cabinet with a part in a drawer.",
              {{"take", "takes the part"},
               {"swap-persist", "swaps a nearly flat part of the same type in, walks away until the chunk unloads, comes back and "
                                "checks the drawer"},
               {"use-key", "presses E to take the part, then again to close the cabinet"}}},
             {"part", "In front of the nearest part lying on a desk or a chair.", {}},
             {"assemble", "All four parts in hand, put together.", {{"<percent>", "the battery's charge (default 100)"}}},
             {"tesla", "The assembled gun, fired at the Wanderer until it is vaporised.",
              {{"<percent>", "the battery's charge (default 100)"}}},
             {"tesla-stalker", "The same, at the Stalker.", {{"<percent>", "the battery's charge (default 100)"}}},
         }},
        {"THE WAY OUT",
         {
             {"clue", "In front of the nearest phone number written on a wall.",
              {{"<n>", "the n-th nearest writing of any kind (0 = the nearest)"}, {"doom", "the nearest note about DOOM"}}},
             {"clue-survey", "How many rooms of exploring pass between copies of the number, and how long a room takes to walk.",
              {{"<routes>", "routes surveyed (default 40)"}}},
             {"hexstream", "The call already made: streams a terminal's HEX log until the memory's line comes up, then pauses it.",
              {{"ping", "...and pings the host it shows (any text does)"}}},
             {"exit-map", "The nearest terminal's chunk becomes the exit: at the terminal it is given, runs MAP (the room is next door).",
              {{"return", "...then goes 100 chunks and 100 floors away and back, counts the chunk's terminals, maps again and walks "
                          "into the room's wall"}}},
             {"glitch", "This chunk becomes the exit: stands in the glitch room next door, facing one of its walls.",
              {{"walk", "walks into the wall"}}},
             {"office", "Straight to the office, through the noclip.",
              {{"\"<x> <z> [yaw] [pitch]\"", "then looks round from there (degrees)"}}},
             {"office-tesla", "Straight to the office with the gun: from a cubicle doorway, fires at the Wanderer at its desk until it "
                              "is vaporised.",
              {{"stalker", "at the Stalker instead"}, {"both", "the Wanderer, then the Stalker"}}},
         }},
        {"MENUS",
         {
             {"quit-prompt", "Presses P, P, Esc, another key, Esc, a held Esc, Esc: checks the pause screen and the quit prompt.", {}},
         }},
    };
    return kGroups;
}

struct StageHelp {
    const char* name;
    const char* about;
};

/// The stages --puzzle starts at (Engine::init maps them onto PuzzleStage).
const StageHelp kStages[] = {
    {"dialed", "The number on the wall has already been called."},
    {"memory", "...and the memory's line has been seen on a terminal's HEX stream."},
};

const Scene* findScene(const std::string& name, const char** group = nullptr) {
    for (const SceneGroup& g : sceneGroups()) {
        for (const Scene& s : g.scenes) {
            if (name == s.name) {
                if (group) *group = g.title;
                return &s;
            }
        }
    }
    return nullptr;
}

bool knownStage(const std::string& name) {
    for (const StageHelp& s : kStages) {
        if (name == s.name) return true;
    }
    return false;
}

// ---- Laying it out -----------------------------------------------------------------------

constexpr size_t kPage = 79; ///< Columns, the banner's width and a little.

/// Width on screen: {} marks take no room.
size_t visible(const std::string& s) {
    return static_cast<size_t>(std::count_if(s.begin(), s.end(), [](char c) { return c != '{' && c != '}'; }));
}

/// Words into lines of at most `width` columns.
std::vector<std::string> wrap(const std::string& text, size_t width) {
    std::vector<std::string> lines;
    std::string line;
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find(' ', i);
        if (j == std::string::npos) j = text.size();
        const std::string word = text.substr(i, j - i);
        if (!line.empty() && visible(line) + 1 + visible(word) > width) {
            lines.push_back(line);
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
        i = j + 1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

/// A label at `indent`, its text wrapped in a column `labelWidth` further on
/// (starting on the next line if the label does not fit before it).
void entry(std::string& out, size_t indent, const std::string& label, size_t labelWidth, const std::string& text) {
    const size_t column = indent + labelWidth;
    std::string head = std::string(indent, ' ') + label;
    if (visible(head) + 2 > column) {
        out += head + "\n";
        head.clear();
    }
    const std::vector<std::string> lines = wrap(text, kPage - column);
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& lead = i == 0 ? head : std::string();
        out += lead + std::string(column - visible(lead), ' ') + lines[i] + "\n";
    }
}

void sceneEntry(std::string& out, const Scene& scene) {
    entry(out, 4, "{" + std::string(scene.name) + "}", 24, scene.about);
    for (const TypeHelp& t : scene.types) entry(out, 6, "--type {" + std::string(t.value) + "}", 22, t.about);
}

// ---- The pages ---------------------------------------------------------------------------

void printGeneral() {
    con::section("USAGE");
    std::string out = "\n    {Backrooms} [seed] [options]\n\n";
    for (const OptionHelp& o : kOptions) entry(out, 4, "{" + std::string(o.usage) + "}", 25, o.about);
    con::print(out);
}

void printScenes() {
    con::section("DEVELOPER SCENES");
    con::print("\n    Backrooms {--demo <scene>} [{--type <text>}]          (spoilers ahead)\n");
    for (const SceneGroup& g : sceneGroups()) {
        std::string out = "\n  {" + std::string(g.title) + "}\n";
        for (const Scene& s : g.scenes) sceneEntry(out, s);
        con::print(out);
    }
}

void printScene(const Scene& scene, const char* group) {
    con::section("DEVELOPER SCENE");
    std::string out = "\n    Backrooms {--demo " + std::string(scene.name) + "}" + (scene.types.empty() ? "" : " [{--type <text>}]") +
                      "      (" + group + ")\n\n";
    sceneEntry(out, scene);
    if (scene.types.empty()) out += std::string(6, ' ') + "(no --type)\n";
    con::print(out);
}

void printStages() {
    con::section("PUZZLE STAGES");
    std::string out = "\n    Backrooms {--puzzle <stage>}          (spoilers ahead: the way out, step by step)\n\n";
    for (const StageHelp& s : kStages) entry(out, 4, "{" + std::string(s.name) + "}", 24, s.about);
    con::print(out);
}

/// Prints the help on `topic` ("" = the options). False if there is none.
bool printHelp(const std::string& topic) {
    const char* group = nullptr;
    if (topic.empty()) printGeneral();
    else if (topic == "demo" || topic == "demos" || topic == "scenes" || topic == "type") printScenes();
    else if (topic == "puzzle") printStages();
    else if (const Scene* scene = findScene(topic, &group)) printScene(*scene, group);
    else return false;
    con::print("\n");
    return true;
}

bool isHelpWord(const char* s) { return std::strcmp(s, "--help") == 0 || std::strcmp(s, "-h") == 0 || std::strcmp(s, "help") == 0; }

bool parseSeed(const char* text, uint64_t& out) {
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 0); // base 0: accepts 0x..., decimal
    if (end == text || *end != '\0') return false;
    out = static_cast<uint64_t>(value);
    return true;
}

} // namespace

bool parseCommandLine(int argc, char* argv[], EngineOptions& options, int& exitCode) {
    exitCode = 1;
    auto fail = [](const std::string& what, const char* hint) {
        con::line("ENGINE", what, con::Level::Error);
        con::detail(hint, con::Level::Error);
        return false;
    };

    bool help = false;
    bool sized = false; // --size: a window of that size, unless --fullscreen says otherwise
    std::string topic; // "" = whatever the rest of the command line is about
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const char* next = i + 1 < argc ? argv[i + 1] : nullptr;
        // An option's value; a missing one is an error.
        auto value = [&]() -> const char* {
            if (!next) return nullptr;
            ++i;
            return next;
        };
        auto missing = [&](const char* usage) { return fail(std::string(arg) + " needs a value: " + usage, "Backrooms --help lists every option."); };

        if (isHelpWord(arg)) {
            help = true;
            if (next && next[0] != '-') topic = argv[++i]; // --help <topic>
        } else if (std::strcmp(arg, "--demo") == 0) {
            // A bare --demo, --demo --help or --demo help: the scenes.
            if (!next || isHelpWord(next)) {
                help = true;
                topic = "demo";
                if (next) ++i;
            } else if (next[0] == '-') {
                return fail("--demo needs a scene's name", "Backrooms --help demo lists them.");
            } else {
                options.demo = argv[++i];
            }
        } else if (std::strcmp(arg, "--puzzle") == 0) {
            if (!next || isHelpWord(next)) {
                help = true;
                topic = "puzzle";
                if (next) ++i;
            } else if (next[0] == '-') {
                return fail("--puzzle needs a stage", "Backrooms --help puzzle lists them.");
            } else {
                options.puzzleStage = argv[++i];
            }
        } else if (std::strcmp(arg, "--type") == 0) {
            // (Values may start with '-': office coordinates can be negative.)
            if (next && isHelpWord(next)) {
                help = true; // about the scene, once it is known
                ++i;
            } else if (const char* v = value()) {
                options.demoInput = v;
            } else {
                return missing("--type <text>");
            }
        } else if (std::strcmp(arg, "--seed") == 0) {
            const char* v = value();
            if (!v) return missing("--seed <n>");
            if (!parseSeed(v, options.seed)) return fail(std::string("Invalid seed: ") + v, "Decimal, or hex with 0x: e.g. --seed 0x1CEB.");
        } else if (std::strcmp(arg, "--level") == 0) {
            const char* v = value();
            if (!v) return missing("--level <n>");
            char* end = nullptr;
            const long level = std::strtol(v, &end, 10);
            if (end == v || *end != '\0') return fail(std::string("Invalid level: ") + v, "A whole number, e.g. --level -1.");
            options.startLevel = static_cast<int>(level);
        } else if (std::strcmp(arg, "--size") == 0) {
            const char* v = value();
            if (!v) return missing("--size <w>x<h>");
            char* end = nullptr;
            const long w = std::strtol(v, &end, 10);
            const long h = (end && (*end == 'x' || *end == 'X')) ? std::strtol(end + 1, &end, 10) : 0;
            if (w < 64 || h < 64 || *end != '\0') return fail(std::string("Invalid size: ") + v, "Width x height, e.g. --size 1280x720.");
            sized = true;
            options.windowWidth = static_cast<int>(w);
            options.windowHeight = static_cast<int>(h);
        } else if (std::strcmp(arg, "--screenshot") == 0) {
            const char* v = value();
            if (!v) return missing("--screenshot <file.bmp>");
            options.screenshotPath = v;
        } else if (std::strcmp(arg, "--delay") == 0) {
            const char* v = value();
            if (!v) return missing("--delay <seconds>");
            options.screenshotDelay = static_cast<float>(std::atof(v));
        } else if (std::strcmp(arg, "--dump-sounds") == 0) {
            const char* v = value();
            if (!v) return missing("--dump-sounds <dir>");
            options.dumpSoundsDir = v;
        } else if (std::strcmp(arg, "--no-entities") == 0) {
            options.noEntities = true;
        } else if (std::strcmp(arg, "--no-title") == 0) {
            options.noTitle = true;
        } else if (std::strcmp(arg, "--fullscreen") == 0) {
            options.windowMode = WindowMode::Fullscreen;
        } else if (std::strcmp(arg, "--windowed") == 0) {
            options.windowMode = WindowMode::Windowed;
        } else if (std::strcmp(arg, "--verbose") == 0) {
            options.verbose = true;
        } else if (arg[0] == '-' || !parseSeed(arg, options.seed)) {
            return fail(std::string("Unknown argument: ") + arg, "Backrooms --help lists every option.");
        }
    }

    if (sized && options.windowMode == WindowMode::Auto) options.windowMode = WindowMode::Windowed;

    if (help) {
        // --demo phone --help: about that scene; --puzzle memory --help: about the stages.
        if (topic.empty() && !options.demo.empty()) topic = options.demo;
        else if (topic.empty() && !options.puzzleStage.empty()) topic = "puzzle";
        if (!printHelp(topic)) {
            return fail("No help on '" + topic + "'", "Topics: demo, puzzle, or a scene's name (Backrooms --help demo lists them).");
        }
        exitCode = 0;
        return false;
    }
    if (!options.demo.empty() && !findScene(options.demo)) {
        return fail("Unknown demo scene '" + options.demo + "'", "Backrooms --help demo lists them.");
    }
    if (!options.puzzleStage.empty() && !knownStage(options.puzzleStage)) {
        return fail("Unknown puzzle stage '" + options.puzzleStage + "'", "Backrooms --help puzzle lists them.");
    }
    exitCode = 0;
    return true;
}
