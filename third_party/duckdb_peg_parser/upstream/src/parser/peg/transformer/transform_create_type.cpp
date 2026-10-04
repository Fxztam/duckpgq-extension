#include "duckpgq/compat/name_metadata.hpp"
#if __has_include("duckdb/common/vector/flat_vector.hpp")
#include "duckdb/common/vector/flat_vector.hpp"
#include "duckdb/common/vector/string_vector.hpp"
#else
#include "duckdb/common/types/vector.hpp"
#endif
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/parsed_data/create_type_info.hpp"

namespace duckdb {
namespace duckpgq_peg {

unique_ptr<CreateStatement> PEGTransformerFactory::TransformCreateTypeStmt(PEGTransformer &transformer,
                                                                           const optional<bool> &if_not_exists,
                                                                           const QualifiedName &qualified_name,
                                                                           unique_ptr<CreateTypeInfo> create_type) {
	auto result = make_uniq<CreateStatement>();
	duckpgq_compat::SetTypeQualifiedName(*create_type, qualified_name);
	create_type->on_conflict =
	    if_not_exists ? OnCreateConflict::IGNORE_ON_CONFLICT : OnCreateConflict::ERROR_ON_CONFLICT;
	result->info = std::move(create_type);
	return result;
}

unique_ptr<CreateTypeInfo> PEGTransformerFactory::TransformCreateTypeFromType(PEGTransformer &transformer,
                                                                              const LogicalType &type) {
	auto result = make_uniq<CreateTypeInfo>();
	result->type = type;
	return result;
}

unique_ptr<CreateTypeInfo>
PEGTransformerFactory::TransformEnumSelectType(PEGTransformer &transformer,
                                               unique_ptr<SelectStatement> select_statement_internal) {
	auto result = make_uniq<CreateTypeInfo>();
	result->query = std::move(select_statement_internal);
	result->type = LogicalType::INVALID;
	return result;
}

unique_ptr<CreateTypeInfo>
PEGTransformerFactory::TransformEnumStringLiteralList(PEGTransformer &transformer,
                                                      const optional<vector<string>> &string_literal) {
	auto result = make_uniq<CreateTypeInfo>();
	idx_t enum_count = string_literal ? string_literal->size() : 0;
	Vector enum_vector(LogicalType::VARCHAR, enum_count);
#if __has_include("duckdb/common/identifier.hpp")
	auto string_data = FlatVector::Writer<string_t>(enum_vector, enum_count);
#else
	auto string_data = FlatVector::GetData<string_t>(enum_vector);
	idx_t index = 0;
#endif
	if (string_literal) {
		for (auto &literal : *string_literal) {
#if __has_include("duckdb/common/identifier.hpp")
			string_data.WriteValue(string_t(literal));
#else
			string_data[index++] = StringVector::AddString(enum_vector, literal);
#endif
		}
	}
	result->type = LogicalType::ENUM(enum_vector, enum_count);
	return result;
}

} // namespace duckpgq_peg
} // namespace duckdb
