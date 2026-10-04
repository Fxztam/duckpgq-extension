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

static unique_ptr<ParsedExpression> MakeSimpleType(Identifier name,
                                                 vector<unique_ptr<ParsedExpression>> children) {
	return make_uniq<TypeExpression>(duckpgq_compat::HostName(name), std::move(children));
}

string PEGTransformerFactory::TransformIdentifierOrKeyword(PEGTransformer &transformer, ParseResult &parse_result) {
	if (parse_result.type == ParseResultType::IDENTIFIER) {
		return parse_result.Cast<IdentifierParseResult>().identifier.GetIdentifierName();
	}
	if (parse_result.type == ParseResultType::KEYWORD) {
		return parse_result.Cast<KeywordParseResult>().keyword;
	}
	if (parse_result.type == ParseResultType::CHOICE) {
		auto &choice_pr = parse_result.Cast<ChoiceParseResult>();
		return transformer.Transform<string>(choice_pr.GetResult());
	}
	if (parse_result.type == ParseResultType::LIST) {
		auto &list_pr = parse_result.Cast<ListParseResult>();
		for (auto child : list_pr.GetChildren()) {
			if (child.get().type == ParseResultType::LIST &&
			    child.get().Cast<ListParseResult>().GetChildren().empty()) {
				continue;
			}
			if (child.get().type == ParseResultType::CHOICE) {
				auto &choice_result = child.get().Cast<ChoiceParseResult>().GetResult();
				if (choice_result.type == ParseResultType::IDENTIFIER) {
					return choice_result.Cast<IdentifierParseResult>().identifier.GetIdentifierName();
				}
				if (choice_result.type == ParseResultType::KEYWORD) {
					return choice_result.Cast<KeywordParseResult>().keyword;
				}
				return transformer.Transform<string>(choice_result);
			}
			if (child.get().type == ParseResultType::IDENTIFIER) {
				return child.get().Cast<IdentifierParseResult>().identifier.GetIdentifierName();
			}
			throw InternalException("Unexpected IdentifierOrKeyword type encountered %s.",
			                        ParseResultToString(child.get().type));
		}
	}
	throw ParserException("Unexpected ParseResult type in identifier transformation.");
}

LogicalType PEGTransformerFactory::TransformType(PEGTransformer &transformer,
                                                 unique_ptr<ParsedExpression> type_variations,
                                                 const optional<vector<int64_t>> &array_bounds) {
	auto type = std::move(type_variations);
	if (array_bounds) {
		for (auto array_size : *array_bounds) {
			vector<unique_ptr<ParsedExpression>> children_types;
			children_types.push_back(std::move(type));

			if (array_size < 0) {
				type = MakeSimpleType(Identifier("list"), std::move(children_types));
			} else {
				children_types.push_back(make_uniq<ConstantExpression>(Value::BIGINT(array_size)));
				type = MakeSimpleType(Identifier("array"), std::move(children_types));
			}
		}
	}
	return LogicalType::UNBOUND(std::move(type));
}

int64_t PEGTransformerFactory::TransformArrayKeyword(PEGTransformer &transformer) {
	return -1;
}

