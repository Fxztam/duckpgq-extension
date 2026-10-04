#include "duckdb/common/enums/date_part_specifier.hpp"
#include "duckdb/common/enums/subquery_type.hpp"
#include "duckdb/parser/expression/subquery_expression.hpp"
#include "duckdb/optimizer/rule/date_trunc_simplification.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/expression/comparison_expression.hpp"
#include "duckdb/parser/expression/between_expression.hpp"
#include "duckdb/parser/expression/operator_expression.hpp"
#include "duckdb/parser/expression/cast_expression.hpp"
#include "duckdb/parser/expression/lambda_expression.hpp"
#include "duckdb/parser/expression/positional_reference_expression.hpp"
#include "duckdb/parser/expression/conjunction_expression.hpp"
#include "duckdb/parser/expression/default_expression.hpp"
#include "duckdb/parser/result_modifier.hpp"
#include "duckdb/parser/expression/collate_expression.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
#include "duckdb/parser/tableref/emptytableref.hpp"
#include "duckdb/parser/parsed_expression_iterator.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckpgq/compat/expression_access.hpp"
#include "duckpgq/compat/window_function.hpp"
#include "duckpgq/compat/function_access.hpp"
#include "duckpgq/compat/alter_access.hpp"

#include "duckpgq/compat/name_metadata.hpp"

