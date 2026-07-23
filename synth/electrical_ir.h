#pragma once

// ============================================================================
//  electrical_ir.h
//  Geometry-free Electrical Intermediate Representation (IR).
//
//  This is the "waist" of the circuit-synthesis compiler: the single stable
//  contract that both the forward path (spec -> topology -> sized design) and
//  any reverse/ingestion path converge on. It contains NO coordinates,
//  rotations, or schematic geometry. Placement/routing operate on this later.
//
//  Alignment with the DB schema (PCB-AI-Database):
//    Node       ~ TopologyNodes(node_name)
//    Component  ~ TopologyComponents(role_name, part_type) + bound Parts(mpn,...)
//    Pin.node   ~ TopologyConnections(component_id, pin_name, node_id)
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace ir {

using NodeId = uint32_t;
constexpr NodeId INVALID_NODE = static_cast<NodeId>(-1);

// Mirrors the DB `Parts.type` / PartTypes integer domain.
enum class PartType : int {
    Unknown   = 0,
    Resistor  = 1,
    Capacitor = 2,
    Inductor  = 3,
    Diode     = 4,
    Mosfet    = 5,
    IC        = 6,
};

inline const char* ToString(PartType t) {
    switch (t) {
        case PartType::Resistor:  return "Resistor";
        case PartType::Capacitor: return "Capacitor";
        case PartType::Inductor:  return "Inductor";
        case PartType::Diode:     return "Diode";
        case PartType::Mosfet:    return "Mosfet";
        case PartType::IC:        return "IC";
        default:                  return "Unknown";
    }
}

// A single pin on a concrete component instance, wired to an electrical node.
struct Pin {
    std::string name;               // symbolic pin name/number: "G","D","S","1","2"
    NodeId      node = INVALID_NODE; // electrical node this pin connects to
};

// A concrete component instance in the design. It may be "unbound" (no MPN yet)
// right after topology instantiation, then "bound" after part selection.
struct Component {
    std::string ref;                 // designator: "Q1","L1","C1"
    std::string role;                // topology role: "HS_SWITCH","OUTPUT_INDUCTOR"
    PartType    type = PartType::Unknown;

    // Filled by the physics sizing layer (ideal target) and part binding.
    double      value   = 0.0;       // ideal/target value (F, H, Ohm, ...)
    std::string mpn;                 // real catalogue part; empty => unbound
    std::string package;             // e.g. "0603","SOT-23"

    std::vector<Pin> pins;

    bool IsBound() const { return !mpn.empty(); }
};

// An electrical node (net). Purely topological -- no geometry.
struct Node {
    std::string name;                // "VIN","SW","VOUT","GND"
};

// The full electrical design: nodes + component instances wired to nodes.
struct Design {
    std::string            topology; // e.g. "buck_converter"
    std::vector<Node>      nodes;
    std::vector<Component> components;

    // Returns existing node id if name already present, else creates it.
    NodeId AddNode(const std::string& name) {
        for (NodeId i = 0; i < nodes.size(); ++i) {
            if (nodes[i].name == name) return i;
        }
        nodes.push_back(Node{name});
        return static_cast<NodeId>(nodes.size() - 1);
    }

    NodeId FindNode(const std::string& name) const {
        for (NodeId i = 0; i < nodes.size(); ++i) {
            if (nodes[i].name == name) return i;
        }
        return INVALID_NODE;
    }

    Component* FindComponent(const std::string& ref) {
        for (auto& c : components) {
            if (c.ref == ref) return &c;
        }
        return nullptr;
    }
};

} // namespace ir
