// ============================================================================
//  synth_api.cpp
//  One-shot JSON API for the synthesis pipeline (stdout).
//  Usage: synth_api.exe [--controller] "prompt text"
// ============================================================================

#include "parts_catalog.h"
#include "catalog_source.h"
#include "synth_pipeline.h"
#include "ir_json.h"

#include <iostream>
#include <sstream>
#include <string>

static std::string Esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else o += c;
    }
    return o;
}

int main(int argc, char** argv) {
    bool controller = false;
    std::string prompt;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--controller") controller = true;
        else prompt = a;
    }
    if (prompt.empty())
        prompt = "design a 12 V to 5 V buck converter at 3 A";

    cat::MockCatalogSource catalog(cat::BuildMockCatalog());
    auto r = pipeline::Run(prompt, catalog, "build/demo_output.kicad_sch", controller);

    if (!r.ok) {
        std::cout << "{\"ok\":false,\"error\":\"" << Esc(r.error) << "\"}\n";
        return 1;
    }

    ir::WriteJson(r.design, "build/demo_netlist.json");

    std::ostringstream bind;
    for (const auto& out : r.bind_report.outcomes) {
        bind << out.ref << " (" << out.role << "): ";
        if (out.skipped) bind << "SKIPPED\n";
        else if (out.bound) bind << "BOUND -> " << out.chosen_mpn << "\n";
        else bind << "FAILED\n";
        for (const auto& rc : out.rejected)
            bind << "    rejected " << rc.mpn << " : " << rc.reason << "\n";
    }

    std::ostringstream nets;
    std::vector<std::vector<std::string>> terminals(r.design.nodes.size());
    for (const auto& c : r.design.components)
        for (const auto& p : c.pins)
            if (p.node != ir::INVALID_NODE && p.node < terminals.size())
                terminals[p.node].push_back(c.ref + "." + p.name);
    for (size_t i = 0; i < r.design.nodes.size(); ++i) {
        nets << r.design.nodes[i].name << ": ";
        for (size_t j = 0; j < terminals[i].size(); ++j) {
            if (j) nets << ", ";
            nets << terminals[i][j];
        }
        nets << "\n";
    }

    std::cout << "{\n";
    std::cout << "  \"ok\": true,\n";
    std::cout << "  \"topology\": \"" << Esc(r.design.topology) << "\",\n";
    std::cout << "  \"kicad_path\": \"" << Esc(r.kicad_path) << "\",\n";
    std::cout << "  \"erc_ok\": " << (r.erc.ok() ? "true" : "false") << ",\n";
    std::cout << "  \"spec\": {\"vin_v\":" << r.spec.vin_v
              << ",\"vout_v\":" << r.spec.vout_v
              << ",\"iout_a\":" << r.spec.iout_a
              << ",\"fsw_hz\":" << r.spec.fsw_hz << "},\n";
    std::cout << "  \"sizing\": {\"duty\":" << r.sizing.duty
              << ",\"delta_il\":" << r.sizing.delta_il
              << ",\"il_peak\":" << r.sizing.il_peak
              << ",\"l_henries\":" << r.sizing.l_henries
              << ",\"cin_farads\":" << r.sizing.cin_farads
              << ",\"cout_farads\":" << r.sizing.cout_farads << "},\n";

    std::cout << "  \"nodes\": [";
    for (size_t i = 0; i < r.design.nodes.size(); ++i) {
        if (i) std::cout << ",";
        std::cout << "\"" << Esc(r.design.nodes[i].name) << "\"";
    }
    std::cout << "],\n";

    std::cout << "  \"components\": [\n";
    for (size_t i = 0; i < r.design.components.size(); ++i) {
        const auto& c = r.design.components[i];
        std::cout << "    {\"ref\":\"" << Esc(c.ref) << "\","
                  << "\"role\":\"" << Esc(c.role) << "\","
                  << "\"mpn\":" << (c.mpn.empty() ? "null" : ("\"" + Esc(c.mpn) + "\"")) << ","
                  << "\"value\":" << c.value << ","
                  << "\"package\":\"" << Esc(c.package) << "\"}"
                  << (i + 1 < r.design.components.size() ? "," : "") << "\n";
    }
    std::cout << "  ],\n";
    std::cout << "  \"bind_trace\": \"" << Esc(bind.str()) << "\",\n";
    std::cout << "  \"nets_text\": \"" << Esc(nets.str()) << "\",\n";
    std::cout << "  \"netlist\": " << ir::ToJson(r.design) << "\n";
    std::cout << "}\n";
    return 0;
}
