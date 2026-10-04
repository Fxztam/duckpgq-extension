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
bool TryNegateLikeFunction(Identifier &function_name) {
	if (function_name == "~~") {
		function_name = "!~~";
		return true;
	} else if (function_name == "~~*") {
		function_name = "!~~*";
		return true;
	} else if (function_name == "~~~") {
		return false;
	} else if (function_name == "regexp_matches") {
		return false;
	} else if (function_name == "regexp_full_match") {
		return false;
	}
	return false;
}

static string RegexMatchOperatorFunctionName(PEGTransformer &transformer) {
	if (transformer.options.regex_match_operator_semantics == RegexMatchOperatorSemantics::FULL) {
		return "regexp_full_match";
	}
	return "regexp_matches";
}

static bool IsRegexMatchFunctionName(const string &function_name) {
	auto name = function_name;
	if (StringUtil::StartsWith(name, "!")) {
		name = name.substr(1);
	}
	return name == "regexp_matches" || name == "regexp_full_match";
}

static bool TryRemoveRegexOperatorNegation(Identifier &function_name) {
	if (function_name == "!regexp_matches") {
		function_name = "regexp_matches";
		return true;
	}
	if (function_name == "!regexp_full_match") {
		function_name = "regexp_full_match";
		return true;
	}
	return false;
}

static bool TryRemoveRegexCaseInsensitiveSuffix(string &function_name) {
	static constexpr const char *REGEX_CASE_INSENSITIVE_SUFFIX = "__case_insensitive";
	if (!StringUtil::EndsWith(function_name, REGEX_CASE_INSENSITIVE_SUFFIX)) {
		return false;
	}
	function_name =
	    function_name.substr(0, function_name.size() - std::char_traits<char>::length(REGEX_CASE_INSENSITIVE_SUFFIX));
	return true;
}

static bool TryGetRegexMatchOperator(const string &op_string, PEGTransformer &transformer, string &function_name,
                                     bool &negated, bool &case_insensitive) {
	if (op_string == "~") {
		function_name = RegexMatchOperatorFunctionName(transformer);
		negated = false;
		case_insensitive = false;
		return true;
	}
	if (op_string == "!~") {
		function_name = RegexMatchOperatorFunctionName(transformer);
		negated = true;
		case_insensitive = false;
		return true;
	}
	if (op_string == "~*") {
		function_name = RegexMatchOperatorFunctionName(transformer);
		negated = false;
		case_insensitive = true;
		return true;
	}
	if (op_string == "!~*") {
		function_name = RegexMatchOperatorFunctionName(transformer);
		negated = true;
		case_insensitive = true;
		return true;
	}
	return false;
}

static unique_ptr<ParsedExpression> MakeFunctionExpression(const string &name,
                                                           vector<unique_ptr<ParsedExpression>> children) {
	return make_uniq<FunctionExpression>(Identifier(name), std::move(children));
}

static unique_ptr<ParsedExpression> MakeBooleanConstant(bool value) {
	return make_uniq<ConstantExpression>(Value::BOOLEAN(value));
}

static unique_ptr<ParsedExpression> MakeListContains(unique_ptr<ParsedExpression> list, bool value) {
	vector<unique_ptr<ParsedExpression>> children;
	children.push_back(std::move(list));
	children.push_back(MakeBooleanConstant(value));
	return MakeFunctionExpression("list_contains", std::move(children));
}

static unique_ptr<ParsedExpression> MakeListHasNull(unique_ptr<ParsedExpression> list) {
	auto is_null = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_IS_NULL,
	                                             make_uniq<ColumnRefExpression>("__regex_match"));

	auto null_check_lambda = make_uniq<LambdaExpression>(vector<string> {"__regex_match"}, std::move(is_null));
	vector<unique_ptr<ParsedExpression>> filter_children;
	filter_children.push_back(std::move(list));
	filter_children.push_back(std::move(null_check_lambda));
	auto null_matches = MakeFunctionExpression("list_filter", std::move(filter_children));

	vector<unique_ptr<ParsedExpression>> length_children;
	length_children.push_back(std::move(null_matches));
	auto null_count = MakeFunctionExpression("len", std::move(length_children));

	return make_uniq<ComparisonExpression>(ExpressionType::COMPARE_GREATERTHAN, std::move(null_count),
	                                       make_uniq<ConstantExpression>(Value::INTEGER(0)));
}

