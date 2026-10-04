#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/compat/function_access.hpp"
namespace duckdb {
namespace duckpgq_peg {
unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformCoalesceExpression(PEGTransformer &transformer,
                                                   vector<unique_ptr<ParsedExpression>> expression) {
	auto result = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_COALESCE);
	for (auto &expr : expression) {
		duckpgq_compat::OperatorChildren(*result).push_back(std::move(expr));
	}
	return std::move(result);
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformUnpackExpression(PEGTransformer &transformer,
                                                                              unique_ptr<ParsedExpression> expression) {
	auto result = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_UNPACK);
	duckpgq_compat::OperatorChildren(*result).push_back(std::move(expression));
	return std::move(result);
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformTryExpression(PEGTransformer &transformer,
                                                                           unique_ptr<ParsedExpression> expression) {
	auto result = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_TRY);
	duckpgq_compat::OperatorChildren(*result).push_back(std::move(expression));
	return std::move(result);
}
} // namespace duckpgq_peg
} // namespace duckdb
