// ============================================================================
//  ir_graph_erc_test.cpp
//  Phase 6 test: take the verified, bound buck IR and feed it into the EXISTING
//  parser_new graph, recover nets via the existing GraphSearch::ExtractNets,
//  and run ERC on the IR. Proves the synthesized design flows into the same
//  circuit-analysis machinery used for parsed schematics.
//
//  Build:
//    g++ -std=c++17 -I synth -I . synth/tests/ir_graph_erc_test.cpp "graph..cpp" graph_search.cpp -o build/ir_graph_erc_test
//  Run:
//    ./build/ir_graph_erc_test
// ============================================================================

#include "electrical_ir.h"
#include "topology_library.h"
#include "topology_instantiate.h"
#include "buck_sizing.h"
#include "parts_catalog.h"
#include "part_binding.h"

#include "ir_graph_bridge.h"
#include "ir_erc.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace ir;

static bool WarningsContain(const erc::ErcReport& r, const std::string& sub) {
    for (const auto& w : r.warnings) if (w.find(sub) != std::string::npos) return true;
    return false;
}

int main() {
    // ---- Rebuild the full verified design (Phases 3-5) -----------------
    Design d = topo::Instantiate(topo::BuildBuckTopology());
    phys::BuckSpec spec;
    spec.vin_v = 12.0; spec.vout_v = 5.0; spec.iout_a = 3.0;
    spec.fsw_hz = 500e3; spec.ripple_current_ratio = 0.30;
    phys::SizeBuck(spec, &d);
    auto req = partbind::ComputeBuckRequirements(spec, phys::SizeBuck(spec, nullptr));
    partbind::BindDesign(d, req, cat::BuildMockCatalog());

    // ---- Bridge into the EXISTING graph + recover nets -----------------
    Graph g = bridge::BuildGraphFromIR(d);
    auto graph_nets = GraphSearch::ExtractNets(g);

    std::cout << "==================================================\n";
    std::cout << " IR -> EXISTING GRAPH BRIDGE\n";
    std::cout << "==================================================\n";
    std::cout << "Vertices : " << g.GetVertices().size() << "\n";
    std::cout << "Edges    : " << g.GetEdges().size() << "\n";
    std::cout << "Nets     : " << graph_nets.size() << "\n\n";

    // 5 nodes + 13 pins = 18 vertices; 13 pin-connection edges.
    // (Q1:3, D1:2, L1:2, C1:2, C2:2, R1:2 = 13 pins)
    assert(g.GetVertices().size() == 18);
    assert(g.GetEdges().size() == 13);
    assert(graph_nets.size() == 5 && "expected VIN, SW, VOUT, GND, DRIVE");

    // ---- Graph-recovered nets must equal the IR nets exactly -----------
    auto recovered = bridge::RecoverNetSignature(g);
    auto truth     = bridge::IrNetSignatureByRef(d);

    for (const auto& kv : truth) {
        std::cout << "  " << kv.first << " : ";
        for (const auto& p : kv.second) std::cout << p << " ";
        std::cout << "\n";
    }

    assert(recovered == truth &&
           "Existing GraphSearch must recover exactly the IR nets");

    // ---- Run ERC on the verified IR ------------------------------------
    erc::ErcReport ercr = erc::RunErc(d);

    std::cout << "\n--------------------------------------------------\n";
    std::cout << " ERC : " << (ercr.ok() ? "OK" : "FAILED") << "\n";
    std::cout << "--------------------------------------------------\n";
    for (const auto& e : ercr.errors)   std::cout << "  ERROR   : " << e << "\n";
    for (const auto& w : ercr.warnings) std::cout << "  WARNING : " << w << "\n";

    // No hard errors: every pin is connected and GND exists.
    assert(ercr.ok() && "bound buck should have no ERC errors");
    // DRIVE has only Q1.G on it (no controller yet) -> floating warning.
    assert(WarningsContain(ercr, "DRIVE") && "DRIVE should warn as floating");

    std::cout << "\n[PASS] Phase 6: IR -> graph/ERC bridge test passed.\n";
    return 0;
}
