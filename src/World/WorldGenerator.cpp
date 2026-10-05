// ---------------------------------------------------------------------------
// WorldGenerator.cpp
// ---------------------------------------------------------------------------
#include "World/WorldGenerator.h"

#include "Actors/TeslaParts.h"
#include "Math/Random.h"
#include "Render/MeshBuilder.h"
#include "World/Decals.h"
#include "World/OfficeLayout.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <vector>

using world::CellRole;
using world::EdgeAxis;
using world::EdgeType;

namespace {

// Salts separating independent random streams derived from the same coords.
constexpr uint64_t kSaltChunk         = 0xC4A7'0001ull;
constexpr uint64_t kSaltLayout        = 0x1A70'0002ull;
constexpr uint64_t kSaltWestBoundary  = 0xB0DE'0003ull;
constexpr uint64_t kSaltSouthBoundary = 0xB0DE'0004ull;
constexpr uint64_t kSaltPillar        = 0x9111'0005ull;
constexpr uint64_t kSaltDoor          = 0xD00B'0006ull;
constexpr uint64_t kSaltLights        = 0x7167'0007ull;
constexpr uint64_t kSaltFurniture     = 0xF0E1'0008ull;
constexpr uint64_t kSaltLevel         = 0x1E7E'0009ull;
constexpr uint64_t kSaltStairwell     = 0x57A1'000Aull;
constexpr uint64_t kSaltTerminal      = 0x7E41'000Bull;
constexpr uint64_t kSaltStairwellRoll = 0x57A1'000Cull;
constexpr uint64_t kSaltPhone         = 0x9403'000Dull;
constexpr uint64_t kSaltCabinet       = 0xCAB1'000Eull;
constexpr uint64_t kSaltItem          = 0x17E3'000Full;
constexpr uint64_t kSaltWriting       = 0x3417'0010ull;
constexpr uint64_t kSaltOffice        = 0x0FF1'0011ull;
constexpr uint64_t kSaltExit          = 0xE417'0012ull;

constexpr float S  = world::kCellSize;
constexpr float H  = world::kCeilingHeight;
constexpr float HT = world::kWallHalf;

constexpr size_t kMaxCachedLayouts = 4096;

/// Minimal union-find for randomised Kruskal.
class DisjointSet {
public:
    explicit DisjointSet(int n) : m_parent(static_cast<size_t>(n)) { std::iota(m_parent.begin(), m_parent.end(), 0); }
    int find(int x) {
        while (m_parent[static_cast<size_t>(x)] != x) {
            m_parent[static_cast<size_t>(x)] = m_parent[static_cast<size_t>(m_parent[static_cast<size_t>(x)])];
            x = m_parent[static_cast<size_t>(x)];
        }
        return x;
    }
    /// Returns true if the sets were distinct (i.e. the edge joins a tree).
    bool unite(int a, int b) {
        a = find(a);
        b = find(b);
        if (a == b) return false;
        m_parent[static_cast<size_t>(a)] = b;
        return true;
    }

private:
    std::vector<int> m_parent;
};

/// Picks a passable edge type with the given door / archway probabilities.
EdgeType pickPassable(rnd::Rng& rng, float doorBias, float archBias) {
    const float p = rng.nextFloat();
    if (p < doorBias) return EdgeType::Door;
    if (p < doorBias + archBias) return EdgeType::Archway;
    return EdgeType::Open;
}

/// Adds a (world-space) box to the static mesh with chunk-local UVs and,
/// optionally, a matching collider.
void addSolid(ChunkBlueprint& bp, const glm::vec3& origin, const AABB& box, MaterialId mat, uint8_t faces,
              bool collide = true) {
    mesh::BoxDesc d;
    d.min = box.min;
    d.max = box.max;
    d.material = mat;
    d.faces = faces;
    d.uvOrigin = origin;
    mesh::addBox(bp.staticMesh, d);
    if (collide) bp.colliders.push_back(box);
}

/// Splits rectangle `r` (x0, z0, x1, z1) around an optional hole into up to
/// four rectangles that cover r minus the hole.
std::vector<glm::vec4> subtractHole(const glm::vec4& r, const std::optional<glm::vec4>& hole) {
    if (!hole) return {r};
    const glm::vec4 h(std::max(hole->x, r.x), std::max(hole->y, r.y), std::min(hole->z, r.z), std::min(hole->w, r.w));
    if (h.x >= h.z || h.y >= h.w) return {r};
    std::vector<glm::vec4> out;
    if (h.y > r.y) out.emplace_back(r.x, r.y, r.z, h.y); // south strip
    if (h.w < r.w) out.emplace_back(r.x, h.w, r.z, r.w); // north strip
    if (h.x > r.x) out.emplace_back(r.x, h.y, h.x, h.w); // west piece
    if (h.z < r.z) out.emplace_back(h.z, h.y, r.z, h.w); // east piece
    return out;
}

/// Geometry helper describing one edge in world space.
struct EdgeFrame {
    EdgeAxis  axis;
    glm::vec2 start; ///< World x/z of the edge's first vertex.
    float     baseY; ///< Floor height of the storey.

    /// Box spanning [s0, s1] along the edge, [y0, y1] above the floor and
    /// +-halfDepth across it.
    AABB box(float s0, float s1, float y0, float y1, float halfDepth) const {
        y0 += baseY;
        y1 += baseY;
        if (axis == EdgeAxis::South) { // runs along +X at z = start.y
            return {glm::vec3(start.x + s0, y0, start.y - halfDepth), glm::vec3(start.x + s1, y1, start.y + halfDepth)};
        }
        // West: runs along +Z at x = start.x
        return {glm::vec3(start.x - halfDepth, y0, start.y + s0), glm::vec3(start.x + halfDepth, y1, start.y + s1)};
    }
    uint8_t longFaces() const { return axis == EdgeAxis::South ? (mesh::FacePosZ | mesh::FaceNegZ) : (mesh::FacePosX | mesh::FaceNegX); }
    uint8_t startCap() const { return axis == EdgeAxis::South ? mesh::FaceNegX : mesh::FaceNegZ; }
    uint8_t endCap() const { return axis == EdgeAxis::South ? mesh::FacePosX : mesh::FacePosZ; }
    glm::vec3 alongDir() const { return axis == EdgeAxis::South ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1); }
    glm::vec3 point(float s) const { return glm::vec3(start.x, baseY, start.y) + alongDir() * s; }
};

/// Whether light can pass an edge at position `along` (0..cell size).
bool edgeBlocksLight(EdgeType type, float along) {
    switch (type) {
    case EdgeType::Open:    return false;
    case EdgeType::Archway: return std::fabs(along - S * 0.5f) > world::kArchWidth * 0.5f - 0.05f;
    case EdgeType::Wall:
    case EdgeType::Door:
    default:                return true; // doors are treated as closed for lighting
    }
}

} // namespace

WorldGenerator::WorldGenerator(uint64_t worldSeed) : m_seed(worldSeed) {}

void WorldGenerator::setRealm(world::Realm realm) {
    m_realm = realm;
    m_layoutCache.clear();
    setExitChunk(std::nullopt);
}

