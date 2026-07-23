// ============================================================================
//  sqlite_catalog_test.cpp
//  Task 1 test: bind the buck using a REAL SQLite database (in-memory), seeded
//  with the catalog. Proves the verify/retry loop works identically against SQL
//  ranked candidates, and that the extended Parts schema (with ratings) drives
//  the same reject-and-retry decisions as the mock.
//
//  Build:
//    g++ -std=c++17 -I synth synth/tests/sqlite_catalog_test.cpp -lsqlite3 -o build/sqlite_catalog_test
//  Run:
//    ./build/sqlite_catalog_test
// ============================================================================

#include "electrical_ir.h"
#include "ir_validate.h"
#include "topology_library.h"
#include "topology_instantiate.h"
#include "buck_sizing.h"
#include "parts_catalog.h"
#include "part_binding.h"
#include "catalog_source.h"
#include "sqlite_catalog.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace ir;

static const partbind::BindOutcome* ForRole(const partbind::BindReport& r, const std::string& role) {
    for (const auto& o : r.outcomes) if (o.role == role) return &o;
    return nullptr;
}

static bool RejectedContains(const partbind::BindOutcome& o, const std::string& mpn) {
    for (const auto& rc : o.rejected) if (rc.mpn == mpn) return true;
    return false;
}

int main() {
    // ---- Real SQLite catalog (in-memory), seeded from the catalog -------
    cat::SqliteCatalogSource db(":memory:");
    assert(db.ok() && "failed to open in-memory sqlite db");
    db.SeedFrom(cat::BuildMockCatalog());

    // Sanity: a direct ranked query returns the two 6.8uH inductors first.
    cat::CandidateQuery q;
    q.type = PartType::Inductor;
    q.has_value_target = true;
    q.value_target = 6.48e-6;
    q.value_tol = 0.30;
    auto inductors = db.Find(q);
    std::cout << "SQL inductor candidates (ranked):\n";
    for (const auto& p : inductors)
        std::cout << "  " << p.mpn << "  " << p.value * 1e6 << "uH  Isat=" << p.i_rating << "A\n";
    assert(inductors.size() >= 2);
    assert(inductors[0].mpn == "IND-6R8-3A0"); // nearest value, cheapest Isat first

    // ---- Full pipeline, binding through the SQLite source ---------------
    Design d = topo::Instantiate(topo::BuildBuckTopology());
    phys::BuckSpec spec;
    spec.vin_v = 12.0; spec.vout_v = 5.0; spec.iout_a = 3.0;
    spec.fsw_hz = 500e3; spec.ripple_current_ratio = 0.30;
    phys::SizeBuck(spec, &d);
    auto req = partbind::ComputeBuckRequirements(spec, phys::SizeBuck(spec, nullptr));

    partbind::BindReport report = partbind::BindDesign(d, req, db); // <-- ICatalogSource

    assert(report.all_bound);

    const auto* L = ForRole(report, "OUTPUT_INDUCTOR");
    assert(L && L->chosen_mpn == "IND-6R8-5A0" && RejectedContains(*L, "IND-6R8-3A0"));
    const auto* Cout = ForRole(report, "OUTPUT_CAP");
    assert(Cout && Cout->chosen_mpn == "CAP-4U7-25V" && RejectedContains(*Cout, "CAP-4U7-6V3"));
    const auto* Cin = ForRole(report, "INPUT_CAP");
    assert(Cin && Cin->chosen_mpn == "CAP-6U8-16V" && RejectedContains(*Cin, "CAP-6U8-10V"));
    const auto* Q = ForRole(report, "HS_SWITCH");
    assert(Q && Q->chosen_mpn == "MOS-30V-8A" && RejectedContains(*Q, "MOS-20V-4A"));
    const auto* Dio = ForRole(report, "FREEWHEEL_DIODE");
    assert(Dio && Dio->chosen_mpn == "DIO-40V-5A" && RejectedContains(*Dio, "DIO-20V-2A"));

    assert(Validate(d).ok());

    std::cout << "\n[PASS] Task 1: SQLite-backed catalog binding test passed.\n";
    return 0;
}
