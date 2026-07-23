// ============================================================================
//  buck_sizing_test.cpp
//  Phase 4 test: size a "12 V -> 5 V buck at 3 A" from the instantiated
//  topology, verify the computed ideal values against hand-calculated numbers,
//  confirm they were written into the IR by role, and reject an invalid spec.
//
//  Build:
//    g++ -std=c++17 -I synth synth/tests/buck_sizing_test.cpp -o build/buck_sizing_test
//  Run:
//    ./build/buck_sizing_test
// ============================================================================

#include "electrical_ir.h"
#include "ir_dump.h"
#include "topology_library.h"
#include "topology_instantiate.h"
#include "buck_sizing.h"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace ir;

static bool Close(double a, double b, double rel = 1e-3) {
    const double denom = std::max(1.0, std::fabs(b));
    return std::fabs(a - b) / denom <= rel;
}

static double ValueOfRole(const Design& d, const std::string& role) {
    for (const auto& c : d.components) if (c.role == role) return c.value;
    return -1.0;
}

int main() {
    // Instantiate the buck topology, then size it from the spec.
    Design d = topo::Instantiate(topo::BuildBuckTopology());

    phys::BuckSpec spec;
    spec.vin_v  = 12.0;
    spec.vout_v = 5.0;
    spec.iout_a = 3.0;
    spec.fsw_hz = 500e3;
    spec.ripple_current_ratio = 0.30;

    phys::SizingResult r = phys::SizeBuck(spec, &d);

    std::cout << "SIZING RESULT (12V -> 5V @ 3A, 500kHz, 30% ripple)\n";
    std::cout << "  duty     = " << r.duty << "\n";
    std::cout << "  dIL      = " << r.delta_il << " A\n";
    std::cout << "  IL_peak  = " << r.il_peak << " A\n";
    std::cout << "  L        = " << r.l_henries * 1e6 << " uH\n";
    std::cout << "  Cout     = " << r.cout_farads * 1e6 << " uF\n";
    std::cout << "  Cin      = " << r.cin_farads * 1e6 << " uF\n";
    std::cout << "  Rload    = " << r.rload_ohms << " Ohm\n\n";

    assert(r.ok && "sizing should succeed for a valid step-down spec");

    // ---- Hand-calculated expected values -------------------------------
    // D = 5/12 = 0.416667
    // dIL = 0.3*3 = 0.9 A ; IL_peak = 3.45 A
    // L = 5*(12-5)/(12*500k*0.9) = 6.4815 uH
    // Cout = 0.9/(8*500k*0.05) = 4.5 uF          (dVout = 1% of 5V = 0.05V)
    // Cin  = 3*0.416667*0.583333/(500k*0.24) = 6.076 uF  (dVin = 2% of 12V = 0.24V)
    // Rload = 5/3 = 1.6667 Ohm
    assert(Close(r.duty, 0.416667));
    assert(Close(r.delta_il, 0.9));
    assert(Close(r.il_peak, 3.45));
    assert(Close(r.l_henries, 6.4815e-6));
    assert(Close(r.cout_farads, 4.5e-6));
    assert(Close(r.cin_farads, 6.0764e-6));
    assert(Close(r.rload_ohms, 1.66667));

    // ---- Values were written into the IR by role -----------------------
    assert(Close(ValueOfRole(d, "OUTPUT_INDUCTOR"), r.l_henries));
    assert(Close(ValueOfRole(d, "OUTPUT_CAP"),      r.cout_farads));
    assert(Close(ValueOfRole(d, "INPUT_CAP"),       r.cin_farads));
    assert(Close(ValueOfRole(d, "LOAD"),            r.rload_ohms));
    // Switch/diode remain scalar-valueless for now.
    assert(ValueOfRole(d, "HS_SWITCH") == 0.0);

    std::cout << "IR after sizing:\n";
    Dump(d);

    // ---- Invalid spec is rejected (not a step-down) --------------------
    phys::BuckSpec bad = spec;
    bad.vout_v = 15.0; // Vout > Vin
    phys::SizingResult br = phys::SizeBuck(bad, nullptr);
    assert(!br.ok && "should reject Vout >= Vin");
    std::cout << "\nRejected invalid spec as expected: " << br.error << "\n";

    std::cout << "\n[PASS] Phase 4: buck sizing test passed.\n";
    return 0;
}
