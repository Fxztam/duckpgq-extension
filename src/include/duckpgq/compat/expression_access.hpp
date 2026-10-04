#pragma once
#include "duckpgq/compat/name_metadata.hpp"
#include "duckdb/parser/tableref/basetableref.hpp"

namespace duckdb {
namespace duckpgq_compat {
// Qualification is structural: dots inside quoted identifiers are not separators.
inline unique_ptr<BaseTableRef> TableFromNames(vector<Identifier> names) {
    if (names.empty() || names.size() > 3) {
        throw ParserException("DuckPGQ: expected table, schema.table or catalog.schema.table");
    }
    auto name = names.back();
    names.pop_back();
    auto qualified = MakeQualifiedName(std::move(names), std::move(name));
    auto result = make_uniq<BaseTableRef>();
#if __has_include("duckdb/common/identifier.hpp")
    result->SetQualifiedName(qualified);
#else
    result->catalog_name = qualified.catalog;
    result->schema_name = qualified.schema;
    result->table_name = qualified.name;
#endif
    return result;
}
} // namespace duckpgq_compat
} // namespace duckdb
