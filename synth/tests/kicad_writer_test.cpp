// ============================================================================
//  kicad_writer_test.cpp
//  Phase 7 test: synthesize + verify the buck IR, place it, emit a .kicad_sch,
//  then ROUND-TRIP it back through the existing parser_new pipeline
//  (Lexer -> Parser -> Interpreter) and confirm the schematic that comes back
//  matches the design we serialized (components, MPNs, pins, net labels).
//
//  Build:
//    g++ -std=c++17 -I synth -I . synth/tests/kicad_writer_test.cpp lexer.cpp parser.cpp interpreter.cpp -o build/kicad_writer_test
//  Run:
//    ./build/kicad_writer_test
// ============================================================================

#include "electrical_ir.h"
#include "topology_library.h"
#include "topology_instantiate.h"
#include "buck_sizing.h"
#include "parts_catalog.h"
#include "part_binding.h"
#include "placement.h"
#include "kicad_writer.h"

#include "lexer.h"        // parser_new root
#include "parser.h"       // parser_new root
#include "interpreter.h"  // parser_new root

#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace ir;

int main() {
    // ---- Full forward pipeline (Phases 3-5) ----------------------------
    Design d = topo::Instantiate(topo::BuildBuckTopology());
    phys::BuckSpec spec;
    spec.vin_v = 12.0; spec.vout_v = 5.0; spec.iout_a = 3.0;
    spec.fsw_hz = 500e3; spec.ripple_current_ratio = 0.30;
    phys::SizeBuck(spec, &d);
    auto req = partbind::ComputeBuckRequirements(spec, phys::SizeBuck(spec, nullptr));
    partbind::BindDesign(d, req, cat::BuildMockCatalog());

    // ---- Geometry + serialization (Phase 7) ----------------------------
    place::Placement plan = place::PlaceDesign(d);
    const std::string path = "build/buck.kicad_sch";
    bool wrote = kicad::WriteKicadSch(d, plan, path);
    assert(wrote && "failed to write .kicad_sch");
    std::cout << "[written] " << path << "\n\n";

    // ---- Round-trip: read it back through the existing pipeline ---------
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    assert(in.is_open());
    const size_t size = static_cast<size_t>(in.tellg());
    in.seekg(0);
    std::vector<char> buf(size + 1);
    in.read(buf.data(), size);
    buf[size] = '\0';
    in.close();

    Lexer lexer(buf.data());
    Parser parser(lexer);
    uint32_t root = parser.Parse();
    assert(root != 0 && "parser returned empty tree");

    Interpreter interp(parser.GetPool());
    Schematic sch = interp.Execute(root);

    // ---- Verify the parsed-back schematic matches the design -----------
    std::cout << "\n=== ROUND-TRIP RESULT ===\n";
    std::cout << "Components: " << sch.components.size() << "\n";
    std::cout << "Labels    : " << sch.labels.size() << "\n";

    assert(sch.components.size() == 6 && "expected 6 components round-tripped");

    std::map<std::string, std::string> ref_value;
    std::map<std::string, size_t> ref_pins;
    for (const auto& c : sch.components) {
        ref_value[c.reference] = c.value;
        ref_pins[c.reference] = c.pins.size();
    }

    // Real MPNs survived serialization as the component Value.
    assert(ref_value["Q1"] == "MOS-30V-8A");
    assert(ref_value["D1"] == "DIO-40V-5A");
    assert(ref_value["L1"] == "IND-6R8-5A0");
    assert(ref_value["C1"] == "CAP-6U8-16V");
    assert(ref_value["C2"] == "CAP-4U7-25V");
    assert(!ref_value["R1"].empty()); // unbound load still carries a value

    // Pins came back via the emitted lib_symbols (geometry round-tripped).
    assert(ref_pins["Q1"] == 3 && "MOSFET should have 3 pins");
    assert(ref_pins["L1"] == 2 && ref_pins["C1"] == 2 &&
           ref_pins["C2"] == 2 && ref_pins["D1"] == 2 && ref_pins["R1"] == 2);

    // Every net name appears as a label on the sheet.
    std::set<std::string> label_names;
    for (const auto& lbl : sch.labels) label_names.insert(lbl.name);
    for (const char* net : {"VIN", "SW", "VOUT", "GND", "DRIVE"}) {
        assert(label_names.count(net) && "missing net label");
    }

    std::cout << "\nRound-tripped components:\n";
    for (const auto& c : sch.components) {
        std::cout << "  " << c.reference << " = " << c.value
                  << "  (pins: " << c.pins.size() << ")\n";
    }

    std::cout << "\n[PASS] Phase 7: kicad_sch write + round-trip test passed.\n";
    return 0;
}
