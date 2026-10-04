#include "duckpgq/compat/name_metadata.hpp"
#include "duckpgq/compat/function_access.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/ast/trigger_type_compat.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/matcher.hpp"
#include "duckdb/common/to_string.hpp"
#include "duckdb/parser/sql_statement.hpp"
#include "duckdb/parser/tableref/showref.hpp"
#include "duckdb/common/enums/date_part_specifier.hpp"
#include "duckdb/common/enums/merge_action_type.hpp"
#include "duckdb/common/enums/subquery_type.hpp"
#include "duckdb/common/exception/conversion_exception.hpp"
#include "duckdb/parser/expression/cast_expression.hpp"
#include "duckdb/parser/query_node/set_operation_node.hpp"
#include "duckdb/parser/statement/merge_into_statement.hpp"
#include "duckdb/parser/constraints/foreign_key_constraint.hpp"

namespace duckdb {
namespace duckpgq_peg {
bool PEGTransformerFactory::ConstructConstantFromExpression(const ParsedExpression &expr, Value &value) {
	// We have to construct it like this because we don't have the ClientContext for binding/executing the expr here
	switch (expr.GetExpressionType()) {
	case ExpressionType::FUNCTION: {
		auto &function = expr.Cast<FunctionExpression>();
		if (duckpgq_compat::FunctionName(function) == "struct_pack") {
			identifier_map_t<bool> unique_names;
			child_list_t<Value> values;
			values.reserve(duckpgq_compat::Arguments(function).size());
			for (const auto &child : duckpgq_compat::Arguments(function)) {
				if (!unique_names.emplace(Identifier((*duckpgq_compat::Expression(child)).GetAlias()), true).second) {
					throw BinderException("Duplicate struct entry name \"%s\"",
					                      Identifier((*duckpgq_compat::Expression(child)).GetAlias()).GetIdentifierName());
				}
				Value child_value;
				if (!ConstructConstantFromExpression((*duckpgq_compat::Expression(child)), child_value)) {
					return false;
				}
				values.emplace_back((*duckpgq_compat::Expression(child)).GetAlias(), std::move(child_value));
			}
			value = Value::STRUCT(std::move(values));
			return true;
		} else if (duckpgq_compat::FunctionName(function) == "list_value") {
			vector<Value> values;
			values.reserve(duckpgq_compat::Arguments(function).size());
			for (const auto &child : duckpgq_compat::Arguments(function)) {
				Value child_value;
				if (!ConstructConstantFromExpression((*duckpgq_compat::Expression(child)), child_value)) {
					return false;
				}
				values.emplace_back(std::move(child_value));
			}

			// figure out child type
			LogicalType child_type(LogicalTypeId::SQLNULL);
			for (auto &child_value : values) {
#if __has_include("duckdb/common/identifier.hpp")
				child_type = LogicalType::DefaultForceMaxLogicalType(child_type, child_value.type());
#else
				child_type = LogicalType::ForceMaxLogicalType(child_type, child_value.type());
#endif
			}

			// finally create the list
			value = Value::LIST(child_type, values);
			return true;
		} else if (duckpgq_compat::FunctionName(function) == "map") {
			Value keys;
			if (!ConstructConstantFromExpression(*duckpgq_compat::Expression(duckpgq_compat::Arguments(function)[0]), keys)) {
				return false;
			}

			Value values;
			if (!ConstructConstantFromExpression(*duckpgq_compat::Expression(duckpgq_compat::Arguments(function)[1]), values)) {
				return false;
			}

			vector<Value> keys_unpacked = ListValue::GetChildren(keys);
			vector<Value> values_unpacked = ListValue::GetChildren(values);

			value = Value::MAP(ListType::GetChildType(keys.type()), ListType::GetChildType(values.type()),
			                   keys_unpacked, values_unpacked);
			return true;
		} else {
			return false;
		}
	}
	case ExpressionType::VALUE_CONSTANT: {
		auto &constant = expr.Cast<ConstantExpression>();
#if __has_include("duckdb/common/identifier.hpp")
		value = constant.GetValue();
#else
		value = constant.value;
#endif
		return true;
	}
	case ExpressionType::OPERATOR_CAST: {
		auto &cast = expr.Cast<CastExpression>();
		Value dummy_value;
#if __has_include("duckdb/common/identifier.hpp")
		const auto &child = cast.Child();
		auto cast_type = UnboundType::TryDefaultBind(cast.TargetType());
#else
		const auto &child = *cast.child;
		const auto &cast_type = cast.cast_type;
#endif
		if (!ConstructConstantFromExpression(child, dummy_value)) {
			return false;
		}

		if (cast_type == LogicalType::INVALID || cast_type == LogicalTypeId::UNBOUND) {
			return false;
		}

		string error_message;
		if (!dummy_value.DefaultTryCastAs(cast_type, value, &error_message)) {
			throw ConversionException("Unable to cast %s to %s", dummy_value.ToString(),
			                          EnumUtil::ToString(cast_type.id()));
		}
		return true;
	}
	default:
		return false;
	}
}
} // namespace duckpgq_peg
} // namespace duckdb
