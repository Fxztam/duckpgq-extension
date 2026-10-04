#include "duckpgq/third_party/duckdb_peg_parser/peg/ast/generic_copy_option.hpp"
#include "duckdb/parser/statement/attach_statement.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"

namespace duckdb {
namespace duckpgq_peg {

unique_ptr<SQLStatement> PEGTransformerFactory::TransformAttachStatement(
    PEGTransformer &transformer, const optional<bool> &or_replace, const optional<bool> &if_not_exists,
    const bool &has_result, unique_ptr<ParsedExpression> database_path, const optional<Identifier> &attach_alias,
    const optional<vector<GenericCopyOption>> &attach_options) {
	auto result = make_uniq<AttachStatement>();
	auto info = make_uniq<AttachInfo>();

	if (or_replace && if_not_exists) {
		throw ParserException("Cannot specify both OR REPLACE and IF NOT EXISTS at the same time");
	}

	if (or_replace) {
		info->on_conflict = OnCreateConflict::REPLACE_ON_CONFLICT;
	} else if (if_not_exists) {
		info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	} else {
		info->on_conflict = OnCreateConflict::ERROR_ON_CONFLICT;
	}

#if __has_include("duckdb/common/identifier.hpp")
	info->parsed_path = std::move(database_path);
	if (attach_alias) {
		info->name = Identifier(*attach_alias);
	}
#else
	if (!database_path || database_path->GetExpressionClass() != ExpressionClass::CONSTANT) {
		throw ParserException("ATTACH requires a string literal path in canonical DuckDB 1.5.5");
	}
	const auto &path = database_path->Cast<ConstantExpression>().value;
	if (path.IsNull() || path.type().id() != LogicalTypeId::VARCHAR) {
		throw ParserException("ATTACH requires a non-null string path in canonical DuckDB 1.5.5");
	}
	info->path = path.GetValue<string>();
	if (attach_alias) info->name = attach_alias->GetIdentifierName();
#endif
	result->info = std::move(info);
	if (!attach_options) {
		return std::move(result);
	}

	auto &attach_info = *result->info;
	SplitGenericOptions(*attach_options, attach_info.parsed_options, attach_info.options, "ATTACH");
	return std::move(result);
}

Identifier PEGTransformerFactory::TransformAttachAlias(PEGTransformer &transformer, const Identifier &col_id) {
	return Identifier(col_id);
}

vector<GenericCopyOption>
PEGTransformerFactory::TransformAttachOptions(PEGTransformer &transformer,
                                              const vector<GenericCopyOption> &generic_copy_option_list) {
	return generic_copy_option_list;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformDatabasePath(PEGTransformer &transformer,
                                                                          unique_ptr<ParsedExpression> expression) {
	return expression;
}

} // namespace duckpgq_peg
} // namespace duckdb