void WorldGenerator::setExitChunk(const std::optional<ChunkCoord>& c) {
    m_exitChunk = c;
    m_exitCell.reset();
    m_exitWalls = 0;
    if (!c || m_realm != world::Realm::Backrooms) return;
    // The room with the most walls (among cells 0..3, which own all four of
    // their edges), so there is plenty of wall to walk into.
    int best = -1;
    for (int lz = 0; lz < kN - 1; ++lz) {
        for (int lx = 0; lx < kN - 1; ++lx) {
            const int gx = c->x * kN + lx, gz = c->z * kN + lz;
            if (cellRole(c->level, gx, gz) != CellRole::Room) continue;
            const EdgeType sides[4] = {edge(c->level, gx, gz, EdgeAxis::West), edge(c->level, gx + 1, gz, EdgeAxis::West),
                                       edge(c->level, gx, gz, EdgeAxis::South), edge(c->level, gx, gz + 1, EdgeAxis::South)};
            int walls = 0;
            for (EdgeType e : sides) walls += e == EdgeType::Wall ? 1 : 0;
            const int score = walls * 16 + static_cast<int>(rnd::hashCoords(m_seed, gx, gz, kSaltExit) & 15u);
            if (score > best) {
                best = score;
                m_exitCell = glm::ivec3(gx, gz, c->level);
                m_exitWalls = walls;
            }
        }
    }
}

bool WorldGenerator::isExitCell(int level, int gx, int gz) const {
    return m_exitCell && m_exitCell->x == gx && m_exitCell->y == gz && m_exitCell->z == level;
}

bool WorldGenerator::isGlitchEdge(int level, int gx, int gz, EdgeAxis axis) const {
    if (!m_exitCell || m_exitCell->z != level) return false;
    const glm::ivec2 before = axis == EdgeAxis::West ? glm::ivec2(gx - 1, gz) : glm::ivec2(gx, gz - 1);
    if (!isExitCell(level, gx, gz) && !isExitCell(level, before.x, before.y)) return false;
    return edge(level, gx, gz, axis) == EdgeType::Wall;
}

MaterialId WorldGenerator::wallMaterial() const {
    return m_realm == world::Realm::Office ? MaterialId::OfficePaint : MaterialId::Wallpaper;
}

uint64_t WorldGenerator::levelSeed(int level) const {
    if (m_realm == world::Realm::Office) {
        return rnd::hashCombine(rnd::splitmix64(m_seed ^ kSaltOffice), static_cast<uint64_t>(static_cast<uint32_t>(level)));
    }
    if (level == 0) return m_seed;
    return rnd::hashCombine(rnd::splitmix64(m_seed ^ kSaltLevel), static_cast<uint64_t>(static_cast<uint32_t>(level)));
}

uint64_t WorldGenerator::chunkSeed(const ChunkCoord& c) const {
    return rnd::hashCoords(levelSeed(c.level), c.x, c.z, kSaltChunk);
}

std::optional<stairs::Placement> WorldGenerator::stairwell(int lowerLevel, int cx, int cz) const {
    if (m_realm == world::Realm::Office) return std::nullopt; // one storey, no stairs
    // Every storey of every chunk gets at least one stairwell. The one rising
    // from `lowerLevel` exists if its own roll succeeds, or if the roll of the
    // one arriving at `lowerLevel` from below failed - otherwise that storey
    // would have none. Both rolls are pure hashes, so this stays deterministic.
    auto rolls = [this, cx, cz](int level) {
        const uint64_t h = rnd::hashCombine(rnd::hashCoords(m_seed, cx, cz, kSaltStairwellRoll),
                                            static_cast<uint64_t>(static_cast<uint32_t>(level)));
        return rnd::toUnit(h) < world::kStairwellChance;
    };
    if (!rolls(lowerLevel) && rolls(lowerLevel - 1)) return std::nullopt;

    rnd::Rng rng(rnd::hashCombine(rnd::hashCoords(m_seed, cx, cz, kSaltStairwell),
                                  static_cast<uint64_t>(static_cast<uint32_t>(lowerLevel))));
    stairs::Placement p;
    // Stairwells rising from odd and even storeys use different columns, so
    // one storey's "up" and "down" stairwells can never share a cell. Rows
    // and columns 1..3 keep the cell and its entrance neighbour inside the
    // chunk, away from the chunk-boundary edges.
    p.lx = (lowerLevel & 1) ? 3 : 1;
    p.lz = rng.rangeInt(1, 3);
    p.rotation = rng.rangeInt(0, 3);
    p.lowerEntrance = rng.chance(0.6f) ? EdgeType::Door : EdgeType::Archway;
    p.upperEntrance = rng.chance(0.6f) ? EdgeType::Door : EdgeType::Archway;
    return p;
}

// ----- Layout -----------------------------------------------------------------

void WorldGenerator::trimCache() const {
    if (m_layoutCache.size() > kMaxCachedLayouts) m_layoutCache.clear();
}

const WorldGenerator::ChunkLayout& WorldGenerator::layout(const ChunkCoord& c) const {
    auto it = m_layoutCache.find(c);
    if (it != m_layoutCache.end()) return it->second;
    return m_layoutCache.emplace(c, buildLayout(c)).first->second;
}

WorldGenerator::ChunkLayout WorldGenerator::buildLayout(const ChunkCoord& c) const {
    ChunkLayout L;
    L.west.fill(EdgeType::Wall);
    L.south.fill(EdgeType::Wall);
    L.roles.fill(CellRole::Room);

    auto idx = [](int lx, int lz) { return lz * kN + lx; };

    // Stairwell cells on this storey: the bottom of one rising from here and
    // the top of one arriving from below.
    struct Special {
        int      cell;
        int      side;
        EdgeType entrance;
    };
    std::vector<Special> specials;
    if (const auto up = stairwell(c.level, c.x, c.z)) {
        L.roles[static_cast<size_t>(idx(up->lx, up->lz))] = CellRole::StairsLower;
        specials.push_back({idx(up->lx, up->lz), stairs::entranceSide(up->rotation), up->lowerEntrance});
    }
    if (const auto down = stairwell(c.level - 1, c.x, c.z)) {
        L.roles[static_cast<size_t>(idx(down->lx, down->lz))] = CellRole::StairsUpper;
        specials.push_back({idx(down->lx, down->lz), stairs::entranceSide(down->rotation), down->upperEntrance});
    }
    auto isSpecial = [&L](int cell) { return L.roles[static_cast<size_t>(cell)] != CellRole::Room; };

    const uint64_t seed = levelSeed(c.level);
    rnd::Rng rng(rnd::hashCoords(seed, c.x, c.z, kSaltLayout));
    // Per-chunk character: from maze-like corridors to cavernous open halls.
    const float openness = rng.range(0.20f, 0.85f);
    const float archBias = rng.range(0.15f, 0.40f);
    const float doorBias = rng.range(0.10f, 0.30f);

    // Interior edges: randomised Kruskal guarantees a spanning tree of
    // passable edges; the remaining edges open up with probability `openness`.
    struct Candidate {
        bool west;
        int  lx, lz;
        int  a, b;
    };
    std::vector<Candidate> edges;
    edges.reserve(2 * kN * kN);
    for (int lz = 0; lz < kN; ++lz) {
        for (int lx = 0; lx < kN; ++lx) {
            if (lx > 0) edges.push_back({true, lx, lz, idx(lx - 1, lz), idx(lx, lz)});
            if (lz > 0) edges.push_back({false, lx, lz, idx(lx, lz - 1), idx(lx, lz)});
        }
    }
    for (size_t i = edges.size(); i > 1; --i) { // Fisher-Yates with our deterministic RNG
        const size_t j = static_cast<size_t>(rng.next() % static_cast<uint32_t>(i));
        std::swap(edges[i - 1], edges[j]);
    }
    DisjointSet sets(kN * kN);
    for (const Candidate& e : edges) {
        // Stairwell cells stay out of the tree: removing a cell that is not on
        // the chunk border leaves the rest of the grid connected.
        if (isSpecial(e.a) || isSpecial(e.b)) continue;
        const bool treeEdge = sets.unite(e.a, e.b);
        const EdgeType t = (treeEdge || rng.chance(openness)) ? pickPassable(rng, doorBias, archBias) : EdgeType::Wall;
        (e.west ? L.west : L.south)[static_cast<size_t>(idx(e.lx, e.lz))] = t;
    }

    // Boundary edges (owned by this chunk: its west and south borders). Each
    // border has one guaranteed passable edge so neighbouring chunks connect.
    {
        rnd::Rng b(rnd::hashCoords(seed, c.x, c.z, kSaltWestBoundary));
        const int guaranteed = b.rangeInt(0, kN - 1);
        for (int lz = 0; lz < kN; ++lz) {
            const bool open = lz == guaranteed || b.chance(0.5f);
            L.west[static_cast<size_t>(idx(0, lz))] = open ? pickPassable(b, 0.2f, 0.3f) : EdgeType::Wall;
        }
    }
    {
        rnd::Rng b(rnd::hashCoords(seed, c.x, c.z, kSaltSouthBoundary));
        const int guaranteed = b.rangeInt(0, kN - 1);
        for (int lx = 0; lx < kN; ++lx) {
            const bool open = lx == guaranteed || b.chance(0.5f);
            L.south[static_cast<size_t>(idx(lx, 0))] = open ? pickPassable(b, 0.2f, 0.3f) : EdgeType::Wall;
        }
    }

    // Stairwells: walled on every side but the entrance, which makes each a
    // leaf hanging off its (connected) neighbour.
    for (const Special& s : specials) {
        const int lx = s.cell % kN, lz = s.cell / kN;
        EdgeType* sides[4] = {&L.west[static_cast<size_t>(idx(lx, lz))], &L.west[static_cast<size_t>(idx(lx + 1, lz))],
                              &L.south[static_cast<size_t>(idx(lx, lz))], &L.south[static_cast<size_t>(idx(lx, lz + 1))]};
        for (int i = 0; i < 4; ++i) *sides[i] = i == s.side ? s.entrance : EdgeType::Wall;
    }
    return L;
}

