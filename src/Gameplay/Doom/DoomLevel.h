#pragma once
// ---------------------------------------------------------------------------
// DoomLevel.h
// A map for the terminal Doom clone: a grid of one-unit cells, each a wall,
// an open cell (with its own floor, ceiling, light level and light effect)
// or a door that rises into the ceiling; one wall cell is the exit switch.
//
// Generation ("rooms and mazes"): odd-aligned rooms are scattered, every
// gap between them is filled with a winding maze, and the regions are
// joined through randomly chosen connectors (a spanning tree, plus a few
// extra connections for loops); connectors into rooms become doors. Most
// dead ends are then eaten back, which leaves long, labyrinthine corridors,
// some ending in a stash. Rooms get a theme (tech, brick, rock, rusted
// metal, marble, hell), ceiling lights, flicker / glow / strobe lights, the
// odd nukage pool or open sky; monsters and items are placed by how far each
// room is from the start, and the exit goes in the farthest room.
//
// Map space: x to the right (east), y down the grid (south); angle 0 looks
// along +x and angles grow turning right (clockwise seen from above).
// ---------------------------------------------------------------------------

#include "Gameplay/Doom/DoomAssets.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace doom {

enum class ThingType : uint8_t {
    Imp, Trooper, Barrel, Lamp,                              // spawned by the map
    Shotgun, Clip, Shells, Medikit, Stimpack, Armor,         // pickups
    Fireball, Puff, Blood, Explosion,                        // transient effects
    Count
};

enum class LightFx : uint8_t { None, Flicker, Glow, Strobe };

struct Tile {
    enum Kind : uint8_t { Empty, Wall, Door, Switch };
    Kind    kind = Wall;
    bool    overrideTex = false;           ///< Wall: faces use `wallTex`, not the viewer's room theme.
    WallTex wallTex = WallTex::Tech;       ///< Door / switch / overridden wall texture.
    WallTex theme = WallTex::Tech;         ///< Open cells: texture of the walls seen from here.
    FlatTex floor = FlatTex::Plate;
    FlatTex ceil = FlatTex::CeilTile;
    bool    sky = false;                   ///< Open to the sky instead of a ceiling.
    uint8_t light = 160;                   ///< 0..255 base light level.
    LightFx fx = LightFx::None;
    uint8_t fxGroup = 0;                   ///< Cells of one group flicker in unison.
    int16_t room = -1;                     ///< Room index, -1 for corridors / walls.
    int16_t door = -1;                     ///< Index into Level::doors.
};

struct Door {
    enum class State : uint8_t { Closed, Opening, Open, Closing };
    int   x = 0, y = 0;
    float open = 0.0f;   ///< 0 shut .. 1 fully raised.
    State state = State::Closed;
    int   wait = 0;      ///< Tics left before an open door closes again.
};

struct Spawn {
    ThingType type;
    glm::vec2 pos;
};

class Level {
public:
    /// A new map. `map` is 1-based and grows the map and its population.
    static Level generate(uint64_t seed, int map);

    int width() const { return m_w; }
    int height() const { return m_h; }
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < m_w && y < m_h; }
    /// Outside the map: solid wall.
    const Tile& at(int x, int y) const;
    Tile& at(int x, int y);

    /// Walls, the switch, and doors that are not (nearly) fully open.
    bool blocksMove(int x, int y) const;
    /// What sight and hitscan cannot pass (doors until half open).
    bool blocksSight(int x, int y) const;
    bool isNukage(int x, int y) const;
    /// A cell's light level this tic, with its light effect applied.
    int lightAt(int x, int y, int tic) const;

    std::vector<Door>  doors;
    std::vector<Spawn> spawns;
    glm::vec2   start{1.5f};
    float       startAngle = 0.0f;
    std::string name;
    int         monsterCount = 0; ///< For the intermission tally.
    int         itemCount = 0;

private:
    int m_w = 0, m_h = 0;
    std::vector<Tile> m_tiles;
};

} // namespace doom
