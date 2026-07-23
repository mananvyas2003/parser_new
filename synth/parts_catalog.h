#pragma once

// ============================================================================
//  parts_catalog.h
//  A parts catalog with RATINGS, plus a ranked-candidate finder. This mirrors
//  the DB's Parts table and DB_FindClosestPart semantics, but:
//    (a) parts carry ratings/parasitics (voltage, current, ESR, power), and
//    (b) the finder returns N RANKED candidates instead of LIMIT 1,
//  which is exactly what the verify/retry loop (part_binding.h) needs.
//
//  For now this is an in-memory mock so Phase 5 is fully testable. Phase 6 can
//  replace FindCandidates() with a real SQLite query returning the same shape.
// ============================================================================

#include "electrical_ir.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace cat {

struct CatalogPart {
    std::string  mpn;
    ir::PartType type = ir::PartType::Unknown;
    double       value = 0.0;        // F / H / Ohm (0 for switches/diodes)
    std::string  package;

    // Ratings / parasitics (0 = not applicable / unspecified).
    double v_rating       = 0.0;     // max voltage (Vds, Vreverse, cap voltage)
    double i_rating       = 0.0;     // max current (Isat, If, Id)
    double esr_ohms       = 0.0;     // capacitor ESR
    double power_rating_w = 0.0;     // resistor power
};

// Query describing what we're searching for. has_value_target=false is used for
// switches/diodes which are selected by ratings, not by a scalar value.
struct CandidateQuery {
    ir::PartType type = ir::PartType::Unknown;
    bool         has_value_target = false;
    double       value_target = 0.0;
    double       value_tol = 0.30;   // relative window, e.g. 0.30 = +/-30%
};

// Returns matching parts, RANKED best-first:
//   1) closest value (when a value target is given),
//   2) then smallest adequate current rating (cheapest-first heuristic),
//   3) then smallest voltage rating.
inline std::vector<CatalogPart> FindCandidates(const std::vector<CatalogPart>& catalog,
                                               const CandidateQuery& q) {
    std::vector<CatalogPart> out;

    for (const auto& p : catalog) {
        if (p.type != q.type) continue;
        if (q.has_value_target) {
            const double lo = q.value_target * (1.0 - q.value_tol);
            const double hi = q.value_target * (1.0 + q.value_tol);
            if (p.value < lo || p.value > hi) continue;
        }
        out.push_back(p);
    }

    std::stable_sort(out.begin(), out.end(),
        [&](const CatalogPart& a, const CatalogPart& b) {
            if (q.has_value_target) {
                const double da = std::fabs(a.value - q.value_target);
                const double db = std::fabs(b.value - q.value_target);
                if (da != db) return da < db;
            }
            if (a.i_rating != b.i_rating) return a.i_rating < b.i_rating;
            return a.v_rating < b.v_rating;
        });

    return out;
}

// A small mock catalog with deliberate decoys so the verify/retry loop has to
// reject the nearest/cheapest candidate and fall through to a valid one.
inline std::vector<CatalogPart> BuildMockCatalog() {
    using PT = ir::PartType;
    return {
        // --- Inductors (target ~6.48uH; 3A decoy fails Isat, 5A passes) ---
        {"IND-6R8-3A0", PT::Inductor, 6.8e-6, "1210", 0, 3.0, 0, 0},
        {"IND-6R8-5A0", PT::Inductor, 6.8e-6, "1210", 0, 5.0, 0, 0},
        {"IND-4R7-6A0", PT::Inductor, 4.7e-6, "1210", 0, 6.0, 0, 0},

        // --- Capacitors (input ~6.08uF/>=15V ; output ~4.5uF/>=10V) -------
        {"CAP-4U7-6V3", PT::Capacitor, 4.7e-6, "0805", 6.3,  0, 0.005, 0}, // out decoy
        {"CAP-4U7-25V", PT::Capacitor, 4.7e-6, "0805", 25.0, 0, 0.005, 0},
        {"CAP-6U8-10V", PT::Capacitor, 6.8e-6, "0805", 10.0, 0, 0.004, 0}, // in decoy
        {"CAP-6U8-16V", PT::Capacitor, 6.8e-6, "0805", 16.0, 0, 0.004, 0},
        {"CAP-10U-25V", PT::Capacitor, 10.0e-6, "1206", 25.0, 0, 0.003, 0}, // out of window
        {"CAP-2U2-25V", PT::Capacitor, 2.2e-6,  "0603", 25.0, 0, 0.005, 0}, // for higher-fsw specs
        {"CAP-0U47-50V",PT::Capacitor, 0.47e-6, "0402", 50.0, 0, 0.010, 0}, // small input cap

        // --- MOSFETs (no value; 20V/4A decoy fails, 30V/8A passes) --------
        {"MOS-20V-4A", PT::Mosfet, 0, "SOT-23",  20.0, 4.0, 0, 0},
        {"MOS-30V-8A", PT::Mosfet, 0, "PowerPAK",30.0, 8.0, 0, 0},
        {"MOS-60V-10A",PT::Mosfet, 0, "DPAK",    60.0, 10.0, 0, 0}, // for higher-Vin specs

        // --- Diodes (no value; 20V/2A decoy fails, 40V/5A passes) ---------
        {"DIO-20V-2A", PT::Diode, 0, "SOD-123", 20.0, 2.0, 0, 0},
        {"DIO-40V-5A", PT::Diode, 0, "SMA",     40.0, 5.0, 0, 0},

        // --- Buck controller IC (for the controller topology variant) -----
        {"BUCK-CTRL-1", PT::IC, 0, "SOIC-8", 40.0, 0, 0, 0},
    };
}

} // namespace cat
