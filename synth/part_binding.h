#pragma once

// ============================================================================
//  part_binding.h
//  Real-part binding with a verify/retry loop -- the core of the synthesis
//  vision. For each role in the (sized) design we:
//    1. derive a PartRequirement from the operating point (value + ratings),
//    2. ask the catalog for RANKED candidates,
//    3. walk the ranking, VERIFYING each candidate against the requirement,
//    4. bind the first candidate that passes; record the ones we rejected,
//    5. re-write the component's value to the REAL part value (never assume the
//       ideal value survives), so downstream stages see the real design.
//
//  This is where "ideal calc -> nearest real part -> re-evaluate -> reject and
//  retry -> converge" actually happens.
// ============================================================================

#include "electrical_ir.h"
#include "parts_catalog.h"
#include "catalog_source.h"
#include "buck_sizing.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace partbind {

struct PartRequirement {
    bool   needs_binding = true;     // false => virtual (e.g. the LOAD)

    bool   has_value_target = false;
    double value_target = 0.0;
    double value_tol = 0.30;

    double min_v_rating = 0.0;
    double min_i_rating = 0.0;
    double min_power_rating = 0.0;
};

// Derive per-role requirements from the buck spec + sizing operating point.
// Margins here are deliberate engineering derating (real design practice).
inline std::unordered_map<std::string, PartRequirement>
ComputeBuckRequirements(const phys::BuckSpec& spec, const phys::SizingResult& s) {
    std::unordered_map<std::string, PartRequirement> req;

    PartRequirement L;
    L.has_value_target = true; L.value_target = s.l_henries; L.value_tol = 0.30;
    L.min_i_rating = s.il_peak * 1.2;          // saturation margin over peak
    req["OUTPUT_INDUCTOR"] = L;

    PartRequirement Cout;
    Cout.has_value_target = true; Cout.value_target = s.cout_farads; Cout.value_tol = 0.40;
    Cout.min_v_rating = spec.vout_v / 0.5;     // 50% ceramic derating
    req["OUTPUT_CAP"] = Cout;

    PartRequirement Cin;
    Cin.has_value_target = true; Cin.value_target = s.cin_farads; Cin.value_tol = 0.40;
    Cin.min_v_rating = spec.vin_v / 0.8;       // 80% derating on input rail
    req["INPUT_CAP"] = Cin;

    PartRequirement Q;
    Q.has_value_target = false;
    Q.min_v_rating = spec.vin_v * 1.5;         // Vds margin over input
    Q.min_i_rating = s.il_peak * 1.5;
    req["HS_SWITCH"] = Q;

    PartRequirement D;
    D.has_value_target = false;
    D.min_v_rating = spec.vin_v * 1.3;         // reverse voltage margin
    D.min_i_rating = spec.iout_a;              // avg diode current bound
    req["FREEWHEEL_DIODE"] = D;

    PartRequirement Load;
    Load.needs_binding = false;                // the load is not a board part
    req["LOAD"] = Load;

    PartRequirement Ctrl;                       // used by the controller variant
    Ctrl.has_value_target = false;
    Ctrl.min_v_rating = spec.vin_v * 1.3;
    req["CONTROLLER"] = Ctrl;

    return req;
}

// Verify one candidate against a requirement. Fills `reason` on failure.
inline bool Verify(const cat::CatalogPart& p, const PartRequirement& req,
                   std::string& reason) {
    if (req.has_value_target) {
        const double lo = req.value_target * (1.0 - req.value_tol);
        const double hi = req.value_target * (1.0 + req.value_tol);
        if (p.value < lo || p.value > hi) {
            reason = "value out of tolerance";
            return false;
        }
    }
    if (req.min_v_rating > 0.0 && p.v_rating < req.min_v_rating) {
        reason = "insufficient voltage rating (" + std::to_string(p.v_rating) +
                 " < " + std::to_string(req.min_v_rating) + ")";
        return false;
    }
    if (req.min_i_rating > 0.0 && p.i_rating < req.min_i_rating) {
        reason = "insufficient current rating (" + std::to_string(p.i_rating) +
                 " < " + std::to_string(req.min_i_rating) + ")";
        return false;
    }
    if (req.min_power_rating > 0.0 && p.power_rating_w < req.min_power_rating) {
        reason = "insufficient power rating";
        return false;
    }
    reason.clear();
    return true;
}

struct RejectedCandidate {
    std::string mpn;
    std::string reason;
};

struct BindOutcome {
    std::string ref;
    std::string role;
    bool        skipped = false;     // virtual component, not bound
    bool        bound = false;
    std::string chosen_mpn;
    std::vector<RejectedCandidate> rejected;
    std::string failure;             // set if needed binding but nothing passed
};

struct BindReport {
    std::vector<BindOutcome> outcomes;
    bool all_bound = false;          // all needs_binding components got a part
};

// Bind real parts into the design. Mutates component mpn/value/package.
// Core version: parts come from any catalog source (mock, SQLite, ...).
inline BindReport BindDesign(
    ir::Design& design,
    const std::unordered_map<std::string, PartRequirement>& requirements,
    const cat::ICatalogSource& source) {

    BindReport report;
    bool all_ok = true;

    for (auto& c : design.components) {
        BindOutcome out;
        out.ref = c.ref;
        out.role = c.role;

        auto it = requirements.find(c.role);
        if (it == requirements.end() || !it->second.needs_binding) {
            out.skipped = true;
            report.outcomes.push_back(std::move(out));
            continue;
        }

        const PartRequirement& req = it->second;

        cat::CandidateQuery q;
        q.type = c.type;
        q.has_value_target = req.has_value_target;
        q.value_target = req.value_target;
        q.value_tol = req.value_tol;

        std::vector<cat::CatalogPart> candidates = source.Find(q);

        for (const auto& cand : candidates) {
            std::string reason;
            if (Verify(cand, req, reason)) {
                c.mpn = cand.mpn;
                c.value = cand.value;      // adopt the REAL value
                c.package = cand.package;
                out.bound = true;
                out.chosen_mpn = cand.mpn;
                break;
            }
            out.rejected.push_back({cand.mpn, reason});
        }

        if (!out.bound) {
            out.failure = "no catalog part satisfied requirements";
            all_ok = false;
        }

        report.outcomes.push_back(std::move(out));
    }

    report.all_bound = all_ok;
    return report;
}

// Convenience overload: bind directly from an in-memory vector of parts.
inline BindReport BindDesign(
    ir::Design& design,
    const std::unordered_map<std::string, PartRequirement>& requirements,
    const std::vector<cat::CatalogPart>& catalog) {
    cat::MockCatalogSource source(catalog);
    return BindDesign(design, requirements, source);
}

} // namespace partbind
