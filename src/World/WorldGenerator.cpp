// ---------------------------------------------------------------------------
// WorldGenerator.cpp
// ---------------------------------------------------------------------------
#include "World/WorldGenerator.h"

#include "Math/Random.h"
#include "Render/MeshBuilder.h"

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

uint64_t WorldGenerator::levelSeed(int level) const {
    if (level == 0) return m_seed;
    return rnd::hashCombine(rnd::splitmix64(m_seed ^ kSaltLevel), static_cast<uint64_t>(static_cast<uint32_t>(level)));
}

uint64_t WorldGenerator::chunkSeed(const ChunkCoord& c) const {
    return rnd::hashCoords(levelSeed(c.level), c.x, c.z, kSaltChunk);
}

std::optional<stairs::Placement> WorldGenerator::stairwell(int lowerLevel, int cx, int cz) const {
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
    const ChunkCoord c{world::floorDiv(gx, kN), world::floorDiv(gz, kN), level};
    const int lx = world::floorMod(gx, kN);
    const int lz = world::floorMod(gz, kN);
    const ChunkLayout& L = layout(c);
    const size_t i = static_cast<size_t>(lz * kN + lx);
    return axis == EdgeAxis::West ? L.west[i] : L.south[i];
}

CellRole WorldGenerator::cellRole(int level, int gx, int gz) const {
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
    placeFurniture(bp, origin);

    // Bounds include a margin for doors swinging into neighbouring chunks and
    // reach up to the next storey's ceiling for stairwell geometry.
    bp.bounds = AABB(origin + glm::vec3(-1.0f, -world::kSlabThickness, -1.0f),
                     origin + glm::vec3(world::kChunkSize + 1.0f, world::kLevelHeight + H, world::kChunkSize + 1.0f));
    return bp;
}

