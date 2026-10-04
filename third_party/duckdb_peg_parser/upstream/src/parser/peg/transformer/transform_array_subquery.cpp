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

namespace duckdb {
namespace duckpgq_peg {
#if __has_include("duckdb/common/identifier.hpp")
unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformArrayParensSelect(PEGTransformer &transformer,
                                                  unique_ptr<SelectStatement> select_statement_internal) {
	auto subquery_expr = make_uniq<SubqueryExpression>();
	subquery_expr->SubqueryMutable() = std::move(select_statement_internal);
	// ARRAY expression
	// wrap subquery into
	// "SELECT CASE WHEN ARRAY_AGG(col) IS NULL THEN [] ELSE ARRAY_AGG(col) END FROM (...) tbl"
	auto select_node = make_uniq<SelectNode>();
	unique_ptr<ParsedExpression> array_agg_child;
	optional_ptr<SelectNode> sub_select;
	if (subquery_expr->Subquery()->node->type == QueryNodeType::SELECT_NODE) {
		// easy case - subquery is a SELECT
		sub_select = subquery_expr->Subquery()->node->Cast<SelectNode>();
		if (sub_select->select_list.size() != 1) {
			throw BinderException(*subquery_expr, "Subquery returns %zu columns - expected 1",
			                      sub_select->select_list.size());
		}
		array_agg_child = make_uniq<PositionalReferenceExpression>(1ULL);
	} else {
		// subquery is not a SELECT but a UNION or CTE
		// we can still support this but it is more challenging since we can't push columns for the ORDER BY
		auto columns_star = make_uniq<StarExpression>();
		columns_star->IsColumnsMutable() = true;
		array_agg_child = std::move(columns_star);
	}

	// ARRAY_AGG(COLUMNS(*))
	vector<unique_ptr<ParsedExpression>> children;
	children.push_back(std::move(array_agg_child));
	auto aggr = make_uniq<FunctionExpression>("array_agg", std::move(children));
	// push ORDER BY modifiers into the array_agg
	for (auto &modifier : subquery_expr->SubqueryMutable()->node->modifiers) {
		if (modifier->type == ResultModifierType::ORDER_MODIFIER) {
			aggr->OrderByMutable() = unique_ptr_cast<ResultModifier, OrderModifier>(modifier->Copy());
			break;
		}
	}
	// transform constants (e.g. ORDER BY 1) into positional references (ORDER BY #1)
	idx_t array_idx = 0;
	if (aggr->OrderBy()) {
		for (auto &order : aggr->OrderByMutable()->orders) {
			if (order.expression->GetExpressionType() == ExpressionType::VALUE_CONSTANT) {
				auto &constant_expr = order.expression->Cast<ConstantExpression>();
				Value bigint_value;
				string error;
				if (constant_expr.GetValue().DefaultTryCastAs(LogicalType::BIGINT, bigint_value, &error)) {
					int64_t order_index = BigIntValue::Get(bigint_value);
					idx_t positional_index = order_index < 0 ? NumericLimits<idx_t>::Maximum() : idx_t(order_index);
					order.expression = make_uniq<PositionalReferenceExpression>(positional_index);
				}
			} else if (sub_select) {
				// if we have a SELECT we can push the ORDER BY clause into the SELECT list and reference it
				auto alias = "__array_internal_idx_" + to_string(++array_idx);
				order.expression->SetAlias(Identifier(alias));
				sub_select->select_list.push_back(std::move(order.expression));
				order.expression = make_uniq<ColumnRefExpression>(Identifier(alias));
			} else {
				// otherwise we remove order qualifications
				RemoveOrderQualificationRecursive(order.expression);
			}
		}
	}
	// ARRAY_AGG(COLUMNS(*)) IS NULL
	auto agg_is_null = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_IS_NULL, aggr->Copy());
	// empty list
	vector<unique_ptr<ParsedExpression>> list_children;
	auto empty_list = make_uniq<FunctionExpression>("list_value", std::move(list_children));
	// CASE
	auto case_expr = make_uniq<CaseExpression>();
	CaseCheck check;
	check.when_expr = std::move(agg_is_null);
	check.then_expr = std::move(empty_list);
	case_expr->CaseChecksMutable().push_back(std::move(check));
	case_expr->ElseMutable() = std::move(aggr);

	select_node->select_list.push_back(std::move(case_expr));

	// FROM (...) tbl
	auto child_subquery = make_uniq<SubqueryRef>(std::move(subquery_expr->SubqueryMutable()));
	select_node->from_table = std::move(child_subquery);

	auto new_subquery = make_uniq<SelectStatement>();
	new_subquery->node = std::move(select_node);
	subquery_expr->SubqueryMutable() = std::move(new_subquery);

