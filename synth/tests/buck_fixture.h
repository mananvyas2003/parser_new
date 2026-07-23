#pragma once

// ============================================================================
//  buck_fixture.h
//  Shared test fixture: the hand-built non-synchronous buck power stage used
//  across phase tests. Matches the design brief's example wiring:
//      Q1.D->VIN, Q1.S->SW, Q1.G->DRIVE, L1.1->SW, L1.2->VOUT, ...
// ============================================================================

#include "electrical_ir.h"

#include <initializer_list>
#include <utility>

namespace ir {

inline Component MakeComponent(
    Design& d,
    const std::string& ref,
    const std::string& role,
    PartType type,
    std::initializer_list<std::pair<const char*, const char*>> pin_nodes) {
    Component c;
    c.ref = ref;
    c.role = role;
    c.type = type;
    for (const auto& pn : pin_nodes) {
        Pin p;
        p.name = pn.first;
        p.node = d.AddNode(pn.second);
        c.pins.push_back(p);
    }
    return c;
}

inline Design BuildBuckPowerStage() {
    Design d;
    d.topology = "buck_converter";

    d.components.push_back(MakeComponent(d, "Q1", "HS_SWITCH", PartType::Mosfet,
                                         {{"D", "VIN"}, {"S", "SW"}, {"G", "DRIVE"}}));
    d.components.push_back(MakeComponent(d, "D1", "FREEWHEEL_DIODE", PartType::Diode,
                                         {{"K", "SW"}, {"A", "GND"}}));
    d.components.push_back(MakeComponent(d, "L1", "OUTPUT_INDUCTOR", PartType::Inductor,
                                         {{"1", "SW"}, {"2", "VOUT"}}));
    d.components.push_back(MakeComponent(d, "Cin", "INPUT_CAP", PartType::Capacitor,
                                         {{"1", "VIN"}, {"2", "GND"}}));
    d.components.push_back(MakeComponent(d, "Cout", "OUTPUT_CAP", PartType::Capacitor,
                                         {{"1", "VOUT"}, {"2", "GND"}}));
    d.components.push_back(MakeComponent(d, "Rload", "LOAD", PartType::Resistor,
                                         {{"1", "VOUT"}, {"2", "GND"}}));
    return d;
}

} // namespace ir
