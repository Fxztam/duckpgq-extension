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





vector<unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformBoundedListExpression(PEGTransformer &transformer,
                                                      optional<vector<unique_ptr<ParsedExpression>>> expression) {
	if (expression) {
		return std::move(*expression);
	}
	return vector<unique_ptr<ParsedExpression>>();
}

// Expression <- LambdaArrowExpression
unique_ptr<ParsedExpression> PEGTransformerFactory::TransformExpression(PEGTransformer &transformer,
                                                                        ParseResult &parse_result) {
	auto stack_check = transformer.StackCheck();
	auto &list_pr = parse_result.Cast<ListParseResult>();
	return transformer.Transform<unique_ptr<ParsedExpression>>(list_pr.Child<ListParseResult>(0));
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformLambdaArrowExpression(
    PEGTransformer &transformer, unique_ptr<ParsedExpression> logical_or_expression,
    optional<vector<unique_ptr<ParsedExpression>>> single_arrow_pair) {
	auto expr = std::move(logical_or_expression);
	if (!single_arrow_pair) {
		return expr;
	}
	for (auto &right_expr : *single_arrow_pair) {
		expr = make_uniq<LambdaExpression>(std::move(expr), std::move(right_expr));
	}
	return expr;
}

static unique_ptr<ParsedExpression> FoldConjunctionExpression(PEGTransformer &transformer,
                                                              unique_ptr<ParsedExpression> expression,
                                                              optional<vector<unique_ptr<ParsedExpression>>> tails,
                                                              ExpressionType conjunction_type) {
	auto expr = std::move(expression);
	if (!tails) {
		return expr;
	}
	auto depth_guard = transformer.StackCheck(tails->size());
	for (auto &tail : *tails) {
		expr = make_uniq<ConjunctionExpression>(conjunction_type, std::move(expr), std::move(tail));
	}
	return expr;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformLogicalOrExpression(
    PEGTransformer &transformer, unique_ptr<ParsedExpression> logical_and_expression,
    optional<vector<unique_ptr<ParsedExpression>>> logical_or_expression_tail) {
	return FoldConjunctionExpression(transformer, std::move(logical_and_expression),
	                                 std::move(logical_or_expression_tail), ExpressionType::CONJUNCTION_OR);
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformLogicalAndExpression(
    PEGTransformer &transformer, unique_ptr<ParsedExpression> logical_not_expression,
    optional<vector<unique_ptr<ParsedExpression>>> logical_and_expression_tail) {
	return FoldConjunctionExpression(transformer, std::move(logical_not_expression),
	                                 std::move(logical_and_expression_tail), ExpressionType::CONJUNCTION_AND);
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformColDefOrExpr(
    PEGTransformer &transformer, unique_ptr<ParsedExpression> col_def_and_expr,
    optional<vector<unique_ptr<ParsedExpression>>> col_def_or_expression_tail) {
	return FoldConjunctionExpression(transformer, std::move(col_def_and_expr), std::move(col_def_or_expression_tail),
	                                 ExpressionType::CONJUNCTION_OR);
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformColDefAndExpr(
    PEGTransformer &transformer, unique_ptr<ParsedExpression> is_distinct_from_expression,
    optional<vector<unique_ptr<ParsedExpression>>> col_def_and_expression_tail) {
	return FoldConjunctionExpression(transformer, std::move(is_distinct_from_expression),
	                                 std::move(col_def_and_expression_tail), ExpressionType::CONJUNCTION_AND);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformLogicalNotExpression(PEGTransformer &transformer, optional<vector<bool>> not_expression,
                                                     unique_ptr<ParsedExpression> is_expression) {
	auto expr = std::move(is_expression);
	if (!not_expression) {
		return expr;
	}
	for (idx_t i = 0; i < not_expression->size(); i++) {
		vector<unique_ptr<ParsedExpression>> inner_list_children;
		inner_list_children.push_back(std::move(expr));
		expr = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(inner_list_children));
	}
	return expr;
}




BinaryExpressionTail
PEGTransformerFactory::TransformBitwiseExpressionTail(PEGTransformer &transformer, const string &bit_operator,
                                                      unique_ptr<ParsedExpression> additive_expression) {
	return {bit_operator, std::move(additive_expression), optional_idx()};
}

BinaryExpressionTail
PEGTransformerFactory::TransformAdditiveExpressionTail(PEGTransformer &transformer, const string &term,
                                                       unique_ptr<ParsedExpression> multiplicative_expression,
                                                       optional_idx query_location) {
	return {term, std::move(multiplicative_expression), query_location};
}

BinaryExpressionTail
PEGTransformerFactory::TransformMultiplicativeExpressionTail(PEGTransformer &transformer, const string &factor,
                                                             unique_ptr<ParsedExpression> exponentiation_expression) {
	return {factor, std::move(exponentiation_expression), optional_idx()};
}

BinaryExpressionTail PEGTransformerFactory::TransformExponentiationExpressionTail(
    PEGTransformer &transformer, const string &exponent_operator, unique_ptr<ParsedExpression> collate_expression) {
	return {exponent_operator, std::move(collate_expression), optional_idx()};
}


unique_ptr<ParsedExpression> PEGTransformerFactory::TransformAtTimeZoneExpression(
    PEGTransformer &transformer, unique_ptr<ParsedExpression> prefix_expression,
    optional<vector<unique_ptr<ParsedExpression>>> at_time_zone_expression_tail) {
	auto expr = std::move(prefix_expression);
	if (!at_time_zone_expression_tail) {
		return expr;
	}
	for (auto &time_zone_expr : *at_time_zone_expression_tail) {
		vector<unique_ptr<ParsedExpression>> time_zone_children;
		time_zone_children.push_back(std::move(time_zone_expr));
		time_zone_children.push_back(std::move(expr));
		auto func_expr = make_uniq<FunctionExpression>("timezone", std::move(time_zone_children));
		expr = std::move(func_expr);
	}
	return expr;
}


// LiteralExpression <- StringLiteral / NumberLiteral / 'NULL' / 'TRUE' / 'FALSE'
unique_ptr<ParsedExpression> PEGTransformerFactory::TransformLiteralExpression(PEGTransformer &transformer,
                                                                               ParseResult &choice_result) {
	if (choice_result.name == "StringLiteral") {
		auto &string_literal = choice_result.Cast<StringLiteralParseResult>();
		return string_literal.ToExpression();
	}
	return transformer.Transform<unique_ptr<ParsedExpression>>(choice_result);
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformParensExpression(PEGTransformer &transformer,
                                                                              unique_ptr<ParsedExpression> expression) {
	return expression;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformConstantLiteral(PEGTransformer &transformer,
                                                                             const Value &child) {
	return make_uniq<ConstantExpression>(child);
}

Value PEGTransformerFactory::TransformFalseLiteral(PEGTransformer &transformer) {
	return Value(false);
}

Value PEGTransformerFactory::TransformTrueLiteral(PEGTransformer &transformer) {
	return Value(true);
}

Value PEGTransformerFactory::TransformNullLiteral(PEGTransformer &transformer) {
	return Value();
}

Value PEGTransformerFactory::TransformUnknownLiteral(PEGTransformer &transformer) {
	return Value();
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformPostfixOperator(PEGTransformer &transformer) {
	vector<unique_ptr<ParsedExpression>> func_children;
	return make_uniq<FunctionExpression>("factorial", std::move(func_children));
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformCastOperator(PEGTransformer &transformer,
                                                                          const LogicalType &type) {
	// We input a dummy constant expression but replace this later with the real expression that precedes this post-fix
	// castOperator
	return make_uniq<CastExpression>(type, make_uniq<ConstantExpression>(Value()));
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformDotMethodOperator(PEGTransformer &transformer,
                                                  unique_ptr<ParsedExpression> method_expression) {
	return method_expression;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformDotColumnOperator(PEGTransformer &transformer,
                                                                               const string &col_label) {
	return make_uniq<ConstantExpression>(col_label);
}


unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformSliceExpression(PEGTransformer &transformer,
                                                vector<unique_ptr<ParsedExpression>> slice_bound) {
	if (slice_bound.empty()) {
		throw ParserException("Empty subscript '[]' is not allowed");
	}
	if (slice_bound.size() == 1) {
		return make_uniq<OperatorExpression>(ExpressionType::ARRAY_EXTRACT, std::move(slice_bound));
	}
	return make_uniq<OperatorExpression>(ExpressionType::ARRAY_SLICE, std::move(slice_bound));
}

vector<unique_ptr<ParsedExpression>> PEGTransformerFactory::TransformSliceBound(
    PEGTransformer &transformer, optional<unique_ptr<ParsedExpression>> expression,
    optional<unique_ptr<ParsedExpression>> end_slice_bound, optional<unique_ptr<ParsedExpression>> step_slice_bound) {
	vector<unique_ptr<ParsedExpression>> slice_bounds;
	if (!end_slice_bound && !step_slice_bound) {
		if (expression && *expression) {
			slice_bounds.push_back(std::move(*expression));
		}
		return slice_bounds;
	}
	if (expression && *expression) {
		slice_bounds.push_back(std::move(*expression));
	} else {
		slice_bounds.push_back(make_uniq<ConstantExpression>(Value::LIST(LogicalType::INTEGER, vector<Value>())));
	}
	if (end_slice_bound && *end_slice_bound) {
		slice_bounds.push_back(std::move(*end_slice_bound));
	} else {
		slice_bounds.push_back(make_uniq<ConstantExpression>(Value::LIST(LogicalType::INTEGER, vector<Value>())));
	}
	if (step_slice_bound && *step_slice_bound) {
		slice_bounds.push_back(std::move(*step_slice_bound));
	}
	return slice_bounds;
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformEndSliceBound(PEGTransformer &transformer,
                                              optional<unique_ptr<ParsedExpression>> end_slice_value) {
	// If either the lower or upper bound is not specified, we use an empty constant LIST,
	// which we handle in the execution.
	if (end_slice_value && *end_slice_value) {
		return std::move(*end_slice_value);
	}
	return make_uniq<ConstantExpression>(Value::LIST(LogicalType::INTEGER, vector<Value>()));
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformEndSliceMinus(PEGTransformer &transformer) {
	return make_uniq<ConstantExpression>(Value::LIST(LogicalType::INTEGER, vector<Value>()));
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformStepSliceBound(PEGTransformer &transformer,
                                               optional<unique_ptr<ParsedExpression>> expression) {
	if (expression && *expression) {
		return std::move(*expression);
	}
	return make_uniq<ConstantExpression>(Value::LIST(LogicalType::INTEGER, vector<Value>()));
}


Identifier PEGTransformerFactory::TransformTableQualification(PEGTransformer &transformer,
                                                              const Identifier &table_name) {
	return table_name;
}

string PEGTransformerFactory::TransformColIdDot(PEGTransformer &transformer, const Identifier &col_id) {
	return col_id.GetIdentifierName();
}



WindowFrame PEGTransformerFactory::TransformFrameClause(PEGTransformer &transformer, const string &framing,
                                                        vector<WindowBoundaryExpression> frame_extent,
                                                        const optional<WindowExcludeMode> &window_exclude_clause) {
	WindowFrame result;
	for (auto &frame : frame_extent) {
		if (StringUtil::CIEquals(framing, "rows")) {
			if (frame.boundary == WindowBoundary::CURRENT_ROW_RANGE) {
				frame.boundary = WindowBoundary::CURRENT_ROW_ROWS;
			} else if (frame.boundary == WindowBoundary::EXPR_PRECEDING_RANGE) {
				frame.boundary = WindowBoundary::EXPR_PRECEDING_ROWS;
			} else if (frame.boundary == WindowBoundary::EXPR_FOLLOWING_RANGE) {
				frame.boundary = WindowBoundary::EXPR_FOLLOWING_ROWS;
			} else if (frame.boundary == WindowBoundary::INVALID) {
				frame.boundary = WindowBoundary::CURRENT_ROW_ROWS;
			}
		} else if (StringUtil::CIEquals(framing, "groups")) {
			if (frame.boundary == WindowBoundary::CURRENT_ROW_RANGE) {
				frame.boundary = WindowBoundary::CURRENT_ROW_GROUPS;
			} else if (frame.boundary == WindowBoundary::EXPR_PRECEDING_RANGE) {
				frame.boundary = WindowBoundary::EXPR_PRECEDING_GROUPS;
			} else if (frame.boundary == WindowBoundary::EXPR_FOLLOWING_RANGE) {
				frame.boundary = WindowBoundary::EXPR_FOLLOWING_GROUPS;
			} else if (frame.boundary == WindowBoundary::INVALID) {
				frame.boundary = WindowBoundary::CURRENT_ROW_GROUPS;
			}
		} else if (StringUtil::CIEquals(framing, "range")) {
			if (frame.boundary == WindowBoundary::INVALID) {
				frame.boundary = WindowBoundary::CURRENT_ROW_RANGE;
			}
		} else {
			throw ParserException("Invalid result from frame: %s", framing);
		}
	}
	if (frame_extent[0].boundary == WindowBoundary::UNBOUNDED_FOLLOWING) {
		throw ParserException("Frame start cannot be UNBOUNDED FOLLOWING");
	}
	result.start = frame_extent[0].boundary;
	if (frame_extent[0].expr) {
		result.start_expr = std::move(frame_extent[0].expr);
	}
	if (frame_extent.size() == 2) {
		if (frame_extent[1].boundary == WindowBoundary::UNBOUNDED_PRECEDING) {
			throw ParserException("Frame end cannot be UNBOUNDED PRECEDING");
		}
		result.end = frame_extent[1].boundary;
		if (frame_extent[1].expr) {
			result.end_expr = std::move(frame_extent[1].expr);
		}
	}
	if (window_exclude_clause) {
		result.exclude_clause = *window_exclude_clause;
	}
	return result;
}

vector<WindowBoundaryExpression>
PEGTransformerFactory::TransformBetweenFrameExtent(PEGTransformer &transformer, WindowBoundaryExpression frame_bound,
                                                   WindowBoundaryExpression frame_bound_1) {
	vector<WindowBoundaryExpression> result;
	result.push_back(std::move(frame_bound));
	result.push_back(std::move(frame_bound_1));
	return result;
}

vector<WindowBoundaryExpression>
PEGTransformerFactory::TransformSingleFrameExtent(PEGTransformer &transformer, WindowBoundaryExpression frame_bound) {
	vector<WindowBoundaryExpression> result;
	result.push_back(std::move(frame_bound));
	WindowBoundaryExpression end_current_row;
	end_current_row.boundary = WindowBoundary::INVALID;
	result.push_back(std::move(end_current_row));
	return result;
}

WindowBoundaryExpression PEGTransformerFactory::TransformFrameUnbounded(PEGTransformer &transformer,
                                                                        const bool &preceding_or_following) {
	WindowBoundaryExpression result;
	if (preceding_or_following) {
		result.boundary = WindowBoundary::UNBOUNDED_PRECEDING;
	} else {
		result.boundary = WindowBoundary::UNBOUNDED_FOLLOWING;
	}
	return result;
}

WindowBoundaryExpression PEGTransformerFactory::TransformFrameExpression(PEGTransformer &transformer,
                                                                         unique_ptr<ParsedExpression> expression,
                                                                         const bool &preceding_or_following) {
	WindowBoundaryExpression result;
	result.expr = std::move(expression);
	if (preceding_or_following) {
		// These are placeholders and will be converted to groups/rows/range later
		result.boundary = WindowBoundary::EXPR_PRECEDING_RANGE;
	} else {
		result.boundary = WindowBoundary::EXPR_FOLLOWING_RANGE;
	}
	return result;
}

WindowBoundaryExpression PEGTransformerFactory::TransformFrameCurrentRow(PEGTransformer &transformer) {
	WindowBoundaryExpression result;
	// These are placeholders and will be converted to groups/rows/range later
	result.boundary = WindowBoundary::CURRENT_ROW_RANGE;
	return result;
}

bool PEGTransformerFactory::TransformPrecedingFrame(PEGTransformer &transformer) {
	return true;
}

bool PEGTransformerFactory::TransformFollowingFrame(PEGTransformer &transformer) {
	return false;
}

WindowExcludeMode PEGTransformerFactory::TransformWindowExcludeClause(PEGTransformer &transformer,
                                                                      const WindowExcludeMode &window_exclude_element) {
	return window_exclude_element;
}

string PEGTransformerFactory::TransformRowsFraming(PEGTransformer &transformer) {
	return "ROWS";
}

string PEGTransformerFactory::TransformRangeFraming(PEGTransformer &transformer) {
	return "RANGE";
}

string PEGTransformerFactory::TransformGroupsFraming(PEGTransformer &transformer) {
	return "GROUPS";
}

WindowExcludeMode PEGTransformerFactory::TransformExcludeCurrentRow(PEGTransformer &transformer) {
	return WindowExcludeMode::CURRENT_ROW;
}

WindowExcludeMode PEGTransformerFactory::TransformExcludeGroup(PEGTransformer &transformer) {
	return WindowExcludeMode::GROUP;
}

WindowExcludeMode PEGTransformerFactory::TransformExcludeTies(PEGTransformer &transformer) {
	return WindowExcludeMode::TIES;
}

WindowExcludeMode PEGTransformerFactory::TransformExcludeNoOthers(PEGTransformer &transformer) {
	return WindowExcludeMode::NO_OTHER;
}

vector<unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformWindowPartition(PEGTransformer &transformer,
                                                vector<unique_ptr<ParsedExpression>> expression) {
	return expression;
}



unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformExtractExpression(PEGTransformer &transformer,
                                                  vector<unique_ptr<ParsedExpression>> extract_arguments) {
	return make_uniq<FunctionExpression>("date_part", std::move(extract_arguments));
}

vector<unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformExtractArguments(PEGTransformer &transformer,
                                                 unique_ptr<ParsedExpression> extract_argument,
                                                 unique_ptr<ParsedExpression> expression) {
	vector<unique_ptr<ParsedExpression>> result;
	result.push_back(std::move(extract_argument));
	result.push_back(std::move(expression));
	return result;
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformExtractDatePartArgument(PEGTransformer &transformer,
                                                        const DatePartSpecifier &extract_date_part) {
	return make_uniq<ConstantExpression>(EnumUtil::ToString(extract_date_part));
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformExtractIdentifierArgument(PEGTransformer &transformer,
                                                                                       const Identifier &identifier) {
	return make_uniq<ConstantExpression>(Value(identifier.GetIdentifierName()));
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformExtractStringArgument(PEGTransformer &transformer,
                                                                                   const string &string_literal) {
	return make_uniq<ConstantExpression>(Value(string_literal));
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformLambdaExpression(
    PEGTransformer &transformer, const vector<Identifier> &col_id_or_string, unique_ptr<ParsedExpression> expression) {
	vector<string> parameters;
	for (auto &parameter : col_id_or_string) {
		parameters.push_back(parameter.GetIdentifierName());
	}
	auto result = make_uniq<LambdaExpression>(parameters, std::move(expression));
	return std::move(result);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformNullIfExpression(PEGTransformer &transformer,
                                                 vector<unique_ptr<ParsedExpression>> null_if_arguments) {
	return make_uniq<FunctionExpression>("nullif", std::move(null_if_arguments));
}

vector<unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformNullIfArguments(PEGTransformer &transformer, unique_ptr<ParsedExpression> expression,
                                                unique_ptr<ParsedExpression> expression_1) {
	vector<unique_ptr<ParsedExpression>> result;
	result.push_back(std::move(expression));
	result.push_back(std::move(expression_1));
	return result;
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformRowExpression(PEGTransformer &transformer,
                                              optional<vector<unique_ptr<ParsedExpression>>> expression) {
	if (!expression) {
		return make_uniq<FunctionExpression>("row", vector<unique_ptr<ParsedExpression>>());
	}
	auto func_expr = make_uniq<FunctionExpression>("row", std::move(*expression));
	return std::move(func_expr);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformSubstringExpression(PEGTransformer &transformer,
                                                    vector<unique_ptr<ParsedExpression>> substring_arguments) {
	return make_uniq<FunctionExpression>("substring", std::move(substring_arguments));
}

vector<unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformSubstringExpressionList(PEGTransformer &transformer,
                                                        vector<unique_ptr<ParsedExpression>> expression) {
	return expression;
}

vector<unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformSubstringParameters(PEGTransformer &transformer,
                                                    unique_ptr<ParsedExpression> expression,
                                                    vector<unique_ptr<ParsedExpression>> substring_from_for) {
	vector<unique_ptr<ParsedExpression>> results;
	results.push_back(std::move(expression));
	for (auto &arg : substring_from_for) {
		results.push_back(std::move(arg));
	}
	return results;
}

vector<unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformSubstringFromOptionalFor(PEGTransformer &transformer,
                                                         unique_ptr<ParsedExpression> from_expression,
                                                         optional<unique_ptr<ParsedExpression>> for_expression) {
	vector<unique_ptr<ParsedExpression>> results;
	results.push_back(std::move(from_expression));
	if (for_expression && *for_expression) {
		results.push_back(std::move(*for_expression));
	}
	return results;
}

vector<unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformSubstringFor(PEGTransformer &transformer, unique_ptr<ParsedExpression> for_expression) {
	vector<unique_ptr<ParsedExpression>> results;
	results.push_back(make_uniq<ConstantExpression>(Value::INTEGER(1)));
	results.push_back(std::move(for_expression));
	return results;
}


TrimArguments PEGTransformerFactory::TransformTrimArguments(PEGTransformer &transformer,
                                                            const optional<string> &trim_direction,
                                                            optional<unique_ptr<ParsedExpression>> trim_source,
                                                            vector<unique_ptr<ParsedExpression>> expression) {
	TrimArguments result;
	result.trim_direction = trim_direction;
	result.expressions = std::move(expression);
	if (trim_source && *trim_source) {
		result.expressions.push_back(std::move(*trim_source));
	}
	return result;
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformTrimSource(PEGTransformer &transformer,
                                           optional<unique_ptr<ParsedExpression>> expression) {
	if (expression) {
		return std::move(*expression);
	}
	return nullptr;
}

string PEGTransformerFactory::TransformTrimBoth(PEGTransformer &transformer) {
	return "trim";
}

string PEGTransformerFactory::TransformTrimLeading(PEGTransformer &transformer) {
	return "ltrim";
}

string PEGTransformerFactory::TransformTrimTrailing(PEGTransformer &transformer) {
	return "rtrim";
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformOverlayExpression(PEGTransformer &transformer,
                                                  vector<unique_ptr<ParsedExpression>> overlay_arguments) {
	return make_uniq<FunctionExpression>("overlay", std::move(overlay_arguments));
}

vector<unique_ptr<ParsedExpression>> PEGTransformerFactory::TransformOverlayParameters(
    PEGTransformer &transformer, unique_ptr<ParsedExpression> expression, unique_ptr<ParsedExpression> expression_1,
    unique_ptr<ParsedExpression> from_expression, optional<unique_ptr<ParsedExpression>> for_expression) {
	vector<unique_ptr<ParsedExpression>> results;
	results.push_back(std::move(expression));
	results.push_back(std::move(expression_1));
	results.push_back(std::move(from_expression));
	if (for_expression && *for_expression) {
		results.push_back(std::move(*for_expression));
	}
	return results;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformFromExpression(PEGTransformer &transformer,
                                                                            unique_ptr<ParsedExpression> expression) {
	return expression;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformForExpression(PEGTransformer &transformer,
                                                                           unique_ptr<ParsedExpression> expression) {
	return expression;
}

vector<unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformOverlayExpressionList(PEGTransformer &transformer,
                                                      vector<unique_ptr<ParsedExpression>> expression) {
	return expression;
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformPositionExpression(PEGTransformer &transformer,
                                                   vector<unique_ptr<ParsedExpression>> position_arguments) {
	return make_uniq<FunctionExpression>("position", std::move(position_arguments));
}

vector<unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformPositionArguments(PEGTransformer &transformer,
                                                  unique_ptr<ParsedExpression> other_operator_expression,
                                                  unique_ptr<ParsedExpression> expression) {
	vector<unique_ptr<ParsedExpression>> result;
	result.push_back(std::move(expression));
	result.push_back(std::move(other_operator_expression));
	return result;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformCastExpression(PEGTransformer &transformer,
                                                                            const bool &cast_or_try_cast,
                                                                            CastArguments cast_arguments) {
	return make_uniq<CastExpression>(cast_arguments.type, std::move(cast_arguments.expression), cast_or_try_cast);
}

CastArguments PEGTransformerFactory::TransformCastArguments(PEGTransformer &transformer,
                                                            unique_ptr<ParsedExpression> expression,
                                                            const LogicalType &type) {
	CastArguments result;
	result.expression = std::move(expression);
	result.type = type;
	return result;
}

bool PEGTransformerFactory::TransformCastKeyword(PEGTransformer &transformer) {
	return false;
}

bool PEGTransformerFactory::TransformTryCastKeyword(PEGTransformer &transformer) {
	return true;
}


unique_ptr<ParsedExpression> PEGTransformerFactory::TransformTypeLiteral(PEGTransformer &transformer,
                                                                         const Identifier &col_id,
                                                                         const string &string_literal) {
	auto colid = col_id.GetIdentifierName();
	auto type = LogicalType(TransformStringToLogicalTypeId(colid));
	if (type.id() == LogicalTypeId::LIST || type.id() == LogicalTypeId::STRUCT) {
		throw ParserException("Cannot convert to type %s, requires exactly one type modifier",
		                      EnumUtil::ToString(type.id()));
	}
	auto child = make_uniq<ConstantExpression>(Value(string_literal));
	auto unbound_type = LogicalType::UNBOUND(make_uniq<TypeExpression>(colid, vector<unique_ptr<ParsedExpression>>()));
	auto result = make_uniq<CastExpression>(unbound_type, std::move(child));
	return std::move(result);
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformDefaultExpression(PEGTransformer &transformer) {
	return make_uniq<DefaultExpression>();
}


unique_ptr<ParsedExpression> PEGTransformerFactory::TransformIntervalStringParameter(PEGTransformer &transformer,
                                                                                     const string &string_literal) {
	return make_uniq<ConstantExpression>(Value(string_literal));
}


unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformMapExpression(PEGTransformer &transformer,
                                              vector<unique_ptr<ParsedExpression>> map_struct_expression) {
	return make_uniq<FunctionExpression>("map", std::move(map_struct_expression));
}

vector<unique_ptr<ParsedExpression>> PEGTransformerFactory::TransformMapStructExpression(
    PEGTransformer &transformer, optional<vector<vector<unique_ptr<ParsedExpression>>>> map_struct_field) {
	vector<unique_ptr<ParsedExpression>> keys;
	vector<unique_ptr<ParsedExpression>> values;

	if (map_struct_field) {
		for (auto &key_val_pair : *map_struct_field) {
			keys.push_back(std::move(key_val_pair[0]));
			values.push_back(std::move(key_val_pair[1]));
		}
	}
	vector<unique_ptr<ParsedExpression>> result;
	result.push_back(make_uniq<FunctionExpression>("list_value", std::move(keys)));
	result.push_back(make_uniq<FunctionExpression>("list_value", std::move(values)));
	return result;
}

vector<unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformMapStructField(PEGTransformer &transformer, unique_ptr<ParsedExpression> expression,
                                               unique_ptr<ParsedExpression> expression_1) {
	vector<unique_ptr<ParsedExpression>> fields;
	fields.push_back(std::move(expression));
	fields.push_back(std::move(expression_1));
	return fields;
}


case_insensitive_map_t<unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformReplaceList(PEGTransformer &transformer,
                                            case_insensitive_map_t<unique_ptr<ParsedExpression>> replace_entries) {
	return replace_entries;
}


ExpressionType PEGTransformerFactory::TransformIsDistinctFromOp(PEGTransformer &transformer, const bool &has_result) {
	if (has_result) {
		return ExpressionType::COMPARE_NOT_DISTINCT_FROM;
	}
	return ExpressionType::COMPARE_DISTINCT_FROM;
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformGroupingExpression(PEGTransformer &transformer, const bool &grouping_or_grouping_id,
                                                   optional<vector<unique_ptr<ParsedExpression>>> expression) {
	vector<unique_ptr<ParsedExpression>> grouping_expressions;
	if (expression) {
		grouping_expressions = std::move(*expression);
	}

	auto result = make_uniq<OperatorExpression>(ExpressionType::GROUPING_FUNCTION, std::move(grouping_expressions));
	return std::move(result);
}

bool PEGTransformerFactory::TransformGroupingKeyword(PEGTransformer &transformer) {
	return false;
}

bool PEGTransformerFactory::TransformGroupingIdKeyword(PEGTransformer &transformer) {
	return true;
}

qualified_column_map_t<string>
PEGTransformerFactory::TransformRenameList(PEGTransformer &transformer,
                                           const qualified_column_map_t<string> &rename_entries) {
	return rename_entries;
}

qualified_column_map_t<string>
PEGTransformerFactory::TransformRenameEntryList(PEGTransformer &transformer,
                                                const vector<pair<QualifiedColumnName, string>> &rename_entry) {
	qualified_column_map_t<string> result;
	for (auto &entry : rename_entry) {
		result[entry.first] = entry.second;
	}
	return result;
}

qualified_column_map_t<string>
PEGTransformerFactory::TransformSingleRenameEntry(PEGTransformer &transformer,
                                                  const pair<QualifiedColumnName, string> &rename_entry) {
	qualified_column_map_t<string> result;
	result[rename_entry.first] = rename_entry.second;
	return result;
}

pair<QualifiedColumnName, string> PEGTransformerFactory::TransformRenameEntry(PEGTransformer &transformer,
                                                                              const QualifiedColumnName &exclude_name,
                                                                              const Identifier &identifier) {
	return make_pair(exclude_name, identifier.GetIdentifierName());
}

bool PEGTransformerFactory::TransformIgnoreNulls(PEGTransformer &transformer) {
	return true;
}

bool PEGTransformerFactory::TransformRespectNulls(PEGTransformer &transformer) {
	return false;
}

} // namespace duckpgq_peg
} // namespace duckdb
