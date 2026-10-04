#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/compat/alter_access.hpp"
#include "duckdb/parser/tableref/pivotref.hpp"

namespace duckdb {
namespace duckpgq_peg {
unique_ptr<TableRef> PEGTransformerFactory::TransformTableUnpivotClauseBody(PEGTransformer &transformer,
                                                                            const vector<string> &unpivot_header,
                                                                            vector<PivotColumn> unpivot_value_list) {
	auto result = make_uniq<PivotRef>();
	result->unpivot_names = duckpgq_compat::HostNames(StringsToIdentifiers(unpivot_header));
	result->pivots = std::move(unpivot_value_list);
	if (result->pivots.size() > 1) {
		throw ParserException("UNPIVOT requires a single pivot element");
	}
	return std::move(result);
}

unique_ptr<TableRef> PEGTransformerFactory::TransformTableUnpivotClause(PEGTransformer &transformer,
                                                                        const optional<bool> &include_or_exclude_nulls,
                                                                        unique_ptr<TableRef> table_unpivot_clause_body,
                                                                        const optional<TableAlias> &table_alias) {
	auto &result = table_unpivot_clause_body->Cast<PivotRef>();
	result.include_nulls = include_or_exclude_nulls.value_or(false);
	if (table_alias) {
		result.alias = duckpgq_compat::HostName(table_alias->name);
		result.column_name_alias = duckpgq_compat::HostNames(table_alias->column_name_alias);
	}
	return table_unpivot_clause_body;
}

void PEGTransformerFactory::GetValueFromExpression(unique_ptr<ParsedExpression> &expr, vector<Value> &result) {
	if (expr->GetExpressionClass() == ExpressionClass::CONSTANT) {
		auto &const_expr = expr->Cast<ConstantExpression>();
		result.push_back(duckpgq_compat::ConstantValue(const_expr));
	} else if (expr->GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		auto &col_ref_expr = expr->Cast<ColumnRefExpression>();
		for (auto &col : duckpgq_compat::ColumnNames(col_ref_expr)) {
			result.push_back(Value(Identifier(col).GetIdentifierName()));
		}
	} else if (expr->GetExpressionClass() == ExpressionClass::FUNCTION) {
		auto &func_expr = expr->Cast<FunctionExpression>();
		if (duckpgq_compat::FunctionName(func_expr) == "row") {
			for (auto &col : duckpgq_compat::Arguments(func_expr)) {
				GetValueFromExpression(duckpgq_compat::Expression(col), result);
			}
		}
	}
}

bool PEGTransformerFactory::TransformPivotInList(unique_ptr<ParsedExpression> &expr, PivotColumnEntry &entry) {
	auto initial_size = entry.values.size();
	switch (expr->GetExpressionType()) {
	case ExpressionType::COLUMN_REF: {
		auto &colref = expr->Cast<ColumnRefExpression>();
		if (colref.IsQualified()) {
			throw ParserException(expr->GetQueryLocation(), "PIVOT IN list cannot contain qualified column references");
		}
		entry.values.emplace_back(colref.GetColumnName());
		return true;
	}
	case ExpressionType::FUNCTION: {
		auto &function = expr->Cast<FunctionExpression>();
		if (duckpgq_compat::FunctionName(function) != "row") {
			return false;
		}
		for (auto &child : duckpgq_compat::Arguments(function)) {
			if (!TransformPivotInList(duckpgq_compat::Expression(child), entry)) {
				entry.values.resize(initial_size);
				return false;
			}
		}
		return true;
	}
	default: {
		Value val;
		if (!ConstructConstantFromExpression(*expr, val)) {
			return false;
		}
		entry.values.push_back(std::move(val));
		return true;
	}
	}
}

static bool PivotEntryIsTuple(const PivotColumnEntry &entry) {
	if (entry.values.size() > 1) {
		return true;
	}
	if (!entry.expr || entry.expr->GetExpressionType() != ExpressionType::FUNCTION) {
		return false;
	}
	auto &function = entry.expr->Cast<FunctionExpression>();
	return duckpgq_compat::FunctionName(function) == "row";
}

unique_ptr<TableRef> PEGTransformerFactory::TransformTablePivotClauseBody(
    PEGTransformer &transformer, vector<unique_ptr<ParsedExpression>> target_list, vector<PivotColumn> pivot_value_list,
    const optional<vector<string>> &pivot_group_by_list) {
	auto result = make_uniq<PivotRef>();
	result->aggregates = std::move(target_list);
	result->pivots = std::move(pivot_value_list);
	if (pivot_group_by_list) {
		result->groups = duckpgq_compat::HostNames(StringsToIdentifiers(*pivot_group_by_list));
	}
	return std::move(result);
}

unique_ptr<TableRef> PEGTransformerFactory::TransformTablePivotClause(PEGTransformer &transformer,
                                                                      unique_ptr<TableRef> table_pivot_clause_body,
                                                                      const optional<TableAlias> &table_alias) {
	auto &result = table_pivot_clause_body->Cast<PivotRef>();
	if (table_alias) {
		result.alias = duckpgq_compat::HostName(table_alias->name);
		result.column_name_alias = duckpgq_compat::HostNames(table_alias->column_name_alias);
	}
	return table_pivot_clause_body;
}

PivotColumn PEGTransformerFactory::TransformPivotValueTarget(PEGTransformer &transformer, ParseResult &choice_result) {
	PivotColumn result;
	if (choice_result.type == ParseResultType::IDENTIFIER) {
		result.pivot_enum = duckpgq_compat::HostName(choice_result.Cast<IdentifierParseResult>().identifier);
	} else {
		result.entries = transformer.Transform<vector<PivotColumnEntry>>(choice_result);
	}
	return result;
}

PivotColumn PEGTransformerFactory::TransformPivotValueList(PEGTransformer &transformer,
                                                           unique_ptr<ParsedExpression> pivot_header,
                                                           PivotColumn pivot_value_target) {
	auto result = std::move(pivot_value_target);
	auto pivot_expression = std::move(pivot_header);
	if (pivot_expression->GetExpressionClass() != ExpressionClass::FUNCTION) {
		result.pivot_expressions.push_back(std::move(pivot_expression));
		return result;
	}
	auto &func_expr = pivot_expression->Cast<FunctionExpression>();
	if (duckpgq_compat::FunctionName(func_expr) != "row") {
		result.pivot_expressions.push_back(std::move(pivot_expression));
		return result;
	}
	// Unpack row() only when IN list entries are tuples (multi-value).
	// For scalar IN entries like IN ('xx'), keep row() as a single compound expression
	// so pivot_expressions.size() matches entry.values.size() (both 1).
	bool has_tuple_entries = false;
	for (auto &entry : result.entries) {
		if (PivotEntryIsTuple(entry)) {
			has_tuple_entries = true;
			break;
		}
	}
	if (has_tuple_entries) {
		for (auto &child : duckpgq_compat::Arguments(func_expr)) {
			result.pivot_expressions.emplace_back(std::move(duckpgq_compat::Expression(child)));
		}
	} else {
		result.pivot_expressions.push_back(std::move(pivot_expression));
	}
	return result;
}

vector<string> PEGTransformerFactory::TransformPivotGroupByList(PEGTransformer &transformer,
                                                                const vector<Identifier> &col_id_or_string) {
	return IdentifiersToStrings(col_id_or_string);
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformPivotHeader(PEGTransformer &transformer,
                                                                         unique_ptr<ParsedExpression> base_expression) {
	return base_expression;
}

PivotColumn PEGTransformerFactory::TransformUnpivotValueList(PEGTransformer &transformer,
                                                             const vector<string> &unpivot_header,
                                                             vector<PivotColumnEntry> unpivot_target_list) {
	PivotColumn result;
	result.unpivot_names = duckpgq_compat::HostNames(StringsToIdentifiers(unpivot_header));
	if (result.unpivot_names.size() != 1) {
		throw ParserException("UNPIVOT requires a single column name for the PIVOT IN clause");
	}
	result.entries = std::move(unpivot_target_list);
	return result;
}

vector<PivotColumnEntry>
PEGTransformerFactory::TransformPivotTargetList(PEGTransformer &transformer,
                                                vector<unique_ptr<ParsedExpression>> target_list) {
	vector<PivotColumnEntry> result;
	for (auto &target : target_list) {
		PivotColumnEntry pivot_entry;
		pivot_entry.alias = target->GetAlias();
		bool transformed = TransformPivotInList(target, pivot_entry);
		if (!transformed) {
			pivot_entry.expr = std::move(target);
		}
		result.push_back(std::move(pivot_entry));
	}
	return result;
}

vector<PivotColumnEntry>
PEGTransformerFactory::TransformUnpivotTargetList(PEGTransformer &transformer,
                                                  vector<unique_ptr<ParsedExpression>> target_list) {
	vector<PivotColumnEntry> result;
	for (auto &target : target_list) {
		PivotColumnEntry pivot_entry;
		pivot_entry.alias = target->GetAlias();
		pivot_entry.expr = std::move(target);
		result.push_back(std::move(pivot_entry));
	}
	return result;
}
} // namespace duckpgq_peg
} // namespace duckdb
