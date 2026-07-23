#pragma once

// ============================================================================
//  topology_instantiate.h
//  Turns an abstract topology TEMPLATE into a concrete but UNBOUND ir::Design:
//    - declares all electrical nodes
//    - creates one ir::Component per template component (x quantity)
//    - assigns reference designators (R1, C1, L1, D1, Q1, U1, ...)
//    - wires each pin to its node
//
//  The result has real structure and connectivity but no MPNs/values yet;
//  those are filled by the sizing (Phase 4) and binding (Phase 5) layers.
// ============================================================================

#include "electrical_ir.h"
#include "topology.h"

#include <string>
#include <unordered_map>

namespace topo {

inline std::string DesignatorPrefix(ir::PartType t) {
    switch (t) {
        case ir::PartType::Resistor:  return "R";
        case ir::PartType::Capacitor: return "C";
        case ir::PartType::Inductor:  return "L";
        case ir::PartType::Diode:     return "D";
        case ir::PartType::Mosfet:    return "Q";
        case ir::PartType::IC:        return "U";
        default:                      return "X";
    }
}

inline ir::Design Instantiate(const Topology& t) {
    ir::Design d;
    d.topology = t.name;

    // 1. Declare all nodes up-front so even 0-degree nodes exist as declared.
    for (const auto& n : t.nodes) {
        d.AddNode(n);
    }

    // 2. Reference designator counters, per type prefix.
    std::unordered_map<std::string, int> counters;

    for (const auto& tc : t.components) {
        const int qty = tc.quantity < 1 ? 1 : tc.quantity;

        for (int i = 0; i < qty; ++i) {
            ir::Component c;
            c.role = tc.role_name;
            c.type = tc.part_type;

            const std::string prefix = DesignatorPrefix(tc.part_type);
            c.ref = prefix + std::to_string(++counters[prefix]);

            for (const auto& conn : tc.connections) {
                ir::Pin p;
                p.name = conn.first;               // symbolic pin name
                p.node = d.AddNode(conn.second);   // resolve/create node
                c.pins.push_back(p);
            }

            d.components.push_back(std::move(c));
        }
    }

    return d;
}

} // namespace topo
