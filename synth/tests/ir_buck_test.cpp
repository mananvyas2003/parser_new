// ============================================================================
//  ir_buck_test.cpp
//  Phase 1 test: hand-build the buck-converter power stage as an Electrical IR,
//  validate structural invariants, and dump it. This proves the IR can express
//  the exact example from the design brief:
//      Q1.G -> DRIVE, Q1.D -> VIN, Q1.S -> SW, L1.1 -> SW, L1.2 -> VOUT, ...
//
//  Build:
//    g++ -std=c++17 -I synth synth/tests/ir_buck_test.cpp -o build/ir_buck_test
//  Run:
//    ./build/ir_buck_test
// ============================================================================

#include "electrical_ir.h"
#include "ir_validate.h"
#include "ir_dump.h"

#include <cassert>
#include <iostream>

using namespace ir;

// Convenience: add a component with pins wired to named nodes.
static Component MakeComponent(Design& d,
                               const std::string& ref,
                               const std::string& role,
                               PartType type,
                               std::initializer_list<
                                   std::pair<const char*, const char*>> pin_nodes) {
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

static Design BuildBuckPowerStage() {
    Design d;
    d.topology = "buck_converter";

    // Non-synchronous buck power stage (matches the brief's example wiring).
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

int main() {
    Design d = BuildBuckPowerStage();

    Dump(d);
    ValidationReport report = Validate(d);
    DumpReport(report);

    // ---- Automated assertions (the actual "test") ----------------------
    // Structure must be valid (errors are fatal; unbound warnings are expected).
    assert(report.ok() && "Buck IR must be structurally valid");

    // Exactly the 5 electrical nodes we expect.
    assert(d.nodes.size() == 5 && "Expected VIN, SW, VOUT, GND, DRIVE");
    assert(d.FindNode("VIN")  != INVALID_NODE);
    assert(d.FindNode("SW")   != INVALID_NODE);
    assert(d.FindNode("VOUT") != INVALID_NODE);
    assert(d.FindNode("GND")  != INVALID_NODE);
    assert(d.FindNode("DRIVE")!= INVALID_NODE);

    // Spot-check the brief's exact connections.
    Component* q1 = d.FindComponent("Q1");
    assert(q1 && q1->pins.size() == 3);
    assert(q1->pins[0].node == d.FindNode("VIN"));   // Q1.D -> VIN
    assert(q1->pins[1].node == d.FindNode("SW"));    // Q1.S -> SW
    assert(q1->pins[2].node == d.FindNode("DRIVE")); // Q1.G -> DRIVE

    Component* l1 = d.FindComponent("L1");
    assert(l1 && l1->pins[0].node == d.FindNode("SW"));   // L1.1 -> SW
    assert(l1->pins[1].node == d.FindNode("VOUT"));       // L1.2 -> VOUT

    // Every component is currently unbound (Phase 5 will bind MPNs).
    for (const auto& c : d.components) assert(!c.IsBound());

    std::cout << "\n[PASS] Phase 1: Electrical IR buck test passed.\n";
    return 0;
}
