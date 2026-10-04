#include "duckdb/parser/parsed_data/create_schema_info.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"

namespace duckdb {
namespace duckpgq_peg {
unique_ptr<CreateStatement> PEGTransformerFactory::TransformCreateSchemaStmt(PEGTransformer &transformer,
                                                                             const optional<bool> &if_not_exists,
                                                                             const QualifiedName &qualified_name) {
	auto result = make_uniq<CreateStatement>();
	auto info = make_uniq<CreateSchemaInfo>();
	info->on_conflict = if_not_exists ? OnCreateConflict::IGNORE_ON_CONFLICT : OnCreateConflict::ERROR_ON_CONFLICT;
	// Store the full dotted path, with an empty trailing name so the new schema lands in the Schema() slot (keeping
	// catalog/schema serialization correct). The leading components are resolved into a catalog + parent-schema chain
	// during binding (see Binder::BindCreateSchema).
#if __has_include("duckdb/common/identifier.hpp")
	info->SetQualifiedName(QualifiedName(qualified_name.Path(), Identifier()));
#else
	if (!qualified_name.catalog.empty()) {
		throw ParserException("Nested schemas are not supported by canonical DuckDB 1.5.5");
	}
	info->catalog = qualified_name.schema;
	info->schema = qualified_name.name;
#endif

	result->info = std::move(info);
	return result;
}

} // namespace duckpgq_peg
} // namespace duckdb
