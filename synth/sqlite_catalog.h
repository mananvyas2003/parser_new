#pragma once

// ============================================================================
//  sqlite_catalog.h
//  A real SQLite-backed ICatalogSource. This replaces the in-memory mock with
//  an actual database query, and -- importantly -- extends the original DB
//  Parts schema (mpn,type,value,package) with the RATINGS columns the
//  verify/retry loop needs (v_rating, i_rating, esr_ohms, power_rating_w).
//
//  The SQL mirrors DB_FindClosestPart from the PCB-AI-Database repo, but:
//    - selects ratings too, and
//    - returns RANKED candidates (no LIMIT 1), so the loop can reject & retry.
//
//  Requires: -lsqlite3
// ============================================================================

#include "catalog_source.h"

#include <sqlite3.h>

#include <string>
#include <vector>

namespace cat {

class SqliteCatalogSource : public ICatalogSource {
public:
    explicit SqliteCatalogSource(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
            db_ = nullptr;
            return;
        }
        CreateSchema();
    }

    ~SqliteCatalogSource() override {
        if (db_) sqlite3_close(db_);
    }

    bool ok() const { return db_ != nullptr; }

    void CreateSchema() {
        const char* sql =
            "CREATE TABLE IF NOT EXISTS Parts ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " mpn TEXT UNIQUE NOT NULL,"
            " type INTEGER NOT NULL,"
            " value REAL NOT NULL,"
            " package TEXT NOT NULL,"
            " v_rating REAL DEFAULT 0,"
            " i_rating REAL DEFAULT 0,"
            " esr_ohms REAL DEFAULT 0,"
            " power_rating_w REAL DEFAULT 0);";
        sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
        sqlite3_exec(db_,
            "CREATE INDEX IF NOT EXISTS idx_parts_type_value ON Parts(type,value);",
            nullptr, nullptr, nullptr);
    }

    bool InsertPart(const CatalogPart& p) {
        const char* sql =
            "INSERT OR IGNORE INTO Parts"
            " (mpn,type,value,package,v_rating,i_rating,esr_ohms,power_rating_w)"
            " VALUES (?,?,?,?,?,?,?,?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_text(stmt, 1, p.mpn.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, static_cast<int>(p.type));
        sqlite3_bind_double(stmt, 3, p.value);
        sqlite3_bind_text(stmt, 4, p.package.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 5, p.v_rating);
        sqlite3_bind_double(stmt, 6, p.i_rating);
        sqlite3_bind_double(stmt, 7, p.esr_ohms);
        sqlite3_bind_double(stmt, 8, p.power_rating_w);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    void SeedFrom(const std::vector<CatalogPart>& parts) {
        sqlite3_exec(db_, "BEGIN;", nullptr, nullptr, nullptr);
        for (const auto& p : parts) InsertPart(p);
        sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    }

    std::vector<CatalogPart> Find(const CandidateQuery& q) const override {
        std::vector<CatalogPart> out;
        if (!db_) return out;

        std::string sql =
            "SELECT mpn,type,value,package,v_rating,i_rating,esr_ohms,power_rating_w"
            " FROM Parts WHERE type = ?";
        if (q.has_value_target) {
            sql += " AND value BETWEEN ? AND ? ORDER BY ABS(value - ?), i_rating, v_rating;";
        } else {
            sql += " ORDER BY i_rating, v_rating;";
        }

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            return out;
        }

        int idx = 1;
        sqlite3_bind_int(stmt, idx++, static_cast<int>(q.type));
        if (q.has_value_target) {
            const double lo = q.value_target * (1.0 - q.value_tol);
            const double hi = q.value_target * (1.0 + q.value_tol);
            sqlite3_bind_double(stmt, idx++, lo);
            sqlite3_bind_double(stmt, idx++, hi);
            sqlite3_bind_double(stmt, idx++, q.value_target);
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            CatalogPart p;
            p.mpn     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            p.type    = static_cast<ir::PartType>(sqlite3_column_int(stmt, 1));
            p.value   = sqlite3_column_double(stmt, 2);
            p.package = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            p.v_rating       = sqlite3_column_double(stmt, 4);
            p.i_rating       = sqlite3_column_double(stmt, 5);
            p.esr_ohms       = sqlite3_column_double(stmt, 6);
            p.power_rating_w = sqlite3_column_double(stmt, 7);
            out.push_back(std::move(p));
        }
        sqlite3_finalize(stmt);
        return out;
    }

private:
    sqlite3* db_ = nullptr;
};

} // namespace cat
