// ============================================================================
//  ir_json_test.cpp
//  Phase 2 test: serialize the buck IR to JSON, verify the contract contains
//  the expected component-centric and net-centric data, and write it to disk.
//
//  Build:
//    g++ -std=c++17 -I synth synth/tests/ir_json_test.cpp -o build/ir_json_test
//  Run:
//    ./build/ir_json_test
// ============================================================================

#include "electrical_ir.h"
#include "ir_json.h"
#include "tests/buck_fixture.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace ir;

static bool Contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

int main() {
    Design d = BuildBuckPowerStage();

    const std::string json = ToJson(d);
    std::cout << json;

    // ---- Contract assertions -------------------------------------------
    assert(Contains(json, "\"topology\": \"buck_converter\""));

    // Component-centric view: Q1 with its pins wired to named nodes.
    assert(Contains(json, "\"ref\": \"Q1\""));
    assert(Contains(json, "\"type\": \"Mosfet\""));
    assert(Contains(json, "{\"name\": \"D\", \"node\": \"VIN\"}"));
    assert(Contains(json, "{\"name\": \"S\", \"node\": \"SW\"}"));
    assert(Contains(json, "{\"name\": \"G\", \"node\": \"DRIVE\"}"));
    assert(Contains(json, "{\"name\": \"1\", \"node\": \"SW\"}"));   // L1.1
    assert(Contains(json, "{\"name\": \"2\", \"node\": \"VOUT\"}")); // L1.2

    // Unbound parts serialize mpn as null.
    assert(Contains(json, "\"mpn\": null"));

    // Net-centric view: VIN net collects Q1.D and Cin.1; GND collects many.
    assert(Contains(json, "\"name\": \"VIN\", \"pins\": [\"Q1.D\", \"Cin.1\"]"));
    assert(Contains(json, "\"name\": \"SW\", \"pins\": [\"Q1.S\", \"D1.K\", \"L1.1\"]"));
    assert(Contains(json, "\"name\": \"VOUT\", \"pins\": [\"L1.2\", \"Cout.1\", \"Rload.1\"]"));
    assert(Contains(json, "\"name\": \"GND\", \"pins\": [\"D1.A\", \"Cin.2\", \"Cout.2\", \"Rload.2\"]"));

    // ---- Write to disk (the artifact downstream stages will read) ------
    const std::string out_path = "build/buck_netlist.json";
    bool wrote = WriteJson(d, out_path);
    assert(wrote && "Failed to write JSON netlist");
    std::cout << "\n[written] " << out_path << "\n";

    std::cout << "\n[PASS] Phase 2: IR JSON serialization test passed.\n";
    return 0;
}