EdgeType WorldGenerator::edge(int level, int gx, int gz, EdgeAxis axis) const {
    if (m_realm == world::Realm::Office) return office::edge(level, gx, gz, axis);
    const ChunkCoord c{world::floorDiv(gx, kN), world::floorDiv(gz, kN), level};
    const int lx = world::floorMod(gx, kN);
    const int lz = world::floorMod(gz, kN);
    const ChunkLayout& L = layout(c);
    const size_t i = static_cast<size_t>(lz * kN + lx);
    return axis == EdgeAxis::West ? L.west[i] : L.south[i];
}

CellRole WorldGenerator::cellRole(int level, int gx, int gz) const {
    if (m_realm == world::Realm::Office) return CellRole::Room;
    const ChunkCoord c{world::floorDiv(gx, kN), world::floorDiv(gz, kN), level};
    return layout(c).roles[static_cast<size_t>(world::floorMod(gz, kN) * kN + world::floorMod(gx, kN))];
}

bool WorldGenerator::vertexHasWall(int level, int gx, int gz) const {
    return edge(level, gx, gz, EdgeAxis::South) != EdgeType::Open ||     // +X
           edge(level, gx - 1, gz, EdgeAxis::South) != EdgeType::Open || // -X
           edge(level, gx, gz, EdgeAxis::West) != EdgeType::Open ||      // +Z
           edge(level, gx, gz - 1, EdgeAxis::West) != EdgeType::Open;    // -Z
}

bool WorldGenerator::vertexHasPillar(int level, int gx, int gz) const {
    if (m_realm == world::Realm::Office) return false;
    if (vertexHasWall(level, gx, gz)) return false;
    return rnd::toUnit(rnd::hashCoords(levelSeed(level), gx, gz, kSaltPillar)) < world::kPillarChance;
}

bool WorldGenerator::isLightBlocked(int level, const glm::vec2& a, const glm::vec2& b) const {
    // Crossings of vertical grid lines x = k*S (west edges).
    if (a.x != b.x) {
        const float lo = std::min(a.x, b.x), hi = std::max(a.x, b.x);
        for (int k = static_cast<int>(std::ceil(lo / S)); k <= static_cast<int>(std::floor(hi / S)); ++k) {
            const float x = static_cast<float>(k) * S;
            if (x <= lo || x >= hi) continue;
            const float t = (x - a.x) / (b.x - a.x);
            const float z = a.y + t * (b.y - a.y);
            const int gz = static_cast<int>(std::floor(z / S));
            if (edgeBlocksLight(edge(level, k, gz, EdgeAxis::West), z - static_cast<float>(gz) * S)) return true;
        }
    }
    // Crossings of horizontal grid lines z = k*S (south edges).
    if (a.y != b.y) {
        const float lo = std::min(a.y, b.y), hi = std::max(a.y, b.y);
        for (int k = static_cast<int>(std::ceil(lo / S)); k <= static_cast<int>(std::floor(hi / S)); ++k) {
            const float z = static_cast<float>(k) * S;
            if (z <= lo || z >= hi) continue;
            const float t = (z - a.y) / (b.y - a.y);
            const float x = a.x + t * (b.x - a.x);
            const int gx = static_cast<int>(std::floor(x / S));
            if (edgeBlocksLight(edge(level, gx, k, EdgeAxis::South), x - static_cast<float>(gx) * S)) return true;
        }
    }
    return false;
}

// ----- Chunk content ----------------------------------------------------------

ChunkBlueprint WorldGenerator::generate(const ChunkCoord& c) const {
    trimCache(); // safe here: no layout references are alive between calls

    ChunkBlueprint bp;
    bp.coord = c;
    bp.seed = chunkSeed(c);
    const glm::vec3 origin(c.originX(), c.originY(), c.originZ());

    buildShell(bp, origin);
    for (int lz = 0; lz < kN; ++lz) {
        for (int lx = 0; lx < kN; ++lx) {
            const int gx = c.x * kN + lx;
            const int gz = c.z * kN + lz;
            buildEdge(bp, origin, gx, gz, EdgeAxis::West);
            buildEdge(bp, origin, gx, gz, EdgeAxis::South);
            buildVertex(bp, origin, gx, gz);
        }
    }
    // An exit room without a wall to walk into gets a slab of glitch standing in its middle.
    if (m_exitCell && m_exitWalls == 0 && m_exitChunk && *m_exitChunk == c) {
        const glm::vec3 mid((static_cast<float>(m_exitCell->x) + 0.5f) * S, origin.y, (static_cast<float>(m_exitCell->y) + 0.5f) * S);
        addGlitchWall(bp, origin, AABB(mid + glm::vec3(-1.2f, 0.0f, -HT), mid + glm::vec3(1.2f, H, HT)), EdgeAxis::South);
    }
    // The stair itself belongs to the storey it rises from; each storey signs
    // its own entrances.
    if (const auto up = stairwell(c.level, c.x, c.z)) {
        const int gx = c.x * kN + up->lx, gz = c.z * kN + up->lz;
        stairs::build(bp.staticMesh, bp.colliders, gx, gz, c.level, up->rotation);
        stairs::addEntranceSign(bp.staticMesh, gx, gz, c.level, up->rotation, true);
    }
    if (const auto down = stairwell(c.level - 1, c.x, c.z)) {
        stairs::addEntranceSign(bp.staticMesh, c.x * kN + down->lx, c.z * kN + down->lz, c.level, down->rotation, false);
    }
    placeLights(bp, origin);
    if (m_realm == world::Realm::Office) {
        office::furnish(bp, levelSeed(c.level));
    } else {
        placeFurniture(bp, origin);
        scrawlWalls(bp, origin);
    }

    // Bounds include a margin for doors swinging into neighbouring chunks and
    // reach up to the next storey's ceiling for stairwell geometry.
    bp.bounds = AABB(origin + glm::vec3(-1.0f, -world::kSlabThickness, -1.0f),
                     origin + glm::vec3(world::kChunkSize + 1.0f, world::kLevelHeight + H, world::kChunkSize + 1.0f));
    return bp;
}

