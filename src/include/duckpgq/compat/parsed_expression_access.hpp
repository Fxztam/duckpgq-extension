#pragma once
#include "duckdb/parser/expression/between_expression.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/comparison_expression.hpp"
#include "duckdb/parser/expression/conjunction_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/expression/star_expression.hpp"
#include "duckpgq/parser/identifier.hpp"

namespace duckdb {
namespace duckpgq_compat {

// Mutable access to child expressions. The pinned fork exposes *Mutable() accessors and Identifier-typed names,
// canonical DuckDB 1.5.5 exposes public members and plain strings.
#if __has_include("duckdb/common/identifier.hpp")
inline unique_ptr<ParsedExpression> &ComparisonLeft(ComparisonExpression &expression) {
	return expression.LeftMutable();
}
inline unique_ptr<ParsedExpression> &ComparisonRight(ComparisonExpression &expression) {
	return expression.RightMutable();
}
inline auto &ConjunctionChildren(ConjunctionExpression &expression) {
	return expression.GetChildrenMutable();
}
inline unique_ptr<ParsedExpression> &BetweenInput(BetweenExpression &expression) {
	return expression.InputMutable();
}
inline unique_ptr<ParsedExpression> &BetweenLowerBound(BetweenExpression &expression) {
	return expression.LowerBoundMutable();
}
inline unique_ptr<ParsedExpression> &BetweenUpperBound(BetweenExpression &expression) {
	return expression.UpperBoundMutable();
}
inline unique_ptr<ParsedExpression> &FunctionFilter(FunctionExpression &expression) {
	return expression.FilterMutable();
}
inline vector<string> ColumnRefNames(const ColumnRefExpression &expression) {
	vector<string> result;
	for (auto &name : expression.ColumnNames()) {
		result.push_back(name.GetIdentifierName());
	}
	return result;
}
inline string StarRelationName(const StarExpression &expression) {
	return expression.RelationName().GetIdentifierName();
}
#else
inline unique_ptr<ParsedExpression> &ComparisonLeft(ComparisonExpression &expression) {
	return expression.left;
}
inline unique_ptr<ParsedExpression> &ComparisonRight(ComparisonExpression &expression) {
	return expression.right;
}
inline auto &ConjunctionChildren(ConjunctionExpression &expression) {
	return expression.children;
}
inline unique_ptr<ParsedExpression> &BetweenInput(BetweenExpression &expression) {
	return expression.input;
}
inline unique_ptr<ParsedExpression> &BetweenLowerBound(BetweenExpression &expression) {
	return expression.lower;
}
inline unique_ptr<ParsedExpression> &BetweenUpperBound(BetweenExpression &expression) {
	return expression.upper;
}
inline unique_ptr<ParsedExpression> &FunctionFilter(FunctionExpression &expression) {
	return expression.filter;
}
inline vector<string> ColumnRefNames(const ColumnRefExpression &expression) {
	return expression.column_names;
}
inline string StarRelationName(const StarExpression &expression) {
	return expression.relation_name;
}
#endif

} // namespace duckpgq_compat
} // namespace duckdb
