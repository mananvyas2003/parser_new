#pragma once

// ============================================================================
//  synth_pipeline.h
//  End-to-end driver that runs the whole circuit-synthesis compiler in one call:
//
//    prompt -> spec -> topology -> instantiate -> size (physics)
//           -> bind real parts (verify/retry) -> validate + ERC
//           -> placement (geometry) -> emit .kicad_sch
//
//  It depends only on the ICatalogSource interface, so it works with either the
//  mock catalog or the real SQLite one.
// ============================================================================

#include "electrical_ir.h"
#include "ir_validate.h"
#include "ir_erc.h"
#include "prompt_parser.h"
#include "topology_library.h"
#include "topology_instantiate.h"
#include "buck_sizing.h"
#include "part_binding.h"
#include "catalog_source.h"
#include "placement.h"
#include "kicad_writer.h"

#include <string>

namespace pipeline {

struct PipelineResult {
    bool                 ok = false;
    std::string          error;

    std::string          prompt;
    phys::BuckSpec       spec;
    phys::SizingResult   sizing;
    ir::Design           design;
    partbind::BindReport     bind_report;
    ir::ValidationReport validation;
    erc::ErcReport       erc;
    std::string          kicad_path;
};

inline PipelineResult Run(const std::string& prompt,
                          const cat::ICatalogSource& catalog,
                          const std::string& out_path,
                          bool with_controller = false) {
    PipelineResult r;
    r.prompt = prompt;

    // 1. Prompt -> spec
    prompt::ParseResult parsed = prompt::ParseBuckPrompt(prompt);
    if (!parsed.ok) {
        r.error = "prompt parse failed: " + parsed.error;
        return r;
    }
    r.spec = parsed.spec;

    // 2. Topology -> instantiate
    topo::Topology t = with_controller ? topo::BuildBuckWithControllerTopology()
                                       : topo::BuildBuckTopology();
    r.design = topo::Instantiate(t);

    // 3. Physics sizing
    r.sizing = phys::SizeBuck(r.spec, &r.design);
    if (!r.sizing.ok) {
        r.error = "sizing failed: " + r.sizing.error;
        return r;
    }

    // 4. Bind real parts (verify/retry)
    auto req = partbind::ComputeBuckRequirements(r.spec, r.sizing);
    r.bind_report = partbind::BindDesign(r.design, req, catalog);
    if (!r.bind_report.all_bound) {
        r.error = "part binding failed (a required component had no valid part)";
        return r;
    }

    // 5. Validate + ERC on the verified IR
    r.validation = ir::Validate(r.design);
    r.erc = erc::RunErc(r.design);
    if (!r.validation.ok() || !r.erc.ok()) {
        r.error = "verified IR failed validation/ERC";
        return r;
    }

    // 6. Geometry + schematic serialization
    place::Placement plan = place::PlaceDesign(r.design);
    if (!kicad::WriteKicadSch(r.design, plan, out_path)) {
        r.error = "failed to write schematic to " + out_path;
        return r;
    }
    r.kicad_path = out_path;

    r.ok = true;
    return r;
}

} // namespace pipeline
