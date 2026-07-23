#pragma once

// ============================================================================
//  buck_sizing.h
//  Physics / math synthesis layer for the buck converter. Converts an
//  engineering SPEC (Vin, Vout, Iout, fsw, ripple targets) into IDEAL component
//  values and operating parameters, then writes those values into an
//  (instantiated) ir::Design by matching component ROLES.
//
//  These are the classic continuous-conduction-mode (CCM) buck equations:
//    D      = Vout / Vin
//    dIL    = ripple_ratio * Iout                     (chosen inductor ripple)
//    L      = Vout*(Vin - Vout) / (Vin * fsw * dIL)
//    IL_pk  = Iout + dIL/2
//    Cout   = dIL / (8 * fsw * dVout)                 (output ripple target)
//    Cin    = Iout * D * (1 - D) / (fsw * dVin)       (input ripple target)
//    Rload  = Vout / Iout
//
//  The output is the IDEAL design. Phase 5 replaces these values with real
//  catalogue parts and re-verifies, because a real part is never exactly ideal.
// ============================================================================

#include "electrical_ir.h"

#include <string>

namespace phys {

struct BuckSpec {
    double vin_v  = 0.0;
    double vout_v = 0.0;
    double iout_a = 0.0;

    double fsw_hz = 500e3;          // switching frequency
    double ripple_current_ratio = 0.30; // dIL / Iout (typical 0.2 - 0.4)

    // Ripple targets. If left <= 0 they default relative to the rail.
    double vout_ripple_v = 0.0;     // default: 1% of Vout
    double vin_ripple_v  = 0.0;     // default: 2% of Vin
};

struct SizingResult {
    bool        ok = false;
    std::string error;

    // Operating point / derived quantities (useful for Phase 5 verification).
    double duty      = 0.0;
    double delta_il  = 0.0;         // inductor ripple current (A)
    double il_peak   = 0.0;         // peak inductor current (A)

    // Ideal component values.
    double l_henries  = 0.0;
    double cout_farads = 0.0;
    double cin_farads  = 0.0;
    double rload_ohms  = 0.0;
};

// Computes ideal values and, if `design` is non-null, writes them into the
// matching components by role. Returns the full sizing result.
inline SizingResult SizeBuck(const BuckSpec& spec, ir::Design* design = nullptr) {
    SizingResult r;

    // --- Spec sanity (a buck must step DOWN) ----------------------------
    if (spec.vin_v <= 0.0 || spec.vout_v <= 0.0 || spec.iout_a <= 0.0) {
        r.error = "Vin, Vout, Iout must all be positive";
        return r;
    }
    if (spec.vout_v >= spec.vin_v) {
        r.error = "Buck requires Vout < Vin (step-down)";
        return r;
    }
    if (spec.fsw_hz <= 0.0 || spec.ripple_current_ratio <= 0.0) {
        r.error = "fsw and ripple_current_ratio must be positive";
        return r;
    }

    const double dVout = (spec.vout_ripple_v > 0.0) ? spec.vout_ripple_v
                                                    : 0.01 * spec.vout_v;
    const double dVin  = (spec.vin_ripple_v  > 0.0) ? spec.vin_ripple_v
                                                    : 0.02 * spec.vin_v;

    // --- Core equations -------------------------------------------------
    r.duty     = spec.vout_v / spec.vin_v;
    r.delta_il = spec.ripple_current_ratio * spec.iout_a;
    r.il_peak  = spec.iout_a + r.delta_il / 2.0;

    r.l_henries = (spec.vout_v * (spec.vin_v - spec.vout_v)) /
                  (spec.vin_v * spec.fsw_hz * r.delta_il);

    r.cout_farads = r.delta_il / (8.0 * spec.fsw_hz * dVout);

    r.cin_farads = (spec.iout_a * r.duty * (1.0 - r.duty)) /
                   (spec.fsw_hz * dVin);

    r.rload_ohms = spec.vout_v / spec.iout_a;

    r.ok = true;

    // --- Write ideal values into the design by role ---------------------
    if (design) {
        for (auto& c : design->components) {
            if      (c.role == "OUTPUT_INDUCTOR") c.value = r.l_henries;
            else if (c.role == "OUTPUT_CAP")      c.value = r.cout_farads;
            else if (c.role == "INPUT_CAP")       c.value = r.cin_farads;
            else if (c.role == "LOAD")            c.value = r.rload_ohms;
            // HS_SWITCH / FREEWHEEL_DIODE are sized by ratings, not a scalar
            // value; those checks arrive with part binding in Phase 5.
        }
    }

    return r;
}

} // namespace phys