namespace duckdb {
namespace duckpgq_peg {
unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformStructExpression(PEGTransformer &transformer,
                                                 optional<vector<FunctionArgument>> struct_field) {
	// {} produces an empty STRUCT, {'a': 1, ...} a named STRUCT - both via struct_pack
	vector<FunctionArgument> fields;
	if (struct_field) {
		fields = std::move(*struct_field);
	}
	return BuildFunctionExpression(duckpgq_compat::MakeQualifiedName(Identifier("struct_pack")), std::move(fields));
}

FunctionArgument PEGTransformerFactory::TransformStructField(PEGTransformer &transformer,
                                                             const Identifier &col_id_or_string,
                                                             unique_ptr<ParsedExpression> expression) {
	auto alias = col_id_or_string.GetIdentifierName();
	expression->SetAlias(duckpgq_compat::HostName(Identifier(alias)));

	return FunctionArgument(Identifier(std::move(alias)), std::move(expression));
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformIsExpression(PEGTransformer &transformer,
                                             unique_ptr<ParsedExpression> is_distinct_from_expression,
                                             optional<vector<unique_ptr<ParsedExpression>>> is_test) {
	auto expr = std::move(is_distinct_from_expression);
	if (!is_test) {
		return expr;
	}
	for (auto &is_expr : *is_test) {
		if (is_expr->GetExpressionClass() == ExpressionClass::COMPARISON) {
			auto compare_expr = unique_ptr_cast<ParsedExpression, ComparisonExpression>(std::move(is_expr));
			#if __has_include("duckdb/common/identifier.hpp")
			compare_expr->LeftMutable() = make_uniq<CastExpression>(LogicalType::BOOLEAN, std::move(expr));
#else
			compare_expr->left = make_uniq<CastExpression>(LogicalType::BOOLEAN, std::move(expr));
#endif
			expr = std::move(compare_expr);
		} else if (is_expr->GetExpressionClass() == ExpressionClass::OPERATOR) {
			auto operator_expr = unique_ptr_cast<ParsedExpression, OperatorExpression>(std::move(is_expr));
			auto &children = duckpgq_compat::OperatorChildren(*operator_expr);
			children.insert(children.begin(), std::move(expr));
			expr = std::move(operator_expr);
		} else {
			throw InternalException("Unexpected expression encountered in IsExpression: %s",
			                        ExpressionClassToString(is_expr->GetExpressionClass()));
		}
	}
	return expr;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformIsLiteral(PEGTransformer &transformer,
                                                                       const bool &has_result,
                                                                       const Value &is_literal_value) {
	if (is_literal_value.IsNull()) {
		auto expr_type = has_result ? ExpressionType::OPERATOR_IS_NOT_NULL : ExpressionType::OPERATOR_IS_NULL;
		return make_uniq<OperatorExpression>(expr_type, nullptr);
	}
	auto expr_type = has_result ? ExpressionType::COMPARE_DISTINCT_FROM : ExpressionType::COMPARE_NOT_DISTINCT_FROM;
	return make_uniq<ComparisonExpression>(expr_type, nullptr, make_uniq<ConstantExpression>(is_literal_value));
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformNotNullKeyword(PEGTransformer &transformer) {
	return make_uniq<OperatorExpression>(ExpressionType::OPERATOR_IS_NOT_NULL, nullptr);
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformNotNullOperator(PEGTransformer &transformer) {
	return make_uniq<OperatorExpression>(ExpressionType::OPERATOR_IS_NOT_NULL, nullptr);
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformIsNullOperator(PEGTransformer &transformer) {
	return make_uniq<OperatorExpression>(ExpressionType::OPERATOR_IS_NULL, nullptr);
}

bool PEGTransformerFactory::TransformNotKeyword(PEGTransformer &transformer) {
	return true;
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformIsDistinctFromExpression(PEGTransformer &transformer,
                                                         unique_ptr<ParsedExpression> comparison_expression,
                                                         optional<vector<IsDistinctFromTail>> is_distinct_from_tail) {
	auto expr = std::move(comparison_expression);
	if (!is_distinct_from_tail) {
		return expr;
	}
	for (auto &is_distinct : *is_distinct_from_tail) {
		auto distinct_operator = make_uniq<ComparisonExpression>(is_distinct.comparison_type, std::move(expr),
		                                                         std::move(is_distinct.expression));
		expr = std::move(distinct_operator);
	}
	return expr;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformComparisonExpression(
    PEGTransformer &transformer, unique_ptr<ParsedExpression> between_in_like_expression,
    optional<vector<ComparisonExpressionTail>> comparison_expression_tail) {
	auto expr = std::move(between_in_like_expression);
	if (!comparison_expression_tail) {
		return expr;
	}
	auto cmp_depth_guard = transformer.StackCheck(comparison_expression_tail->size());
	for (auto &comparison_expr : *comparison_expression_tail) {
		auto right_expr = std::move(comparison_expr.expression);
		for (idx_t i = 0; i < comparison_expr.not_keywords.size(); i++) {
			vector<unique_ptr<ParsedExpression>> inner_list_children;
			inner_list_children.push_back(std::move(right_expr));
			right_expr = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(inner_list_children));
		}
		expr = make_uniq<ComparisonExpression>(comparison_expr.comparison_type, std::move(expr), std::move(right_expr));
	}
	return expr;
}

IsDistinctFromTail
PEGTransformerFactory::TransformIsDistinctFromTail(PEGTransformer &transformer,
                                                   const ExpressionType &is_distinct_from_op,
                                                   unique_ptr<ParsedExpression> comparison_expression) {
	return {is_distinct_from_op, std::move(comparison_expression)};
}

ComparisonExpressionTail PEGTransformerFactory::TransformComparisonExpressionTail(
    PEGTransformer &transformer, const ExpressionType &comparison_operator, optional<vector<bool>> not_expression,
    unique_ptr<ParsedExpression> between_in_like_expression) {
	ComparisonExpressionTail result;
	result.comparison_type = comparison_operator;
	if (not_expression) {
		result.not_keywords = std::move(*not_expression);
	}
	result.expression = std::move(between_in_like_expression);
	return result;
}

ExpressionType PEGTransformerFactory::TransformOperatorEqual(PEGTransformer &transformer) {
	return ExpressionType::COMPARE_EQUAL;
}

ExpressionType PEGTransformerFactory::TransformOperatorNotEqual(PEGTransformer &transformer) {
	return ExpressionType::COMPARE_NOTEQUAL;
}

ExpressionType PEGTransformerFactory::TransformOperatorLessThan(PEGTransformer &transformer) {
	return ExpressionType::COMPARE_LESSTHAN;
}

ExpressionType PEGTransformerFactory::TransformOperatorGreaterThan(PEGTransformer &transformer) {
	return ExpressionType::COMPARE_GREATERTHAN;
}

ExpressionType PEGTransformerFactory::TransformOperatorLessThanEquals(PEGTransformer &transformer) {
	return ExpressionType::COMPARE_LESSTHANOREQUALTO;
}

ExpressionType PEGTransformerFactory::TransformOperatorGreaterThanEquals(PEGTransformer &transformer) {
	return ExpressionType::COMPARE_GREATERTHANOREQUALTO;
}
} // namespace duckpgq_peg
} // namespace duckdb
