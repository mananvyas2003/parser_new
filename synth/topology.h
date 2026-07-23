#pragma once

// ============================================================================
//  topology.h
//  In-memory model of a circuit TOPOLOGY TEMPLATE. This is the code-level
//  mirror of the DB schema (PCB-AI-Database), which stores topology as data:
//
//    Topologies(name, description, category)
//    TopologyComponents(role_name, part_type, quantity)
//    TopologyNodes(node_name)
//    TopologyConnections(component_id, pin_name, node_id)
//
//  A template is deliberately abstract: it has ROLES (not MPNs) and symbolic
//  pin/node names (not values or geometry). Instantiation (topology_instantiate.h)
//  turns a template into an unbound ir::Design.
//
//  Note: the DB keeps connections in a separate table keyed by component_id and
//  node_id. Here we inline each component's (pin_name -> node_name) pairs, which
//  is the same information as the TopologyComponents x TopologyConnections x
//  TopologyNodes join, just flattened for convenient in-memory use.
// ============================================================================

#include "electrical_ir.h"

#include <string>
#include <utility>
#include <vector>

namespace topo {

// One template component (a role to be filled by a real part later).
struct TemplateComponent {
    std::string   role_name;   // e.g. "HS_SWITCH", "OUTPUT_INDUCTOR"
    ir::PartType  part_type;   // e.g. PartType::Mosfet
    int           quantity = 1; // >1 => N identical instances wired in parallel

    // (pin_name -> node_name) for this component, e.g. {"D","VIN"}.
    std::vector<std::pair<std::string, std::string>> connections;
};

// A full topology template.
struct Topology {
    std::string name;         // "buck_converter"
    std::string description;  // human blurb
    std::string category;     // "dc-dc"

    std::vector<std::string>       nodes;      // declared electrical nodes
    std::vector<TemplateComponent> components; // roles + their wiring
};

} // namespace topo
