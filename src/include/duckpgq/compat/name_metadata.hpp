#pragma once
#include "duckpgq/parser/identifier.hpp"
#include "duckdb/parser/qualified_name.hpp"
#include "duckdb/parser/sql_statement.hpp"
#include "duckdb/parser/parsed_data/create_type_info.hpp"

namespace duckdb {
namespace duckpgq_compat {
inline QualifiedName MakeQualifiedName(vector<Identifier> path, Identifier name) {
    if (path.size() > 2) throw ParserException("DuckPGQ: expected at most catalog.schema.name");
#if __has_include("duckdb/common/identifier.hpp")
    return QualifiedName(std::move(path), std::move(name));
#else
    QualifiedName result;
    result.catalog = path.size() == 2 ? path[0].GetIdentifierName() : INVALID_CATALOG;
    result.schema = path.empty() ? INVALID_SCHEMA : path.back().GetIdentifierName();
    result.name = name.GetIdentifierName();
    return result;
#endif
}
inline QualifiedName MakeQualifiedName(Identifier name) { return MakeQualifiedName({}, std::move(name)); }
inline void SetTypeQualifiedName(CreateTypeInfo &info, const QualifiedName &name) {
#if __has_include("duckdb/common/identifier.hpp")
    info.SetQualifiedName(name);
#else
    info.catalog = name.catalog;
    info.schema = name.schema;
    info.name = name.name;
#endif
}
inline string UseTargetText(const QualifiedName &name) {
#if __has_include("duckdb/common/identifier.hpp")
    const auto &catalog = name.Catalog().GetIdentifierName();
    const auto &schema = name.Schema().GetIdentifierName();
    const auto &value = name.Name().GetIdentifierName();
#else
    const auto &catalog = name.catalog;
    const auto &schema = name.schema;
    const auto &value = name.name;
#endif
    if (!catalog.empty()) throw ParserException("Expected \"USE database\" or \"USE database.schema\"");
    auto quoted = KeywordHelper::WriteOptionallyQuoted(value, '"');
    return schema.empty() ? quoted : KeywordHelper::WriteOptionallyQuoted(schema, '"') + "." + quoted;
}
inline void TransferStatementParameters(SQLStatement &statement, const identifier_map_t<idx_t> &parameters,
                                        bool anonymous) {
    // Empty outer transforms must not erase metadata already attached inside.
    if (!parameters.empty()) {
#if __has_include("duckdb/common/identifier.hpp")
        statement.named_param_map = parameters;
#else
        case_insensitive_map_t<idx_t> converted;
        for (const auto &entry : parameters) converted.emplace(entry.first.GetIdentifierName(), entry.second);
        statement.named_param_map = std::move(converted);
#endif
    }
#if __has_include("duckdb/common/identifier.hpp")
    statement.has_anonymous_parameters = anonymous;
#else
    // 1.5.5 has no separate flag. Anonymous parameters are represented by
    // numbered identifiers in the map and ParameterExpression, as in its parser.
    (void)anonymous;
#endif
}
} // namespace duckpgq_compat
} // namespace duckdb
