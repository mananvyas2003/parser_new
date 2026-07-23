#pragma once

// ============================================================================
//  ir_json.h
//  Serializes the Electrical IR to a stable JSON netlist. This is THE contract
//  that later stages (graph build, ERC, SPICE, schematic writer) read, so they
//  never need to know about prompts, SQLite, or geometry.
//
//  Emits two complementary views of the same design:
//    "components" : component-centric (ref, role, type, mpn, pins -> node)
//    "nets"       : net-centric      (node name -> list of "ref.pin" terminals)
// ============================================================================

#include "electrical_ir.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace ir {

inline std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

// Renders a double without trailing noise; keeps enough precision for values.
inline std::string JsonNumber(double v) {
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

inline std::string ToJson(const Design& design) {
    std::ostringstream o;

    o << "{\n";
    o << "  \"topology\": \"" << JsonEscape(design.topology) << "\",\n";

    // --- nodes ----------------------------------------------------------
    o << "  \"nodes\": [";
    for (size_t i = 0; i < design.nodes.size(); ++i) {
        o << (i ? ", " : "") << "\"" << JsonEscape(design.nodes[i].name) << "\"";
    }
    o << "],\n";

    // --- components (component-centric view) ----------------------------
    o << "  \"components\": [\n";
    for (size_t ci = 0; ci < design.components.size(); ++ci) {
        const Component& c = design.components[ci];
        o << "    {\n";
        o << "      \"ref\": \""     << JsonEscape(c.ref)  << "\",\n";
        o << "      \"role\": \""    << JsonEscape(c.role) << "\",\n";
        o << "      \"type\": \""    << ToString(c.type)   << "\",\n";
        o << "      \"value\": "     << JsonNumber(c.value) << ",\n";
        o << "      \"mpn\": "       << (c.mpn.empty()
                                          ? std::string("null")
                                          : "\"" + JsonEscape(c.mpn) + "\"") << ",\n";
        o << "      \"package\": \"" << JsonEscape(c.package) << "\",\n";
        o << "      \"pins\": [";
        for (size_t pi = 0; pi < c.pins.size(); ++pi) {
            const Pin& p = c.pins[pi];
            const std::string node =
                (p.node == INVALID_NODE) ? std::string("null")
                                         : "\"" + JsonEscape(design.nodes[p.node].name) + "\"";
            o << (pi ? ", " : "")
              << "{\"name\": \"" << JsonEscape(p.name) << "\", \"node\": " << node << "}";
        }
        o << "]\n";
        o << "    }" << (ci + 1 < design.components.size() ? "," : "") << "\n";
    }
    o << "  ],\n";

    // --- nets (net-centric view) ----------------------------------------
    // For each node, collect all "ref.pin" terminals attached to it.
    std::vector<std::vector<std::string>> terminals(design.nodes.size());
    for (const Component& c : design.components) {
        for (const Pin& p : c.pins) {
            if (p.node != INVALID_NODE && p.node < design.nodes.size()) {
                terminals[p.node].push_back(c.ref + "." + p.name);
            }
        }
    }

    o << "  \"nets\": [\n";
    for (size_t ni = 0; ni < design.nodes.size(); ++ni) {
        o << "    {\"name\": \"" << JsonEscape(design.nodes[ni].name) << "\", \"pins\": [";
        for (size_t ti = 0; ti < terminals[ni].size(); ++ti) {
            o << (ti ? ", " : "") << "\"" << JsonEscape(terminals[ni][ti]) << "\"";
        }
        o << "]}" << (ni + 1 < design.nodes.size() ? "," : "") << "\n";
    }
    o << "  ]\n";

    o << "}\n";
    return o.str();
}

inline bool WriteJson(const Design& design, const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << ToJson(design);
    return f.good();
}

} // namespace ir
