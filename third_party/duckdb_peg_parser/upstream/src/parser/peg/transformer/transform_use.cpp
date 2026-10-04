#include "duckpgq/compat/name_metadata.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/sql_statement.hpp"

namespace duckdb {
namespace duckpgq_peg {

// UseStatement <- 'USE' UseTarget
unique_ptr<SQLStatement> PEGTransformerFactory::TransformUseStatement(PEGTransformer &transformer,
                                                                      const QualifiedName &use_target) {
	auto value_str = duckpgq_compat::UseTargetText(use_target);

	auto value_expr = make_uniq<ConstantExpression>(Value(value_str));
	return make_uniq<SetVariableStatement>("schema", std::move(value_expr), SetScope::AUTOMATIC);
}

// UseTarget <- UseTargetCatalogSchema / SchemaName / CatalogName
QualifiedName PEGTransformerFactory::TransformSchemaNameAsUseTarget(PEGTransformer &transformer,
                                                                    const Identifier &schema_name) {
	QualifiedName result;
	result = duckpgq_compat::MakeQualifiedName(schema_name);
	return result;
}

QualifiedName PEGTransformerFactory::TransformCatalogNameAsUseTarget(PEGTransformer &transformer,
                                                                     const Identifier &catalog_name) {
	QualifiedName result;
	result = duckpgq_compat::MakeQualifiedName(catalog_name);
	return result;
}

// UseTargetCatalogSchema <- CatalogName '.' ReservedSchemaName DotIdentifier*
QualifiedName
PEGTransformerFactory::TransformUseTargetCatalogSchema(PEGTransformer &transformer, const Identifier &catalog_name,
                                                       const Identifier &reserved_schema_name,
                                                       const optional<vector<Identifier>> &dot_identifier) {
	if (dot_identifier && !dot_identifier->empty()) {
		throw ParserException("Expected \"USE database\" or \"USE database.schema\"");
	}
	auto result = duckpgq_compat::MakeQualifiedName({catalog_name}, reserved_schema_name);
	return result;
}

Identifier PEGTransformerFactory::TransformDotIdentifier(PEGTransformer &transformer, const Identifier &identifier) {
	return identifier;
}
} // namespace duckpgq_peg
} // namespace duckdb
