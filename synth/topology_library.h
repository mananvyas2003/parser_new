#pragma once

// ============================================================================
//  topology_library.h
//  Code-defined topology templates. For now these are hard-coded here; in the
//  final system they are rows in the DB's Topologies/TopologyComponents/
//  TopologyNodes/TopologyConnections tables. This file is the single place that
//  encodes "what a buck converter IS" as reusable engineering knowledge.
// ============================================================================

#include "topology.h"

namespace topo {

// Non-synchronous buck converter power stage.
//   Nodes : VIN, SW, VOUT, GND, DRIVE
//   Q(HS switch) D->VIN S->SW G->DRIVE
//   D(freewheel) K->SW  A->GND
//   L(output)    1->SW  2->VOUT
//   C(input)     1->VIN 2->GND
//   C(output)    1->VOUT 2->GND
//   R(load)      1->VOUT 2->GND
inline Topology BuildBuckTopology() {
    Topology t;
    t.name        = "buck_converter";
    t.description = "Non-synchronous buck (step-down) converter power stage";
    t.category    = "dc-dc";

    t.nodes = {"VIN", "SW", "VOUT", "GND", "DRIVE"};

    t.components = {
        {"HS_SWITCH",       ir::PartType::Mosfet,    1, {{"D", "VIN"}, {"S", "SW"}, {"G", "DRIVE"}}},
        {"FREEWHEEL_DIODE", ir::PartType::Diode,     1, {{"K", "SW"}, {"A", "GND"}}},
        {"OUTPUT_INDUCTOR", ir::PartType::Inductor,  1, {{"1", "SW"}, {"2", "VOUT"}}},
        {"INPUT_CAP",       ir::PartType::Capacitor, 1, {{"1", "VIN"}, {"2", "GND"}}},
        {"OUTPUT_CAP",      ir::PartType::Capacitor, 1, {{"1", "VOUT"}, {"2", "GND"}}},
        {"LOAD",            ir::PartType::Resistor,  1, {{"1", "VOUT"}, {"2", "GND"}}},
    };

    return t;
}

// Buck converter with an integrated controller/gate-driver IC. The controller
// drives the DRIVE net (so it is no longer floating), senses the output via FB,
// and shares VIN/SW/GND with the power stage. Node set is unchanged.
inline Topology BuildBuckWithControllerTopology() {
    Topology t = BuildBuckTopology();
    t.name        = "buck_converter_controlled";
    t.description = "Buck converter with controller / gate-driver IC";

    // Controller pins map onto existing nodes (FB senses VOUT).
    t.components.push_back(
        {"CONTROLLER", ir::PartType::IC, 1,
         {{"VIN", "VIN"}, {"GND", "GND"}, {"SW", "SW"}, {"FB", "VOUT"}, {"GATE", "DRIVE"}}});

    return t;
}

} // namespace topo
