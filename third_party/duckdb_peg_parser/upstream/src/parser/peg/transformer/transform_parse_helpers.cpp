#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/compat/name_metadata.hpp"
namespace duckdb {
namespace duckpgq_peg {
QualifiedName PEGTransformerFactory::StringToQualifiedName(vector<string> input) {
	if (input.empty()) {
		throw InternalException("QualifiedName cannot be made with an empty input.");
	}
	if (input.size() == 1) {
		return duckpgq_compat::MakeQualifiedName(Identifier(input[0]));
	} else if (input.size() == 2) {
		return duckpgq_compat::MakeQualifiedName({Identifier(input[0])}, Identifier(input[1]));
	} else if (input.size() == 3) {
		return duckpgq_compat::MakeQualifiedName({Identifier(input[0]), Identifier(input[1])}, Identifier(input[2]));
	} else {
		throw ParserException("Too many qualifications found - expected [catalog.schema.name] or [schema.name]");
	}
}
bool PEGTransformerFactory::ExpressionIsEmptyStar(const ParsedExpression &expr) {
	if (expr.GetExpressionClass() != ExpressionClass::STAR) {
		return false;
	}
	auto &star = expr.Cast<StarExpression>();
#if __has_include("duckdb/common/identifier.hpp")
	if (!star.IsColumns() && star.ExcludeList().empty() && star.ReplaceList().empty()) {
#else
	if (!star.columns && star.exclude_list.empty() && star.replace_list.empty()) {
#endif
		return true;
	}
	return false;
}
vector<reference<ParseResult>> PEGTransformerFactory::ExtractParseResultsFromList(ParseResult &parse_result) {
	// List(D) <- D (',' D)* ','?
	vector<reference<ParseResult>> result;
	auto &list_pr = parse_result.Cast<ListParseResult>();
	result.push_back(list_pr.GetChild(0));
	auto &opt_child = list_pr.Child<OptionalParseResult>(1);
	if (opt_child.HasResult()) {
		auto &repeat_result = opt_child.GetResult().Cast<RepeatParseResult>();
		for (auto &child : repeat_result.GetChildren()) {
			auto &list_child = child.get().Cast<ListParseResult>();
			result.push_back(list_child.GetChild(1));
		}
	}
	return result;
}

ParseResult &PEGTransformerFactory::ExtractResultFromParens(ParseResult &parse_result) {
	// Parens(D) <- '(' D ')'
	auto &list_pr = parse_result.Cast<ListParseResult>();
	return list_pr.GetChild(1);
}
} // namespace duckpgq_peg
} // namespace duckdb