static unique_ptr<ParsedExpression> TransformRegexAnyAllList(unique_ptr<ParsedExpression> left_expr,
                                                             unique_ptr<ParsedExpression> right_expr,
                                                             const string &function_name, bool negated,
                                                             bool case_insensitive, bool is_any) {
	vector<unique_ptr<ParsedExpression>> regex_children;
	regex_children.push_back(std::move(left_expr));
	regex_children.push_back(make_uniq<ColumnRefExpression>("__regex_pattern"));
	if (case_insensitive) {
		regex_children.push_back(make_uniq<ConstantExpression>(Value("i")));
	}
	unique_ptr<ParsedExpression> regex_match = MakeFunctionExpression(function_name, std::move(regex_children));
	if (negated) {
		regex_match = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(regex_match));
	}

	auto pattern_lambda = make_uniq<LambdaExpression>(vector<string> {"__regex_pattern"}, std::move(regex_match));
	vector<unique_ptr<ParsedExpression>> transform_children;
	transform_children.push_back(std::move(right_expr));
	transform_children.push_back(std::move(pattern_lambda));
	auto match_list = MakeFunctionExpression("list_transform", std::move(transform_children));

	auto result = make_uniq<CaseExpression>();
	CaseCheck has_decisive_value;
	has_decisive_value.when_expr = MakeListContains(match_list->Copy(), is_any);
	has_decisive_value.then_expr = MakeBooleanConstant(is_any);
	result->CaseChecksMutable().push_back(std::move(has_decisive_value));

	CaseCheck has_null_value;
	has_null_value.when_expr = MakeListHasNull(match_list->Copy());
	has_null_value.then_expr = make_uniq<ConstantExpression>(Value());
	result->CaseChecksMutable().push_back(std::move(has_null_value));

	result->ElseMutable() = MakeBooleanConstant(!is_any);
	return std::move(result);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformBetweenInLikeExpression(PEGTransformer &transformer,
                                                        unique_ptr<ParsedExpression> other_operator_expression,
                                                        optional<BetweenInLikeOperator> between_in_like_op) {
	auto expr = std::move(other_operator_expression);
	if (!between_in_like_op) {
		return expr;
	}
	auto between_in_like_expr = std::move(between_in_like_op->expression);
	bool has_not = between_in_like_op->has_not;
	if (between_in_like_expr->GetExpressionClass() == ExpressionClass::BETWEEN) {
		auto between_expr = unique_ptr_cast<ParsedExpression, BetweenExpression>(std::move(between_in_like_expr));
		between_expr->InputMutable() = std::move(expr);
		if (has_not) {
			expr = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(between_expr));
		} else {
			expr = std::move(between_expr);
		}
	} else if (between_in_like_expr->GetExpressionClass() == ExpressionClass::FUNCTION) {
		auto func_expr = unique_ptr_cast<ParsedExpression, FunctionExpression>(std::move(between_in_like_expr));
		if (func_expr->FunctionName() == "contains") {
			func_expr->GetArgumentsMutable().push_back(std::move(expr));
		} else {
			func_expr->GetArgumentsMutable().insert(func_expr->GetArgumentsMutable().begin(), std::move(expr));
		}
		auto function_name = func_expr->FunctionName();
		auto regex_operator_negated = TryRemoveRegexOperatorNegation(function_name);
		bool negated_like = has_not && !regex_operator_negated && TryNegateLikeFunction(function_name);
		func_expr->SetQualifiedName(func_expr->GetQualifiedName().WithName(std::move(function_name)));
		if (has_not) {
			if (regex_operator_negated) {
				expr = std::move(func_expr);
			} else if (!negated_like) {
				// If it wasn't a special "Like" function, wrap it in a standard NOT operator
				expr = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(func_expr));
			} else {
				expr = std::move(func_expr);
			}
		} else if (regex_operator_negated) {
			expr = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(func_expr));
		} else {
			expr = std::move(func_expr);
		}
	} else if (between_in_like_expr->GetExpressionClass() == ExpressionClass::OPERATOR) {
		auto &operator_expr = between_in_like_expr->Cast<OperatorExpression>();
		operator_expr.GetChildrenMutable().insert(operator_expr.GetChildrenMutable().begin(), std::move(expr));
		if (has_not) {
			expr = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(between_in_like_expr));
		} else {
			expr = std::move(between_in_like_expr);
		}
	} else if (between_in_like_expr->GetExpressionClass() == ExpressionClass::SUBQUERY) {
		auto &subquery_expr = between_in_like_expr->Cast<SubqueryExpression>();
		subquery_expr.GetChildMutable() = std::move(expr);
		if (has_not) {
			expr = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(between_in_like_expr));
		} else {
			expr = std::move(between_in_like_expr);
		}
	}
	return expr;
}

