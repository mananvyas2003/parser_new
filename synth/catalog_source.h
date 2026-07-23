#pragma once

// ============================================================================
//  catalog_source.h
//  Abstraction over "where parts come from". The binding/verify loop depends on
//  this interface, not on any concrete storage, so we can swap the in-memory
//  mock for a real SQLite-backed catalog without touching the synthesis logic.
// ============================================================================

#include "parts_catalog.h"

#include <utility>
#include <vector>

namespace cat {

// Returns ranked candidates for a query (best-first).
struct ICatalogSource {
    virtual ~ICatalogSource() = default;
    virtual std::vector<CatalogPart> Find(const CandidateQuery& q) const = 0;
};

// In-memory source backed by a vector (wraps the existing FindCandidates()).
class MockCatalogSource : public ICatalogSource {
public:
    explicit MockCatalogSource(std::vector<CatalogPart> parts)
        : parts_(std::move(parts)) {}

    std::vector<CatalogPart> Find(const CandidateQuery& q) const override {
        return FindCandidates(parts_, q);
    }

private:
    std::vector<CatalogPart> parts_;
};

} // namespace cat
