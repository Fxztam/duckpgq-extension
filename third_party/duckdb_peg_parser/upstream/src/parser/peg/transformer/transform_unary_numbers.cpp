#include "duckdb/common/extra_type_info.hpp"
#include "duckdb/common/enums/date_part_specifier.hpp"
#include "duckdb/common/operator/cast_operators.hpp"
#include "duckdb/common/types/decimal.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/common/types/hugeint.hpp"
#include "duckdb/common/limits.hpp"
#include "duckdb/common/operator/negate.hpp"
#include "duckdb/parser/expression/type_expression.hpp"
#include "duckdb/common/types/bignum.hpp"

#include "duckpgq/compat/alter_access.hpp"

namespace duckdb {
namespace duckpgq_peg {
unique_ptr<ParsedExpression> PEGTransformerFactory::TryNegateValue(const ConstantExpression &expr) {
	auto &val = duckpgq_compat::ConstantValue(expr);

	switch (val.type().id()) {
	case LogicalTypeId::INTEGER: {
		auto raw = val.GetValue<int32_t>();
		if (!NegateOperator::CanNegate<int32_t>(raw)) {
			return make_uniq<ConstantExpression>(Value::BIGINT(-static_cast<int64_t>(raw)));
		}
		return make_uniq<ConstantExpression>(Value::INTEGER(-raw));
	}
	case LogicalTypeId::BIGINT: {
		auto raw = val.GetValue<int64_t>();
		if (!NegateOperator::CanNegate<int64_t>(raw)) {
			return make_uniq<ConstantExpression>(Value::HUGEINT(-static_cast<hugeint_t>(raw)));
		}
		return make_uniq<ConstantExpression>(Value::BIGINT(-raw));
	}
	case LogicalTypeId::HUGEINT: {
		auto raw = val.GetValue<hugeint_t>();
		if (!NegateOperator::CanNegate<hugeint_t>(raw)) {
			return nullptr;
		}
		return make_uniq<ConstantExpression>(Value::HUGEINT(-raw));
	}
	case LogicalTypeId::UHUGEINT: {
		auto uval = val.GetValue<uhugeint_t>();
		uhugeint_t abs_min_hugeint = static_cast<uhugeint_t>(NumericLimits<hugeint_t>::Maximum()) + 1;

		if (uval == abs_min_hugeint) {
			return make_uniq<ConstantExpression>(Value::HUGEINT(NumericLimits<hugeint_t>::Minimum()));
		}
		if (uval < abs_min_hugeint) {
			return make_uniq<ConstantExpression>(Value::HUGEINT(-static_cast<hugeint_t>(uval)));
		}
		return nullptr;
	}
	case LogicalTypeId::DOUBLE:
		return make_uniq<ConstantExpression>(Value::DOUBLE(-val.GetValue<double>()));
	default:
		return nullptr;
	}
}

unique_ptr<ParsedExpression> PEGTransformerFactory::ConvertNumberToValue(string val) {
	string_t str_val(val);
	bool try_cast_as_integer = true;
	bool try_cast_as_decimal = true;
	optional_idx decimal_position = optional_idx::Invalid();
	idx_t num_underscores = 0;
	idx_t num_integer_underscores = 0;
	for (idx_t i = 0; i < str_val.GetSize(); i++) {
		if (val[i] == '.') {
			// decimal point: cast as either decimal or double
			try_cast_as_integer = false;
			decimal_position = i;
		}
		if (val[i] == 'e' || val[i] == 'E') {
			// found exponent, cast as double
			try_cast_as_integer = false;
			try_cast_as_decimal = false;
		}
		if (val[i] == '_') {
			num_underscores++;
			if (!decimal_position.IsValid()) {
				num_integer_underscores++;
			}
		}
	}
	if (try_cast_as_integer) {
		int32_t int_value;
		if (TryCast::Operation<string_t, int32_t>(str_val, int_value)) {
			return make_uniq<ConstantExpression>(Value::INTEGER(int_value));
		}
		int64_t bigint_value;
		// try to cast as bigint first
		if (TryCast::Operation<string_t, int64_t>(str_val, bigint_value)) {
			// successfully cast to bigint: bigint value
			return make_uniq<ConstantExpression>(Value::BIGINT(bigint_value));
		}
		hugeint_t hugeint_value;
		// if that is not successful; try to cast as hugeint
		if (TryCast::Operation<string_t, hugeint_t>(str_val, hugeint_value)) {
			// successfully cast to bigint: bigint value
			return make_uniq<ConstantExpression>(Value::HUGEINT(hugeint_value));
		}
		uhugeint_t uhugeint_value;
		// if that is not successful; try to cast as uhugeint
		if (TryCast::Operation<string_t, uhugeint_t>(str_val, uhugeint_value)) {
			// successfully cast to bigint: bigint value
			return make_uniq<ConstantExpression>(Value::UHUGEINT(uhugeint_value));
		}
		// if that is not successful; try to cast as bignum for very large integers
		// this preserves precision for integers that exceed uhugeint limits
		try {
			auto bignum_str = Bignum::VarcharToBignum(str_val);
			return make_uniq<ConstantExpression>(Value::BIGNUM(bignum_str));
		} catch (const ConversionException &) {
			// if bignum parsing fails (e.g., invalid format), continue to decimal or double fallback
		}
	}
	idx_t decimal_offset = val[0] == '-' ? 3 : 2;
	if (try_cast_as_decimal && decimal_position.IsValid() &&
	    str_val.GetSize() - num_underscores < Decimal::MAX_WIDTH_DECIMAL + decimal_offset) {
		// figure out the width/scale based on the decimal position
		auto width = NumericCast<uint8_t>(str_val.GetSize() - 1 - num_underscores);
		auto scale = NumericCast<uint8_t>(width - decimal_position.GetIndex() + num_integer_underscores);
		if (val[0] == '-') {
			width--;
		}
		if (width <= Decimal::MAX_WIDTH_DECIMAL) {
			// we can cast the value as a decimal
			Value val_width = Value(str_val).DefaultCastAs(LogicalType::DECIMAL(width, scale));
			return make_uniq<ConstantExpression>(std::move(val_width));
		}
	}
	// if there is a decimal or the value is too big to cast as either hugeint or bigint
	double dbl_value = Cast::Operation<string_t, double>(str_val);
	return make_uniq<ConstantExpression>(Value::DOUBLE(dbl_value));
}

// NumberLiteral <- < [+-]?[0-9]*([.][0-9]*)? >
unique_ptr<ParsedExpression> PEGTransformerFactory::TransformNumberLiteral(PEGTransformer &transformer,
                                                                           ParseResult &parse_result) {
	auto &literal_pr = parse_result.Cast<NumberParseResult>();
	return ConvertNumberToValue(literal_pr.number);
}

bool IsNumberLiteral(ParseResult &pr) {
	if (pr.name == "BaseExpression") {
		auto &list = pr.Cast<ListParseResult>();
		if (list.GetChild(1).Cast<OptionalParseResult>().HasResult()) {
			return false;
		}
		return IsNumberLiteral(list.GetChild(0));
	}
	if (pr.name == "SingleExpression") {
		auto &list = pr.Cast<ListParseResult>();
		return IsNumberLiteral(list.GetChild(0).Cast<ChoiceParseResult>().GetResult());
	}
	if (pr.name == "LiteralExpression") {
		auto &list = pr.Cast<ListParseResult>();
		return IsNumberLiteral(list.GetChild(0).Cast<ChoiceParseResult>().GetResult());
	}
	return pr.name == "NumberLiteral";
}

string GetRawText(ParseResult &pr) {
	if (pr.name == "NumberLiteral") {
		return pr.Cast<NumberParseResult>().number;
	}
	if (pr.name == "BaseExpression") {
		return GetRawText(pr.Cast<ListParseResult>().GetChild(0));
	}
	if (pr.name == "SingleExpression" || pr.name == "LiteralExpression") {
		auto &list = pr.Cast<ListParseResult>();
		return GetRawText(list.GetChild(0).Cast<ChoiceParseResult>().GetResult());
	}
	return "";
}

// PrefixExpression <- PrefixOperator* BaseExpression
unique_ptr<ParsedExpression> PEGTransformerFactory::TransformPrefixExpression(PEGTransformer &transformer,
                                                                              ParseResult &parse_result) {
	auto &list_pr = parse_result.Cast<ListParseResult>();
	auto &prefix_opt = list_pr.Child<OptionalParseResult>(0);
	auto &base_expr_pr = list_pr.Child<ListParseResult>(1);

	if (!prefix_opt.HasResult()) {
		return transformer.Transform<unique_ptr<ParsedExpression>>(base_expr_pr);
	}

	auto &prefix_repeat = prefix_opt.GetResult().Cast<RepeatParseResult>();

	// --- SPECIAL CASE: Handle -<Number> atomically to prevent overflow/precision loss ---
	// We only do this if there is exactly one prefix and it is a minus.
	if (prefix_repeat.GetChildren().size() == 1) {
		auto prefix = transformer.Transform<string>(prefix_repeat.GetChildren()[0]);
		if (prefix == "-" && IsNumberLiteral(base_expr_pr)) {
			string raw_number = GetRawText(base_expr_pr);
			string full_text = "-" + raw_number;
			return ConvertNumberToValue(full_text);
		}
	}

	auto expr = transformer.Transform<unique_ptr<ParsedExpression>>(base_expr_pr);

	vector<string> prefixes;
	for (auto &child_ref : prefix_repeat.GetChildren()) {
		prefixes.push_back(transformer.Transform<string>(child_ref));
	}

	for (auto it = prefixes.rbegin(); it != prefixes.rend(); ++it) {
		const string &prefix = *it;

		if (prefix == "-" && expr->GetExpressionType() == ExpressionType::VALUE_CONSTANT) {
			auto &const_expr = expr->Cast<ConstantExpression>();
			if (auto negated_expr = TryNegateValue(const_expr)) {
				expr = std::move(negated_expr);
				continue;
			}
		}

		vector<unique_ptr<ParsedExpression>> children;
		children.push_back(std::move(expr));
		auto func_expr = make_uniq<FunctionExpression>(duckpgq_compat::HostName(Identifier(prefix)), std::move(children));
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
