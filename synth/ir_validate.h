#pragma once

// ============================================================================
//  ir_validate.h
//  Structural validation for the Electrical IR. This checks the design is a
//  well-formed electrical graph BEFORE any physics, sizing, or geometry runs.
//  It is intentionally geometry-agnostic and cheap so every phase can re-run it.
// ============================================================================

#include "electrical_ir.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace ir {

struct ValidationReport {
    std::vector<std::string> errors;   // must be empty for a valid design
    std::vector<std::string> warnings; // non-fatal (floating nets, unbound, ...)

    bool ok() const { return errors.empty(); }
};

inline ValidationReport Validate(const Design& design) {
    ValidationReport report;

    // --- Unique component references ------------------------------------
    std::unordered_map<std::string, int> ref_count;
    for (const auto& c : design.components) {
        if (c.ref.empty()) {
            report.errors.push_back("Component with empty reference designator");
        }
        ref_count[c.ref]++;
    }
    for (const auto& kv : ref_count) {
        if (kv.second > 1) {
            report.errors.push_back("Duplicate component reference: " + kv.first);
        }
    }

    // --- Unique node names ----------------------------------------------
    std::unordered_map<std::string, int> node_count;
    for (const auto& n : design.nodes) {
        node_count[n.name]++;
    }
    for (const auto& kv : node_count) {
        if (kv.second > 1) {
            report.errors.push_back("Duplicate node name: " + kv.first);
        }
    }

    // --- Pin -> node integrity + net degree -----------------------------
    std::vector<int> pins_on_node(design.nodes.size(), 0);

    for (const auto& c : design.components) {
        for (const auto& p : c.pins) {
            if (p.node == INVALID_NODE) {
                report.warnings.push_back(
                    "Unconnected pin " + c.ref + "." + p.name);
                continue;
            }
            if (p.node >= design.nodes.size()) {
                report.errors.push_back(
                    "Pin " + c.ref + "." + p.name +
                    " references invalid node id " + std::to_string(p.node));
                continue;
            }
            pins_on_node[p.node]++;
        }
    }

    // --- Floating nets (a net electrically needs >= 2 terminals) --------
    for (NodeId i = 0; i < design.nodes.size(); ++i) {
        if (pins_on_node[i] == 0) {
            report.warnings.push_back("Node '" + design.nodes[i].name +
                                      "' has no pins attached");
        } else if (pins_on_node[i] == 1) {
            report.warnings.push_back("Node '" + design.nodes[i].name +
                                      "' is floating (only 1 pin)");
        }
    }

    // --- Unbound components (informational until Phase 5) ---------------
    for (const auto& c : design.components) {
        if (!c.IsBound()) {
            report.warnings.push_back("Component " + c.ref +
                                      " is unbound (no MPN)");
        }
    }

    return report;
}

} // namespace ir
