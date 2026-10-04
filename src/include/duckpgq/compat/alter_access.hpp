#pragma once
#include "duckpgq/compat/name_metadata.hpp"
#include "duckpgq/compat/function_access.hpp"
#include "duckdb/parser/parsed_data/alter_info.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/tableref/basetableref.hpp"
namespace duckdb {
namespace duckpgq_compat {
#if __has_include("duckdb/common/identifier.hpp")
inline const Identifier &HostName(const Identifier &name) { return name; }
inline const vector<Identifier> &HostNames(const vector<Identifier> &names) { return names; }
inline const auto &ColumnNames(const ColumnRefExpression &expr) { return expr.ColumnNames(); }
inline void SetAlterName(AlterInfo &info, const QualifiedName &name) { info.SetQualifiedName(name); }
inline void SetAlterName(AlterInfo &info, const BaseTableRef &table) { info.SetQualifiedName(table.GetQualifiedName()); }
inline void SetAlterTable(BaseTableRef &table, const AlterEntryData &data) { table.SetQualifiedName(data.GetQualifiedName()); }
#else
inline const string &HostName(const Identifier &name) { return name.GetIdentifierName(); }
inline vector<string> HostNames(const vector<Identifier> &names) { return IdentifiersToStrings(names); }
inline const auto &ColumnNames(const ColumnRefExpression &expr) { return expr.column_names; }
inline void SetAlterName(AlterInfo &info, const QualifiedName &name) {
    info.catalog = name.catalog; info.schema = name.schema; info.name = name.name;
}
inline void SetAlterName(AlterInfo &info, const BaseTableRef &table) {
    info.catalog = table.catalog_name; info.schema = table.schema_name; info.name = table.table_name;
}
inline void SetAlterTable(BaseTableRef &table, const AlterEntryData &data) {
    table.catalog_name = data.catalog; table.schema_name = data.schema; table.table_name = data.name;
}
#endif
} // namespace duckpgq_compat
} // namespace duckdb