BetweenInLikeOperator
PEGTransformerFactory::TransformBetweenInLikeOp(PEGTransformer &transformer, const bool &has_result,
                                                unique_ptr<ParsedExpression> between_in_like_op_expression) {
	BetweenInLikeOperator result;
	result.has_not = has_result;
	result.expression = std::move(between_in_like_op_expression);
	return result;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformInClause(PEGTransformer &transformer,
                                                                      unique_ptr<ParsedExpression> in_expression) {
	return in_expression;
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformInContainsExpression(PEGTransformer &transformer,
                                                     unique_ptr<ParsedExpression> other_operator_expression) {
	vector<unique_ptr<ParsedExpression>> children;
	children.push_back(std::move(other_operator_expression));
	return make_uniq<FunctionExpression>("contains", std::move(children));
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformInExpressionList(PEGTransformer &transformer,
                                                 vector<unique_ptr<ParsedExpression>> expression) {
	auto in_children = std::move(expression);
	if (in_children.size() == 1 && in_children[0]->GetExpressionClass() == ExpressionClass::SUBQUERY) {
		auto &subquery_expr = in_children[0]->Cast<SubqueryExpression>();
		auto result = make_uniq<SubqueryExpression>();
		result->GetSubqueryTypeMutable() = SubqueryType::ANY;
		result->GetComparisonTypeMutable() = ExpressionType::COMPARE_EQUAL;
		result->SubqueryMutable() = std::move(subquery_expr.SubqueryMutable());
		return std::move(result);
	}
	auto result = make_uniq<OperatorExpression>(ExpressionType::COMPARE_IN, std::move(in_children));
	return std::move(result);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformInSelectStatement(PEGTransformer &transformer,
                                                  unique_ptr<SelectStatement> select_statement_internal) {
	auto result = make_uniq<SubqueryExpression>();
	result->GetSubqueryTypeMutable() = SubqueryType::ANY;
	result->GetComparisonTypeMutable() = ExpressionType::COMPARE_EQUAL;
	result->SubqueryMutable() = std::move(select_statement_internal);
	return std::move(result);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformBetweenClause(PEGTransformer &transformer,
                                              unique_ptr<ParsedExpression> other_operator_expression,
                                              unique_ptr<ParsedExpression> other_operator_expression_1) {
	auto result = make_uniq<BetweenExpression>(nullptr, std::move(other_operator_expression),
	                                           std::move(other_operator_expression_1));
	return std::move(result);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformLikeClause(PEGTransformer &transformer, const string &like_variations,
                                           unique_ptr<ParsedExpression> other_operator_expression,
                                           optional<unique_ptr<ParsedExpression>> escape_clause) {
	string like_variation = like_variations;
	bool case_insensitive_regex = TryRemoveRegexCaseInsensitiveSuffix(like_variation);
	bool is_regex_operator = IsRegexMatchFunctionName(like_variation);
	vector<unique_ptr<ParsedExpression>> like_children;
	like_children.push_back(std::move(other_operator_expression));
	if (case_insensitive_regex && escape_clause) {
		throw ParserException(
		    "ESCAPE clause is not supported with case-insensitive regular expression match operators");
	}
	if (escape_clause) {
		if (like_variation == "~~") {
			like_variation = "like_escape";
		} else if (like_variation == "~~*") {
			like_variation = "ilike_escape";
		}
		like_children.push_back(std::move(*escape_clause));
	}
	if (case_insensitive_regex) {
		like_children.push_back(make_uniq<ConstantExpression>(Value("i")));
	}
	auto result = make_uniq<FunctionExpression>(Identifier(like_variation), std::move(like_children));
	if (!is_regex_operator) {
		result->IsOperatorMutable() = true;
	}
	return std::move(result);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformEscapeClause(PEGTransformer &transformer,
                                             unique_ptr<ParsedExpression> comparison_expression) {
	return comparison_expression;
}

string PEGTransformerFactory::TransformLikeToken(PEGTransformer &transformer) {
	return "~~";
}

string PEGTransformerFactory::TransformILikeToken(PEGTransformer &transformer) {
	return "~~*";
}

string PEGTransformerFactory::TransformGlobToken(PEGTransformer &transformer) {
	return "~~~";
}

string PEGTransformerFactory::TransformSimilarToToken(PEGTransformer &transformer) {
	return "regexp_full_match";
}

string PEGTransformerFactory::TransformRegexMatchToken(PEGTransformer &transformer) {
	return RegexMatchOperatorFunctionName(transformer);
}

string PEGTransformerFactory::TransformRegexInsensitiveMatchToken(PEGTransformer &transformer) {
	return RegexMatchOperatorFunctionName(transformer) + "__case_insensitive";
}

string PEGTransformerFactory::TransformNotILikeOp(PEGTransformer &transformer) {
	return "!~~*";
}

string PEGTransformerFactory::TransformNotLikeOp(PEGTransformer &transformer) {
	return "!~~";
}

string PEGTransformerFactory::TransformNotRegexInsensitiveMatchOp(PEGTransformer &transformer) {
	return "!" + RegexMatchOperatorFunctionName(transformer) + "__case_insensitive";
}

string PEGTransformerFactory::TransformNotSimilarToOp(PEGTransformer &transformer) {
	return "!" + RegexMatchOperatorFunctionName(transformer);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformOtherOperatorExpression(PEGTransformer &transformer,
                                                        unique_ptr<ParsedExpression> bitwise_expression,
                                                        optional<vector<OtherOperatorTail>> other_operator_tail) {
	auto expr = std::move(bitwise_expression);
	if (!other_operator_tail) {
		return expr;
	}
	for (auto &other_operator_expr : *other_operator_tail) {
		auto right_expr = std::move(other_operator_expr.expression);
		if (other_operator_expr.op.is_any_all) {
			auto op_string = other_operator_expr.op.name;
			auto is_any = other_operator_expr.op.is_any;

			// Map operator string to ExpressionType (INVALID if not a comparison operator)
			auto expression_type = OperatorToExpressionType(op_string);

			auto subquery_expr = make_uniq<SubqueryExpression>();
			if (right_expr->GetExpressionClass() == ExpressionClass::SUBQUERY) {
				if (expression_type == ExpressionType::INVALID) {
					throw ParserException("ANY and ALL operators require one of =,<>,>,<,>=,<= comparisons!");
				}
				subquery_expr->GetSubqueryTypeMutable() = SubqueryType::ANY;
				subquery_expr->GetComparisonTypeMutable() = expression_type;
				auto &right_expr_subquery = right_expr->Cast<SubqueryExpression>();
				subquery_expr->SubqueryMutable() = std::move(right_expr_subquery.SubqueryMutable());
				subquery_expr->GetChildMutable() = std::move(expr);
				if (!is_any) {
					// ALL sublink is equivalent to NOT(ANY) with inverted comparison
					// e.g. [= ALL()] is equivalent to [NOT(<> ANY())]
					// first invert the comparison type
					subquery_expr->GetComparisonTypeMutable() =
					    NegateComparisonExpression(subquery_expr->GetComparisonType());
					return make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(subquery_expr));
				}
				expr = std::move(subquery_expr);
			} else {
				string regex_function_name;
				bool regex_negated;
				bool regex_case_insensitive;
				if (TryGetRegexMatchOperator(op_string, transformer, regex_function_name, regex_negated,
				                             regex_case_insensitive)) {
					expr = TransformRegexAnyAllList(std::move(expr), std::move(right_expr), regex_function_name,
					                                regex_negated, regex_case_insensitive, is_any);
					continue;
				}
				// left=ANY(right)
				// we turn this into left=ANY((SELECT UNNEST(right)))
				if (expression_type == ExpressionType::INVALID) {
					throw ParserException("Unsupported comparison \"%s\" for ANY/ALL subquery", op_string);
				}
				auto select_statement = make_uniq<SelectStatement>();
				auto select_node = make_uniq<SelectNode>();
				vector<unique_ptr<ParsedExpression>> children;
				children.push_back(std::move(right_expr));

				select_node->select_list.push_back(make_uniq<FunctionExpression>("UNNEST", std::move(children)));
				select_node->from_table = make_uniq<EmptyTableRef>();
				select_statement->node = std::move(select_node);
				subquery_expr->SubqueryMutable() = std::move(select_statement);
				subquery_expr->GetSubqueryTypeMutable() = SubqueryType::ANY;
				subquery_expr->GetChildMutable() = std::move(expr);
				subquery_expr->GetComparisonTypeMutable() = expression_type;
				if (!is_any) {
					// ALL sublink is equivalent to NOT(ANY) with inverted comparison
					// e.g. [= ALL()] is equivalent to [NOT(<> ANY())]
					// first invert the comparison type
					subquery_expr->GetComparisonTypeMutable() =
					    NegateComparisonExpression(subquery_expr->GetComparisonType());
					return make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(subquery_expr));
				}
				return std::move(subquery_expr);
			}
		} else {
			auto other_operator = std::move(other_operator_expr.op.name);
			vector<unique_ptr<ParsedExpression>> children_function;
			children_function.push_back(std::move(expr));
			children_function.push_back(std::move(right_expr));
			vector split_operator = StringUtil::Split(other_operator, ".");
			string schema_name;
			string func_name = "";
			if (split_operator.size() == 1) {
				func_name = split_operator[0];
			} else if (split_operator.size() == 2) {
				schema_name = split_operator[0];
				func_name = split_operator[1];
			} else {
				throw ParserException("Too many identifiers found, expected schema.operator or operator");
			}

			auto func_expr = make_uniq<FunctionExpression>(
			    QualifiedName(Identifier(), Identifier(std::move(schema_name)), Identifier(std::move(func_name))),
			    std::move(children_function));
			func_expr->IsOperatorMutable() = true;
			expr = std::move(func_expr);
		}
	}
	return expr;
}

OtherOperatorTail PEGTransformerFactory::TransformOtherOperatorTail(PEGTransformer &transformer,
                                                                    ParsedOperator other_operator,
                                                                    unique_ptr<ParsedExpression> bitwise_expression) {
	OtherOperatorTail result;
	result.op = std::move(other_operator);
	result.expression = std::move(bitwise_expression);
	return result;
}

ParsedOperator PEGTransformerFactory::TransformOtherOperator(PEGTransformer &transformer, ParseResult &choice_result) {
	ParsedOperator result;
	// OperatorLiteral matches any operator token and produces an OperatorParseResult directly
	if (choice_result.type == ParseResultType::OPERATOR) {
		result.name = choice_result.Cast<OperatorParseResult>().operator_token;
		return result;
	}
	if (StringUtil::CIEquals(choice_result.name, "AnyAllOperator")) {
		auto any_all = transformer.Transform<pair<string, bool>>(choice_result);
		result.name = any_all.first;
		result.is_any_all = true;
		result.is_any = any_all.second;
		return result;
	}
	result.name = transformer.Transform<string>(choice_result);
	return result;
}

string PEGTransformerFactory::TransformQualifiedOperator(PEGTransformer &transformer,
                                                         const string &qualified_operator_contents) {
	return qualified_operator_contents;
}

string PEGTransformerFactory::TransformQualifiedOperatorContents(PEGTransformer &transformer,
                                                                 const optional<vector<string>> &col_id_dot,
                                                                 const string &any_op) {
	vector<string> result;
	if (col_id_dot) {
		result = *col_id_dot;
	}
	result.push_back(any_op);
	return StringUtil::Join(result, ".");
}

pair<string, bool> PEGTransformerFactory::TransformAnyAllOperator(PEGTransformer &transformer, const string &any_op,
                                                                  const bool &any_or_all) {
	return make_pair(any_op, any_or_all);
}

bool PEGTransformerFactory::TransformSubqueryAny(PEGTransformer &transformer) {
	return true;
}

bool PEGTransformerFactory::TransformSubqueryAll(PEGTransformer &transformer) {
	return false;
}
#else
bool TryNegateLikeFunction(Identifier &function_name) {
	if (function_name == "~~") {
		function_name = "!~~";
		return true;
	} else if (function_name == "~~*") {
		function_name = "!~~*";
		return true;
	} else if (function_name == "~~~") {
		return false;
	} else if (function_name == "regexp_matches") {
		return false;
	} else if (function_name == "regexp_full_match") {
		return false;
	}
	return false;
}

static string RegexMatchOperatorFunctionName(PEGTransformer &transformer) {
	// DuckDB 1.5.5 has no regex_match_operator_semantics option.
	return "regexp_full_match";
}

static bool IsRegexMatchFunctionName(const string &function_name) {
	auto name = function_name;
	if (StringUtil::StartsWith(name, "!")) {
		name = name.substr(1);
	}
	return name == "regexp_matches" || name == "regexp_full_match";
}

static bool TryRemoveRegexOperatorNegation(Identifier &function_name) {
	if (function_name == "!regexp_matches") {
		function_name = "regexp_matches";
		return true;
	}
	if (function_name == "!regexp_full_match") {
		function_name = "regexp_full_match";
		return true;
	}
	return false;
}

static bool TryRemoveRegexCaseInsensitiveSuffix(string &function_name) {
	static constexpr const char *REGEX_CASE_INSENSITIVE_SUFFIX = "__case_insensitive";
	if (!StringUtil::EndsWith(function_name, REGEX_CASE_INSENSITIVE_SUFFIX)) {
		return false;
	}
	function_name =
	    function_name.substr(0, function_name.size() - std::char_traits<char>::length(REGEX_CASE_INSENSITIVE_SUFFIX));
	return true;
}

static bool TryGetRegexMatchOperator(const string &op_string, PEGTransformer &transformer, string &function_name,
                                     bool &negated, bool &case_insensitive) {
	if (op_string == "~") {
		function_name = RegexMatchOperatorFunctionName(transformer);
		negated = false;
		case_insensitive = false;
		return true;
	}
	if (op_string == "!~") {
		function_name = RegexMatchOperatorFunctionName(transformer);
		negated = true;
		case_insensitive = false;
		return true;
	}
	if (op_string == "~*") {
		function_name = RegexMatchOperatorFunctionName(transformer);
		negated = false;
		case_insensitive = true;
		return true;
	}
	if (op_string == "!~*") {
		function_name = RegexMatchOperatorFunctionName(transformer);
		negated = true;
		case_insensitive = true;
		return true;
	}
	return false;
}

static unique_ptr<ParsedExpression> MakeFunctionExpression(const string &name,
                                                           vector<unique_ptr<ParsedExpression>> children) {
	return make_uniq<FunctionExpression>(name, std::move(children));
}

static unique_ptr<ParsedExpression> MakeBooleanConstant(bool value) {
	return make_uniq<ConstantExpression>(Value::BOOLEAN(value));
}

static unique_ptr<ParsedExpression> MakeListContains(unique_ptr<ParsedExpression> list, bool value) {
	vector<unique_ptr<ParsedExpression>> children;
	children.push_back(std::move(list));
	children.push_back(MakeBooleanConstant(value));
	return MakeFunctionExpression("list_contains", std::move(children));
}

static unique_ptr<ParsedExpression> MakeListHasNull(unique_ptr<ParsedExpression> list) {
	auto is_null = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_IS_NULL,
	                                             make_uniq<ColumnRefExpression>("__regex_match"));

	auto null_check_lambda = make_uniq<LambdaExpression>(vector<string> {"__regex_match"}, std::move(is_null));
	vector<unique_ptr<ParsedExpression>> filter_children;
	filter_children.push_back(std::move(list));
	filter_children.push_back(std::move(null_check_lambda));
	auto null_matches = MakeFunctionExpression("list_filter", std::move(filter_children));

	vector<unique_ptr<ParsedExpression>> length_children;
	length_children.push_back(std::move(null_matches));
	auto null_count = MakeFunctionExpression("len", std::move(length_children));

	return make_uniq<ComparisonExpression>(ExpressionType::COMPARE_GREATERTHAN, std::move(null_count),
	                                       make_uniq<ConstantExpression>(Value::INTEGER(0)));
}

static unique_ptr<ParsedExpression> TransformRegexAnyAllList(unique_ptr<ParsedExpression> left_expr,
                                                             unique_ptr<ParsedExpression> right_expr,
                                                             const string &function_name, bool negated,
                                                             bool case_insensitive, bool is_any) {
	vector<unique_ptr<ParsedExpression>> regex_children;
	regex_children.push_back(std::move(left_expr));
	regex_children.push_back(make_uniq<ColumnRefExpression>("__regex_pattern"));
	if (case_insensitive) {
		regex_children.push_back(make_uniq<ConstantExpression>(Value("i")));
	}
	unique_ptr<ParsedExpression> regex_match = MakeFunctionExpression(function_name, std::move(regex_children));
	if (negated) {
		regex_match = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(regex_match));
	}

	auto pattern_lambda = make_uniq<LambdaExpression>(vector<string> {"__regex_pattern"}, std::move(regex_match));
	vector<unique_ptr<ParsedExpression>> transform_children;
	transform_children.push_back(std::move(right_expr));
	transform_children.push_back(std::move(pattern_lambda));
	auto match_list = MakeFunctionExpression("list_transform", std::move(transform_children));

	auto result = make_uniq<CaseExpression>();
	CaseCheck has_decisive_value;
	has_decisive_value.when_expr = MakeListContains(match_list->Copy(), is_any);
	has_decisive_value.then_expr = MakeBooleanConstant(is_any);
	result->case_checks.push_back(std::move(has_decisive_value));

	CaseCheck has_null_value;
	has_null_value.when_expr = MakeListHasNull(match_list->Copy());
	has_null_value.then_expr = make_uniq<ConstantExpression>(Value());
	result->case_checks.push_back(std::move(has_null_value));

	result->else_expr = MakeBooleanConstant(!is_any);
	return std::move(result);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformBetweenInLikeExpression(PEGTransformer &transformer,
                                                        unique_ptr<ParsedExpression> other_operator_expression,
                                                        optional<BetweenInLikeOperator> between_in_like_op) {
	auto expr = std::move(other_operator_expression);
	if (!between_in_like_op) {
		return expr;
	}
	auto between_in_like_expr = std::move(between_in_like_op->expression);
	bool has_not = between_in_like_op->has_not;
	if (between_in_like_expr->GetExpressionClass() == ExpressionClass::BETWEEN) {
		auto between_expr = unique_ptr_cast<ParsedExpression, BetweenExpression>(std::move(between_in_like_expr));
		between_expr->input = std::move(expr);
		if (has_not) {
			expr = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(between_expr));
		} else {
			expr = std::move(between_expr);
		}
	} else if (between_in_like_expr->GetExpressionClass() == ExpressionClass::FUNCTION) {
		auto func_expr = unique_ptr_cast<ParsedExpression, FunctionExpression>(std::move(between_in_like_expr));
		if (func_expr->function_name == "contains") {
			func_expr->children.push_back(std::move(expr));
		} else {
			func_expr->children.insert(func_expr->children.begin(), std::move(expr));
		}
		auto function_name = Identifier(func_expr->function_name);
		auto regex_operator_negated = TryRemoveRegexOperatorNegation(function_name);
		bool negated_like = has_not && !regex_operator_negated && TryNegateLikeFunction(function_name);
		func_expr->function_name = function_name.GetIdentifierName();
		if (has_not) {
			if (regex_operator_negated) {
				expr = std::move(func_expr);
			} else if (!negated_like) {
				// If it wasn't a special "Like" function, wrap it in a standard NOT operator
				expr = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(func_expr));
			} else {
				expr = std::move(func_expr);
			}
		} else if (regex_operator_negated) {
			expr = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(func_expr));
		} else {
			expr = std::move(func_expr);
		}
	} else if (between_in_like_expr->GetExpressionClass() == ExpressionClass::OPERATOR) {
		auto &operator_expr = between_in_like_expr->Cast<OperatorExpression>();
		operator_expr.children.insert(operator_expr.children.begin(), std::move(expr));
		if (has_not) {
			expr = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(between_in_like_expr));
		} else {
			expr = std::move(between_in_like_expr);
		}
	} else if (between_in_like_expr->GetExpressionClass() == ExpressionClass::SUBQUERY) {
		auto &subquery_expr = between_in_like_expr->Cast<SubqueryExpression>();
		subquery_expr.child = std::move(expr);
		if (has_not) {
			expr = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(between_in_like_expr));
		} else {
			expr = std::move(between_in_like_expr);
		}
	}
	return expr;
}

