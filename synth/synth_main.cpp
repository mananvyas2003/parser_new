// ============================================================================
//  synth_main.cpp
//  CLI entry point for the circuit-synthesis compiler. Give it a prompt; it
//  runs the full pipeline and emits a .kicad_sch.
//
//  Build:
//    g++ -std=c++17 -I synth synth/synth_main.cpp -o build/synth
//  Run:
//    ./build/synth "design a 12 V to 5 V buck converter at 3 A"
//    ./build/synth --controller "24V to 3.3V buck at 2A, 1 MHz"
// ============================================================================

#include "parts_catalog.h"
#include "catalog_source.h"
#include "synth_pipeline.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    bool with_controller = false;
    std::string prompt;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--controller") with_controller = true;
        else prompt = a;
    }
    if (prompt.empty()) {
        prompt = "design a 12 V to 5 V buck converter at 3 A";
    }

    cat::MockCatalogSource catalog(cat::BuildMockCatalog());
    pipeline::PipelineResult r =
        pipeline::Run(prompt, catalog, "build/output.kicad_sch", with_controller);

    std::cout << "Prompt : " << r.prompt << "\n";
    if (!r.ok) {
        std::cout << "FAILED : " << r.error << "\n";
        return 1;
    }

    std::cout << "Spec   : " << r.spec.vin_v << "V -> " << r.spec.vout_v
              << "V @ " << r.spec.iout_a << "A, " << r.spec.fsw_hz / 1e3 << " kHz\n";
    std::cout << "Sizing : L=" << r.sizing.l_henries * 1e6 << "uH  Cin="
              << r.sizing.cin_farads * 1e6 << "uF  Cout=" << r.sizing.cout_farads * 1e6
              << "uF\n";
    std::cout << "Design : " << r.design.components.size() << " components, "
              << r.design.nodes.size() << " nets\n";
    std::cout << "Parts  :\n";
    for (const auto& c : r.design.components) {
        std::cout << "    " << c.ref << " (" << c.role << ") = "
                  << (c.IsBound() ? c.mpn : "<virtual/unbound>") << "\n";
    }
    std::cout << "ERC    : " << (r.erc.ok() ? "OK" : "FAILED") << "\n";
    std::cout << "Output : " << r.kicad_path << "\n";
    return 0;
}
