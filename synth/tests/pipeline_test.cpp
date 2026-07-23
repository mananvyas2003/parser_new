// ============================================================================
//  pipeline_test.cpp
//  Task 4 test: run the entire pipeline from a natural-language prompt and
//  confirm the end-to-end result (spec, bound parts, ERC, emitted schematic).
//
//  Build:
//    g++ -std=c++17 -I synth synth/tests/pipeline_test.cpp -o build/pipeline_test
//  Run:
//    ./build/pipeline_test
// ============================================================================

#include "parts_catalog.h"
#include "catalog_source.h"
#include "synth_pipeline.h"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>

static bool FileNonEmpty(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    return f.is_open() && f.tellg() > 0;
}

int main() {
    cat::MockCatalogSource catalog(cat::BuildMockCatalog());

    // ---- Power-stage variant from a prompt -----------------------------
    auto r = pipeline::Run("design a 12 V to 5 V buck converter at 3 A",
                           catalog, "build/pipeline_buck.kicad_sch", false);
    std::cout << (r.ok ? "OK" : ("FAIL: " + r.error)) << "\n";
    assert(r.ok);
    assert(std::fabs(r.spec.vin_v - 12.0) < 1e-9);
    assert(r.design.components.size() == 6);
    assert(r.bind_report.all_bound);
    assert(r.erc.ok());

    // Real MPNs present in the synthesized design.
    assert(r.design.FindComponent("L1")->mpn == "IND-6R8-5A0");
    assert(r.design.FindComponent("Q1")->mpn == "MOS-30V-8A");
    assert(FileNonEmpty(r.kicad_path));

    // ---- Controller variant: DRIVE not floating, 7 components ----------
    auto rc = pipeline::Run("24V to 3.3V buck at 2 A, 1 MHz",
                            catalog, "build/pipeline_buck_ctrl.kicad_sch", true);
    std::cout << (rc.ok ? "OK" : ("FAIL: " + rc.error)) << "\n";
    assert(rc.ok);
    assert(std::fabs(rc.spec.vin_v - 24.0) < 1e-9);
    assert(std::fabs(rc.spec.fsw_hz - 1e6) < 1e-3);
    assert(rc.design.components.size() == 7);
    assert(rc.design.FindComponent("U1")->mpn == "BUCK-CTRL-1");
    assert(rc.erc.ok());
    assert(FileNonEmpty(rc.kicad_path));

    // ---- A spec that cannot be a buck should fail cleanly --------------
    auto bad = pipeline::Run("boost 5 V to 12 V at 1 A", catalog,
                             "build/should_not_exist.kicad_sch", false);
    assert(!bad.ok); // "boost" topology not recognized here

    std::cout << "\n[PASS] Task 4: end-to-end pipeline test passed.\n";
    return 0;
}