	subquery_expr->GetSubqueryTypeMutable() = SubqueryType::SCALAR;
	return std::move(subquery_expr);
}
#else
unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformArrayParensSelect(PEGTransformer &transformer,
                                                  unique_ptr<SelectStatement> select_statement_internal) {
	auto subquery_expr = make_uniq<SubqueryExpression>();
	subquery_expr->subquery = std::move(select_statement_internal);
	// ARRAY expression
	// wrap subquery into
	// "SELECT CASE WHEN ARRAY_AGG(col) IS NULL THEN [] ELSE ARRAY_AGG(col) END FROM (...) tbl"
	auto select_node = make_uniq<SelectNode>();
	unique_ptr<ParsedExpression> array_agg_child;
	optional_ptr<SelectNode> sub_select;
	if (subquery_expr->subquery->node->type == QueryNodeType::SELECT_NODE) {
		// easy case - subquery is a SELECT
		sub_select = subquery_expr->subquery->node->Cast<SelectNode>();
		if (sub_select->select_list.size() != 1) {
			throw BinderException(*subquery_expr, "Subquery returns %zu columns - expected 1",
			                      sub_select->select_list.size());
		}
		array_agg_child = make_uniq<PositionalReferenceExpression>(1ULL);
	} else {
		// subquery is not a SELECT but a UNION or CTE
		// we can still support this but it is more challenging since we can't push columns for the ORDER BY
		auto columns_star = make_uniq<StarExpression>();
		columns_star->columns = true;
		array_agg_child = std::move(columns_star);
	}

	// ARRAY_AGG(COLUMNS(*))
	vector<unique_ptr<ParsedExpression>> children;
	children.push_back(std::move(array_agg_child));
	auto aggr = make_uniq<FunctionExpression>("array_agg", std::move(children));
	// push ORDER BY modifiers into the array_agg
	for (auto &modifier : subquery_expr->subquery->node->modifiers) {
		if (modifier->type == ResultModifierType::ORDER_MODIFIER) {
			aggr->order_bys = unique_ptr_cast<ResultModifier, OrderModifier>(modifier->Copy());
			break;
		}
	}
	// transform constants (e.g. ORDER BY 1) into positional references (ORDER BY #1)
	idx_t array_idx = 0;
	if (aggr->order_bys) {
		for (auto &order : aggr->order_bys->orders) {
			if (order.expression->GetExpressionType() == ExpressionType::VALUE_CONSTANT) {
				auto &constant_expr = order.expression->Cast<ConstantExpression>();
				Value bigint_value;
				string error;
				if (constant_expr.value.DefaultTryCastAs(LogicalType::BIGINT, bigint_value, &error)) {
					int64_t order_index = BigIntValue::Get(bigint_value);
					idx_t positional_index = order_index < 0 ? NumericLimits<idx_t>::Maximum() : idx_t(order_index);
					order.expression = make_uniq<PositionalReferenceExpression>(positional_index);
				}
			} else if (sub_select) {
				// if we have a SELECT we can push the ORDER BY clause into the SELECT list and reference it
				auto alias = "__array_internal_idx_" + to_string(++array_idx);
				order.expression->SetAlias(alias);
				sub_select->select_list.push_back(std::move(order.expression));
				order.expression = make_uniq<ColumnRefExpression>(alias);
			} else {
				// otherwise we remove order qualifications
				RemoveOrderQualificationRecursive(order.expression);
			}
		}
	}
	// ARRAY_AGG(COLUMNS(*)) IS NULL
	auto agg_is_null = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_IS_NULL, aggr->Copy());
	// empty list
	vector<unique_ptr<ParsedExpression>> list_children;
	auto empty_list = make_uniq<FunctionExpression>("list_value", std::move(list_children));
	// CASE
	auto case_expr = make_uniq<CaseExpression>();
	CaseCheck check;
	check.when_expr = std::move(agg_is_null);
	check.then_expr = std::move(empty_list);
	case_expr->case_checks.push_back(std::move(check));
	case_expr->else_expr = std::move(aggr);

	select_node->select_list.push_back(std::move(case_expr));

	// FROM (...) tbl
	auto child_subquery = make_uniq<SubqueryRef>(std::move(subquery_expr->subquery));
	select_node->from_table = std::move(child_subquery);

	auto new_subquery = make_uniq<SelectStatement>();
	new_subquery->node = std::move(select_node);
	subquery_expr->subquery = std::move(new_subquery);

	subquery_expr->subquery_type = SubqueryType::SCALAR;
	return std::move(subquery_expr);
}
#endif
} // namespace duckpgq_peg
} // namespace duckdb