void WorldGenerator::buildShell(ChunkBlueprint& bp, const glm::vec3& origin) const {
    const ChunkCoord& c = bp.coord;
    glm::vec4 chunkRect(origin.x, origin.z, origin.x + world::kChunkSize, origin.z + world::kChunkSize);
    MaterialId floorMat = MaterialId::Carpet;
    if (m_realm == world::Realm::Office) {
        // Only the building has a floor and a ceiling: there is nothing outside it.
        if (c.level != office::kLevel) return;
        chunkRect = glm::vec4(std::max(chunkRect.x, 0.0f), std::max(chunkRect.y, 0.0f),
                              std::min(chunkRect.z, static_cast<float>(office::kWidth) * S),
                              std::min(chunkRect.w, static_cast<float>(office::kDepth) * S));
        if (chunkRect.x >= chunkRect.z || chunkRect.y >= chunkRect.w) return;
        floorMat = MaterialId::OfficeCarpet;
    }

    // Stairwell shafts pierce the floor (arriving from below) and the
    // ceiling (rising from here).
    std::optional<glm::vec4> floorHole, ceilingHole;
    if (const auto down = stairwell(c.level - 1, c.x, c.z)) {
        floorHole = stairs::holeRect(c.x * kN + down->lx, c.z * kN + down->lz, down->rotation);
    }
    if (const auto up = stairwell(c.level, c.x, c.z)) {
        ceilingHole = stairs::holeRect(c.x * kN + up->lx, c.z * kN + up->lz, up->rotation);
    }

    const float y = origin.y;
    for (const glm::vec4& r : subtractHole(chunkRect, floorHole)) {
        // Carpet: the top face of a thin slab below the floor; the collider is
        // the full slab thickness so nothing can tunnel through.
        addSolid(bp, origin, AABB(glm::vec3(r.x, y - 0.1f, r.y), glm::vec3(r.z, y, r.w)), floorMat,
                 mesh::FacePosY, false);
        bp.colliders.emplace_back(glm::vec3(r.x, y - world::kSlabThickness, r.y), glm::vec3(r.z, y, r.w));
    }
    for (const glm::vec4& r : subtractHole(chunkRect, ceilingHole)) {
        // Acoustic tile ceiling: the bottom face of a slab above the ceiling height.
        addSolid(bp, origin, AABB(glm::vec3(r.x, y + H, r.y), glm::vec3(r.z, y + H + 0.1f, r.w)),
                 MaterialId::CeilingTile, mesh::FaceNegY, false);
        bp.colliders.emplace_back(glm::vec3(r.x, y + H, r.y), glm::vec3(r.z, y + world::kLevelHeight, r.w));
    }
}

void WorldGenerator::buildEdge(ChunkBlueprint& bp, const glm::vec3& origin, int gx, int gz, EdgeAxis axis) const {
    const int level = bp.coord.level;
    const EdgeType type = edge(level, gx, gz, axis);
    if (type == EdgeType::Open) return;

    const EdgeFrame f{axis, glm::vec2(static_cast<float>(gx) * S, static_cast<float>(gz) * S), origin.y};
    const float s0 = HT;          // wall runs between the corner posts
    const float s1 = S - HT;
    const float mid = S * 0.5f;

    const MaterialId wall = wallMaterial();
    if (type == EdgeType::Wall) {
        if (isGlitchEdge(level, gx, gz, axis)) {
            addGlitchWall(bp, origin, f.box(s0, s1, 0.0f, H, HT), axis);
            return;
        }
        addSolid(bp, origin, f.box(s0, s1, 0.0f, H, HT), wall, f.longFaces());
        return;
    }

    if (type == EdgeType::Archway) {
        const float hw = world::kArchWidth * 0.5f;
        addSolid(bp, origin, f.box(s0, mid - hw, 0.0f, H, HT), wall, f.longFaces() | f.endCap());
        addSolid(bp, origin, f.box(mid + hw, s1, 0.0f, H, HT), wall, f.longFaces() | f.startCap());
        addSolid(bp, origin, f.box(mid - hw, mid + hw, world::kArchHeight, H, HT), wall,
                 f.longFaces() | mesh::FaceNegY);
        return;
    }

    // ---- Door frame -------------------------------------------------------------
    const float hw = world::kDoorOpeningWidth * 0.5f;
    const float jw = world::kDoorFrameWidth;
    const float hd = world::kDoorOpeningHeight;
    const float frameDepth = HT + world::kDoorFrameProtrude;

    // Wall segments either side and the header above (reveals hidden by the frame).
    addSolid(bp, origin, f.box(s0, mid - hw, 0.0f, H, HT), wall, f.longFaces());
    addSolid(bp, origin, f.box(mid + hw, s1, 0.0f, H, HT), wall, f.longFaces());
    addSolid(bp, origin, f.box(mid - hw, mid + hw, hd, H, HT), wall, f.longFaces());

    // Painted steel frame: two jambs and a head jamb.
    addSolid(bp, origin, f.box(mid - hw, mid - hw + jw, 0.0f, hd, frameDepth), MaterialId::GrayMetal,
             mesh::FaceSides);
    addSolid(bp, origin, f.box(mid + hw - jw, mid + hw, 0.0f, hd, frameDepth), MaterialId::GrayMetal,
             mesh::FaceSides);
    addSolid(bp, origin, f.box(mid - hw + jw, mid + hw - jw, hd - jw, hd, frameDepth), MaterialId::GrayMetal,
             f.longFaces() | mesh::FaceNegY);

    // Door leaf: hinge side chosen deterministically from the edge hash -
    // except at a stairwell's entrance, where it is hinged on the side away
    // from the stairs: pushed open into the stairwell (it swings away from
    // whoever opens it), the door lies flat against the far side of the
    // lobby, clear of the way to the flights.
    const uint64_t id = rnd::hashCoords(levelSeed(level), gx, gz, kSaltDoor + static_cast<uint64_t>(axis));
    bool hingeAtStart = (id & 1u) == 0u;
    const glm::ivec2 before = axis == EdgeAxis::West ? glm::ivec2(gx - 1, gz) : glm::ivec2(gx, gz - 1);
    const glm::ivec2 after(gx, gz);
    const glm::ivec2 stairCell = cellRole(level, before.x, before.y) != CellRole::Room ? before : after;
    const CellRole stairRole = cellRole(level, stairCell.x, stairCell.y);
    if (stairRole != CellRole::Room) {
        const int lower = stairRole == CellRole::StairsLower ? level : level - 1;
        if (const auto s = stairwell(lower, world::floorDiv(stairCell.x, kN), world::floorDiv(stairCell.y, kN))) {
            // Where the way goes once through the door: to the foot of the first
            // flight (the bottom storey), or from the head of the second (the top).
            const std::vector<glm::vec3> route = stairs::climbRoute(stairCell.x, stairCell.y, lower, s->rotation);
            const glm::vec3 way = route[stairRole == CellRole::StairsLower ? 2 : 8];
            hingeAtStart = glm::dot(way - f.point(mid), f.alongDir()) > 0.0f; // the way leads to the end side
        }
    }
    DoorPlacement door;
    door.id = id;
    door.gx = gx;
    door.gz = gz;
    door.axis = axis;
    if (hingeAtStart) {
        door.hinge = f.point(mid - hw + jw);
        door.closedDir = f.alongDir();
    } else {
        door.hinge = f.point(mid + hw - jw);
        door.closedDir = -f.alongDir();
    }
    bp.doors.push_back(door);
}

