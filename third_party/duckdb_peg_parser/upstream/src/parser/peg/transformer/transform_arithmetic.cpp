#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/compat/alter_access.hpp"
namespace duckdb {
namespace duckpgq_peg {
unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformBitwiseExpression(PEGTransformer &transformer,
                                                  unique_ptr<ParsedExpression> additive_expression,
                                                  optional<vector<BinaryExpressionTail>> bitwise_expression_tail) {
	auto expr = std::move(additive_expression);
	if (!bitwise_expression_tail) {
		return expr;
	}
	auto bit_depth_guard = transformer.StackCheck(bitwise_expression_tail->size());
	for (auto &bit_expr : *bitwise_expression_tail) {
		vector<unique_ptr<ParsedExpression>> bit_children;
		bit_children.push_back(std::move(expr));
		bit_children.push_back(std::move(bit_expr.expression));
		auto func_expr = make_uniq<FunctionExpression>(duckpgq_compat::HostName(Identifier(std::move(bit_expr.op))), std::move(bit_children));
		#if __has_include("duckdb/common/identifier.hpp")
		func_expr->IsOperatorMutable() = true;
#else
		func_expr->is_operator = true;
#endif
		expr = std::move(func_expr);
	}
	return expr;
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformAdditiveExpression(PEGTransformer &transformer,
                                                   unique_ptr<ParsedExpression> multiplicative_expression,
                                                   optional<vector<BinaryExpressionTail>> additive_expression_tail) {
	auto expr = std::move(multiplicative_expression);
	if (!additive_expression_tail) {
		return expr;
	}
	auto add_depth_guard = transformer.StackCheck(additive_expression_tail->size());
	for (auto &term_expr : *additive_expression_tail) {
		vector<unique_ptr<ParsedExpression>> term_children;
		term_children.push_back(std::move(expr));
		term_children.push_back(std::move(term_expr.expression));
		auto func_expr = make_uniq<FunctionExpression>(duckpgq_compat::HostName(Identifier(std::move(term_expr.op))), std::move(term_children));
		#if __has_include("duckdb/common/identifier.hpp")
		func_expr->IsOperatorMutable() = true;
#else
		func_expr->is_operator = true;
#endif
		if (term_expr.query_location.IsValid()) {
			transformer.SetQueryLocation(*func_expr, term_expr.query_location);
		}
		expr = std::move(func_expr);
	}
	return expr;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformMultiplicativeExpression(
    PEGTransformer &transformer, unique_ptr<ParsedExpression> exponentiation_expression,
    optional<vector<BinaryExpressionTail>> multiplicative_expression_tail) {
	auto expr = std::move(exponentiation_expression);
	if (!multiplicative_expression_tail) {
		return expr;
	}
	auto mul_depth_guard = transformer.StackCheck(multiplicative_expression_tail->size());
	for (auto &factor_expr : *multiplicative_expression_tail) {
		auto factor = std::move(factor_expr.op);
		if (factor == "/" && transformer.options.integer_division) {
			factor = "//";
		}
		vector<unique_ptr<ParsedExpression>> factor_children;
		factor_children.push_back(std::move(expr));
		factor_children.push_back(std::move(factor_expr.expression));
		auto func_expr = make_uniq<FunctionExpression>(duckpgq_compat::HostName(Identifier(std::move(factor))), std::move(factor_children));
		#if __has_include("duckdb/common/identifier.hpp")
		func_expr->IsOperatorMutable() = true;
#else
		func_expr->is_operator = true;
#endif
		expr = std::move(func_expr);
	}
	return expr;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformExponentiationExpression(
    PEGTransformer &transformer, unique_ptr<ParsedExpression> collate_expression,
    optional<vector<BinaryExpressionTail>> exponentiation_expression_tail) {
	auto expr = std::move(collate_expression);
	if (!exponentiation_expression_tail) {
		return expr;
	}
	for (auto &exponent_expr : *exponentiation_expression_tail) {
		vector<unique_ptr<ParsedExpression>> exponent_children;
		exponent_children.push_back(std::move(expr));
		exponent_children.push_back(std::move(exponent_expr.expression));
		auto func_expr =
		    make_uniq<FunctionExpression>(duckpgq_compat::HostName(Identifier(std::move(exponent_expr.op))), std::move(exponent_children));
		#if __has_include("duckdb/common/identifier.hpp")
		func_expr->IsOperatorMutable() = true;
#else
		func_expr->is_operator = true;
#endif
		expr = std::move(func_expr);
	}
	return expr;
}
} // namespace duckpgq_peg
} // namespace duckdb