int64_t PEGTransformerFactory::TransformSquareBracketsArray(PEGTransformer &transformer,
                                                            optional<unique_ptr<ParsedExpression>> expression) {
	if (!expression) {
		// Empty array so we return -1 to signify it's a list
		return -1;
	}
	auto &array_size = *expression;
	if (array_size->GetExpressionClass() != ExpressionClass::CONSTANT) {
		throw ParserException("Expected a constant number as array size");
	}
	auto &const_number = array_size->Cast<ConstantExpression>();
	if (!duckpgq_compat::ConstantValue(const_number).type().IsIntegral()) {
		throw BinderException("Expected an integer as array bound instead of %s", duckpgq_compat::ConstantValue(const_number).ToString());
	}
	auto number_val = duckpgq_compat::ConstantValue(const_number).GetValue<int64_t>();
	if (number_val < 0) {
		throw ParserException("Array size must be greater than 0");
	}
	return number_val;
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformTimeType(PEGTransformer &transformer, const LogicalTypeId &time_or_timestamp,
                                         optional<vector<unique_ptr<ParsedExpression>>> type_modifiers,
                                         const optional<bool> &time_zone) {
	auto type = time_or_timestamp;
	vector<unique_ptr<ParsedExpression>> modifiers;
	if (type_modifiers) {
		modifiers = std::move(*type_modifiers);
	}
	auto with_timezone = time_zone && *time_zone;
	if (type == LogicalTypeId::TIME) {
		if (!modifiers.empty()) {
			throw ParserException("Type TIME does not allow any modifiers");
		}
		if (with_timezone) {
			return MakeSimpleType(Identifier(EnumUtil::ToString(LogicalType::TIME_TZ)),
			                                 vector<unique_ptr<ParsedExpression>> {});
		}
		return MakeSimpleType(Identifier(EnumUtil::ToString(LogicalType::TIME)),
		                                 vector<unique_ptr<ParsedExpression>> {});
	}
	if (type == LogicalTypeId::TIMESTAMP) {
		if (modifiers.empty()) {
			if (with_timezone) {
				return MakeSimpleType(Identifier(EnumUtil::ToString(LogicalType::TIMESTAMP_TZ)),
				                                 vector<unique_ptr<ParsedExpression>> {});
			}
			return MakeSimpleType(Identifier(EnumUtil::ToString(LogicalType::TIMESTAMP)),
			                                 vector<unique_ptr<ParsedExpression>> {});
		}
		if (modifiers.size() > 1) {
			throw ParserException("TIMESTAMP only supports a single modifier");
		}
		if (modifiers[0]->GetExpressionClass() != ExpressionClass::CONSTANT) {
			throw ParserException("Expected a constant expression for timestamp precision");
		}
		auto timestamp_precision = duckpgq_compat::ConstantValue(modifiers[0]->Cast<ConstantExpression>()).GetValue<int64_t>();
		if (timestamp_precision > 10) {
			throw ParserException("TIMESTAMP only supports until nano-second precision (9)");
		}
		if (timestamp_precision < 0) {
			throw ParserException("TIMESTAMP precision should be between 0 and 10 (inclusive)");
		}
		if (timestamp_precision == 0) {
			return MakeSimpleType(Identifier(EnumUtil::ToString(LogicalType::TIMESTAMP_S)),
			                                 vector<unique_ptr<ParsedExpression>> {});
		}
		if (timestamp_precision <= 3) {
			return MakeSimpleType(Identifier(EnumUtil::ToString(LogicalType::TIMESTAMP_MS)),
			                                 vector<unique_ptr<ParsedExpression>> {});
		}
		if (timestamp_precision <= 6) {
			// Corresponds to microseconds, which is the default TIMESTAMP
			return MakeSimpleType(Identifier(EnumUtil::ToString(LogicalType::TIMESTAMP)),
			                                 vector<unique_ptr<ParsedExpression>> {});
		}
		return MakeSimpleType(Identifier(EnumUtil::ToString(LogicalType::TIMESTAMP_NS)),
		                                 vector<unique_ptr<ParsedExpression>> {});
	}
	throw ParserException("Unexpected time type encountered");
}

bool PEGTransformerFactory::TransformTimeZone(PEGTransformer &transformer, const bool &with_or_without) {
	return with_or_without;
}

bool PEGTransformerFactory::TransformWithRule(PEGTransformer &transformer) {
	return true;
}

bool PEGTransformerFactory::TransformWithoutRule(PEGTransformer &transformer) {
	return false;
}

LogicalTypeId PEGTransformerFactory::TransformTimeTypeId(PEGTransformer &transformer) {
	return LogicalTypeId::TIME;
}

LogicalTypeId PEGTransformerFactory::TransformTimestampTypeId(PEGTransformer &transformer) {
	return LogicalTypeId::TIMESTAMP;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformSimpleNumericType(PEGTransformer &transformer,
                                                                               const string &child) {
	return MakeSimpleType(Identifier(child), vector<unique_ptr<ParsedExpression>> {});
}

string PEGTransformerFactory::TransformIntType(PEGTransformer &transformer) {
	return LogicalTypeIdToString(LogicalTypeId::INTEGER);
}

string PEGTransformerFactory::TransformIntegerType(PEGTransformer &transformer) {
	return LogicalTypeIdToString(LogicalTypeId::INTEGER);
}

string PEGTransformerFactory::TransformSmallintType(PEGTransformer &transformer) {
	return LogicalTypeIdToString(LogicalTypeId::SMALLINT);
}

string PEGTransformerFactory::TransformBigintType(PEGTransformer &transformer) {
	return LogicalTypeIdToString(LogicalTypeId::BIGINT);
}

string PEGTransformerFactory::TransformRealType(PEGTransformer &transformer) {
	return LogicalTypeIdToString(LogicalTypeId::FLOAT);
}

string PEGTransformerFactory::TransformBooleanType(PEGTransformer &transformer) {
	return LogicalTypeIdToString(LogicalTypeId::BOOLEAN);
}

string PEGTransformerFactory::TransformDoubleType(PEGTransformer &transformer) {
	return LogicalTypeIdToString(LogicalTypeId::DOUBLE);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformFloatType(PEGTransformer &transformer,
                                          optional<unique_ptr<ParsedExpression>> number_literal) {
	return MakeSimpleType(Identifier("FLOAT"), vector<unique_ptr<ParsedExpression>> {});
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformDecimalType(PEGTransformer &transformer,
                                            optional<vector<unique_ptr<ParsedExpression>>> type_modifiers) {
	vector<unique_ptr<ParsedExpression>> modifiers;
	if (type_modifiers) {
		modifiers = std::move(*type_modifiers);
	}
	return MakeSimpleType(Identifier("DECIMAL"), std::move(modifiers));
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformDecType(PEGTransformer &transformer,
                                        optional<vector<unique_ptr<ParsedExpression>>> type_modifiers) {
	return TransformDecimalType(transformer, std::move(type_modifiers));
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformNumericModType(PEGTransformer &transformer,
                                               optional<vector<unique_ptr<ParsedExpression>>> type_modifiers) {
	return TransformDecimalType(transformer, std::move(type_modifiers));
}

vector<unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformTypeModifiers(PEGTransformer &transformer,
                                              optional<vector<unique_ptr<ParsedExpression>>> expression) {
	if (!expression) {
		return vector<unique_ptr<ParsedExpression>> {};
	}
	for (auto &expr : *expression) {
		if (expr->GetExpressionClass() != ExpressionClass::CONSTANT) {
			throw ParserException("Expected a constant as type modifier");
		}
	}
	return std::move(*expression);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformCharacterSimpleType(PEGTransformer &transformer,
                                                    optional<vector<unique_ptr<ParsedExpression>>> type_modifiers) {
	vector<unique_ptr<ParsedExpression>> modifiers;
	if (type_modifiers) {
		modifiers = std::move(*type_modifiers);
	}
	return MakeSimpleType(Identifier("VARCHAR"), std::move(modifiers));
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformQualifiedSimpleType(PEGTransformer &transformer,
                                                    const QualifiedName &qualified_type_name,
                                                    optional<vector<unique_ptr<ParsedExpression>>> type_modifiers) {
	vector<unique_ptr<ParsedExpression>> modifiers;
	if (type_modifiers) {
		modifiers = std::move(*type_modifiers);
	}
#if __has_include("duckdb/common/identifier.hpp")
	return make_uniq<TypeExpression>(qualified_type_name, std::move(modifiers));
#else
	return make_uniq<TypeExpression>(qualified_type_name.catalog, qualified_type_name.schema,
	                                 qualified_type_name.name, std::move(modifiers));
#endif
}

QualifiedName PEGTransformerFactory::TransformTypeNameAsQualifiedName(PEGTransformer &transformer,
                                                                      const Identifier &type_name) {
	auto result = duckpgq_compat::MakeQualifiedName(type_name);
	return result;
}

QualifiedName PEGTransformerFactory::TransformSchemaReservedTypeName(PEGTransformer &transformer,
                                                                     const Identifier &schema_qualification,
                                                                     const Identifier &reserved_type_name) {
	auto result = duckpgq_compat::MakeQualifiedName({schema_qualification}, reserved_type_name);
	return result;
}

QualifiedName PEGTransformerFactory::TransformCatalogReservedSchemaTypeName(
    PEGTransformer &transformer, const Identifier &catalog_qualification,
    const Identifier &reserved_schema_qualification, const Identifier &reserved_type_name) {
	auto result = duckpgq_compat::MakeQualifiedName({catalog_qualification, reserved_schema_qualification}, reserved_type_name);
	return result;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformMapType(PEGTransformer &transformer,
                                                                     const vector<LogicalType> &type) {
	if (type.size() != 2) {
		throw ParserException("Map type needs exactly two entries, key and value type.");
	}
	vector<unique_ptr<ParsedExpression>> map_children;
	map_children.push_back(UnboundType::GetTypeExpression(type[0])->Copy());
	map_children.push_back(UnboundType::GetTypeExpression(type[1])->Copy());
	return MakeSimpleType(Identifier("MAP"), std::move(map_children));
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformTupleType(PEGTransformer &transformer,
                                                                       const vector<LogicalType> &type) {
	vector<unique_ptr<ParsedExpression>> tuple_children;
	for (auto &child : type) {
		tuple_children.push_back(UnboundType::GetTypeExpression(child)->Copy());
	}
	return MakeSimpleType(Identifier("TUPLE"), std::move(tuple_children));
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformRowType(PEGTransformer &transformer,
                                        const optional<child_list_t<LogicalType>> &col_id_type_list) {
	vector<unique_ptr<ParsedExpression>> struct_children;
	if (col_id_type_list) {
		for (auto &child : *col_id_type_list) {
			auto &type_expr = UnboundType::GetTypeExpression(child.second);
			auto new_type_expr = type_expr->Copy();
			new_type_expr->SetAlias(child.first);
			struct_children.push_back(std::move(new_type_expr));
		}
	}
	return MakeSimpleType(Identifier("STRUCT"), std::move(struct_children));
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformGeometryType(PEGTransformer &transformer,
                                             optional<unique_ptr<ParsedExpression>> expression) {
	if (!expression) {
		return MakeSimpleType(Identifier("GEOMETRY"), vector<unique_ptr<ParsedExpression>> {});
	}
	auto geo_modifier = std::move(*expression);
	vector<unique_ptr<ParsedExpression>> geo_children;
	if (geo_modifier->GetExpressionClass() != ExpressionClass::CONSTANT) {
		throw ParserException("Expected a constant as type modifier");
	}
	geo_children.push_back(std::move(geo_modifier));
	return MakeSimpleType(Identifier("GEOMETRY"), std::move(geo_children));
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformVariantType(PEGTransformer &transformer) {
	return MakeSimpleType(Identifier("VARIANT"), vector<unique_ptr<ParsedExpression>> {});
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformUnionType(PEGTransformer &transformer,
                                          const child_list_t<LogicalType> &col_id_type_list) {
	vector<unique_ptr<ParsedExpression>> union_children;
	for (auto &colid : col_id_type_list) {
		auto &type_expr = UnboundType::GetTypeExpression(colid.second);
		auto new_type_expr = type_expr->Copy();
		new_type_expr->SetAlias(colid.first);
		union_children.push_back(std::move(new_type_expr));
	}
	return MakeSimpleType(Identifier("UNION"), std::move(union_children));
}

child_list_t<LogicalType>
PEGTransformerFactory::TransformColIdTypeList(PEGTransformer &transformer,
                                              const vector<pair<Identifier, LogicalType>> &col_id_type) {
	child_list_t<LogicalType> result;
	for (auto &child : col_id_type) {
		result.emplace_back(duckpgq_compat::HostName(child.first), child.second);
	}
	return result;
}

pair<Identifier, LogicalType> PEGTransformerFactory::TransformColIdType(PEGTransformer &transformer,
                                                                        const Identifier &col_id,
                                                                        const LogicalType &type) {
	return make_pair(Identifier(col_id), type);
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformBitType(
    PEGTransformer &transformer, const bool &has_result,
    optional<vector<unique_ptr<ParsedExpression>>> expression) { // NOLINT(performance-unnecessary-value-param)
	return MakeSimpleType(Identifier("BIT"), vector<unique_ptr<ParsedExpression>> {});
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformIntervalWithoutSpecifier(PEGTransformer &transformer) {
	return MakeSimpleType(Identifier("INTERVAL"), vector<unique_ptr<ParsedExpression>> {});
}

DatePartSpecifier PEGTransformerFactory::TransformIntervalToIntervalAsType(PEGTransformer &transformer,
                                                                           ParseResult &parse_result) {
	return DatePartSpecifier::INVALID;
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformIntervalWithRangeSpecifier(PEGTransformer &transformer,
                                                           const DatePartSpecifier &interval_to_interval_as_type) {
	return MakeSimpleType(Identifier("INTERVAL"), vector<unique_ptr<ParsedExpression>> {});
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformIntervalWithSimpleSpecifier(PEGTransformer &transformer,
                                                            const DatePartSpecifier &interval) {
	return MakeSimpleType(Identifier("INTERVAL"), vector<unique_ptr<ParsedExpression>> {});
}

DatePartSpecifier PEGTransformerFactory::TransformYearKeyword(PEGTransformer &transformer) {
	return DatePartSpecifier::YEAR;
}

DatePartSpecifier PEGTransformerFactory::TransformMonthKeyword(PEGTransformer &transformer) {
	return DatePartSpecifier::MONTH;
}

DatePartSpecifier PEGTransformerFactory::TransformDayKeyword(PEGTransformer &transformer) {
	return DatePartSpecifier::DAY;
}

DatePartSpecifier PEGTransformerFactory::TransformHourKeyword(PEGTransformer &transformer) {
	return DatePartSpecifier::HOUR;
}

DatePartSpecifier PEGTransformerFactory::TransformMinuteKeyword(PEGTransformer &transformer) {
	return DatePartSpecifier::MINUTE;
}

DatePartSpecifier PEGTransformerFactory::TransformSecondKeyword(PEGTransformer &transformer) {
	return DatePartSpecifier::SECOND;
}

DatePartSpecifier PEGTransformerFactory::TransformMillisecondKeyword(PEGTransformer &transformer) {
	return DatePartSpecifier::MILLISECONDS;
}

DatePartSpecifier PEGTransformerFactory::TransformMicrosecondKeyword(PEGTransformer &transformer) {
	return DatePartSpecifier::MICROSECONDS;
}

DatePartSpecifier PEGTransformerFactory::TransformWeekKeyword(PEGTransformer &transformer) {
	return DatePartSpecifier::WEEK;
}

DatePartSpecifier PEGTransformerFactory::TransformQuarterKeyword(PEGTransformer &transformer) {
	return DatePartSpecifier::QUARTER;
}

DatePartSpecifier PEGTransformerFactory::TransformDecadeKeyword(PEGTransformer &transformer) {
	return DatePartSpecifier::DECADE;
}

DatePartSpecifier PEGTransformerFactory::TransformCenturyKeyword(PEGTransformer &transformer) {
	return DatePartSpecifier::CENTURY;
}

DatePartSpecifier PEGTransformerFactory::TransformMillenniumKeyword(PEGTransformer &transformer) {
	return DatePartSpecifier::MILLENNIUM;
}

static DatePartSpecifier UnsupportedIntervalRange(DatePartSpecifier first_interval, DatePartSpecifier second_interval) {
	throw ParserException("%s TO %s is not supported", EnumUtil::ToString(first_interval),
	                      EnumUtil::ToString(second_interval));
}

DatePartSpecifier PEGTransformerFactory::TransformYearToMonth(PEGTransformer &transformer,
                                                              const DatePartSpecifier &year_keyword,
                                                              const DatePartSpecifier &month_keyword) {
	return UnsupportedIntervalRange(year_keyword, month_keyword);
}

DatePartSpecifier PEGTransformerFactory::TransformDayToHour(PEGTransformer &transformer,
                                                            const DatePartSpecifier &day_keyword,
                                                            const DatePartSpecifier &hour_keyword) {
	return UnsupportedIntervalRange(day_keyword, hour_keyword);
}

DatePartSpecifier PEGTransformerFactory::TransformDayToMinute(PEGTransformer &transformer,
                                                              const DatePartSpecifier &day_keyword,
                                                              const DatePartSpecifier &minute_keyword) {
	return UnsupportedIntervalRange(day_keyword, minute_keyword);
}

DatePartSpecifier PEGTransformerFactory::TransformDayToSecond(PEGTransformer &transformer,
                                                              const DatePartSpecifier &day_keyword,
                                                              const DatePartSpecifier &second_keyword) {
	return UnsupportedIntervalRange(day_keyword, second_keyword);
}

DatePartSpecifier PEGTransformerFactory::TransformHourToMinute(PEGTransformer &transformer,
                                                               const DatePartSpecifier &hour_keyword,
                                                               const DatePartSpecifier &minute_keyword) {
	return UnsupportedIntervalRange(hour_keyword, minute_keyword);
}

DatePartSpecifier PEGTransformerFactory::TransformHourToSecond(PEGTransformer &transformer,
                                                               const DatePartSpecifier &hour_keyword,
                                                               const DatePartSpecifier &second_keyword) {
	return UnsupportedIntervalRange(hour_keyword, second_keyword);
}

DatePartSpecifier PEGTransformerFactory::TransformMinuteToSecond(PEGTransformer &transformer,
                                                                 const DatePartSpecifier &minute_keyword,
                                                                 const DatePartSpecifier &second_keyword) {
	return UnsupportedIntervalRange(minute_keyword, second_keyword);
}


unique_ptr<ParsedExpression> PEGTransformerFactory::TransformSetofType(PEGTransformer &transformer,
                                                                       const LogicalType &type) {
	return UnboundType::GetTypeExpression(type)->Copy();
}

// StringLiteral <- '\'' [^\']* '\''
string PEGTransformerFactory::TransformStringLiteral(PEGTransformer &transformer, ParseResult &parse_result) {
	auto &string_literal_pr = parse_result.Cast<StringLiteralParseResult>();
	return string_literal_pr.result;
}

Identifier PEGTransformerFactory::TransformConstraintName(PEGTransformer &transformer,
                                                          const Identifier &col_id_or_string) {
	return Identifier(col_id_or_string);
}
} // namespace duckpgq_peg
} // namespace duckdb
