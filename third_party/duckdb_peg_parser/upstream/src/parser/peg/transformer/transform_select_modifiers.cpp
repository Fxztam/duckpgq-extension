#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/compat/alter_access.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
namespace duckdb {
namespace duckpgq_peg {
unique_ptr<TableRef> PEGTransformerFactory::TransformTableFunctionLateralOpt(
    PEGTransformer &transformer, const optional<bool> &lateral, const QualifiedName &qualified_table_function,
    vector<FunctionArgument> table_function_arguments, const optional<bool> &with_ordinality,
    const optional<TableAlias> &table_alias) {
	auto result = make_uniq<TableFunctionRef>();

	result->with_ordinality =
	    with_ordinality.value_or(false) ? OrdinalityType::WITH_ORDINALITY : OrdinalityType::WITHOUT_ORDINALITY;
	result->function = BuildFunctionExpression(qualified_table_function, std::move(table_function_arguments));
	if (table_alias) {
		result->alias = duckpgq_compat::HostName(table_alias->name);
		result->column_name_alias = duckpgq_compat::HostNames(table_alias->column_name_alias);
	}
	return std::move(result);
}

unique_ptr<TableRef> PEGTransformerFactory::TransformTableFunctionAliasColon(
    PEGTransformer &transformer, const Identifier &table_alias_colon, const QualifiedName &qualified_table_function,
    vector<FunctionArgument> table_function_arguments, const optional<bool> &with_ordinality,
    optional<unique_ptr<SampleOptions>> sample_clause) {
	auto result = make_uniq<TableFunctionRef>();
	result->with_ordinality =
	    with_ordinality.value_or(false) ? OrdinalityType::WITH_ORDINALITY : OrdinalityType::WITHOUT_ORDINALITY;
	result->function = BuildFunctionExpression(qualified_table_function, std::move(table_function_arguments));
	result->alias = duckpgq_compat::HostName(table_alias_colon);
	if (sample_clause) {
		result->sample = std::move(*sample_clause);
	}
	return std::move(result);
}

string PEGTransformerFactory::TransformVersionAtUnit(PEGTransformer &transformer) {
	return "VERSION";
}

string PEGTransformerFactory::TransformTimestampAtUnit(PEGTransformer &transformer) {
	return "TIMESTAMP";
}

OrderType PEGTransformerFactory::TransformDescendingOrder(PEGTransformer &transformer) {
	return OrderType::DESCENDING;
}

OrderType PEGTransformerFactory::TransformAscendingOrder(PEGTransformer &transformer) {
	return OrderType::ASCENDING;
}

OrderByNullType PEGTransformerFactory::TransformNullsFirst(PEGTransformer &transformer) {
	return OrderByNullType::NULLS_FIRST;
}

OrderByNullType PEGTransformerFactory::TransformNullsLast(PEGTransformer &transformer) {
	return OrderByNullType::NULLS_LAST;
}

unique_ptr<ResultModifier> PEGTransformerFactory::VerifyLimitOffset(LimitPercentResult &limit,
                                                                    LimitPercentResult &offset) {
	if (offset.is_percent) {
		throw ParserException("Percentage for offsets are not supported.");
	}
#if !__has_include("duckdb/common/identifier.hpp")
	if (limit.is_percent) {
		if (!limit.expression && !offset.expression) return nullptr;
		auto result = make_uniq<LimitPercentModifier>();
		result->limit = std::move(limit.expression);
		result->offset = std::move(offset.expression);
		return result;
	}
#endif
	auto result = make_uniq<LimitModifier>();
#if __has_include("duckdb/common/identifier.hpp")
	result->limit_type = limit.is_percent ? LimitValueType::PERCENTAGE : LimitValueType::ROW_COUNT;
#endif
	if (limit.expression) {
		result->limit = std::move(limit.expression);
	}
	if (offset.expression) {
		result->offset = std::move(offset.expression);
	}
	if (!result->limit && !result->offset) {
		return nullptr;
	}
	return std::move(result);
}

LimitPercentResult PEGTransformerFactory::TransformLimitExpression(PEGTransformer &transformer,
                                                                   unique_ptr<ParsedExpression> expression,
                                                                   const bool &has_result) {
	LimitPercentResult result;
	result.expression = std::move(expression);
	result.is_percent = has_result;
	return result;
}
} // namespace duckpgq_peg
} // namespace duckdb