void WorldGenerator::buildVertex(ChunkBlueprint& bp, const glm::vec3& origin, int gx, int gz) const {
    const int level = bp.coord.level;
    const glm::vec3 p(static_cast<float>(gx) * S, origin.y, static_cast<float>(gz) * S);

    const bool px = edge(level, gx, gz, EdgeAxis::South) != EdgeType::Open;
    const bool nx = edge(level, gx - 1, gz, EdgeAxis::South) != EdgeType::Open;
    const bool pz = edge(level, gx, gz, EdgeAxis::West) != EdgeType::Open;
    const bool nz = edge(level, gx, gz - 1, EdgeAxis::West) != EdgeType::Open;

    if (px || nx || pz || nz) {
        // Corner post joining the walls; faces hidden against walls are skipped.
        uint8_t faces = 0;
        if (!px) faces |= mesh::FacePosX;
        if (!nx) faces |= mesh::FaceNegX;
        if (!pz) faces |= mesh::FacePosZ;
        if (!nz) faces |= mesh::FaceNegZ;
        const AABB post(p + glm::vec3(-HT, 0.0f, -HT), p + glm::vec3(HT, H, HT));
        mesh::BoxDesc d;
        d.min = post.min;
        d.max = post.max;
        d.material = wallMaterial();
        d.faces = faces;
        d.uvOrigin = origin;
        if (faces) mesh::addBox(bp.staticMesh, d);
        bp.colliders.push_back(post);
        return;
    }

    if (vertexHasPillar(level, gx, gz)) {
        const float h = world::kPillarSize * 0.5f;
        addSolid(bp, origin, AABB(p + glm::vec3(-h, 0.0f, -h), p + glm::vec3(h, H, h)), wallMaterial(),
                 mesh::FaceSides);
    }
}

