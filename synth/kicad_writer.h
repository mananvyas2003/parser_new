#pragma once

// ============================================================================
//  kicad_writer.h
//  Output/backend stage: serialize a verified IR + placement into a .kicad_sch
//  s-expression. Per the design brief, this is SERIALIZATION, not circuit
//  reasoning -- the IR already knows every component (with real MPN), pin, net,
//  and connection. Here we only emit symbols, a minimal lib_symbols with pin
//  geometry, and a net label at each pin (label-based connectivity).
// ============================================================================

#include "electrical_ir.h"
#include "placement.h"

#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace kicad {

inline std::string Num(double v) {
    std::ostringstream o;
    o << v;
    return o.str();
}

inline std::string LibNameFor(ir::PartType t) {
    switch (t) {
        case ir::PartType::Resistor:  return "synth:R";
        case ir::PartType::Capacitor: return "synth:C";
        case ir::PartType::Inductor:  return "synth:L";
        case ir::PartType::Diode:     return "synth:D";
        case ir::PartType::Mosfet:    return "synth:Q";
        case ir::PartType::IC:        return "synth:U";
        default:                      return "synth:X";
    }
}

// The value string shown on the schematic: the real MPN if bound, else the
// ideal computed value (so unbound/virtual parts still carry information).
inline std::string ValueString(const ir::Component& c) {
    if (c.IsBound()) return c.mpn;
    return Num(c.value);
}

inline std::string ToKicadSch(const ir::Design& design, const place::Placement& plan) {
    std::ostringstream o;

    // Index placement by ref for quick lookup.
    std::map<std::string, const place::PlacedComponent*> by_ref;
    for (const auto& pc : plan.components) by_ref[pc.ref] = &pc;

    o << "(kicad_sch\n";
    o << "\t(version 20250114)\n";
    o << "\t(generator \"parser_new_synth\")\n";
    o << "\t(generator_version \"1.0\")\n";

    // --- lib_symbols: one definition per part type in use ----------------
    // Pin geometry is taken from the first placed component of each type
    // (all instances of a type share the same pin fan-out here).
    std::map<ir::PartType, const place::PlacedComponent*> proto;
    for (const auto& pc : plan.components) {
        proto.emplace(pc.type, &pc);
    }

    o << "\t(lib_symbols\n";
    for (const auto& kv : proto) {
        const std::string lib = LibNameFor(kv.first);
        const place::PlacedComponent& pc = *kv.second;

        o << "\t\t(symbol \"" << lib << "\"\n";
        o << "\t\t\t(symbol \"" << lib << "_0_1\"\n";
        for (const auto& pn : pc.pin_offsets) {
            o << "\t\t\t\t(pin passive line "
              << "(at " << Num(pn.second.x) << " " << Num(pn.second.y) << " 0) "
              << "(length 1.27) "
              << "(name \"~\") "
              << "(number \"" << pn.first << "\"))\n";
        }
        o << "\t\t\t)\n";
        o << "\t\t)\n";
    }
    o << "\t)\n";

    // --- symbol instances ------------------------------------------------
    for (const auto& c : design.components) {
        const place::PlacedComponent* pc = by_ref[c.ref];
        const double x = pc ? pc->at.x : 0.0;
        const double y = pc ? pc->at.y : 0.0;

        o << "\t(symbol\n";
        o << "\t\t(lib_id \"" << LibNameFor(c.type) << "\")\n";
        o << "\t\t(at " << Num(x) << " " << Num(y) << " 0)\n";
        o << "\t\t(unit 1)\n";
        o << "\t\t(property \"Reference\" \"" << c.ref << "\" (at " << Num(x)
          << " " << Num(y - 5.08) << " 0))\n";
        o << "\t\t(property \"Value\" \"" << ValueString(c) << "\" (at " << Num(x)
          << " " << Num(y + 5.08) << " 0))\n";
        if (!c.package.empty()) {
            o << "\t\t(property \"Footprint\" \"" << c.package << "\" (at " << Num(x)
              << " " << Num(y) << " 0))\n";
        }
        o << "\t)\n";
    }

    // --- net labels at each pin (label-based connectivity) ---------------
    for (const auto& c : design.components) {
        const place::PlacedComponent* pc = by_ref[c.ref];
        if (!pc) continue;
        for (size_t j = 0; j < c.pins.size() && j < pc->pin_offsets.size(); ++j) {
            const ir::Pin& pin = c.pins[j];
            if (pin.node == ir::INVALID_NODE) continue;
            const std::string net = design.nodes[pin.node].name;
            const double wx = pc->at.x + pc->pin_offsets[j].second.x;
            const double wy = pc->at.y + pc->pin_offsets[j].second.y;
            o << "\t(label \"" << net << "\" (at " << Num(wx) << " " << Num(wy)
              << " 0))\n";
        }
    }

    o << ")\n";
    return o.str();
}

inline bool WriteKicadSch(const ir::Design& design, const place::Placement& plan,
                          const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << ToKicadSch(design, plan);
    return f.good();
}

} // namespace kicad
