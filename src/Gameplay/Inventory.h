#pragma once
// ---------------------------------------------------------------------------
// Inventory.h
// What the player carries: one slot per part type (so at most one of each)
// and whether the four have been put together into the gun.
//
// Picking up is an exchange of values with an ItemSite: whatever the site
// holds comes into the slot of its type, and whatever that slot held (if
// anything) goes back into the site - so a swap leaves the dropped part
// exactly where the new one was, charge and all. The gun keeps working
// through a swap: it is the parts in the slots, so a flat battery can be
// hot-swapped for a fresh one without taking the gun apart.
// ---------------------------------------------------------------------------

#include "Gameplay/Items.h"

#include <array>
#include <optional>

class Inventory {
public:
    /// What exchange() did.
    enum class Exchange : uint8_t {
        None,     ///< The site was empty.
        PickedUp, ///< The part was taken; nothing of its type was held.
        Swapped,  ///< The held part of that type was left in its place.
    };

    const std::optional<Item>& slot(PartType type) const { return m_slots[static_cast<size_t>(type)]; }
    bool has(PartType type) const { return slot(type).has_value(); }
    /// All four parts are held.
    bool complete() const;

    /// The gun is put together (all four parts held).
    bool assembled() const { return m_assembled; }
    /// Puts the gun together; false unless complete().
    bool assemble();

    /// Exchanges the contents of an item site with the slot of its part type.
    Exchange exchange(std::optional<Item>& site);

    /// Adds a part outright (developer scenes); replaces any held part of its type.
    void give(const Item& item);

    /// The battery's state of charge (0 without a battery).
    float charge() const;
    /// Drains the battery by `amount` (clamped at 0).
    void drain(float amount);

private:
    std::array<std::optional<Item>, kPartTypeCount> m_slots{};
    bool m_assembled = false;
};
