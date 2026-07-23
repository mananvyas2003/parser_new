#pragma once

// ============================================================================
//  placement.h
//  Geometry stage. This runs ONLY after the electrical design is synthesized
//  and verified: it turns the abstract IR (which has no coordinates) into
//  physical positions -- component placements and per-pin offsets. This is the
//  first point in the whole pipeline where geometry exists at all.
//
//  The placement here is intentionally simple (a grid). Real placement/routing
//  (clearances, thermal, routing cost) is a separate optimization problem; this
//  layer just produces a valid, serializable geometry for the schematic writer.
// ============================================================================

#include "electrical_ir.h"

#include <string>
#include <utility>
#include <vector>

namespace place {

struct XY {
    double x = 0.0;
    double y = 0.0;
};

struct PlacedComponent {
    std::string ref;
    ir::PartType type = ir::PartType::Unknown;
    XY at;
    double rotation = 0.0;
    // (pin name -> offset from the component origin), same order as IR pins.
    std::vector<std::pair<std::string, XY>> pin_offsets;
};

struct Placement {
    std::vector<PlacedComponent> components;
};

// Grid placement + vertical pin fan-out. Deterministic and geometry-valid.
inline Placement PlaceDesign(const ir::Design& d) {
    Placement plan;

    const double origin = 25.4;   // mm
    const double spacing = 50.8;  // mm between component slots
    const double pin_pitch = 2.54;
    const int cols = 3;

    for (size_t i = 0; i < d.components.size(); ++i) {
        const ir::Component& c = d.components[i];

        PlacedComponent pc;
        pc.ref = c.ref;
        pc.type = c.type;
        pc.at.x = origin + (static_cast<int>(i) % cols) * spacing;
        pc.at.y = origin + (static_cast<int>(i) / cols) * spacing;
        pc.rotation = 0.0;

        const int n = static_cast<int>(c.pins.size());
        for (int j = 0; j < n; ++j) {
            XY off;
            off.x = 0.0;
            off.y = ((n - 1) / 2.0 - j) * pin_pitch; // centered vertical fan-out
            pc.pin_offsets.emplace_back(c.pins[j].name, off);
        }

        plan.components.push_back(std::move(pc));
    }

    return plan;
}

} // namespace place
