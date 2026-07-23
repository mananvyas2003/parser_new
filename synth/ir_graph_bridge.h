#pragma once

// ============================================================================
//  ir_graph_bridge.h
//  Bridges the geometry-free Electrical IR into the EXISTING parser_new graph
//  (graph.h / graph_search.h). This proves the circuit-analysis machinery that
//  was written for parsed .kicad_sch files can operate, unchanged, on a
//  SYNTHESIZED design -- because both converge on the same net structure.
//
//  Construction is geometry-free: every vertex sits at the origin. We model:
//    - one "net vertex" (VertexType::Label) per electrical node, and
//    - one "pin vertex" (VertexType::Pin) per component pin,
//  joined by PinConnection edges. GraphSearch::ExtractNets then recovers the
//  nets as connected components -- reusing the existing DFS net extractor.
// ============================================================================

#include "electrical_ir.h"

#include "graph.h"          // from parser_new root
#include "graph_search.h"   // from parser_new root

#include <map>
#include <set>
#include <string>
#include <vector>

namespace bridge {

// Build the existing Graph type from the IR (no geometry required).
inline Graph BuildGraphFromIR(const ir::Design& d) {
    Graph g;
    const Point origin{0.0, 0.0};

    std::vector<VertexID> node_vtx(d.nodes.size(), INVALID_VERTEX);
    for (ir::NodeId i = 0; i < d.nodes.size(); ++i) {
        VertexID v = g.AddVertex(origin, VertexType::Label);
        g.GetVertex(v).label = d.nodes[i].name;
        node_vtx[i] = v;
    }

    for (const auto& c : d.components) {
        for (const auto& p : c.pins) {
            VertexID pv = g.AddVertex(origin, VertexType::Pin);
            GraphVertex& vx = g.GetVertex(pv);
            vx.component = c.ref;
            vx.pin = p.name;
            if (p.node != ir::INVALID_NODE && p.node < node_vtx.size()) {
                g.AddEdge(pv, node_vtx[p.node], EdgeType::PinConnection);
            }
        }
    }

    return g;
}

// Recover a "net name -> {ref.pin}" signature from the graph's extracted nets.
inline std::map<std::string, std::set<std::string>> RecoverNetSignature(Graph& g) {
    std::map<std::string, std::set<std::string>> sig;
    auto nets = GraphSearch::ExtractNets(g);

    for (const auto& net : nets) {
        std::string label;
        std::vector<std::string> pins;
        for (VertexID v : net) {
            const GraphVertex& vx = g.GetVertex(v);
            if (vx.type == VertexType::Label && !vx.label.empty()) {
                label = vx.label;
            } else if (vx.type == VertexType::Pin) {
                pins.push_back(vx.component + "." + vx.pin);
            }
        }
        if (!label.empty()) {
            for (const auto& p : pins) sig[label].insert(p);
        }
    }
    return sig;
}

// The same signature computed directly from the IR (ground truth to compare).
inline std::map<std::string, std::set<std::string>>
IrNetSignatureByRef(const ir::Design& d) {
    std::map<std::string, std::set<std::string>> sig;
    for (const auto& c : d.components) {
        for (const auto& p : c.pins) {
            if (p.node == ir::INVALID_NODE) continue;
            sig[d.nodes[p.node].name].insert(c.ref + "." + p.name);
        }
    }
    return sig;
}

} // namespace bridge