void WorldGenerator::buildShell(ChunkBlueprint& bp, const glm::vec3& origin) const {
    const ChunkCoord& c = bp.coord;
    const glm::vec4 chunkRect(origin.x, origin.z, origin.x + world::kChunkSize, origin.z + world::kChunkSize);

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
        addSolid(bp, origin, AABB(glm::vec3(r.x, y - 0.1f, r.y), glm::vec3(r.z, y, r.w)), MaterialId::Carpet,
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

    if (type == EdgeType::Wall) {
        addSolid(bp, origin, f.box(s0, s1, 0.0f, H, HT), MaterialId::Wallpaper, f.longFaces());
        return;
    }

    if (type == EdgeType::Archway) {
        const float hw = world::kArchWidth * 0.5f;
        addSolid(bp, origin, f.box(s0, mid - hw, 0.0f, H, HT), MaterialId::Wallpaper, f.longFaces() | f.endCap());
        addSolid(bp, origin, f.box(mid + hw, s1, 0.0f, H, HT), MaterialId::Wallpaper, f.longFaces() | f.startCap());
        addSolid(bp, origin, f.box(mid - hw, mid + hw, world::kArchHeight, H, HT), MaterialId::Wallpaper,
                 f.longFaces() | mesh::FaceNegY);
        return;
    }

    // ---- Door frame -------------------------------------------------------------
    const float hw = world::kDoorOpeningWidth * 0.5f;
    const float jw = world::kDoorFrameWidth;
    const float hd = world::kDoorOpeningHeight;
    const float frameDepth = HT + world::kDoorFrameProtrude;

    // Wall segments either side and the header above (reveals hidden by the frame).
    addSolid(bp, origin, f.box(s0, mid - hw, 0.0f, H, HT), MaterialId::Wallpaper, f.longFaces());
    addSolid(bp, origin, f.box(mid + hw, s1, 0.0f, H, HT), MaterialId::Wallpaper, f.longFaces());
    addSolid(bp, origin, f.box(mid - hw, mid + hw, hd, H, HT), MaterialId::Wallpaper, f.longFaces());

    // Painted steel frame: two jambs and a head jamb.
    addSolid(bp, origin, f.box(mid - hw, mid - hw + jw, 0.0f, hd, frameDepth), MaterialId::GrayMetal,
             mesh::FaceSides);
    addSolid(bp, origin, f.box(mid + hw - jw, mid + hw, 0.0f, hd, frameDepth), MaterialId::GrayMetal,
             mesh::FaceSides);
    addSolid(bp, origin, f.box(mid - hw + jw, mid + hw - jw, hd - jw, hd, frameDepth), MaterialId::GrayMetal,
             f.longFaces() | mesh::FaceNegY);

    // Door leaf: hinge side chosen deterministically from the edge hash.
    const uint64_t id = rnd::hashCoords(levelSeed(level), gx, gz, kSaltDoor + static_cast<uint64_t>(axis));
    const bool hingeAtStart = (id & 1u) == 0u;
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
        d.material = MaterialId::Wallpaper;
        d.faces = faces;
        d.uvOrigin = origin;
        if (faces) mesh::addBox(bp.staticMesh, d);
        bp.colliders.push_back(post);
        return;
    }

    if (vertexHasPillar(level, gx, gz)) {
        const float h = world::kPillarSize * 0.5f;
        addSolid(bp, origin, AABB(p + glm::vec3(-h, 0.0f, -h), p + glm::vec3(h, H, h)), MaterialId::Wallpaper,
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
            for (const glm::vec2& lc : centres) {
                const glm::vec2 local = alongX ? glm::vec2(lc.y, lc.x) : lc;
                addFixture(bp, origin, cellMin + local, alongX, lightIndex, unrest, nullptr);
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

    for (int lz = 0; lz < kN; ++lz) {
        for (int lx = 0; lx < kN; ++lx) {
            const int gx = bp.coord.x * kN + lx;
            const int gz = bp.coord.z * kN + lz;
            if (cellRole(level, gx, gz) != CellRole::Room) continue; // stairwells stay clear
            rnd::Rng rng(rnd::hashCoords(bp.seed, lx, lz, kSaltFurniture));
            // Terminals and phones use their own streams so furniture layouts are unaffected.
            rnd::Rng terminalRng(rnd::hashCoords(bp.seed, lx, lz, kSaltTerminal));
            rnd::Rng phoneRng(rnd::hashCoords(bp.seed, lx, lz, kSaltPhone));
            int deskCount = 0;

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
            auto add = [&bp](FurnitureType t, const glm::vec3& p, float yaw) {
                bp.furniture.push_back(Furniture::makeInstance(t, p, yaw));
            };

            // Frame of a wall side: inner face midpoint, inward normal, along-wall axis.
            auto wallFrame = [&](int side, glm::vec3& face, glm::vec3& n, glm::vec3& r) {
                n = inward[side];
                r = glm::vec3(-n.z, 0.0f, n.x);
                face = cellCenter - n * (S * 0.5f - T * 0.5f);
            };

            // Occasionally a desk carries a retro computer, facing the chair.
            auto maybeTerminal = [&]() {
                if (!terminalRng.chance(world::kTerminalChance)) return false;
                const glm::mat4 model =
                    glm::translate(bp.furniture.back().model, glm::vec3(-0.2f, 0.75f, -0.04f)); // on the desk top
                const uint64_t id = rnd::hashCoords(levelSeed(level), gx, gz, kSaltTerminal + static_cast<uint64_t>(deskCount));
                bp.terminals.push_back({id, model, terminalRng.chance(0.85f)});
                return true;
            };

            // Often a desk has a telephone: beside the computer (turned towards
            // the chair), or anywhere along the desk if there is none.
            auto maybePhone = [&](bool besideTerminal) {
                if (!phoneRng.chance(world::kPhoneChance)) return;
                const float x = besideTerminal ? phoneRng.range(0.40f, 0.52f) : phoneRng.range(-0.40f, 0.50f);
                const float z = phoneRng.range(-0.14f, 0.02f);
                const float yaw = besideTerminal ? phoneRng.range(-0.45f, -0.12f) : -0.5f * x + phoneRng.range(-0.15f, 0.15f);
                const glm::mat4 model = glm::rotate(glm::translate(bp.furniture.back().model, glm::vec3(x, 0.75f, z)), yaw,
                                                    glm::vec3(0.0f, 1.0f, 0.0f));
                const uint64_t id = rnd::hashCoords(levelSeed(level), gx, gz, kSaltPhone + static_cast<uint64_t>(deskCount));
                bp.phones.push_back({id, model});
            };

            // A desk against the wall, a chair pulled up to it, optional
            // partitions and a filing cabinet: a classic cubicle.
            auto cubicle = [&](int side, bool partitions) {
                glm::vec3 face, n, r;
                wallFrame(side, face, n, r);
                const glm::vec3 base = face + r * rng.range(-0.5f, 0.5f);
                add(FurnitureType::Desk, base + n * (0.375f + 0.03f), yawFacing(n));
                maybePhone(maybeTerminal());
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
            if (!walls.empty() && roll < 0.24f) {
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
