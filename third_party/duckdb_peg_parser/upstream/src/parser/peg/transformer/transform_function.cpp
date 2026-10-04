#include "duckpgq/compat/window_function.hpp"
#include "duckpgq/compat/function_access.hpp"
namespace duckdb {
namespace duckpgq_peg {
static auto &CaseChecks(CaseExpression &e) {
#if __has_include("duckdb/common/identifier.hpp")
return e.CaseChecksMutable();
#else
return e.case_checks;
#endif
}
static auto &CaseElse(CaseExpression &e) {
#if __has_include("duckdb/common/identifier.hpp")
return e.ElseMutable();
#else
return e.else_expr;
#endif
}
unique_ptr<ParsedExpression> PEGTransformerFactory::TransformFunctionExpression(
    PEGTransformer &transformer, const QualifiedName &function_identifier,
    MethodArguments function_expression_arguments, optional<vector<OrderByNode>> within_group_clause,
    optional<unique_ptr<ParsedExpression>> filter_clause, const bool &has_result,
    optional<unique_ptr<WindowExpression>> over_clause) {
#if !__has_include("duckdb/common/identifier.hpp")
    if (over_clause) {
        return BuildWindowFunction(transformer, function_identifier, std::move(function_expression_arguments),
            std::move(within_group_clause), filter_clause ? std::move(*filter_clause) : nullptr,
            has_result, std::move(*over_clause));
    }
#endif
	auto qualified_function = function_identifier;
	bool export_clause = has_result;
	auto distinct = function_expression_arguments.distinct;
	auto function_children = std::move(function_expression_arguments.arguments);
	auto order_modifier = make_uniq<OrderModifier>();
	order_modifier->orders = std::move(function_expression_arguments.order_bys);

	unique_ptr<ParsedExpression> filter_expr;
	if (filter_clause) {
		filter_expr = std::move(*filter_clause);
	}
	if (function_children.size() == 1 && ExpressionIsEmptyStar(*function_children[0].GetExpressionMutable()) &&
	    !distinct && order_modifier->orders.empty()) {
		// COUNT(*) gets converted into COUNT()
		function_children.clear();
	}
	#if __has_include("duckdb/common/identifier.hpp")
	auto lowercase_name = StringUtil::Lower(qualified_function.Name().GetIdentifierName());
#else
	auto lowercase_name = StringUtil::Lower(qualified_function.name);
#endif

#if __has_include("duckdb/common/identifier.hpp")
	if (over_clause) {
		if (transformer.in_window_definition) {
			throw ParserException("window functions are not allowed in window definitions");
		}
		//	We map first/last OVER() to first_value/last_value.
		//	Not sure the semantics match, but we are stuck with it.
		if (lowercase_name == "first" || lowercase_name == "last") {
			lowercase_name += "_value";
		}

		if (export_clause) {
			throw ParserException("EXPORT_STATE is not supported for window functions!");
		}

		transformer.in_window_definition = true;
		auto expr = std::move(*over_clause);
		expr->SetQualifiedName(QualifiedName(qualified_function.Catalog(), qualified_function.Schema(), Identifier()));
		expr->SetFunctionName(lowercase_name);

		for (auto &arg : function_children) {
			expr->GetArgumentsMutable().push_back(std::move(arg));
		}

		expr->HasIgnoreNullsMutable() = function_expression_arguments.has_ignore_nulls;
		expr->IgnoreNullsMutable() = function_expression_arguments.ignore_nulls;
		expr->FilterMutable() = std::move(filter_expr);
		expr->ArgOrdersMutable() = std::move(order_modifier->orders);
		expr->DistinctMutable() = distinct;
		transformer.in_window_definition = false;
		return std::move(expr);
	}
#endif
	if (lowercase_name == "count" && function_children.empty()) {
		lowercase_name = "count_star";
	}

	if (lowercase_name == "if") {
		if (function_children.size() != 3) {
			throw ParserException("Wrong number of arguments to IF.");
		}
		for (auto &arg : function_children) {
			if (arg.HasName()) {
				throw ParserException("Named arguments are not supported in IF expressions");
			}
		}

		auto expr = make_uniq<CaseExpression>();
		CaseCheck check;
		check.when_expr = std::move(function_children[0].GetExpressionMutable());
		check.then_expr = std::move(function_children[1].GetExpressionMutable());
		CaseChecks(*expr).push_back(std::move(check));
		CaseElse(*expr) = std::move(function_children[2].GetExpressionMutable());
		return std::move(expr);
	}
	if (lowercase_name == "unpack") {
		if (function_children.size() != 1) {
			throw ParserException("Wrong number of arguments to the UNPACK operator");
		}
		auto expr = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_UNPACK);
		for (auto &arg : function_children) {
			if (arg.HasName()) {
				throw ParserException("Named arguments are not supported in UNPACK operator");
			}
			duckpgq_compat::OperatorChildren(*expr).push_back(std::move(arg.GetExpressionMutable()));
		}
		return std::move(expr);
	}
	if (lowercase_name == "try") {
		if (function_children.size() != 1) {
			throw ParserException("Wrong number of arguments provided to TRY expression");
		}
		auto try_expression = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_TRY);
		for (auto &arg : function_children) {
			if (arg.HasName()) {
				throw ParserException("Named arguments are not supported in TRY expression");
			}
			duckpgq_compat::OperatorChildren(*try_expression).push_back(std::move(arg.GetExpressionMutable()));
		}
		return std::move(try_expression);
	}
	if (lowercase_name == "construct_array") {
		auto construct_array = make_uniq<OperatorExpression>(ExpressionType::ARRAY_CONSTRUCTOR);
		for (auto &arg : function_children) {
			if (arg.HasName()) {
				throw ParserException("Named arguments are not supported in array constructors");
			}
			duckpgq_compat::OperatorChildren(*construct_array).push_back(std::move(arg.GetExpressionMutable()));
		}
		return std::move(construct_array);
	}
	if (lowercase_name == "ifnull") {
		if (function_children.size() != 2) {
			throw ParserException("Wrong number of arguments to IFNULL.");
		}
		for (auto &arg : function_children) {
			if (arg.HasName()) {
				throw ParserException("Named arguments are not supported in IFNULL expressions");
			}
		}

		//  Two-argument COALESCE
		auto coalesce_op = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_COALESCE);
		duckpgq_compat::OperatorChildren(*coalesce_op).push_back(std::move(function_children[0].GetExpressionMutable()));
		duckpgq_compat::OperatorChildren(*coalesce_op).push_back(std::move(function_children[1].GetExpressionMutable()));
		return std::move(coalesce_op);
	}
	if (lowercase_name == "date") {
		if (function_children.size() != 1) {
			throw ParserException("Wrong number of arguments provided to DATE function");
		}
		return std::move(
		    make_uniq<CastExpression>(LogicalType::DATE, std::move(function_children[0].GetExpressionMutable())));
	}
	if (function_expression_arguments.has_ignore_nulls) {
		throw ParserException("RESPECT/IGNORE NULLS is not supported for non-window functions");
	}
	if (within_group_clause) {
		auto order_by_clause = std::move(*within_group_clause);
		if (distinct) {
			throw ParserException("DISTINCT is not allowed in combination with WITHIN GROUP");
		}
		if (!order_modifier->orders.empty()) {
			throw ParserException("Cannot use multiple ORDER BY statements with WITHIN GROUP");
		}
		if (!order_modifier) {
			throw InternalException("ORDER modifier for WITHIN GROUP is not initialized");
		}
		order_modifier->orders = std::move(order_by_clause);
		if (order_modifier->orders.size() != 1) {
			throw ParserException("Cannot use multiple ORDER BY clauses with WITHIN GROUP");
		}
		if (lowercase_name == "percentile_cont") {
			if (function_children.size() != 1) {
				throw ParserException("Wrong number of arguments for PERCENTILE_CONT");
			}
			lowercase_name = "quantile_cont";
		} else if (lowercase_name == "percentile_disc") {
			if (function_children.size() != 1) {
				throw ParserException("Wrong number of arguments for PERCENTILE_DISC");
			}
			lowercase_name = "quantile_disc";
		} else if (lowercase_name == "mode") {
			if (!function_children.empty()) {
				throw ParserException("Wrong number of arguments for MODE");
			}
			lowercase_name = "mode";
		} else {
			throw ParserException("Unknown ordered aggregate \"%s\".", lowercase_name);
		}
	}
#if __has_include("duckdb/common/identifier.hpp")
	auto result = make_uniq<FunctionExpression>(
	    QualifiedName(qualified_function.Catalog(), qualified_function.Schema(), Identifier(lowercase_name)),
	    std::move(function_children), std::move(filter_expr), std::move(order_modifier), distinct, false,
	    export_clause);
#else
	auto result = make_uniq<FunctionExpression>(qualified_function.catalog, qualified_function.schema, lowercase_name,
	    LowerFunctionArguments(std::move(function_children)), std::move(filter_expr), std::move(order_modifier), distinct, false, export_clause);
#endif

	return std::move(result);
}
} // namespace duckpgq_peg
} // namespace duckdb