BetweenInLikeOperator
PEGTransformerFactory::TransformBetweenInLikeOp(PEGTransformer &transformer, const bool &has_result,
                                                unique_ptr<ParsedExpression> between_in_like_op_expression) {
	BetweenInLikeOperator result;
	result.has_not = has_result;
	result.expression = std::move(between_in_like_op_expression);
	return result;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformInClause(PEGTransformer &transformer,
                                                                      unique_ptr<ParsedExpression> in_expression) {
	return in_expression;
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformInContainsExpression(PEGTransformer &transformer,
                                                     unique_ptr<ParsedExpression> other_operator_expression) {
	vector<unique_ptr<ParsedExpression>> children;
	children.push_back(std::move(other_operator_expression));
	return make_uniq<FunctionExpression>("contains", std::move(children));
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformInExpressionList(PEGTransformer &transformer,
                                                 vector<unique_ptr<ParsedExpression>> expression) {
	auto in_children = std::move(expression);
	if (in_children.size() == 1 && in_children[0]->GetExpressionClass() == ExpressionClass::SUBQUERY) {
		auto &subquery_expr = in_children[0]->Cast<SubqueryExpression>();
		auto result = make_uniq<SubqueryExpression>();
		result->subquery_type = SubqueryType::ANY;
		result->comparison_type = ExpressionType::COMPARE_EQUAL;
		result->subquery = std::move(subquery_expr.subquery);
		return std::move(result);
	}
	auto result = make_uniq<OperatorExpression>(ExpressionType::COMPARE_IN, std::move(in_children));
	return std::move(result);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformInSelectStatement(PEGTransformer &transformer,
                                                  unique_ptr<SelectStatement> select_statement_internal) {
	auto result = make_uniq<SubqueryExpression>();
	result->subquery_type = SubqueryType::ANY;
	result->comparison_type = ExpressionType::COMPARE_EQUAL;
	result->subquery = std::move(select_statement_internal);
	return std::move(result);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformBetweenClause(PEGTransformer &transformer,
                                              unique_ptr<ParsedExpression> other_operator_expression,
                                              unique_ptr<ParsedExpression> other_operator_expression_1) {
	auto result = make_uniq<BetweenExpression>(nullptr, std::move(other_operator_expression),
	                                           std::move(other_operator_expression_1));
	return std::move(result);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformLikeClause(PEGTransformer &transformer, const string &like_variations,
                                           unique_ptr<ParsedExpression> other_operator_expression,
                                           optional<unique_ptr<ParsedExpression>> escape_clause) {
	string like_variation = like_variations;
	bool case_insensitive_regex = TryRemoveRegexCaseInsensitiveSuffix(like_variation);
	bool is_regex_operator = IsRegexMatchFunctionName(like_variation);
	vector<unique_ptr<ParsedExpression>> like_children;
	like_children.push_back(std::move(other_operator_expression));
	if (case_insensitive_regex && escape_clause) {
		throw ParserException(
		    "ESCAPE clause is not supported with case-insensitive regular expression match operators");
	}
	if (escape_clause) {
		if (like_variation == "~~") {
			like_variation = "like_escape";
		} else if (like_variation == "~~*") {
			like_variation = "ilike_escape";
		}
		like_children.push_back(std::move(*escape_clause));
	}
	if (case_insensitive_regex) {
		like_children.push_back(make_uniq<ConstantExpression>(Value("i")));
	}
	auto result = make_uniq<FunctionExpression>(like_variation, std::move(like_children));
	if (!is_regex_operator) {
		result->is_operator = true;
	}
	return std::move(result);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformEscapeClause(PEGTransformer &transformer,
                                             unique_ptr<ParsedExpression> comparison_expression) {
	return comparison_expression;
}

string PEGTransformerFactory::TransformLikeToken(PEGTransformer &transformer) {
	return "~~";
}

string PEGTransformerFactory::TransformILikeToken(PEGTransformer &transformer) {
	return "~~*";
}

string PEGTransformerFactory::TransformGlobToken(PEGTransformer &transformer) {
	return "~~~";
}

string PEGTransformerFactory::TransformSimilarToToken(PEGTransformer &transformer) {
	return "regexp_full_match";
}

string PEGTransformerFactory::TransformRegexMatchToken(PEGTransformer &transformer) {
	return RegexMatchOperatorFunctionName(transformer);
}

string PEGTransformerFactory::TransformRegexInsensitiveMatchToken(PEGTransformer &transformer) {
	return RegexMatchOperatorFunctionName(transformer) + "__case_insensitive";
}

string PEGTransformerFactory::TransformNotILikeOp(PEGTransformer &transformer) {
	return "!~~*";
}

string PEGTransformerFactory::TransformNotLikeOp(PEGTransformer &transformer) {
	return "!~~";
}

string PEGTransformerFactory::TransformNotRegexInsensitiveMatchOp(PEGTransformer &transformer) {
	return "!" + RegexMatchOperatorFunctionName(transformer) + "__case_insensitive";
}

string PEGTransformerFactory::TransformNotSimilarToOp(PEGTransformer &transformer) {
	return "!" + RegexMatchOperatorFunctionName(transformer);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformOtherOperatorExpression(PEGTransformer &transformer,
                                                        unique_ptr<ParsedExpression> bitwise_expression,
                                                        optional<vector<OtherOperatorTail>> other_operator_tail) {
	auto expr = std::move(bitwise_expression);
	if (!other_operator_tail) {
		return expr;
	}
	for (auto &other_operator_expr : *other_operator_tail) {
		auto right_expr = std::move(other_operator_expr.expression);
		if (other_operator_expr.op.is_any_all) {
			auto op_string = other_operator_expr.op.name;
			auto is_any = other_operator_expr.op.is_any;

			// Map operator string to ExpressionType (INVALID if not a comparison operator)
			auto expression_type = OperatorToExpressionType(op_string);

			auto subquery_expr = make_uniq<SubqueryExpression>();
			if (right_expr->GetExpressionClass() == ExpressionClass::SUBQUERY) {
				if (expression_type == ExpressionType::INVALID) {
					throw ParserException("ANY and ALL operators require one of =,<>,>,<,>=,<= comparisons!");
				}
				subquery_expr->subquery_type = SubqueryType::ANY;
				subquery_expr->comparison_type = expression_type;
				auto &right_expr_subquery = right_expr->Cast<SubqueryExpression>();
				subquery_expr->subquery = std::move(right_expr_subquery.subquery);
				subquery_expr->child = std::move(expr);
				if (!is_any) {
					// ALL sublink is equivalent to NOT(ANY) with inverted comparison
					// e.g. [= ALL()] is equivalent to [NOT(<> ANY())]
					// first invert the comparison type
					subquery_expr->comparison_type =
					    NegateComparisonExpression(subquery_expr->comparison_type);
					return make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(subquery_expr));
				}
				expr = std::move(subquery_expr);
			} else {
				string regex_function_name;
				bool regex_negated;
				bool regex_case_insensitive;
				if (TryGetRegexMatchOperator(op_string, transformer, regex_function_name, regex_negated,
				                             regex_case_insensitive)) {
					expr = TransformRegexAnyAllList(std::move(expr), std::move(right_expr), regex_function_name,
					                                regex_negated, regex_case_insensitive, is_any);
					continue;
				}
				// left=ANY(right)
				// we turn this into left=ANY((SELECT UNNEST(right)))
				if (expression_type == ExpressionType::INVALID) {
					throw ParserException("Unsupported comparison \"%s\" for ANY/ALL subquery", op_string);
				}
				auto select_statement = make_uniq<SelectStatement>();
				auto select_node = make_uniq<SelectNode>();
				vector<unique_ptr<ParsedExpression>> children;
				children.push_back(std::move(right_expr));

				select_node->select_list.push_back(make_uniq<FunctionExpression>("UNNEST", std::move(children)));
				select_node->from_table = make_uniq<EmptyTableRef>();
				select_statement->node = std::move(select_node);
				subquery_expr->subquery = std::move(select_statement);
				subquery_expr->subquery_type = SubqueryType::ANY;
				subquery_expr->child = std::move(expr);
				subquery_expr->comparison_type = expression_type;
				if (!is_any) {
					// ALL sublink is equivalent to NOT(ANY) with inverted comparison
					// e.g. [= ALL()] is equivalent to [NOT(<> ANY())]
					// first invert the comparison type
					subquery_expr->comparison_type =
					    NegateComparisonExpression(subquery_expr->comparison_type);
					return make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(subquery_expr));
				}
				return std::move(subquery_expr);
			}
		} else {
			auto other_operator = std::move(other_operator_expr.op.name);
			vector<unique_ptr<ParsedExpression>> children_function;
			children_function.push_back(std::move(expr));
			children_function.push_back(std::move(right_expr));
			vector split_operator = StringUtil::Split(other_operator, ".");
			string schema_name;
			string func_name = "";
			if (split_operator.size() == 1) {
				func_name = split_operator[0];
			} else if (split_operator.size() == 2) {
				schema_name = split_operator[0];
				func_name = split_operator[1];
			} else {
				throw ParserException("Too many identifiers found, expected schema.operator or operator");
			}

			auto func_expr = make_uniq<FunctionExpression>(
			    INVALID_CATALOG, std::move(schema_name), std::move(func_name),
			    std::move(children_function));
			func_expr->is_operator = true;
			expr = std::move(func_expr);
		}
	}
	return expr;
}

OtherOperatorTail PEGTransformerFactory::TransformOtherOperatorTail(PEGTransformer &transformer,
                                                                    ParsedOperator other_operator,
                                                                    unique_ptr<ParsedExpression> bitwise_expression) {
	OtherOperatorTail result;
	result.op = std::move(other_operator);
	result.expression = std::move(bitwise_expression);
	return result;
}

ParsedOperator PEGTransformerFactory::TransformOtherOperator(PEGTransformer &transformer, ParseResult &choice_result) {
	ParsedOperator result;
	// OperatorLiteral matches any operator token and produces an OperatorParseResult directly
	if (choice_result.type == ParseResultType::OPERATOR) {
		result.name = choice_result.Cast<OperatorParseResult>().operator_token;
		return result;
	}
	if (StringUtil::CIEquals(choice_result.name, "AnyAllOperator")) {
		auto any_all = transformer.Transform<pair<string, bool>>(choice_result);
		result.name = any_all.first;
		result.is_any_all = true;
		result.is_any = any_all.second;
		return result;
	}
	result.name = transformer.Transform<string>(choice_result);
	return result;
}

string PEGTransformerFactory::TransformQualifiedOperator(PEGTransformer &transformer,
                                                         const string &qualified_operator_contents) {
	return qualified_operator_contents;
}

string PEGTransformerFactory::TransformQualifiedOperatorContents(PEGTransformer &transformer,
                                                                 const optional<vector<string>> &col_id_dot,
                                                                 const string &any_op) {
	vector<string> result;
	if (col_id_dot) {
		result = *col_id_dot;
	}
	result.push_back(any_op);
	return StringUtil::Join(result, ".");
}

pair<string, bool> PEGTransformerFactory::TransformAnyAllOperator(PEGTransformer &transformer, const string &any_op,
                                                                  const bool &any_or_all) {
	return make_pair(any_op, any_or_all);
}

bool PEGTransformerFactory::TransformSubqueryAny(PEGTransformer &transformer) {
	return true;
}

bool PEGTransformerFactory::TransformSubqueryAll(PEGTransformer &transformer) {
	return false;
}
#endif
} // namespace duckpgq_peg
} // namespace duckdb
