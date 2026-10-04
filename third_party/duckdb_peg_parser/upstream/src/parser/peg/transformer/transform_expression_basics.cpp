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
void PEGTransformerFactory::RemoveOrderQualificationRecursive(unique_ptr<ParsedExpression> &root_expr) {
#if __has_include("duckdb/common/identifier.hpp")
    ParsedExpressionIterator::VisitExpressionMutable<ColumnRefExpression>(*root_expr, [&](ColumnRefExpression &col_ref) {
        auto &names = col_ref.ColumnNamesMutable();
        if (names.size() > 1) names = vector<Identifier>{names.back()};
    });
#else
    std::function<void(ParsedExpression &)> visit = [&](ParsedExpression &expr) {
        if (expr.GetExpressionClass() == ExpressionClass::COLUMN_REF) {
            auto &names = expr.Cast<ColumnRefExpression>().column_names;
            if (names.size() > 1) names = vector<string>{names.back()};
        }
        ParsedExpressionIterator::EnumerateChildren(expr, visit);
    };
    visit(*root_expr);
#endif
}
unique_ptr<SQLStatement>
PEGTransformerFactory::TransformExpressionStatement(PEGTransformer &transformer,
                                                    vector<unique_ptr<ParsedExpression>> expression_alias) {
	auto expressions = std::move(expression_alias);
	auto select_statement = make_uniq<SelectStatement>();
	auto select_node = make_uniq<SelectNode>();

	bool any_column_ref = false;
	for (auto &expr : expressions) {
		if (expr->GetExpressionClass() == ExpressionClass::COLUMN_REF) {
			any_column_ref = true;
			break;
		}
	}

	if (any_column_ref && expressions.size() > 1) {
		throw ParserException("Mix of table names and expressions is not supported. "
		                      "Use SELECT to explicitly specify what you want to query.");
	}

	if (any_column_ref) {
		// Single COLUMN_REF: treat as table scan
		auto &col_expr = expressions[0]->Cast<ColumnRefExpression>();
		vector<Identifier> names;
		for (const auto &name : duckpgq_compat::ColumnNames(col_expr)) names.emplace_back(name);
		select_node->from_table = duckpgq_compat::TableFromNames(std::move(names));
		select_node->select_list.push_back(make_uniq<StarExpression>());
	} else {
		for (auto &expr : expressions) {
			select_node->select_list.push_back(std::move(expr));
		}
		select_node->from_table = make_uniq<EmptyTableRef>();
	}

	select_statement->node = std::move(select_node);
	return std::move(select_statement);
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformBaseExpression(PEGTransformer &transformer,
                                               unique_ptr<ParsedExpression> single_expression,
                                               optional<vector<unique_ptr<ParsedExpression>>> indirection_list) {
	auto expr = std::move(single_expression);
	if (!indirection_list) {
		return expr;
	}

	bool prev_indirection_was_cast = false;
	for (auto &indirection_expr : *indirection_list) {
		if (indirection_expr->GetExpressionClass() == ExpressionClass::CAST) {
			auto cast_expr = unique_ptr_cast<ParsedExpression, CastExpression>(std::move(indirection_expr));
			duckpgq_compat::CastChildMutable(*cast_expr) = std::move(expr);
			expr = std::move(cast_expr);
			prev_indirection_was_cast = true;
		} else if (indirection_expr->GetExpressionClass() == ExpressionClass::OPERATOR) {
			if (prev_indirection_was_cast) {
				throw ParserException(
				    "Subscript/slice cannot be applied directly after a cast operator (e.g. x::TYPE[1:3] is not "
				    "allowed). Wrap the cast in parentheses: (x::TYPE)[1:3]");
			}
			auto operator_expr = unique_ptr_cast<ParsedExpression, OperatorExpression>(std::move(indirection_expr));
			auto &children = duckpgq_compat::OperatorChildren(*operator_expr);
			children.insert(children.begin(), std::move(expr));
			expr = std::move(operator_expr);
			prev_indirection_was_cast = false;
		} else if (indirection_expr->GetExpressionClass() == ExpressionClass::FUNCTION) {
			auto function_expr = unique_ptr_cast<ParsedExpression, FunctionExpression>(std::move(indirection_expr));
			auto &arguments = duckpgq_compat::Arguments(*function_expr);
			arguments.insert(arguments.begin(), std::move(expr));
			expr = std::move(function_expr);
			prev_indirection_was_cast = false;
		} else if (indirection_expr->GetExpressionClass() == ExpressionClass::CONSTANT) {
			vector<unique_ptr<ParsedExpression>> struct_children;
			struct_children.push_back(std::move(expr));
			struct_children.push_back(std::move(indirection_expr));
			auto struct_expr =
			    make_uniq<OperatorExpression>(ExpressionType::STRUCT_EXTRACT, std::move(struct_children));
			expr = std::move(struct_expr);
			prev_indirection_was_cast = false;
		} else {
			throw NotImplementedException("Unhandled case for Base Expression with indirection");
		}
	}
	return expr;
}

// ColumnReference <- CatalogReservedSchemaTableColumnName / SchemaReservedTableColumnName / TableReservedColumnName /
// NestedColumnName
unique_ptr<ParsedExpression> PEGTransformerFactory::TransformColumnReference(PEGTransformer &transformer,
                                                                             unique_ptr<ColumnRefExpression> child) {
	return std::move(child);
}

unique_ptr<ColumnRefExpression> PEGTransformerFactory::TransformCatalogReservedSchemaTableColumnName(
    PEGTransformer &transformer, const Identifier &catalog_qualification,
    const Identifier &reserved_schema_qualification, const Identifier &reserved_table_qualification,
    const Identifier &reserved_column_name) {
	vector<Identifier> column_names;
	column_names.push_back(catalog_qualification);
	column_names.push_back(reserved_schema_qualification);
	column_names.push_back(reserved_table_qualification);
	column_names.push_back(reserved_column_name);
	return make_uniq<ColumnRefExpression>(duckpgq_compat::HostNames(column_names));
}

unique_ptr<ColumnRefExpression> PEGTransformerFactory::TransformSchemaReservedTableColumnName(
    PEGTransformer &transformer, const Identifier &schema_qualification, const Identifier &reserved_table_qualification,
    const Identifier &reserved_column_name) {
	vector<Identifier> column_names;
	column_names.push_back(schema_qualification);
	column_names.push_back(reserved_table_qualification);
	column_names.push_back(reserved_column_name);
	return make_uniq<ColumnRefExpression>(duckpgq_compat::HostNames(column_names));
}

Identifier PEGTransformerFactory::TransformReservedTableQualification(PEGTransformer &transformer,
                                                                      const Identifier &reserved_table_name) {
	return reserved_table_name;
}


MethodArguments PEGTransformerFactory::TransformFunctionExpressionArgumentList(
    PEGTransformer &transformer, const optional<bool> &distinct_or_all,
    optional<vector<FunctionArgument>> function_argument_list, optional<vector<OrderByNode>> order_by_clause,
    const optional<bool> &ignore_or_respect_nulls) {
	MethodArguments result;
	if (distinct_or_all) {
		result.distinct = *distinct_or_all;
	}
	if (function_argument_list) {
		result.arguments = std::move(*function_argument_list);
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

vector<OrderByNode> PEGTransformerFactory::TransformWithinGroupClause(PEGTransformer &transformer,
                                                                      vector<OrderByNode> order_by_clause) {
	return order_by_clause;
}

bool PEGTransformerFactory::TransformDistinctKeyword(PEGTransformer &transformer) {
	return true;
}

bool PEGTransformerFactory::TransformAllKeyword(PEGTransformer &transformer) {
	return false;
}

QualifiedName PEGTransformerFactory::TransformFunctionIdentifier(PEGTransformer &transformer,
                                                                 ParseResult &choice_result) {
	if (choice_result.type == ParseResultType::IDENTIFIER) {
		auto result = duckpgq_compat::MakeQualifiedName(choice_result.Cast<IdentifierParseResult>().identifier);
		return result;
	}
	return transformer.Transform<QualifiedName>(choice_result);
}

QualifiedName PEGTransformerFactory::TransformSchemaReservedFunctionName(PEGTransformer &transformer,
                                                                         const Identifier &schema_qualification,
                                                                         const Identifier &reserved_function_name) {
	auto result = duckpgq_compat::MakeQualifiedName({schema_qualification}, reserved_function_name);
	return result;
}

QualifiedName PEGTransformerFactory::TransformCatalogReservedSchemaFunctionName(
    PEGTransformer &transformer, const Identifier &catalog_qualification,
    const optional<Identifier> &reserved_schema_qualification, const Identifier &reserved_function_name) {
	if (reserved_schema_qualification) {
		return duckpgq_compat::MakeQualifiedName({catalog_qualification, *reserved_schema_qualification}, reserved_function_name);
	} else {
		return duckpgq_compat::MakeQualifiedName({catalog_qualification}, reserved_function_name);
	}
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformArrayBoundedListExpression(
    PEGTransformer &transformer, const bool &has_result, vector<unique_ptr<ParsedExpression>> bounded_list_expression) {
	bool is_array = has_result;
	if (!is_array) {
		return make_uniq<FunctionExpression>("list_value", std::move(bounded_list_expression));
	}
	return make_uniq<OperatorExpression>(ExpressionType::ARRAY_CONSTRUCTOR, std::move(bounded_list_expression));
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformFilterClause(PEGTransformer &transformer,
                                             unique_ptr<ParsedExpression> filter_clause_expression) {
	return filter_clause_expression;
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformFilterClauseExpression(PEGTransformer &transformer,
                                                       unique_ptr<ParsedExpression> filter_clause_contents) {
	return filter_clause_contents;
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformFilterClauseContents(PEGTransformer &transformer, const bool &has_result,
                                                     unique_ptr<ParsedExpression> expression) {
	return expression;
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformParenthesisExpression(PEGTransformer &transformer,
                                                      optional<vector<unique_ptr<ParsedExpression>>> expression) {
	// ParenthesisExpression <- Parens(List(Expression)?)
	// Python-style tuples: (), (x,) and (a, b, ...) all build an (unnamed) row/TUPLE.
	// A single (x) without a trailing comma is grouping and is handled earlier by ParensExpression.
	vector<unique_ptr<ParsedExpression>> children;
	if (expression) {
		children = std::move(*expression);
	}
	return make_uniq<FunctionExpression>("row", std::move(children));
}
} // namespace duckpgq_peg
} // namespace duckdb
