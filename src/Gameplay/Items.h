#pragma once
// ---------------------------------------------------------------------------
// Items.h
// The scavengeable parts of the makeshift Tesla coil gun, and the places in
// the world where they rest.
//
// An Item is a plain value: its identity (derived from where it was first
// generated), its type and - for batteries - its state of charge. Items are
// never referenced, only moved: between an ItemSite in a chunk and a slot of
// the player's Inventory. Swapping a held part for one found in the world is
// therefore an exchange of two values, and a depleted battery put down
// somewhere carries its charge with it wherever it is stored.
//
// ItemSites are generated with their chunk (a desk top, a chair seat, a
// drawer of a filing cabinet), each with a deterministic id. The generator
// decides what a site holds at first; once the player has changed it, the
// ChunkManager remembers the site's contents across unload / reload, exactly
// as it remembers door and phone states.
// ---------------------------------------------------------------------------

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>

/// The four parts of the gun.
enum class PartType : uint8_t {
    Battery = 0,   ///< Lithium battery pack: stores the charge the discharges drain.
    Driver,        ///< Driver electronics box with the primary coil on its front.
    Coil,          ///< Tall, finely wound coil.
    TopLoad,       ///< Aluminium toroid with the breakout spike.
    Count
};

inline constexpr int kPartTypeCount = static_cast<int>(PartType::Count);

/// One part.
struct Item {
    uint64_t id = 0;                   ///< Deterministic identity (hash of the site it was generated in).
    PartType type = PartType::Battery;
    float    charge = 1.0f;            ///< Battery state of charge, 0..1 (other parts: unused).
};

/// Where an item site is.
enum class SiteKind : uint8_t { Desk, Chair, Drawer };

/// A place an item can rest in.
struct ItemSite {
    uint64_t            id = 0;
    SiteKind            kind = SiteKind::Desk;
    glm::mat4           local{1.0f};  ///< Item -> world (desks, chairs) or -> drawer space (drawers).
    int                 cabinet = -1; ///< Drawers: index of the chunk's cabinet...
    int                 drawer = -1;  ///< ...and which drawer.
    std::optional<Item> item;         ///< What rests here now.
    bool                modified = false; ///< Changed by the player: remembered across chunk reloads.
    glm::mat4           world{1.0f};  ///< Current item -> world transform (drawers slide).
};

/// Upper-case display name ("BATTERY PACK", ...).
inline const char* partName(PartType type) {
    switch (type) {
    case PartType::Battery:       return "BATTERY PACK";
    case PartType::Driver:        return "DRIVER BOX";
    case PartType::Coil:          return "COIL";
    case PartType::TopLoad:       return "TOP LOAD";
    default:                      return "?";
    }
}

/// Lit segments of a battery's four-LED charge gauge (0 = flat).
inline int batteryBars(float charge) {
    if (charge <= 0.0f) return 0;
    if (charge <= 0.25f) return 1;
    if (charge <= 0.5f) return 2;
    if (charge <= 0.75f) return 3;
    return 4;
}