void WorldGenerator::addFixture(ChunkBlueprint& bp, const glm::vec3& origin, const glm::vec2& centre, bool alongX,
                                int& lightIndex, float unrest, const glm::ivec2* shaftCell) const {
    const glm::vec2 half = alongX ? glm::vec2(world::kFixtureLength, world::kFixtureWidth) * 0.5f
                                  : glm::vec2(world::kFixtureWidth, world::kFixtureLength) * 0.5f;
    const float x0w = centre.x - half.x, x1w = centre.x + half.x;
    const float z0w = centre.y - half.y, z1w = centre.y + half.y;
    const float yc = origin.y + H;
    const float yb = yc - world::kFixtureDepth;

    // Housing sides (metal).
    mesh::BoxDesc housing;
    housing.min = glm::vec3(x0w, yb, z0w);
    housing.max = glm::vec3(x1w, yc, z1w);
    housing.material = MaterialId::GrayMetal;
    housing.faces = mesh::FaceSides;
    housing.uvOrigin = origin;
    mesh::addBox(bp.staticMesh, housing);

    // Emissive diffuser facing down, UV (0..1) across the fixture:
    // u across the short side, v along the long side (tube direction).
    const glm::vec3 corners[4] = {{x0w, yb, z0w}, {x1w, yb, z0w}, {x1w, yb, z1w}, {x0w, yb, z1w}};
    const glm::vec2 uvZ[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const glm::vec2 uvX[4] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
    mesh::addQuad(bp.staticMesh, corners, glm::vec3(0, -1, 0), alongX ? uvX : uvZ, MaterialId::LightPanel,
                  static_cast<float>(lightIndex));

    LightFixture light(glm::vec3(centre.x, yb - 0.005f, centre.y), half,
                       rnd::hashCombine(bp.seed, static_cast<uint64_t>(lightIndex)), unrest);
    computeLightVisibility(light, bp.coord.level, shaftCell);
    bp.lights.push_back(std::move(light));
    ++lightIndex;
}

void WorldGenerator::placeLights(ChunkBlueprint& bp, const glm::vec3& origin) const {
    const float tile = world::kCeilingTileSize;
    const int level = bp.coord.level;
    // Chunk-wide electrical "unrest" (squared -> most chunks are calm). The
    // farther from level 0, the worse the wiring: deeper storeys are darker.
    rnd::Rng chunkRng(rnd::hashCombine(bp.seed, kSaltLights));
    float unrest = chunkRng.nextFloat() * chunkRng.nextFloat();
    unrest = std::min(1.0f, unrest + std::min(0.45f, 0.06f * static_cast<float>(std::abs(level))));

    int lightIndex = 0;
    if (m_realm == world::Realm::Office) {
        // A regular grid of fixtures, bright and mostly healthy - mostly.
        const float calm = 0.05f + 0.2f * chunkRng.nextFloat() * chunkRng.nextFloat();
        for (int lz = 0; lz < kN; ++lz) {
            for (int lx = 0; lx < kN; ++lx) {
                const int gx = bp.coord.x * kN + lx, gz = bp.coord.z * kN + lz;
                if (level != office::kLevel) continue;
                const glm::vec2 cellMin(static_cast<float>(gx) * S, static_cast<float>(gz) * S);
                for (const glm::vec2& c : office::fixtures(gx, gz)) addFixture(bp, origin, cellMin + c, false, lightIndex, calm, nullptr);
            }
        }
        return;
    }
    for (int lz = 0; lz < kN; ++lz) {
        for (int lx = 0; lx < kN; ++lx) {
            const int gx = bp.coord.x * kN + lx;
            const int gz = bp.coord.z * kN + lz;
            const CellRole role = cellRole(level, gx, gz);
            if (role == CellRole::StairsLower) {
                const auto up = stairwell(level, bp.coord.x, bp.coord.z);
                const stairs::FixtureSpot spot = stairs::lobbyFixture(gx, gz, up->rotation);
                addFixture(bp, origin, spot.center, spot.alongX, lightIndex, unrest, nullptr);
                continue;
            }
            if (role == CellRole::StairsUpper) {
                const auto down = stairwell(level - 1, bp.coord.x, bp.coord.z);
                const stairs::FixtureSpot spot = stairs::shaftFixture(gx, gz, down->rotation);
                const glm::ivec2 cell(gx, gz);
                addFixture(bp, origin, spot.center, spot.alongX, lightIndex, unrest, &cell);
                continue;
            }

            rnd::Rng rng(rnd::hashCoords(bp.seed, lx, lz, kSaltLights));
            const float roll = rng.nextFloat();
            if (roll < 0.07f) continue; // occasional cell with no fixture at all

            // Fixture centres in cell-local coordinates, snapped to the ceiling
            // tile grid, expressed for a fixture whose long axis runs along Z.
            std::vector<glm::vec2> centres;
            const float x0 = (rng.chance(0.5f) ? 3.5f : 4.5f) * tile; // one tile wide
            if (roll < 0.60f) {
                centres.push_back({x0, 4.0f * tile});                  // single, centred
            } else if (roll < 0.82f) {
                centres.push_back({x0, 2.0f * tile});                  // two in line
                centres.push_back({x0, 6.0f * tile});
            } else {
                centres.push_back({2.5f * tile, 4.0f * tile});         // two side by side
                centres.push_back({5.5f * tile, 4.0f * tile});
            }
            const bool alongX = rng.chance(0.5f);

            const glm::vec2 cellMin(origin.x + static_cast<float>(lx) * S, origin.z + static_cast<float>(lz) * S);
            // The glitch room's tubes are failing too.
            const float cellUnrest = isExitCell(level, gx, gz) ? 1.0f : unrest;
            for (const glm::vec2& lc : centres) {
                const glm::vec2 local = alongX ? glm::vec2(lc.y, lc.x) : lc;
                addFixture(bp, origin, cellMin + local, alongX, lightIndex, cellUnrest, nullptr);
            }
        }
    }
}

void WorldGenerator::computeLightVisibility(LightFixture& light, int level, const glm::ivec2* shaftCell) const {
    const float gs = world::kLightGridCell;
    const float R = world::kLightRange;
    const glm::vec2 c(light.center().x, light.center().z);
    const int gx0 = static_cast<int>(std::floor((c.x - R) / gs));
    const int gx1 = static_cast<int>(std::floor((c.x + R) / gs));
    const int gz0 = static_cast<int>(std::floor((c.y - R) / gs));
    const int gz1 = static_cast<int>(std::floor((c.y + R) / gs));
    const float inset = 0.25f;

    std::vector<glm::ivec3>& cells = light.visibleCells();
    cells.clear();
    for (int gz = gz0; gz <= gz1; ++gz) {
        for (int gx = gx0; gx <= gx1; ++gx) {
            const glm::vec2 mn(static_cast<float>(gx) * gs, static_cast<float>(gz) * gs);
            const glm::vec2 mx = mn + glm::vec2(gs);
            const glm::vec2 nearest = glm::clamp(c, mn, mx);
            if (glm::length(nearest - c) > R) continue; // outside the light's range

            // The cell is lit if any of five sample points has line of sight.
            const bool containsLight = c.x >= mn.x && c.x < mx.x && c.y >= mn.y && c.y < mx.y;
            bool visible = containsLight;
            const glm::vec2 samples[5] = {(mn + mx) * 0.5f,
                                          {mn.x + inset, mn.y + inset}, {mx.x - inset, mn.y + inset},
                                          {mx.x - inset, mx.y - inset}, {mn.x + inset, mx.y - inset}};
            for (int i = 0; i < 5 && !visible; ++i) {
                visible = !isLightBlocked(level, c, samples[i]);
            }
            if (visible) cells.emplace_back(gx, gz, level);
        }
    }

    // A fixture over a stairwell shaft shines down it: the light-grid cells of
    // the shaft on the storey below see it too.
    if (shaftCell) {
        const int perCell = static_cast<int>(S / gs + 0.5f);
        for (int dz = 0; dz < perCell; ++dz) {
            for (int dx = 0; dx < perCell; ++dx) {
                cells.emplace_back(shaftCell->x * perCell + dx, shaftCell->y * perCell + dz, level - 1);
            }
        }
    }
}

void WorldGenerator::placeFurniture(ChunkBlueprint& bp, const glm::vec3& origin) const {
    const float T = world::kWallThickness;
    const int level = bp.coord.level;

    // The exit chunk always has a terminal (its MAP shows the way out): one
    // room - one with a wall, if any - gets a desk and a computer whatever its roll.
    int forcedCell = -1;
    if (m_exitChunk && *m_exitChunk == bp.coord) {
        const int start = static_cast<int>(rnd::hashCoords(bp.seed, 0, 0, kSaltExit) % static_cast<uint64_t>(kN * kN));
        int anyRoom = -1;
        for (int k = 0; k < kN * kN && forcedCell < 0; ++k) {
            const int cell = (start + k) % (kN * kN);
            const int gx = bp.coord.x * kN + cell % kN, gz = bp.coord.z * kN + cell / kN;
            if (cellRole(level, gx, gz) != CellRole::Room || isExitCell(level, gx, gz)) continue;
            if (anyRoom < 0) anyRoom = cell;
            const bool walled = edge(level, gx, gz, EdgeAxis::West) == EdgeType::Wall || edge(level, gx + 1, gz, EdgeAxis::West) == EdgeType::Wall ||
                                edge(level, gx, gz, EdgeAxis::South) == EdgeType::Wall || edge(level, gx, gz + 1, EdgeAxis::South) == EdgeType::Wall;
            if (walled) forcedCell = cell;
        }
        if (forcedCell < 0) forcedCell = anyRoom; // all open plan: the desk stands free
    }

    for (int lz = 0; lz < kN; ++lz) {
        for (int lx = 0; lx < kN; ++lx) {
            const int gx = bp.coord.x * kN + lx;
            const int gz = bp.coord.z * kN + lz;
            if (cellRole(level, gx, gz) != CellRole::Room) continue; // stairwells stay clear
            if (isExitCell(level, gx, gz)) continue;                // the glitch room is bare
            const bool forceTerminal = lz * kN + lx == forcedCell;
            rnd::Rng rng(rnd::hashCoords(bp.seed, lx, lz, kSaltFurniture));
            // Terminals and phones use their own streams so furniture layouts are unaffected.
            rnd::Rng terminalRng(rnd::hashCoords(bp.seed, lx, lz, kSaltTerminal));
            rnd::Rng phoneRng(rnd::hashCoords(bp.seed, lx, lz, kSaltPhone));
            // ...as do the gun's parts and the cabinets' ids.
            rnd::Rng itemRng(rnd::hashCoords(bp.seed, lx, lz, kSaltItem));
            int deskCount = 0;
            int cabinetCount = 0;
            int siteCount = 0;

            const glm::vec3 cellMin = origin + glm::vec3(static_cast<float>(lx) * S, 0.0f, static_cast<float>(lz) * S);
            const glm::vec3 cellCenter = cellMin + glm::vec3(S * 0.5f, 0.0f, S * 0.5f);

            // Solid walls of this cell: 0 = west, 1 = east, 2 = south, 3 = north.
            const EdgeType sides[4] = {edge(level, gx, gz, EdgeAxis::West), edge(level, gx + 1, gz, EdgeAxis::West),
                                       edge(level, gx, gz, EdgeAxis::South), edge(level, gx, gz + 1, EdgeAxis::South)};
            const glm::vec3 inward[4] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
            std::vector<int> walls;
            for (int i = 0; i < 4; ++i) {
                if (sides[i] == EdgeType::Wall) walls.push_back(i);
            }

            // Orientation helpers: yaw mapping local +Z / +X onto a direction.
            auto yawFacing = [](const glm::vec3& dir) { return std::atan2(dir.x, dir.z); };
            auto yawAlongX = [](const glm::vec3& dir) { return std::atan2(-dir.z, dir.x); };
            // Occasionally a part of the Tesla coil gun rests somewhere (see Gameplay/Items).
            auto maybeItem = [&](float chance, SiteKind kind, const glm::mat4& surface, int cabinet, int drawer) {
                const uint64_t id = rnd::hashCoords(levelSeed(level), gx, gz, kSaltItem + static_cast<uint64_t>(siteCount++));
                if (!itemRng.chance(chance)) return;
                Item item;
                item.id = id;
                const float pick = itemRng.nextFloat();
                const float rest = (1.0f - world::kBatteryShare) / 3.0f;
                item.type = pick < world::kBatteryShare              ? PartType::Battery
                            : pick < world::kBatteryShare + rest        ? PartType::Driver
                            : pick < world::kBatteryShare + 2.0f * rest ? PartType::Coil
                                                                         : PartType::TopLoad;
                item.charge = item.type == PartType::Battery ? itemRng.range(world::kBatteryMinCharge, 1.0f) : 1.0f;
                ItemSite site;
                site.id = id;
                site.kind = kind;
                site.local = surface * tesla::restTransform(item.type, kind, itemRng.range(-3.14159f, 3.14159f));
                site.cabinet = cabinet;
                site.drawer = drawer;
                site.item = item;
                bp.items.push_back(site);
            };

            // Filing cabinets are actors (their drawers open); everything else is static furniture.
            auto add = [&](FurnitureType t, const glm::vec3& p, float yaw) {
                const FurnitureInstance inst = Furniture::makeInstance(t, p, yaw);
                if (t != FurnitureType::FileCabinet) {
                    bp.furniture.push_back(inst);
                    if (t == FurnitureType::Chair) {
                        maybeItem(world::kPartChairChance, SiteKind::Chair,
                                  glm::translate(inst.model, glm::vec3(0.0f, 0.50f, 0.02f)), -1, -1);
                    }
                    return;
                }
                const uint64_t id = rnd::hashCoords(levelSeed(level), gx, gz, kSaltCabinet + static_cast<uint64_t>(cabinetCount++));
                bp.cabinets.push_back({id, inst.model});
                const int index = static_cast<int>(bp.cabinets.size()) - 1;
                // One roll for the whole cabinet; a part, if there is one, lies in one drawer picked at random.
                const int partDrawer = itemRng.chance(world::kPartCabinetChance) ? itemRng.rangeInt(0, FileCabinet::kDrawers - 1) : -1;
                for (int d = 0; d < FileCabinet::kDrawers; ++d) {
                    maybeItem(d == partDrawer ? 1.0f : 0.0f, SiteKind::Drawer, FileCabinet::itemSurface(), index, d);
                }
            };

            // Frame of a wall side: inner face midpoint, inward normal, along-wall axis.
            auto wallFrame = [&](int side, glm::vec3& face, glm::vec3& n, glm::vec3& r) {
                n = inward[side];
                r = glm::vec3(-n.z, 0.0f, n.x);
                face = cellCenter - n * (S * 0.5f - T * 0.5f);
            };

            // Occasionally a desk carries a retro computer, facing the chair.
            auto maybeTerminal = [&]() {
                if (!terminalRng.chance(world::kTerminalChance) && !(forceTerminal && deskCount == 0)) return false;
                const glm::mat4 model =
                    glm::translate(bp.furniture.back().model, glm::vec3(-0.2f, 0.75f, -0.04f)); // on the desk top
                const uint64_t id = rnd::hashCoords(levelSeed(level), gx, gz, kSaltTerminal + static_cast<uint64_t>(deskCount));
                bp.terminals.push_back({id, model, terminalRng.chance(0.85f) || forceTerminal});
                return true;
            };

            // Often a desk has a telephone: beside the computer (turned towards
            // the chair), or anywhere along the desk if there is none.
            // Returns where along the desk it stands (or a far-off value if there is none).
            auto maybePhone = [&](bool besideTerminal) {
                if (!phoneRng.chance(world::kPhoneChance)) return 1e3f;
                const float x = besideTerminal ? phoneRng.range(0.40f, 0.52f) : phoneRng.range(-0.40f, 0.50f);
                const float z = phoneRng.range(-0.14f, 0.02f);
                const float yaw = besideTerminal ? phoneRng.range(-0.45f, -0.12f) : -0.5f * x + phoneRng.range(-0.15f, 0.15f);
                const glm::mat4 model = glm::rotate(glm::translate(bp.furniture.back().model, glm::vec3(x, 0.75f, z)), yaw,
                                                    glm::vec3(0.0f, 1.0f, 0.0f));
                const uint64_t id = rnd::hashCoords(levelSeed(level), gx, gz, kSaltPhone + static_cast<uint64_t>(deskCount));
                bp.phones.push_back({id, model});
                return x;
            };

            // A part on the desk top, clear of the computer (left of centre) and the phone.
            auto maybeDeskItem = [&](bool terminal, float phoneX) {
                const glm::mat4 desk = bp.furniture.back().model;
                float spots[3] = {-0.55f, 0.30f, 0.56f};
                const int first = itemRng.rangeInt(0, 2);
                for (int k = 0; k < 3; ++k) {
                    const float x = spots[(first + k) % 3];
                    if (terminal && x < 0.14f) continue;
                    if (std::fabs(x - phoneX) < 0.27f) continue;
                    maybeItem(world::kPartDeskChance, SiteKind::Desk,
                              glm::translate(desk, glm::vec3(x, 0.75f, itemRng.range(-0.18f, 0.12f))), -1, -1);
                    return;
                }
            };

            // A desk against the wall, a chair pulled up to it, optional
            // partitions and a filing cabinet: a classic cubicle.
            auto cubicle = [&](int side, bool partitions) {
                glm::vec3 face, n, r;
                wallFrame(side, face, n, r);
                const glm::vec3 base = face + r * rng.range(-0.5f, 0.5f);
                add(FurnitureType::Desk, base + n * (0.375f + 0.03f), yawFacing(n));
                const bool terminal = maybeTerminal();
                const float phoneX = maybePhone(terminal);
                maybeDeskItem(terminal, phoneX);
                ++deskCount;
                add(FurnitureType::Chair,
                    base + n * (0.78f + rng.range(0.25f, 0.55f)) + r * rng.range(-0.3f, 0.3f),
                    yawFacing(-n) + rng.range(-0.6f, 0.6f));
                if (partitions) {
                    for (float s : {-1.0f, 1.0f}) {
                        add(FurnitureType::Partition, base + r * (s * 0.80f) + n * 0.76f, yawAlongX(n));
                    }
                }
                if (rng.chance(0.5f)) {
                    const float s = rng.chance(0.5f) ? 1.0f : -1.0f;
                    add(FurnitureType::FileCabinet, base + r * (s * 1.09f) + n * 0.345f, yawFacing(n));
                }
            };

            const float roll = rng.nextFloat();
            if (forceTerminal && walls.empty()) {
                // A desk in the middle of the room, the computer facing its chair.
                const glm::vec3 n(0.0f, 0.0f, 1.0f);
                add(FurnitureType::Desk, cellCenter, yawFacing(n));
                maybeTerminal();
                maybePhone(true);
                ++deskCount;
                add(FurnitureType::Chair, cellCenter + n * (0.78f + rng.range(0.25f, 0.55f)), yawFacing(-n) + rng.range(-0.6f, 0.6f));
            } else if (forceTerminal) {
                cubicle(walls[static_cast<size_t>(rng.rangeInt(0, static_cast<int>(walls.size()) - 1))], true);
            } else if (!walls.empty() && roll < 0.24f) {
                const int side = walls[static_cast<size_t>(rng.rangeInt(0, static_cast<int>(walls.size()) - 1))];
                cubicle(side, true);
                // Occasionally a second cubicle on the opposite wall.
                const int opposite = side ^ 1;
                if (sides[opposite] == EdgeType::Wall && rng.chance(0.35f)) cubicle(opposite, true);
            } else if (!walls.empty() && roll < 0.40f) {
                // A row of filing cabinets pushed against a wall, some askew.
                const int side = walls[static_cast<size_t>(rng.rangeInt(0, static_cast<int>(walls.size()) - 1))];
                glm::vec3 face, n, r;
                wallFrame(side, face, n, r);
                const int count = rng.rangeInt(1, 3);
                const float start = rng.range(-1.2f, 1.2f - 0.47f * static_cast<float>(count - 1));
                for (int i = 0; i < count; ++i) {
                    const float gap = rng.range(0.0f, 0.06f);
                    add(FurnitureType::FileCabinet,
                        face + r * (start + 0.47f * static_cast<float>(i)) + n * (0.345f + gap),
                        yawFacing(n) + rng.range(-0.06f, 0.06f));
                }
            } else if (roll < 0.47f) {
                // An abandoned chair drifting somewhere in the room.
                const glm::vec3 p = cellMin + glm::vec3(rng.range(1.4f, 3.6f), 0.0f, rng.range(1.4f, 3.6f));
                add(FurnitureType::Chair, p, rng.range(0.0f, 6.2831853f));
            } else if (!walls.empty() && roll < 0.53f) {
                const int side = walls[static_cast<size_t>(rng.rangeInt(0, static_cast<int>(walls.size()) - 1))];
                cubicle(side, false);
            }
        }
    }
}

void WorldGenerator::scrawlWalls(ChunkBlueprint& bp, const glm::vec3& origin) const {
    const int level = bp.coord.level;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 inward[4] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int lz = 0; lz < kN; ++lz) {
        for (int lx = 0; lx < kN; ++lx) {
            const int gx = bp.coord.x * kN + lx, gz = bp.coord.z * kN + lz;
            if (cellRole(level, gx, gz) != CellRole::Room || isExitCell(level, gx, gz)) continue;
            // Its own stream: writing never changes a layout, and a layout change never moves the writing.
            rnd::Rng rng(rnd::hashCoords(bp.seed, lx, lz, kSaltWriting));
            const struct { int gx, gz; EdgeAxis axis; } sides[4] = {
                {gx, gz, EdgeAxis::West}, {gx + 1, gz, EdgeAxis::West}, {gx, gz, EdgeAxis::South}, {gx, gz + 1, EdgeAxis::South}};
            const glm::vec3 centre = origin + glm::vec3((static_cast<float>(lx) + 0.5f) * S, 0.0f, (static_cast<float>(lz) + 0.5f) * S);
            for (int i = 0; i < 4; ++i) {
                if (edge(level, sides[i].gx, sides[i].gz, sides[i].axis) != EdgeType::Wall) continue;
                if (!rng.chance(world::kWritingChance)) continue;
                if (isGlitchEdge(level, sides[i].gx, sides[i].gz, sides[i].axis)) continue; // the exit room's walls will not hold ink
                const decals::Scrawl note = decals::scrawl(rng, m_wallClue, 3.6f);
                float width = 0.0f;
                for (const std::string& l : note.lines) width = std::max(width, decals::lineWidth(l, note.charHeight));
                const float height = note.charHeight * (1.0f + 1.7f * static_cast<float>(note.lines.size() - 1));
                // Somewhere between the corner posts, around eye level (lower for bigger writing).
                const float room = std::max(0.0f, S - 2.0f * HT - 0.4f - width);
                const float along = rng.range(-0.5f, 0.5f) * room;
                const float top = std::clamp(rng.range(1.35f, 2.0f), 0.9f + height, H - 0.25f);
                const glm::vec3 n = inward[i];
                const glm::vec3 right = glm::cross(-n, up);
                const glm::vec3 face = centre - n * (S * 0.5f - HT - world::kDecalOffset);
                decals::TextLayout layout;
                layout.charHeight = note.charHeight;
                layout.wobble = note.wobble;
                layout.slant = rng.range(-0.05f, 0.05f) * note.wobble;
                const glm::vec3 anchor = face + right * (along - 0.5f * width) + up * (top - note.charHeight);
                decals::addText(bp.staticMesh, note.lines, anchor, right, up, note.ink, layout, rng.next());
                bp.clues.push_back({face + right * along + up * (top - 0.5f * height), n, note.clue});
            }
        }
    }
}

