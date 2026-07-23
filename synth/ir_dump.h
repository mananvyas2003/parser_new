#pragma once

// ============================================================================
//  ir_dump.h
//  Human-readable console dump of an Electrical IR design. Debug aid only;
//  the machine-readable contract (JSON netlist) arrives in Phase 2.
// ============================================================================

#include "electrical_ir.h"
#include "ir_validate.h"

#include <iostream>

namespace ir {

inline const char* NodeName(const Design& d, NodeId id) {
    return id == INVALID_NODE ? "<none>" : d.nodes[id].name.c_str();
}

inline void Dump(const Design& design) {
    std::cout << "==================================================\n";
    std::cout << " ELECTRICAL IR : " << design.topology << "\n";
    std::cout << "==================================================\n";

    std::cout << "\nNodes (" << design.nodes.size() << "):\n";
    for (NodeId i = 0; i < design.nodes.size(); ++i) {
        std::cout << "  [" << i << "] " << design.nodes[i].name << "\n";
    }

    std::cout << "\nComponents (" << design.components.size() << "):\n";
    for (const auto& c : design.components) {
        std::cout << "  " << c.ref << "  role=" << c.role
                  << "  type=" << ToString(c.type)
                  << "  value=" << c.value
                  << "  mpn=" << (c.mpn.empty() ? "<unbound>" : c.mpn) << "\n";
        for (const auto& p : c.pins) {
            std::cout << "      ." << p.name << " -> " << NodeName(design, p.node)
                      << "\n";
        }
    }
}

inline void DumpReport(const ValidationReport& report) {
    std::cout << "\n--------------------------------------------------\n";
    std::cout << " VALIDATION : " << (report.ok() ? "OK" : "FAILED") << "\n";
    std::cout << "--------------------------------------------------\n";
    for (const auto& e : report.errors)   std::cout << "  ERROR   : " << e << "\n";
    for (const auto& w : report.warnings) std::cout << "  WARNING : " << w << "\n";
    if (report.errors.empty() && report.warnings.empty()) {
        std::cout << "  (no issues)\n";
    }
}

} // namespace ir
