// ============================================================================
//  part_binding_test.cpp
//  Phase 5 test: full forward slice for "12 V -> 5 V buck @ 3 A":
//    topology -> instantiate -> size -> bind real parts (verify/retry).
//  Verifies that the nearest/cheapest candidate is REJECTED when it fails a
//  rating, and the loop falls through to a valid part -- for every role.
//
//  Build:
//    g++ -std=c++17 -I synth synth/tests/part_binding_test.cpp -o build/part_binding_test
//  Run:
//    ./build/part_binding_test
// ============================================================================

#include "electrical_ir.h"
#include "ir_validate.h"
#include "ir_dump.h"
#include "topology_library.h"
#include "topology_instantiate.h"
#include "buck_sizing.h"
#include "parts_catalog.h"
#include "part_binding.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace ir;

static const partbind::BindOutcome* OutcomeForRole(const partbind::BindReport& r,
                                               const std::string& role) {
    for (const auto& o : r.outcomes) if (o.role == role) return &o;
    return nullptr;
}

static bool RejectedContains(const partbind::BindOutcome& o, const std::string& mpn) {
    for (const auto& rc : o.rejected) if (rc.mpn == mpn) return true;
    return false;
}

int main() {
    // ---- Full forward slice --------------------------------------------
    Design d = topo::Instantiate(topo::BuildBuckTopology());

    phys::BuckSpec spec;
    spec.vin_v = 12.0; spec.vout_v = 5.0; spec.iout_a = 3.0;
    spec.fsw_hz = 500e3; spec.ripple_current_ratio = 0.30;

    phys::SizingResult sizing = phys::SizeBuck(spec, &d);
    assert(sizing.ok);

    auto requirements = partbind::ComputeBuckRequirements(spec, sizing);
    auto catalog = cat::BuildMockCatalog();
    partbind::BindReport report = partbind::BindDesign(d, requirements, catalog);

    // ---- Print the binding trace ---------------------------------------
    std::cout << "==================================================\n";
    std::cout << " PART BINDING TRACE\n";
    std::cout << "==================================================\n";
    for (const auto& o : report.outcomes) {
        std::cout << o.ref << " (" << o.role << "): ";
        if (o.skipped)       std::cout << "SKIPPED (virtual)\n";
        else if (o.bound)    std::cout << "BOUND -> " << o.chosen_mpn << "\n";
        else                 std::cout << "FAILED (" << o.failure << ")\n";
        for (const auto& rc : o.rejected) {
            std::cout << "      rejected " << rc.mpn << " : " << rc.reason << "\n";
        }
    }

    // ---- All real (non-virtual) components bound -----------------------
    assert(report.all_bound && "every required component must bind");

    // ---- Reject-and-retry happened for each role -----------------------
    const auto* L = OutcomeForRole(report, "OUTPUT_INDUCTOR");
    assert(L && L->bound && L->chosen_mpn == "IND-6R8-5A0");
    assert(RejectedContains(*L, "IND-6R8-3A0")); // 3A fails Isat, retried

    const auto* Cout = OutcomeForRole(report, "OUTPUT_CAP");
    assert(Cout && Cout->chosen_mpn == "CAP-4U7-25V");
    assert(RejectedContains(*Cout, "CAP-4U7-6V3")); // 6.3V fails, retried

    const auto* Cin = OutcomeForRole(report, "INPUT_CAP");
    assert(Cin && Cin->chosen_mpn == "CAP-6U8-16V");
    assert(RejectedContains(*Cin, "CAP-6U8-10V")); // 10V fails, retried

    const auto* Q = OutcomeForRole(report, "HS_SWITCH");
    assert(Q && Q->chosen_mpn == "MOS-30V-8A");
    assert(RejectedContains(*Q, "MOS-20V-4A")); // 4A fails Id, retried

    const auto* Dio = OutcomeForRole(report, "FREEWHEEL_DIODE");
    assert(Dio && Dio->chosen_mpn == "DIO-40V-5A");
    assert(RejectedContains(*Dio, "DIO-20V-2A")); // 2A fails If, retried

    const auto* Load = OutcomeForRole(report, "LOAD");
    assert(Load && Load->skipped); // virtual load, not a board part

    // ---- IR now carries real MPNs + real (not ideal) values ------------
    Component* l1 = d.FindComponent("L1");
    assert(l1 && l1->IsBound() && l1->mpn == "IND-6R8-5A0");
    assert(l1->value == 6.8e-6);          // real value replaced ideal 6.48uH
    assert(l1->package == "1210");

    // ---- Design still structurally valid; only the load is unbound -----
    ValidationReport vr = Validate(d);
    assert(vr.ok());

    std::cout << "\nFINAL BOUND DESIGN:\n";
    Dump(d);

    std::cout << "\n[PASS] Phase 5: part binding + verify/retry test passed.\n";
    return 0;
}
