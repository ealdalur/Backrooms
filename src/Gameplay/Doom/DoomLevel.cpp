// ---------------------------------------------------------------------------
// DoomLevel.cpp
// ---------------------------------------------------------------------------
#include "Gameplay/Doom/DoomLevel.h"

#include "Math/Random.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <deque>
#include <numeric>

namespace doom {
namespace {

const int kDx[4] = {1, -1, 0, 0};
const int kDy[4] = {0, 0, 1, -1};

struct Room {
    int x, y, w, h;
    glm::ivec2 center() const { return {x + w / 2, y + h / 2}; }
    bool contains(int cx, int cy) const { return cx >= x && cy >= y && cx < x + w && cy < y + h; }
};

FlatTex floorFor(WallTex theme, rnd::Rng& rng) {
    switch (theme) {
    case WallTex::Tech:   return rng.chance(0.5f) ? FlatTex::Tiles : FlatTex::Plate;
    case WallTex::Metal:  return FlatTex::Plate;
    case WallTex::Brown:  return rng.chance(0.5f) ? FlatTex::Dirt : FlatTex::Tiles;
    case WallTex::Stone:  return FlatTex::Dirt;
    case WallTex::Marble: return FlatTex::Tiles;
    case WallTex::Hell:   return FlatTex::HellRock;
    default:              return FlatTex::Plate;
    }
}

const char* const kMapNames[] = {"SUBSTATION",  "COOLANT PLANT", "LOWER OFFICES", "RELAY STATION", "YELLOW ROOMS",
                                 "SERVER HALL", "THE STAIRWELL", "NO EXIT",       "LEVEL 0"};

} // namespace

const Tile& Level::at(int x, int y) const {
    static const Tile kSolid{};
    return inside(x, y) ? m_tiles[static_cast<size_t>(y * m_w + x)] : kSolid;
}

Tile& Level::at(int x, int y) {
    static Tile scratch;
    if (!inside(x, y)) {
        scratch = Tile{};
        return scratch;
    }
    return m_tiles[static_cast<size_t>(y * m_w + x)];
}

bool Level::blocksMove(int x, int y) const {
    const Tile& t = at(x, y);
    if (t.kind == Tile::Door) return doors[static_cast<size_t>(t.door)].open < 0.9f;
    return t.kind != Tile::Empty;
}

bool Level::blocksSight(int x, int y) const {
    const Tile& t = at(x, y);
    if (t.kind == Tile::Door) return doors[static_cast<size_t>(t.door)].open < 0.5f;
    return t.kind != Tile::Empty;
}

bool Level::isNukage(int x, int y) const {
    const FlatTex f = at(x, y).floor;
    return at(x, y).kind == Tile::Empty && (f == FlatTex::Nukage0 || f == FlatTex::Nukage1 || f == FlatTex::Nukage2);
}

int Level::lightAt(int x, int y, int tic) const {
    const Tile& t = at(x, y);
    const int base = t.light;
    switch (t.fx) {
    case LightFx::Flicker: {
        // Mostly lit, with sudden drops that hold for a few tics.
        const uint32_t h = rnd::hash32(static_cast<uint32_t>(t.fxGroup) * 7919u + static_cast<uint32_t>(tic / 4));
        return (h & 7u) == 0 ? std::max(40, base - 104) : base;
    }
    case LightFx::Glow:
        return base - static_cast<int>(48.0f * (0.5f + 0.5f * std::sin(static_cast<float>(tic) * 0.09f + t.fxGroup)));
    case LightFx::Strobe:
        return (tic + t.fxGroup * 13) % 35 < 5 ? base : std::max(32, base - 112);
    case LightFx::None:
    default:
        return base;
    }
}

Level Level::generate(uint64_t seed, int map) {
    rnd::Rng rng(seed);
    Level L;
    const int size = std::min(33 + 8 * (map - 1), 49);
    L.m_w = L.m_h = size;
    L.m_tiles.assign(static_cast<size_t>(size * size), Tile{});
    L.name = "E1M" + std::to_string(map) + ": " + kMapNames[(map - 1) % 9];
    auto idx = [size](int x, int y) { return static_cast<size_t>(y * size + x); };

    // ---- Rooms (odd-aligned, so the maze can run between them).
    std::vector<int> region(static_cast<size_t>(size * size), -1);
    std::vector<Room> rooms;
    const int wantRooms = size * size / 85;
    for (int attempt = 0; attempt < 500 && static_cast<int>(rooms.size()) < wantRooms; ++attempt) {
        const int w = rng.rangeInt(1, 4) * 2 + 1, h = rng.rangeInt(1, 4) * 2 + 1;
        const int x = rng.rangeInt(0, (size - w - 1) / 2 - 1) * 2 + 1, y = rng.rangeInt(0, (size - h - 1) / 2 - 1) * 2 + 1;
        if (x + w >= size - 1 || y + h >= size - 1) continue;
        bool clear = true;
        for (const Room& r : rooms)
            if (x < r.x + r.w + 1 && r.x < x + w + 1 && y < r.y + r.h + 1 && r.y < y + h + 1) clear = false;
        if (!clear) continue;
        const int id = static_cast<int>(rooms.size());
        rooms.push_back({x, y, w, h});
        for (int cy = y; cy < y + h; ++cy)
            for (int cx = x; cx < x + w; ++cx) {
                L.at(cx, cy).kind = Tile::Empty;
                L.at(cx, cy).room = static_cast<int16_t>(id);
                region[idx(cx, cy)] = id;
            }
    }
    const int roomCount = static_cast<int>(rooms.size());
    int regions = roomCount;

    // ---- Mazes in all the space left (growing tree, biased to wind).
    for (int y = 1; y < size - 1; y += 2) {
        for (int x = 1; x < size - 1; x += 2) {
            if (L.at(x, y).kind != Tile::Wall) continue;
            const int id = regions++;
            auto carve = [&](int cx, int cy) {
                L.at(cx, cy).kind = Tile::Empty;
                region[idx(cx, cy)] = id;
            };
            carve(x, y);
            std::vector<glm::ivec2> stack{{x, y}};
            int lastDir = -1;
            while (!stack.empty()) {
                const glm::ivec2 c = stack.back();
                int options[4], n = 0;
                bool canContinue = false;
                for (int d = 0; d < 4; ++d) {
                    const int nx = c.x + kDx[d] * 2, ny = c.y + kDy[d] * 2;
                    if (nx > 0 && ny > 0 && nx < size - 1 && ny < size - 1 && L.at(nx, ny).kind == Tile::Wall) {
                        options[n++] = d;
                        canContinue = canContinue || d == lastDir;
                    }
                }
                if (n == 0) {
                    stack.pop_back();
                    lastDir = -1;
                    continue;
                }
                const int d = canContinue && rng.chance(0.4f) ? lastDir : options[rng.next() % static_cast<uint32_t>(n)];
                carve(c.x + kDx[d], c.y + kDy[d]);
                carve(c.x + kDx[d] * 2, c.y + kDy[d] * 2);
                stack.push_back({c.x + kDx[d] * 2, c.y + kDy[d] * 2});
                lastDir = d;
            }
        }
    }

    // ---- Connectors: wall cells between two different regions, in a straight line.
    struct Connector {
        int x, y, a, b;
        bool horizontal; ///< Regions left and right (the wall runs north-south).
    };
    std::vector<Connector> connectors;
    for (int y = 1; y < size - 1; ++y) {
        for (int x = 1; x < size - 1; ++x) {
            if (L.at(x, y).kind != Tile::Wall) continue;
            const int l = region[idx(x - 1, y)], r = region[idx(x + 1, y)], u = region[idx(x, y - 1)], d = region[idx(x, y + 1)];
            if (l >= 0 && r >= 0 && l != r && u < 0 && d < 0) connectors.push_back({x, y, l, r, true});
            else if (u >= 0 && d >= 0 && u != d && l < 0 && r < 0) connectors.push_back({x, y, u, d, false});
        }
    }
    for (size_t i = connectors.size(); i > 1; --i) std::swap(connectors[i - 1], connectors[rng.next() % i]);

    std::vector<int> parent(static_cast<size_t>(regions));
    std::iota(parent.begin(), parent.end(), 0);
    auto find = [&](int r) {
        while (parent[static_cast<size_t>(r)] != r) r = parent[static_cast<size_t>(r)] = parent[static_cast<size_t>(parent[static_cast<size_t>(r)])];
        return r;
    };
    auto sidesClosed = [&](const Connector& c) {
        return c.horizontal ? L.at(c.x, c.y - 1).kind == Tile::Wall && L.at(c.x, c.y + 1).kind == Tile::Wall
                            : L.at(c.x - 1, c.y).kind == Tile::Wall && L.at(c.x + 1, c.y).kind == Tile::Wall;
    };
    auto open = [&](const Connector& c) {
        Tile& t = L.at(c.x, c.y);
        const bool intoRoom = c.a < roomCount || c.b < roomCount;
        t.kind = intoRoom && sidesClosed(c) && rng.chance(0.65f) ? Tile::Door : Tile::Empty;
        region[idx(c.x, c.y)] = c.a < roomCount ? c.b : c.a; // corridor side, for its theme
    };
    for (const Connector& c : connectors) {
        const int a = find(c.a), b = find(c.b);
        if (a != b) {
            open(c);
            parent[static_cast<size_t>(a)] = b;
        } else if (rng.chance(0.04f) && sidesClosed(c)) {
            open(c); // a loop
        }
    }

    // ---- Eat back dead ends (not all the way: long, winding ones stay, with stashes).
    auto passable = [&](int x, int y) { const Tile::Kind k = L.at(x, y).kind; return k == Tile::Empty || k == Tile::Door; };
    auto exits = [&](int x, int y) {
        int n = 0;
        for (int d = 0; d < 4; ++d) n += passable(x + kDx[d], y + kDy[d]) ? 1 : 0;
        return n;
    };
    for (int pass = 0; pass < 12; ++pass) {
        bool changed = false;
        for (int y = 1; y < size - 1; ++y)
            for (int x = 1; x < size - 1; ++x) {
                Tile& t = L.at(x, y);
                if (!passable(x, y) || t.room >= 0 || exits(x, y) > 1) continue;
                t.kind = Tile::Wall;
                region[idx(x, y)] = -1;
                changed = true;
            }
        if (!changed) break;
    }

    // ---- Themes, flats and light.
    static const WallTex kRoomThemes[3][6] = {
        {WallTex::Tech, WallTex::Tech, WallTex::Metal, WallTex::Brown, WallTex::Stone, WallTex::Tech},
        {WallTex::Tech, WallTex::Brown, WallTex::Stone, WallTex::Metal, WallTex::Marble, WallTex::Brown},
        {WallTex::Brown, WallTex::Stone, WallTex::Marble, WallTex::Hell, WallTex::Metal, WallTex::Hell},
    };
    static const WallTex kCorridorThemes[3][2] = {
        {WallTex::Metal, WallTex::Tech}, {WallTex::Brown, WallTex::Metal}, {WallTex::Stone, WallTex::Hell}};
    const int era = std::min(map - 1, 2);
    std::vector<WallTex> regionTheme(static_cast<size_t>(regions));
    std::vector<int> regionLight(static_cast<size_t>(regions));
    std::vector<LightFx> regionFx(static_cast<size_t>(regions), LightFx::None);
    std::vector<FlatTex> regionFloor(static_cast<size_t>(regions));
    for (int r = 0; r < regions; ++r) {
        const bool room = r < roomCount;
        regionTheme[static_cast<size_t>(r)] = room ? kRoomThemes[era][rng.next() % 6] : kCorridorThemes[era][rng.next() % 2];
        regionLight[static_cast<size_t>(r)] = room ? rng.rangeInt(140, 224) : rng.rangeInt(100, 156);
        const float fx = rng.nextFloat();
        regionFx[static_cast<size_t>(r)] = room ? (fx < 0.12f ? LightFx::Flicker : fx < 0.18f ? LightFx::Glow : fx < 0.23f ? LightFx::Strobe : LightFx::None)
                                                : (fx < 0.1f ? LightFx::Flicker : LightFx::None);
        regionFloor[static_cast<size_t>(r)] = room ? floorFor(regionTheme[static_cast<size_t>(r)], rng) : FlatTex::Plate;
    }
    // The start room is always a calm, well-lit tech room.
    if (roomCount > 0) {
        regionTheme[0] = WallTex::Tech;
        regionFx[0] = LightFx::None;
        regionLight[0] = 208;
    }
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            Tile& t = L.at(x, y);
            const int r = region[idx(x, y)];
            if (r < 0 || t.kind == Tile::Wall) continue;
            t.theme = regionTheme[static_cast<size_t>(r)];
            t.floor = regionFloor[static_cast<size_t>(r)];
            t.light = static_cast<uint8_t>(regionLight[static_cast<size_t>(r)]);
            t.fx = regionFx[static_cast<size_t>(r)];
            t.fxGroup = static_cast<uint8_t>(r % 251);
            if (t.room >= 0) {
                const Room& room = rooms[static_cast<size_t>(t.room)];
                if ((x - room.x) % 2 == 1 && (y - room.y) % 2 == 1) t.ceil = FlatTex::CeilLight;
            }
        }
    }

    // ---- Doors: index them, light them like their surroundings.
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            Tile& t = L.at(x, y);
            if (t.kind != Tile::Door) continue;
            t.door = static_cast<int16_t>(L.doors.size());
            L.doors.push_back({x, y});
            t.wallTex = WallTex::Door;
            t.floor = FlatTex::Plate;
            t.fx = LightFx::None;
        }

    // ---- Distances from the start, through open cells and doors.
    const glm::ivec2 startCell = roomCount > 0 ? rooms[0].center() : glm::ivec2(1, 1);
    std::vector<int> dist(static_cast<size_t>(size * size), INT_MAX);
    {
        std::deque<glm::ivec2> q{startCell};
        dist[idx(startCell.x, startCell.y)] = 0;
        while (!q.empty()) {
            const glm::ivec2 c = q.front();
            q.pop_front();
            for (int d = 0; d < 4; ++d) {
                const int nx = c.x + kDx[d], ny = c.y + kDy[d];
                if (!passable(nx, ny) || dist[idx(nx, ny)] != INT_MAX) continue;
                dist[idx(nx, ny)] = dist[idx(c.x, c.y)] + 1;
                q.push_back({nx, ny});
            }
        }
    }
    auto roomDist = [&](int r) { const glm::ivec2 c = rooms[static_cast<size_t>(r)].center(); return dist[idx(c.x, c.y)]; };
    int maxDist = 1, exitRoom = 0;
    for (int r = 1; r < roomCount; ++r) {
        const int d = roomDist(r);
        if (d != INT_MAX && d > maxDist) {
            maxDist = d;
            exitRoom = r;
        }
    }
    auto roomNear = [&](float fraction) {
        int best = -1, bestErr = INT_MAX;
        for (int r = 1; r < roomCount; ++r) {
            const int d = roomDist(r);
            if (d == INT_MAX || r == exitRoom) continue;
            const int err = std::abs(d - static_cast<int>(fraction * static_cast<float>(maxDist)));
            if (err < bestErr) {
                bestErr = err;
                best = r;
            }
        }
        return best;
    };

    // ---- Special rooms: one open to the sky, some with nukage pools.
    for (int r = 1; r < roomCount; ++r) {
        const Room& room = rooms[static_cast<size_t>(r)];
        if (room.w * room.h >= 25 && r != exitRoom && rng.chance(0.5f)) {
            for (int y = room.y; y < room.y + room.h; ++y)
                for (int x = room.x; x < room.x + room.w; ++x) {
                    Tile& t = L.at(x, y);
                    t.sky = true;
                    t.floor = FlatTex::Dirt;
                    t.theme = rng.chance(0.5f) ? WallTex::Stone : WallTex::Brown;
                    t.light = 216;
                    t.fx = LightFx::None;
                }
            break;
        }
    }
    for (int r = 1; r < roomCount; ++r) {
        const Room& room = rooms[static_cast<size_t>(r)];
        if (room.w < 5 || room.h < 5 || L.at(room.x, room.y).sky || !rng.chance(0.3f)) continue;
        for (int y = room.y + 1; y < room.y + room.h - 1; ++y)
            for (int x = room.x + 1; x < room.x + room.w - 1; ++x) L.at(x, y).floor = FlatTex::Nukage0;
        for (int y = room.y; y < room.y + room.h; ++y)
            for (int x = room.x; x < room.x + room.w; ++x) L.at(x, y).fx = LightFx::Glow;
    }

    // ---- Computer banks along tech walls; the exit switch in the farthest room.
    std::vector<glm::ivec2> exitWalls;
    for (int y = 1; y < size - 1; ++y)
        for (int x = 1; x < size - 1; ++x) {
            Tile& t = L.at(x, y);
            if (t.kind != Tile::Wall) continue;
            for (int d = 0; d < 4; ++d) {
                const Tile& n = L.at(x + kDx[d], y + kDy[d]);
                if (n.kind != Tile::Empty || n.room < 0) continue;
                if (n.room == exitRoom) exitWalls.push_back({x, y});
                if ((n.theme == WallTex::Tech || n.theme == WallTex::Metal) && !n.sky && rng.chance(0.1f)) {
                    t.overrideTex = true;
                    t.wallTex = WallTex::Computer;
                }
            }
        }
    if (!exitWalls.empty()) {
        const glm::ivec2 e = exitWalls[rng.next() % exitWalls.size()];
        Tile& t = L.at(e.x, e.y);
        t.kind = Tile::Switch;
        t.overrideTex = true;
        t.wallTex = WallTex::Switch;
    }

    // ---- Things.
    std::vector<uint8_t> occupied(static_cast<size_t>(size * size), 0);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) occupied[idx(startCell.x + dx, startCell.y + dy)] = 1;
    auto place = [&](ThingType type, glm::ivec2 cell, float jitter = 0.2f) {
        if (!L.inside(cell.x, cell.y) || occupied[idx(cell.x, cell.y)] || L.at(cell.x, cell.y).kind != Tile::Empty) return false;
        occupied[idx(cell.x, cell.y)] = 1;
        L.spawns.push_back({type, glm::vec2(cell) + 0.5f + glm::vec2(rng.range(-jitter, jitter), rng.range(-jitter, jitter))});
        if (type == ThingType::Imp || type == ThingType::Trooper) ++L.monsterCount;
        if (type >= ThingType::Shotgun && type <= ThingType::Armor) ++L.itemCount;
        return true;
    };
    auto randomCell = [&](const Room& room, bool edge) {
        for (int tries = 0; tries < 30; ++tries) {
            glm::ivec2 c(rng.rangeInt(room.x, room.x + room.w - 1), rng.rangeInt(room.y, room.y + room.h - 1));
            if (edge) {
                if (rng.chance(0.5f)) c.x = rng.chance(0.5f) ? room.x : room.x + room.w - 1;
                else c.y = rng.chance(0.5f) ? room.y : room.y + room.h - 1;
            }
            if (!occupied[idx(c.x, c.y)] && !L.isNukage(c.x, c.y)) return c;
        }
        return glm::ivec2(-1);
    };
    const float impShare = std::min(0.8f, 0.4f + 0.12f * static_cast<float>(map - 1));
    for (int r = 1; r < roomCount; ++r) {
        const Room& room = rooms[static_cast<size_t>(r)];
        if (roomDist(r) == INT_MAX) continue;
        const bool quiet = rng.chance(0.25f);
        const int monsters = quiet ? 0 : std::clamp(room.w * room.h / 18 + rng.rangeInt(0, 1) + (map - 1) / 2, 1, 5);
        for (int k = 0; k < monsters; ++k) place(rng.chance(impShare) ? ThingType::Imp : ThingType::Trooper, randomCell(room, false));
        if (rng.chance(0.45f)) place(ThingType::Clip, randomCell(room, false));
        if (rng.chance(0.25f)) place(ThingType::Medikit, randomCell(room, false));
        if (rng.chance(0.35f)) {
            const int barrels = rng.rangeInt(1, 3);
            for (int k = 0; k < barrels; ++k) place(ThingType::Barrel, randomCell(room, true), 0.1f);
        }
        if (!L.at(room.x, room.y).sky && rng.chance(0.4f)) {
            place(ThingType::Lamp, {room.x, room.y}, 0.0f);
            place(ThingType::Lamp, {room.x + room.w - 1, room.y + room.h - 1}, 0.0f);
        }
    }
    if (roomCount > 0) place(ThingType::Clip, startCell + glm::ivec2(2, 0));
    const int shotgunRoom = roomNear(0.3f);
    if (shotgunRoom >= 0) {
        const Room& room = rooms[static_cast<size_t>(shotgunRoom)];
        if (!place(ThingType::Shotgun, room.center(), 0.05f)) place(ThingType::Shotgun, randomCell(room, false));
        place(ThingType::Shells, randomCell(room, false));
        for (int r = 1; r < roomCount; ++r)
            if (roomDist(r) > roomDist(shotgunRoom) && roomDist(r) != INT_MAX && rng.chance(0.35f))
                place(ThingType::Shells, randomCell(rooms[static_cast<size_t>(r)], false));
    }
    const int armorRoom = roomNear(0.6f);
    if (armorRoom >= 0) place(ThingType::Armor, randomCell(rooms[static_cast<size_t>(armorRoom)], false));
    for (int y = 1; y < size - 1; ++y)
        for (int x = 1; x < size - 1; ++x) {
            const Tile& t = L.at(x, y);
            if (t.kind != Tile::Empty || t.room >= 0 || dist[idx(x, y)] == INT_MAX) continue;
            if (exits(x, y) == 1) {
                // A stash at the end of a winding dead end.
                const float r = rng.nextFloat();
                place(r < 0.4f ? ThingType::Stimpack : r < 0.7f ? ThingType::Shells : ThingType::Medikit, {x, y}, 0.1f);
            } else if (dist[idx(x, y)] > 10 && rng.chance(0.012f)) {
                place(map > 1 && rng.chance(0.4f) ? ThingType::Imp : ThingType::Trooper, {x, y}, 0.1f);
            }
        }

    // ---- The player starts in room 0, looking along its longer side.
    L.start = glm::vec2(startCell) + 0.5f;
    if (roomCount > 0) {
        const Room& room = rooms[0];
        L.startAngle = room.w >= room.h ? 0.0f : 1.5707963f;
        if (rng.chance(0.5f)) L.startAngle += 3.14159265f;
    }
    return L;
}

} // namespace doom
