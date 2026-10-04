#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/compat/name_metadata.hpp"
#include "duckdb/parser/expression/operator_expression.hpp"
namespace duckdb {
namespace duckpgq_peg {
#if __has_include("duckdb/common/identifier.hpp")
unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformMethodExpression(PEGTransformer &transformer, const string &col_label,
                                                 MethodArguments method_expression_arguments) {
	if (method_expression_arguments.arguments.size() == 1 &&
	    ExpressionIsEmptyStar(method_expression_arguments.arguments[0].GetExpression())) {
		// COUNT(*) gets converted into COUNT()
		method_expression_arguments.arguments.clear();
	}
	if (method_expression_arguments.has_ignore_nulls) {
		throw ParserException("RESPECT/IGNORE NULLS is not supported for non-window functions");
	}
	auto result =
	    make_uniq<FunctionExpression>(Identifier(col_label), std::move(method_expression_arguments.arguments));
	result->DistinctMutable() = method_expression_arguments.distinct;
	if (!method_expression_arguments.order_bys.empty()) {
		auto order_by_modifier = make_uniq<OrderModifier>();
		order_by_modifier->orders = std::move(method_expression_arguments.order_bys);
		result->OrderByMutable() = std::move(order_by_modifier);
	}
	return std::move(result);
}

MethodArguments
PEGTransformerFactory::TransformMethodExpressionArguments(PEGTransformer &transformer,
                                                          MethodArguments method_expression_argument_list) {
	return method_expression_argument_list;
}

MethodArguments PEGTransformerFactory::TransformMethodExpressionArgumentList(
    PEGTransformer &transformer, const optional<bool> &distinct_or_all,
    optional<vector<FunctionArgument>> method_function_arguments, optional<vector<OrderByNode>> order_by_clause,
    const optional<bool> &ignore_or_respect_nulls) {
	MethodArguments result;
	if (distinct_or_all) {
		result.distinct = *distinct_or_all;
	}
	if (method_function_arguments) {
		result.arguments = std::move(*method_function_arguments);
	}
	if (order_by_clause) {
		result.order_bys = std::move(*order_by_clause);
	}
	if (ignore_or_respect_nulls) {
		result.has_ignore_nulls = true;
		result.ignore_nulls = *ignore_or_respect_nulls;
	}
	return result;
}

vector<FunctionArgument>
PEGTransformerFactory::TransformMethodFunctionArguments(PEGTransformer &transformer,
                                                        vector<FunctionArgument> function_argument) {
	return function_argument;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformStarExpression(
    PEGTransformer &transformer, const optional<vector<string>> &star_qualifier_list,
    const optional<qualified_column_set_t> &exclude_list,
    optional<case_insensitive_map_t<unique_ptr<ParsedExpression>>> replace_list,
    const optional<qualified_column_map_t<string>> &rename_list) {
	auto result = make_uniq<StarExpression>();
	if (star_qualifier_list) {
		if (star_qualifier_list->size() > 1) {
			throw ParserException("Did not expect more than one column in front of a star expression");
		}
		result->RelationNameMutable() = Identifier((*star_qualifier_list)[0]);
	}
	if (exclude_list) {
		result->ExcludeListMutable() = *exclude_list;
	}
	if (replace_list) {
		for (auto &replace_entry : *replace_list) {
			result->ReplaceListMutable()[Identifier(replace_entry.first)] = std::move(replace_entry.second);
		}
		for (auto &replace_entry : result->ReplaceList()) {
			if (result->ExcludeList().find(QualifiedColumnName(replace_entry.first)) != result->ExcludeList().end()) {
				throw ParserException("Column \"%s\" cannot occur in both EXCLUDE and REPLACE list",
				                      replace_entry.first);
			}
		}
	}
	if (rename_list) {
		for (auto &rename_entry : *rename_list) {
			result->RenameListMutable()[rename_entry.first] = Identifier(rename_entry.second);
		}
		for (auto &rename_column : result->RenameList()) {
			if (result->ExcludeList().find(rename_column.first) != result->ExcludeList().end()) {
				throw ParserException("Column \"%s\" cannot occur in both EXCLUDE and RENAME list",
				                      rename_column.first.ToString());
			}
			if (result->ReplaceList().find(rename_column.first.column) != result->ReplaceList().end()) {
				throw ParserException("Column \"%s\" cannot occur in both REPLACE and RENAME list",
				                      rename_column.first.ToString());
			}
		}
	}
	return std::move(result);
}

qualified_column_set_t PEGTransformerFactory::TransformExcludeList(PEGTransformer &transformer,
                                                                   const qualified_column_set_t &exclude_names) {
	return exclude_names;
}

qualified_column_set_t
PEGTransformerFactory::TransformExcludeNameList(PEGTransformer &transformer,
                                                const vector<QualifiedColumnName> &exclude_name) {
	qualified_column_set_t result;
	for (auto &exclude_column : exclude_name) {
		if (result.find(exclude_column) != result.end()) {
			throw ParserException("Duplicate entry \"%s\" in EXCLUDE list", exclude_column.ToString());
		}
		result.insert(exclude_column);
	}
	return result;
}

qualified_column_set_t PEGTransformerFactory::TransformExcludeNameSingle(PEGTransformer &transformer,
                                                                         const QualifiedColumnName &exclude_name) {
	qualified_column_set_t result;
	result.insert(exclude_name);
	return result;
}

QualifiedColumnName PEGTransformerFactory::TransformExcludeDottedName(PEGTransformer &transformer,
                                                                      const vector<string> &dotted_identifier) {
	auto result_string = StringUtil::Join(dotted_identifier, ".");
	return QualifiedColumnName::Parse(result_string);
}

QualifiedColumnName PEGTransformerFactory::TransformExcludeColumnName(PEGTransformer &transformer,
                                                                      const Identifier &col_id_or_string) {
	return QualifiedColumnName(col_id_or_string);
}
#else
unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformMethodExpression(PEGTransformer &transformer, const string &col_label,
                                                 MethodArguments method_expression_arguments) {
	if (method_expression_arguments.arguments.size() == 1 &&
	    ExpressionIsEmptyStar(method_expression_arguments.arguments[0].GetExpression())) {
		// COUNT(*) gets converted into COUNT()
		method_expression_arguments.arguments.clear();
	}
	if (method_expression_arguments.has_ignore_nulls) {
		throw ParserException("RESPECT/IGNORE NULLS is not supported for non-window functions");
	}
	auto result =
	    BuildFunctionExpression(duckpgq_compat::MakeQualifiedName(Identifier(col_label)), std::move(method_expression_arguments.arguments));
	result->distinct = method_expression_arguments.distinct;
	if (!method_expression_arguments.order_bys.empty()) {
		auto order_by_modifier = make_uniq<OrderModifier>();
		order_by_modifier->orders = std::move(method_expression_arguments.order_bys);
		result->order_bys = std::move(order_by_modifier);
	}
	return std::move(result);
}

MethodArguments
PEGTransformerFactory::TransformMethodExpressionArguments(PEGTransformer &transformer,
                                                          MethodArguments method_expression_argument_list) {
	return method_expression_argument_list;
}

MethodArguments PEGTransformerFactory::TransformMethodExpressionArgumentList(
    PEGTransformer &transformer, const optional<bool> &distinct_or_all,
    optional<vector<FunctionArgument>> method_function_arguments, optional<vector<OrderByNode>> order_by_clause,
    const optional<bool> &ignore_or_respect_nulls) {
	MethodArguments result;
	if (distinct_or_all) {
		result.distinct = *distinct_or_all;
	}
	if (method_function_arguments) {
		result.arguments = std::move(*method_function_arguments);
	}
	if (order_by_clause) {
		result.order_bys = std::move(*order_by_clause);
	}
	if (ignore_or_respect_nulls) {
		result.has_ignore_nulls = true;
		result.ignore_nulls = *ignore_or_respect_nulls;
	}
	return result;
}

vector<FunctionArgument>
PEGTransformerFactory::TransformMethodFunctionArguments(PEGTransformer &transformer,
                                                        vector<FunctionArgument> function_argument) {
	return function_argument;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformStarExpression(
    PEGTransformer &transformer, const optional<vector<string>> &star_qualifier_list,
    const optional<qualified_column_set_t> &exclude_list,
    optional<case_insensitive_map_t<unique_ptr<ParsedExpression>>> replace_list,
    const optional<qualified_column_map_t<string>> &rename_list) {
	auto result = make_uniq<StarExpression>();
	if (star_qualifier_list) {
		if (star_qualifier_list->size() > 1) {
			throw ParserException("Did not expect more than one column in front of a star expression");
		}
		result->relation_name = (*star_qualifier_list)[0];
	}
	if (exclude_list) {
		result->exclude_list = *exclude_list;
	}
	if (replace_list) {
		for (auto &replace_entry : *replace_list) {
			result->replace_list[replace_entry.first] = std::move(replace_entry.second);
		}
		for (auto &replace_entry : result->replace_list) {
			if (result->exclude_list.find(QualifiedColumnName(replace_entry.first)) != result->exclude_list.end()) {
				throw ParserException("Column \"%s\" cannot occur in both EXCLUDE and REPLACE list",
				                      replace_entry.first);
			}
		}
	}
	if (rename_list) {
		for (auto &rename_entry : *rename_list) {
			result->rename_list[rename_entry.first] = rename_entry.second;
		}
		for (auto &rename_column : result->rename_list) {
			if (result->exclude_list.find(rename_column.first) != result->exclude_list.end()) {
				throw ParserException("Column \"%s\" cannot occur in both EXCLUDE and RENAME list",
				                      rename_column.first.ToString());
			}
			if (result->replace_list.find(rename_column.first.column) != result->replace_list.end()) {
				throw ParserException("Column \"%s\" cannot occur in both REPLACE and RENAME list",
				                      rename_column.first.ToString());
			}
		}
	}
	return std::move(result);
}

qualified_column_set_t PEGTransformerFactory::TransformExcludeList(PEGTransformer &transformer,
                                                                   const qualified_column_set_t &exclude_names) {
	return exclude_names;
}

qualified_column_set_t
PEGTransformerFactory::TransformExcludeNameList(PEGTransformer &transformer,
                                                const vector<QualifiedColumnName> &exclude_name) {
	qualified_column_set_t result;
	for (auto &exclude_column : exclude_name) {
		if (result.find(exclude_column) != result.end()) {
			throw ParserException("Duplicate entry \"%s\" in EXCLUDE list", exclude_column.ToString());
		}
		result.insert(exclude_column);
	}
	return result;
}

qualified_column_set_t PEGTransformerFactory::TransformExcludeNameSingle(PEGTransformer &transformer,
                                                                         const QualifiedColumnName &exclude_name) {
	qualified_column_set_t result;
	result.insert(exclude_name);
	return result;
}

QualifiedColumnName PEGTransformerFactory::TransformExcludeDottedName(PEGTransformer &transformer,
                                                                      const vector<string> &dotted_identifier) {
	auto result_string = StringUtil::Join(dotted_identifier, ".");
	return QualifiedColumnName::Parse(result_string);
}

QualifiedColumnName PEGTransformerFactory::TransformExcludeColumnName(PEGTransformer &transformer,
                                                                      const Identifier &col_id_or_string) {
	return QualifiedColumnName(col_id_or_string.GetIdentifierName());
}
#endif
#if __has_include("duckdb/common/identifier.hpp")
unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformColumnsExpression(PEGTransformer &transformer, const bool &has_result,
                                                  unique_ptr<ParsedExpression> expression) {
	bool unpack = has_result;
	auto result = make_uniq<StarExpression>();
	if (expression->GetExpressionType() == ExpressionType::STAR) {
		auto star_expr = unique_ptr_cast<ParsedExpression, StarExpression>(std::move(expression));
		if (star_expr->IsColumns()) {
			result->ExpressionMutable() = std::move(star_expr);
		} else {
			result = std::move(star_expr);
		}
	} else if (expression->GetExpressionType() == ExpressionType::LAMBDA) {
		vector<unique_ptr<ParsedExpression>> children;
		children.push_back(make_uniq<StarExpression>());
		children.push_back(std::move(expression));
		auto list_filter = make_uniq<FunctionExpression>("list_filter", std::move(children));
		result->ExpressionMutable() = std::move(list_filter);
	} else {
		result->ExpressionMutable() = std::move(expression);
	}
	result->IsColumnsMutable() = true;
	if (unpack) {
		return make_uniq<OperatorExpression>(ExpressionType::OPERATOR_UNPACK, std::move(result));
	}
	return std::move(result);
}
#else
unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformColumnsExpression(PEGTransformer &transformer, const bool &has_result,
                                                  unique_ptr<ParsedExpression> expression) {
	bool unpack = has_result;
	auto result = make_uniq<StarExpression>();
	if (expression->GetExpressionType() == ExpressionType::STAR) {
		auto star_expr = unique_ptr_cast<ParsedExpression, StarExpression>(std::move(expression));
		if (star_expr->columns) {
			result->expr = std::move(star_expr);
		} else {
			result = std::move(star_expr);
		}
	} else if (expression->GetExpressionType() == ExpressionType::LAMBDA) {
		vector<unique_ptr<ParsedExpression>> children;
		children.push_back(make_uniq<StarExpression>());
		children.push_back(std::move(expression));
		auto list_filter = make_uniq<FunctionExpression>("list_filter", std::move(children));
		result->expr = std::move(list_filter);
	} else {
		result->expr = std::move(expression);
	}
	result->columns = true;
	if (unpack) {
		return make_uniq<OperatorExpression>(ExpressionType::OPERATOR_UNPACK, std::move(result));
	}
	return std::move(result);
}
#endif
} // namespace duckpgq_peg
} // namespace duckdb
