#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/compat/alter_access.hpp"
#include "duckdb/parser/expression/collate_expression.hpp"
#include "duckdb/parser/expression/parameter_expression.hpp"
#include "duckdb/parser/expression/positional_reference_expression.hpp"
namespace duckdb {
namespace duckpgq_peg {
unique_ptr<ParsedExpression> PEGTransformerFactory::TransformCollateExpression(
    PEGTransformer &transformer, unique_ptr<ParsedExpression> at_time_zone_expression,
    optional<vector<unique_ptr<ParsedExpression>>> collate_expression_tail) {
	auto expr = std::move(at_time_zone_expression);
	if (!collate_expression_tail) {
		return expr;
	}
	for (auto &collate_string_expr : *collate_expression_tail) {
		string collate_string;
		if (collate_string_expr->GetExpressionClass() == ExpressionClass::CONSTANT) {
			auto &const_expr = collate_string_expr->Cast<ConstantExpression>();
			collate_string = duckpgq_compat::ConstantValue(const_expr).GetValue<string>();
		} else if (collate_string_expr->GetExpressionClass() == ExpressionClass::COLUMN_REF) {
			auto &col_ref = collate_string_expr->Cast<ColumnRefExpression>();
			collate_string = StringUtil::Join(duckpgq_compat::ColumnNames(col_ref), ".");
		} else {
			throw NotImplementedException("Unexpected expression encountered for collate, %s",
			                              EnumUtil::ToString(collate_string_expr->GetExpressionClass()));
		}
		auto collate_expr = make_uniq<CollateExpression>(collate_string, std::move(expr));
		expr = std::move(collate_expr);
	}
	return expr;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformAnonymousParameter(PEGTransformer &transformer) {
	// AnonymousParameter <- '?'
	auto expr = make_uniq<ParameterExpression>();

	// Auto-increment the parameter count
	idx_t known_param_index = transformer.ParamCount() + 1;
	string identifier = StringUtil::Format("%d", known_param_index);

	// Register it
	transformer.SetParam(Identifier(identifier), known_param_index, PreparedParamType::AUTO_INCREMENT);
	transformer.SetParamCount(MaxValue<idx_t>(transformer.ParamCount(), known_param_index));
	transformer.has_anonymous_parameters = true;

	#if __has_include("duckdb/common/identifier.hpp")
	expr->IdentifierMutable() = Identifier(identifier);
#else
	expr->identifier = identifier;
#endif
	return std::move(expr);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformQuestionMarkNumberedParameter(PEGTransformer &transformer,
                                                              unique_ptr<ParsedExpression> number_literal) {
	// QuestionMarkNumberedParameter <- '?' NumberLiteral
	auto &const_expr = number_literal->Cast<ConstantExpression>();
	int32_t param_number = duckpgq_compat::ConstantValue(const_expr).GetValue<int32_t>();

	if (param_number <= 0) {
		throw ParserException("Parameter numbers must be greater than 0");
	}

	auto expr = make_uniq<ParameterExpression>();
	string identifier = duckpgq_compat::ConstantValue(const_expr).ToString();
	idx_t known_param_index = DConstants::INVALID_INDEX;

	transformer.GetParam(Identifier(identifier), known_param_index, PreparedParamType::POSITIONAL);

	if (known_param_index == DConstants::INVALID_INDEX) {
		known_param_index = NumericCast<idx_t>(param_number);
		transformer.SetParam(Identifier(identifier), known_param_index, PreparedParamType::POSITIONAL);
	}

	#if __has_include("duckdb/common/identifier.hpp")
	expr->IdentifierMutable() = Identifier(identifier);
#else
	expr->identifier = identifier;
#endif
	transformer.SetParamCount(MaxValue<idx_t>(transformer.ParamCount(), known_param_index));
	return std::move(expr);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformNumberedParameter(PEGTransformer &transformer,
                                                  unique_ptr<ParsedExpression> number_literal) {
	// NumberedParameter <- '$' NumberLiteral
	auto &const_expr = number_literal->Cast<ConstantExpression>();
	int32_t param_number = duckpgq_compat::ConstantValue(const_expr).GetValue<int32_t>();

	if (param_number <= 0) {
		throw ParserException("Parameter numbers must be greater than 0");
	}

	auto expr = make_uniq<ParameterExpression>();
	string identifier = duckpgq_compat::ConstantValue(const_expr).ToString();
	idx_t known_param_index = DConstants::INVALID_INDEX;

	transformer.GetParam(Identifier(identifier), known_param_index, PreparedParamType::POSITIONAL);

	if (known_param_index == DConstants::INVALID_INDEX) {
		known_param_index = NumericCast<idx_t>(param_number);
		transformer.SetParam(Identifier(identifier), known_param_index, PreparedParamType::POSITIONAL);
	}

	#if __has_include("duckdb/common/identifier.hpp")
	expr->IdentifierMutable() = Identifier(identifier);
#else
	expr->identifier = identifier;
#endif
	transformer.SetParamCount(MaxValue<idx_t>(transformer.ParamCount(), known_param_index));
	transformer.has_anonymous_parameters = true;
	return std::move(expr);
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformColLabelParameter(PEGTransformer &transformer,
                                                                               const string &col_label) {
	// ColLabelParameter <- '$' ColLabel
	const string &identifier = col_label;

	auto expr = make_uniq<ParameterExpression>();
	idx_t known_param_index = DConstants::INVALID_INDEX;

	transformer.GetParam(Identifier(identifier), known_param_index, PreparedParamType::NAMED);

	if (known_param_index == DConstants::INVALID_INDEX) {
		// New named parameter gets the next available index
		known_param_index = transformer.ParamCount() + 1;
		transformer.SetParam(Identifier(identifier), known_param_index, PreparedParamType::NAMED);
	}

	#if __has_include("duckdb/common/identifier.hpp")
	expr->IdentifierMutable() = Identifier(identifier);
#else
	expr->identifier = identifier;
#endif
	transformer.SetParamCount(MaxValue<idx_t>(transformer.ParamCount(), known_param_index));
	return std::move(expr);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformPositionalExpression(PEGTransformer &transformer,
                                                     unique_ptr<ParsedExpression> number_literal) {
	auto &const_expr = number_literal->Cast<ConstantExpression>();
	int32_t index = duckpgq_compat::ConstantValue(const_expr).GetValue<int32_t>();
	if (index <= 0) {
		throw ParserException("Positional reference node needs to be >= 1");
	}
	return make_uniq<PositionalReferenceExpression>(NumericCast<idx_t>(index));
}
} // namespace duckpgq_peg
} // namespace duckdb
