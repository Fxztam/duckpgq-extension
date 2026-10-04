#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/expression/comparison_expression.hpp"
namespace duckdb {
namespace duckpgq_peg {
#if __has_include("duckdb/common/identifier.hpp")
unique_ptr<WindowExpression> PEGTransformerFactory::TransformWindowFrameContents(
    PEGTransformer &transformer, optional<vector<unique_ptr<ParsedExpression>>> window_partition,
    optional<vector<OrderByNode>> order_by_clause, optional<WindowFrame> frame_clause) {
	//! Create a dummy result to add modifiers to
	auto result = make_uniq<WindowExpression>(string(), string(), string());
	if (window_partition) {
		result->PartitionsMutable() = std::move(*window_partition);
	}
	if (order_by_clause) {
		result->OrderByMutable() = std::move(*order_by_clause);
		for (auto &order : result->OrderByMutable()) {
			if (order.expression->GetExpressionType() == ExpressionType::STAR) {
				auto &star = order.expression->Cast<StarExpression>();
				if (!star.Expression()) {
					throw ParserException("Cannot ORDER BY ALL in a window expression");
				}
			}
		}
	}
	if (frame_clause) {
		result->WindowStartMutable() = frame_clause->start;
		result->WindowEndMutable() = frame_clause->end;
		result->StartExprMutable() = std::move(frame_clause->start_expr);
		result->EndExprMutable() = std::move(frame_clause->end_expr);
		result->WindowExcludeMutable() = frame_clause->exclude_clause;
	} else {
		result->WindowStartMutable() = WindowBoundary::UNBOUNDED_PRECEDING;
		result->WindowEndMutable() = WindowBoundary::CURRENT_ROW_RANGE;
	}
	return result;
}
unique_ptr<ParsedExpression> PEGTransformerFactory::TransformCaseExpression(
    PEGTransformer &transformer, optional<unique_ptr<ParsedExpression>> expression, vector<CaseCheck> case_when_then,
    optional<unique_ptr<ParsedExpression>> case_else) {
	auto result = make_uniq<CaseExpression>();

	for (auto &case_expr : case_when_then) {
		CaseCheck new_case;
		if (expression) {
			new_case.when_expr = make_uniq<ComparisonExpression>(ExpressionType::COMPARE_EQUAL, (*expression)->Copy(),
			                                                     std::move(case_expr.when_expr));
		} else {
			new_case.when_expr = std::move(case_expr.when_expr);
		}
		new_case.then_expr = std::move(case_expr.then_expr);
		result->CaseChecksMutable().push_back(std::move(new_case));
	}
	if (case_else) {
		result->ElseMutable() = std::move(*case_else);
	} else {
		result->ElseMutable() = make_uniq<ConstantExpression>(Value());
	}
	return std::move(result);
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformCaseElse(PEGTransformer &transformer,
                                                                      unique_ptr<ParsedExpression> expression) {
	return expression;
}

CaseCheck PEGTransformerFactory::TransformCaseWhenThen(PEGTransformer &transformer,
                                                       unique_ptr<ParsedExpression> expression,
                                                       unique_ptr<ParsedExpression> expression_1) {
	CaseCheck result;
	result.when_expr = std::move(expression);
	result.then_expr = std::move(expression_1);
	return result;
}
#else
unique_ptr<WindowExpression> PEGTransformerFactory::TransformWindowFrameContents(
    PEGTransformer &transformer, optional<vector<unique_ptr<ParsedExpression>>> window_partition,
    optional<vector<OrderByNode>> order_by_clause, optional<WindowFrame> frame_clause) {
	//! Create a dummy result to add modifiers to
	auto result = make_uniq<WindowExpression>(ExpressionType::WINDOW_AGGREGATE, string(), string(), string());
	if (window_partition) {
		result->partitions = std::move(*window_partition);
	}
	if (order_by_clause) {
		result->orders = std::move(*order_by_clause);
		for (auto &order : result->orders) {
			if (order.expression->GetExpressionType() == ExpressionType::STAR) {
				auto &star = order.expression->Cast<StarExpression>();
				if (!star.expr) {
					throw ParserException("Cannot ORDER BY ALL in a window expression");
				}
			}
		}
	}
	if (frame_clause) {
		result->start = frame_clause->start;
		result->end = frame_clause->end;
		result->start_expr = std::move(frame_clause->start_expr);
		result->end_expr = std::move(frame_clause->end_expr);
		result->exclude_clause = frame_clause->exclude_clause;
	} else {
		result->start = WindowBoundary::UNBOUNDED_PRECEDING;
		result->end = WindowBoundary::CURRENT_ROW_RANGE;
	}
	return result;
}
unique_ptr<ParsedExpression> PEGTransformerFactory::TransformCaseExpression(
    PEGTransformer &transformer, optional<unique_ptr<ParsedExpression>> expression, vector<CaseCheck> case_when_then,
    optional<unique_ptr<ParsedExpression>> case_else) {
	auto result = make_uniq<CaseExpression>();

	for (auto &case_expr : case_when_then) {
		CaseCheck new_case;
		if (expression) {
			new_case.when_expr = make_uniq<ComparisonExpression>(ExpressionType::COMPARE_EQUAL, (*expression)->Copy(),
			                                                     std::move(case_expr.when_expr));
		} else {
			new_case.when_expr = std::move(case_expr.when_expr);
		}
		new_case.then_expr = std::move(case_expr.then_expr);
		result->case_checks.push_back(std::move(new_case));
	}
	if (case_else) {
		result->else_expr = std::move(*case_else);
	} else {
		result->else_expr = make_uniq<ConstantExpression>(Value());
	}
	return std::move(result);
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformCaseElse(PEGTransformer &transformer,
                                                                      unique_ptr<ParsedExpression> expression) {
	return expression;
}

CaseCheck PEGTransformerFactory::TransformCaseWhenThen(PEGTransformer &transformer,
                                                       unique_ptr<ParsedExpression> expression,
                                                       unique_ptr<ParsedExpression> expression_1) {
	CaseCheck result;
	result.when_expr = std::move(expression);
	result.then_expr = std::move(expression_1);
	return result;
}
#endif
} // namespace duckpgq_peg
} // namespace duckdb
