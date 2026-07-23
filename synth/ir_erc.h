#pragma once

// ============================================================================
//  ir_erc.h
//  Electrical Rule Check operating directly on the geometry-free IR. This is
//  the analysis stage the brief calls "electrical-rule checking": it works on
//  the verified IR without knowing anything about prompts, SQLite, or geometry.
//
//  Rules (electrical, beyond the purely structural ir_validate.h):
//    ERROR   : a pin is left unconnected
//    ERROR   : the design has no ground (GND) reference net
//    WARNING : a net has a single terminal (floating / undriven)
//    WARNING : a declared net has no terminals at all
// ============================================================================

#include "electrical_ir.h"

#include <string>
#include <vector>

namespace erc {

struct ErcReport {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    bool ok() const { return errors.empty(); }
};

inline ErcReport RunErc(const ir::Design& d) {
    ErcReport r;

    std::vector<int> terminals(d.nodes.size(), 0);

    for (const auto& c : d.components) {
        for (const auto& p : c.pins) {
            if (p.node == ir::INVALID_NODE) {
                r.errors.push_back("Unconnected pin " + c.ref + "." + p.name);
                continue;
            }
            if (p.node < terminals.size()) terminals[p.node]++;
        }
    }

    if (d.FindNode("GND") == ir::INVALID_NODE) {
        r.errors.push_back("No ground (GND) reference net in design");
    }

    for (ir::NodeId i = 0; i < d.nodes.size(); ++i) {
        if (terminals[i] == 0) {
            r.warnings.push_back("Net '" + d.nodes[i].name +
                                 "' has no terminals");
        } else if (terminals[i] == 1) {
            r.warnings.push_back("Net '" + d.nodes[i].name +
                                 "' is floating (single terminal)");
        }
    }

    return r;
}

} // namespace erc
