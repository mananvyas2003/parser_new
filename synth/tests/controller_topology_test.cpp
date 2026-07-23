// ============================================================================
//  controller_topology_test.cpp
//  Task 3 test: the controller variant adds a gate-driver IC on the DRIVE net,
//  so DRIVE is no longer floating. Verifies the controller binds, DRIVE now has
//  two terminals, and ERC no longer warns about DRIVE.
//
//  Build:
//    g++ -std=c++17 -I synth synth/tests/controller_topology_test.cpp -o build/controller_topology_test
//  Run:
//    ./build/controller_topology_test
// ============================================================================

#include "electrical_ir.h"
#include "ir_dump.h"
#include "topology_library.h"
#include "topology_instantiate.h"
#include "buck_sizing.h"
#include "parts_catalog.h"
#include "part_binding.h"
#include "ir_erc.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace ir;

static int TerminalsOnNet(const Design& d, const std::string& net) {
    NodeId id = d.FindNode(net);
    if (id == INVALID_NODE) return -1;
    int n = 0;
    for (const auto& c : d.components)
        for (const auto& p : c.pins)
            if (p.node == id) ++n;
    return n;
}

static bool ErcMentions(const erc::ErcReport& r, const std::string& sub) {
    for (const auto& w : r.warnings) if (w.find(sub) != std::string::npos) return true;
    for (const auto& e : r.errors)   if (e.find(sub) != std::string::npos) return true;
    return false;
}

int main() {
    Design d = topo::Instantiate(topo::BuildBuckWithControllerTopology());

    // 7 components now (power stage 6 + controller), still 5 nodes.
    assert(d.components.size() == 7);
    assert(d.nodes.size() == 5);

    phys::BuckSpec spec;
    spec.vin_v = 12.0; spec.vout_v = 5.0; spec.iout_a = 3.0;
    spec.fsw_hz = 500e3; spec.ripple_current_ratio = 0.30;
    phys::SizeBuck(spec, &d);
    auto req = partbind::ComputeBuckRequirements(spec, phys::SizeBuck(spec, nullptr));
    partbind::BindReport report = partbind::BindDesign(d, req, cat::BuildMockCatalog());

    assert(report.all_bound && "controller design should fully bind");

    // Controller (U1) bound to the IC.
    Component* u1 = d.FindComponent("U1");
    assert(u1 && u1->IsBound() && u1->mpn == "BUCK-CTRL-1");

    // DRIVE now has 2 terminals: Q1.G and U1.GATE.
    assert(TerminalsOnNet(d, "DRIVE") == 2);

    // ERC: no floating DRIVE anymore.
    erc::ErcReport ercr = erc::RunErc(d);
    std::cout << "ERC : " << (ercr.ok() ? "OK" : "FAILED") << "\n";
    for (const auto& w : ercr.warnings) std::cout << "  WARNING: " << w << "\n";

    assert(ercr.ok());
    assert(!ErcMentions(ercr, "DRIVE") && "DRIVE should no longer be flagged");

    std::cout << "\nControlled buck design:\n";
    Dump(d);

    std::cout << "\n[PASS] Task 3: controller topology test passed.\n";
    return 0;
}