void WorldGenerator::addGlitchWall(ChunkBlueprint& bp, const glm::vec3& origin, const AABB& box, EdgeAxis axis) const {
    // Both long faces, cut into loose ~0.4 m tiles. Each tile carries its own
    // seed (in the vertex's light-index slot): the shaders make every tile
    // jump in and out of the wall, smear, blink out and come back on its own
    // schedule. UVs are the wallpaper's, so between glitches it still looks like the wall.
    const bool alongX = axis == EdgeAxis::South;
    const float a0 = alongX ? box.min.x : box.min.z, a1 = alongX ? box.max.x : box.max.z;
    const float y0 = box.min.y, y1 = box.max.y;
    const int nu = std::max(1, static_cast<int>(std::round((a1 - a0) / 0.4f)));
    const int nv = std::max(1, static_cast<int>(std::round((y1 - y0) / 0.42f)));
    const float wallTile = materialInfo(MaterialId::Wallpaper).tileSize;
    const float originAlong = alongX ? origin.x : origin.z;
    uint32_t seed = static_cast<uint32_t>(rnd::hashCoords(m_seed, static_cast<int>(a0 * 10.0f), static_cast<int>(box.min.z * 10.0f + box.min.x), kSaltExit) & 0xFFFu);
    for (int face = 0; face < 2; ++face) {
        const float across = face == 0 ? (alongX ? box.min.z : box.min.x) : (alongX ? box.max.z : box.max.x);
        const glm::vec3 n = alongX ? glm::vec3(0.0f, 0.0f, face == 0 ? -1.0f : 1.0f) : glm::vec3(face == 0 ? -1.0f : 1.0f, 0.0f, 0.0f);
        auto at = [&](float a, float y) { return alongX ? glm::vec3(a, y, across) : glm::vec3(across, y, a); };
        for (int j = 0; j < nv; ++j) {
            for (int i = 0; i < nu; ++i) {
                const float u0 = a0 + (a1 - a0) * static_cast<float>(i) / nu, u1 = a0 + (a1 - a0) * static_cast<float>(i + 1) / nu;
                const float v0 = y0 + (y1 - y0) * static_cast<float>(j) / nv, v1 = y0 + (y1 - y0) * static_cast<float>(j + 1) / nv;
                glm::vec3 corners[4] = {at(u0, v0), at(u1, v0), at(u1, v1), at(u0, v1)};
                glm::vec2 uvs[4] = {{(u0 - originAlong) / wallTile, (v0 - origin.y) / wallTile},
                                    {(u1 - originAlong) / wallTile, (v0 - origin.y) / wallTile},
                                    {(u1 - originAlong) / wallTile, (v1 - origin.y) / wallTile},
                                    {(u0 - originAlong) / wallTile, (v1 - origin.y) / wallTile}};
                if (glm::dot(glm::cross(corners[1] - corners[0], corners[2] - corners[1]), n) < 0.0f) {
                    std::swap(corners[1], corners[3]);
                    std::swap(uvs[1], uvs[3]);
                }
                seed = (seed * 1103515245u + 12345u) & 0xFFFFu;
                mesh::addQuad(bp.staticMesh, corners, n, uvs, MaterialId::Glitch, static_cast<float>(seed));
            }
        }
    }
    bp.glitchZones.push_back(box);
}
