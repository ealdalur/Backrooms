// ---------------------------------------------------------------------------
// Inventory.cpp
// ---------------------------------------------------------------------------
#include "Gameplay/Inventory.h"

#include <algorithm>
#include <utility>

bool Inventory::complete() const {
    return std::all_of(m_slots.begin(), m_slots.end(), [](const std::optional<Item>& s) { return s.has_value(); });
}

bool Inventory::assemble() {
    if (!complete()) return false;
    m_assembled = true;
    return true;
}

Inventory::Exchange Inventory::exchange(std::optional<Item>& site) {
    if (!site) return Exchange::None;
    std::optional<Item>& held = m_slots[static_cast<size_t>(site->type)];
    const bool swap = held.has_value();
    std::swap(held, site);
    return swap ? Exchange::Swapped : Exchange::PickedUp;
}

void Inventory::give(const Item& item) { m_slots[static_cast<size_t>(item.type)] = item; }

float Inventory::charge() const {
    const std::optional<Item>& battery = slot(PartType::Battery);
    return battery ? battery->charge : 0.0f;
}

void Inventory::drain(float amount) {
    std::optional<Item>& battery = m_slots[static_cast<size_t>(PartType::Battery)];
    if (battery) battery->charge = std::max(0.0f, battery->charge - amount);
}
