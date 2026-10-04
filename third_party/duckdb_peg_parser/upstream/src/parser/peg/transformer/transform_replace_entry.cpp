#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/compat/alter_access.hpp"
namespace duckdb {
namespace duckpgq_peg {
case_insensitive_map_t<unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformReplaceEntrySingle(PEGTransformer &transformer,
                                                   pair<string, unique_ptr<ParsedExpression>> replace_entry) {
	case_insensitive_map_t<unique_ptr<ParsedExpression>> entry_map;
	entry_map.insert(std::move(replace_entry));
	return entry_map;
}

case_insensitive_map_t<unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformReplaceEntryList(PEGTransformer &transformer,
                                                 vector<pair<string, unique_ptr<ParsedExpression>>> replace_entry) {
	case_insensitive_map_t<unique_ptr<ParsedExpression>> entry_map;
	for (auto &entry : replace_entry) {
		if (entry_map.find(entry.first) != entry_map.end()) {
			throw ParserException("Duplicate entry \"%s\" in REPLACE list", entry.first);
		}
		entry_map.insert(std::move(entry));
	}
	return entry_map;
}

pair<string, unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformReplaceEntry(PEGTransformer &transformer, unique_ptr<ParsedExpression> expression,
                                             unique_ptr<ParsedExpression> column_reference) {
	if (column_reference->GetExpressionClass() != ExpressionClass::COLUMN_REF) {
		throw InternalException("Expected a column reference in the replace entry");
	}
	auto &col_ref = column_reference->Cast<ColumnRefExpression>();
	auto column_name = col_ref.GetColumnName();
	#if __has_include("duckdb/common/identifier.hpp")
	return make_pair(column_name.GetIdentifierName(), std::move(expression));
#else
	return make_pair(std::move(column_name), std::move(expression));
#endif
}
unique_ptr<ColumnRefExpression> PEGTransformerFactory::TransformTableReservedColumnName(
    PEGTransformer &transformer, const Identifier &table_qualification, const Identifier &reserved_column_name) {
	return make_uniq<ColumnRefExpression>(duckpgq_compat::HostName(reserved_column_name), duckpgq_compat::HostName(table_qualification));
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformTrimExpression(PEGTransformer &transformer,
                                                                            TrimArguments trim_arguments) {
	string function_name = "trim";
	if (trim_arguments.trim_direction) {
		function_name = *trim_arguments.trim_direction;
	}
	return make_uniq<FunctionExpression>(duckpgq_compat::HostName(Identifier(function_name)), std::move(trim_arguments.expressions));
}
} // namespace duckpgq_peg
} // namespace duckdb
