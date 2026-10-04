#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/compat/alter_access.hpp"
#include "duckdb/common/enums/date_part_specifier.hpp"
#include "duckdb/optimizer/rule/date_trunc_simplification.hpp"
namespace duckdb {
namespace duckpgq_peg {
LogicalType PEGTransformerFactory::GetIntervalTargetType(DatePartSpecifier date_part) {
	switch (date_part) {
	case DatePartSpecifier::YEAR:
	case DatePartSpecifier::MONTH:
	case DatePartSpecifier::DAY:
	case DatePartSpecifier::WEEK:
	case DatePartSpecifier::QUARTER:
	case DatePartSpecifier::DECADE:
	case DatePartSpecifier::CENTURY:
	case DatePartSpecifier::MILLENNIUM:
		return LogicalType::INTEGER;
	case DatePartSpecifier::HOUR:
	case DatePartSpecifier::MINUTE:
	case DatePartSpecifier::MICROSECONDS:
		return LogicalType::BIGINT;
	case DatePartSpecifier::MILLISECONDS:
	case DatePartSpecifier::SECOND:
		return LogicalType::DOUBLE;
	default:
		throw InternalException("Unsupported interval post-fix");
	}
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformIntervalLiteral(PEGTransformer &transformer,
                                                unique_ptr<ParsedExpression> interval_parameter,
                                                const optional<DatePartSpecifier> &interval) {
	DatePartSpecifier interval_unit = DatePartSpecifier::INVALID;
	if (interval) {
		interval_unit = *interval;
	}
	auto expr = std::move(interval_parameter);
	auto func_name = DateTruncSimplificationRule::DatePartToFunc(interval_unit);
	if (func_name.empty()) {
		expr = make_uniq<CastExpression>(LogicalType::INTERVAL, std::move(expr));
		return expr;
	}
	LogicalType parse_type = LogicalType::DOUBLE;
	expr = make_uniq<CastExpression>(parse_type, std::move(expr));
	auto target_type = GetIntervalTargetType(interval_unit);
	if (target_type != parse_type) {
		vector<unique_ptr<ParsedExpression>> children;
		children.push_back(std::move(expr));
		expr = make_uniq<FunctionExpression>("trunc", std::move(children));
		expr = make_uniq<CastExpression>(target_type, std::move(expr));
	}
	vector<unique_ptr<ParsedExpression>> children;
	children.push_back(std::move(expr));
	auto result = make_uniq<FunctionExpression>(duckpgq_compat::HostName(Identifier(func_name)), std::move(children));
	return std::move(result);
}
} // namespace duckpgq_peg
} // namespace duckdb
