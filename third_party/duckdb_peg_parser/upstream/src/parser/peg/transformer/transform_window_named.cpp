#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
namespace duckdb {
namespace duckpgq_peg {
#if __has_include("duckdb/common/identifier.hpp")
unique_ptr<WindowExpression> PEGTransformerFactory::TransformOverClause(PEGTransformer &transformer,
                                                                        ParseResult &parse_result) {
	if (transformer.in_window_definition) {
		throw ParserException("window functions are not allowed in window definitions");
	}
	auto &list_pr = parse_result.Cast<ListParseResult>();
	transformer.in_window_definition = true;
	auto window_frame = transformer.Transform<unique_ptr<WindowExpression>>(list_pr.GetChild(1));
	transformer.in_window_definition = false;
	return window_frame;
}

unique_ptr<WindowExpression> PEGTransformerFactory::TransformWindowFrame(PEGTransformer &transformer,
                                                                         ParseResult &choice_result) {
	if (choice_result.type == ParseResultType::IDENTIFIER) {
		auto window_name = choice_result.Cast<IdentifierParseResult>().identifier;
		return transformer.GetWindowClause(window_name);
	}
	return transformer.Transform<unique_ptr<WindowExpression>>(choice_result);
}

unique_ptr<WindowExpression> PEGTransformerFactory::TransformParensIdentifier(PEGTransformer &transformer,
                                                                              const Identifier &identifier) {
	auto window_name = identifier.GetIdentifierName();
	auto window_clause = transformer.GetWindowClause(identifier);
	if (window_clause->StartExpr() || window_clause->EndExpr() ||
	    !transformer.IsWindowFrameDefault(window_clause->WindowStart(), window_clause->WindowEnd())) {
		throw ParserException("cannot copy window \"%s\" because it has a frame clause", window_name);
	}
	return window_clause;
}

unique_ptr<WindowExpression>
PEGTransformerFactory::TransformWindowFrameContentsParens(PEGTransformer &transformer,
                                                          unique_ptr<WindowExpression> window_frame_contents) {
	return window_frame_contents;
}

unique_ptr<WindowExpression>
PEGTransformerFactory::TransformWindowFrameNameContentsParens(PEGTransformer &transformer,
                                                              unique_ptr<WindowExpression> window_frame_name_contents) {
	return window_frame_name_contents;
}

unique_ptr<WindowExpression>
PEGTransformerFactory::TransformWindowFrameNameContents(PEGTransformer &transformer,
                                                        const optional<Identifier> &base_window_name,
                                                        unique_ptr<WindowExpression> window_frame_contents) {
	if (!base_window_name) {
		return window_frame_contents;
	}
	auto window_name = base_window_name->GetIdentifierName();
	auto lower_name = StringUtil::Lower(window_name);
	if (lower_name == "partition" || lower_name == "range" || lower_name == "rows" || lower_name == "groups") {
		throw ParserException("Invalid window name \"%s\"", window_name);
	}
	auto copied_window = transformer.GetWindowClause(*base_window_name);
	if (copied_window->StartExpr() || copied_window->EndExpr() ||
	    !transformer.IsWindowFrameDefault(copied_window->WindowStart(), copied_window->WindowEnd())) {
		throw ParserException("cannot copy window \"%s\" because it has a frame clause", window_name);
	}
	copied_window->WindowStartMutable() = window_frame_contents->WindowStart();
	copied_window->WindowEndMutable() = window_frame_contents->WindowEnd();
	copied_window->WindowExcludeMutable() = window_frame_contents->WindowExclude();
	copied_window->StartExprMutable() = std::move(window_frame_contents->StartExprMutable());
	copied_window->EndExprMutable() = std::move(window_frame_contents->EndExprMutable());

	if (!copied_window->OrderBy().empty() && !window_frame_contents->OrderBy().empty()) {
		throw ParserException("Cannot override ORDER BY clause of window \"%s\"", window_name);
	}
	if (copied_window->OrderBy().empty()) {
		copied_window->OrderByMutable() = std::move(window_frame_contents->OrderByMutable());
	}
	if (!copied_window->Partitions().empty() && !window_frame_contents->Partitions().empty()) {
		throw ParserException("Cannot override PARTITION BY clause of window \"%s\"", window_name);
	}
	if (copied_window->Partitions().empty()) {
		copied_window->PartitionsMutable() = std::move(window_frame_contents->PartitionsMutable());
	}
	return copied_window;
}

Identifier PEGTransformerFactory::TransformBaseWindowName(PEGTransformer &transformer, const Identifier &identifier) {
	return identifier;
}
#else
unique_ptr<WindowExpression> PEGTransformerFactory::TransformOverClause(PEGTransformer &transformer,
                                                                        ParseResult &parse_result) {
	if (transformer.in_window_definition) {
		throw ParserException("window functions are not allowed in window definitions");
	}
	auto &list_pr = parse_result.Cast<ListParseResult>();
	transformer.in_window_definition = true;
	auto window_frame = transformer.Transform<unique_ptr<WindowExpression>>(list_pr.GetChild(1));
	transformer.in_window_definition = false;
	return window_frame;
}

unique_ptr<WindowExpression> PEGTransformerFactory::TransformWindowFrame(PEGTransformer &transformer,
                                                                         ParseResult &choice_result) {
	if (choice_result.type == ParseResultType::IDENTIFIER) {
		auto window_name = choice_result.Cast<IdentifierParseResult>().identifier;
		return transformer.GetWindowClause(window_name);
	}
	return transformer.Transform<unique_ptr<WindowExpression>>(choice_result);
}

unique_ptr<WindowExpression> PEGTransformerFactory::TransformParensIdentifier(PEGTransformer &transformer,
                                                                              const Identifier &identifier) {
	auto window_name = identifier.GetIdentifierName();
	auto window_clause = transformer.GetWindowClause(identifier);
	if (window_clause->start_expr || window_clause->end_expr ||
	    !transformer.IsWindowFrameDefault(window_clause->start, window_clause->end)) {
		throw ParserException("cannot copy window \"%s\" because it has a frame clause", window_name);
	}
	return window_clause;
}

unique_ptr<WindowExpression>
PEGTransformerFactory::TransformWindowFrameContentsParens(PEGTransformer &transformer,
                                                          unique_ptr<WindowExpression> window_frame_contents) {
	return window_frame_contents;
}

unique_ptr<WindowExpression>
PEGTransformerFactory::TransformWindowFrameNameContentsParens(PEGTransformer &transformer,
                                                              unique_ptr<WindowExpression> window_frame_name_contents) {
	return window_frame_name_contents;
}

unique_ptr<WindowExpression>
PEGTransformerFactory::TransformWindowFrameNameContents(PEGTransformer &transformer,
                                                        const optional<Identifier> &base_window_name,
                                                        unique_ptr<WindowExpression> window_frame_contents) {
	if (!base_window_name) {
		return window_frame_contents;
	}
	auto window_name = base_window_name->GetIdentifierName();
	auto lower_name = StringUtil::Lower(window_name);
	if (lower_name == "partition" || lower_name == "range" || lower_name == "rows" || lower_name == "groups") {
		throw ParserException("Invalid window name \"%s\"", window_name);
	}
	auto copied_window = transformer.GetWindowClause(*base_window_name);
	if (copied_window->start_expr || copied_window->end_expr ||
	    !transformer.IsWindowFrameDefault(copied_window->start, copied_window->end)) {
		throw ParserException("cannot copy window \"%s\" because it has a frame clause", window_name);
	}
	copied_window->start = window_frame_contents->start;
	copied_window->end = window_frame_contents->end;
	copied_window->exclude_clause = window_frame_contents->exclude_clause;
	copied_window->start_expr = std::move(window_frame_contents->start_expr);
	copied_window->end_expr = std::move(window_frame_contents->end_expr);

	if (!copied_window->orders.empty() && !window_frame_contents->orders.empty()) {
		throw ParserException("Cannot override ORDER BY clause of window \"%s\"", window_name);
	}
	if (copied_window->orders.empty()) {
		copied_window->orders = std::move(window_frame_contents->orders);
	}
	if (!copied_window->partitions.empty() && !window_frame_contents->partitions.empty()) {
		throw ParserException("Cannot override PARTITION BY clause of window \"%s\"", window_name);
	}
	if (copied_window->partitions.empty()) {
		copied_window->partitions = std::move(window_frame_contents->partitions);
	}
	return copied_window;
}

Identifier PEGTransformerFactory::TransformBaseWindowName(PEGTransformer &transformer, const Identifier &identifier) {
	return identifier;
}
#endif
} // namespace duckpgq_peg
} // namespace duckdb
