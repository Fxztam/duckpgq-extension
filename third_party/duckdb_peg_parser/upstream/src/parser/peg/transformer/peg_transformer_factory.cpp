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

unique_ptr<SQLStatement> PEGTransformerFactory::TransformStatement(PEGTransformer &transformer,
                                                                   ParseResult &parse_result) {
	auto &list_pr = parse_result.Cast<ListParseResult>();
	auto &choice_pr = list_pr.Child<ChoiceParseResult>(0);
	auto result = transformer.Transform<unique_ptr<SQLStatement>>(choice_pr.GetResult());
	duckpgq_compat::TransferStatementParameters(*result, transformer.named_parameter_map,
	                                           transformer.has_anonymous_parameters);
	return result;
}

unique_ptr<SQLStatement> PEGTransformerFactory::TransformStatementTrampoline(PEGTransformer &transformer,
                                                                             ParseResult &parse_result) {
	auto &list_pr = parse_result.Cast<ListParseResult>();
	auto &choice_pr = list_pr.Child<ChoiceParseResult>(0);
	auto &choice_result = choice_pr.GetResult();
	auto &ops_map = GeneratedTrampolineOps();
	auto ops_entry = ops_map.find(choice_result.name);
	if (ops_entry == ops_map.end()) {
		throw NotImplementedException("No trampoline transformer for statement rule '%s'", choice_result.name);
	}

	TransformStack stack(transformer);
	auto result = stack.Execute<unique_ptr<SQLStatement>>(choice_result, *ops_entry->second);
	duckpgq_compat::TransferStatementParameters(*result, transformer.named_parameter_map,
	                                           transformer.has_anonymous_parameters);
	return result;
}

unique_ptr<TransformResultValue>
PEGTransformerFactory::TransformStatementTrampolineInternal(PEGTransformer &transformer, ParseResult &parse_result) {
	auto result = TransformStatementTrampoline(transformer, parse_result);
	return make_uniq<TypedTransformResult<unique_ptr<SQLStatement>>>(std::move(result));
}

void PEGTransformerFactory::RegisterGeneratedTrampoline() {
	trampoline_transform_functions["Statement"] = &PEGTransformerFactory::TransformStatementTrampolineInternal;
}

static unique_ptr<SQLStatement> ExtractAndTransformStatement(PEGTransformer &transformer,
                                                             const vector<MatcherToken> &tokens, ParseResult &stmt_pr,
                                                             optional_idx terminator_offset) {
	auto stmt = transformer.Transform<unique_ptr<SQLStatement>>(stmt_pr);

	duckpgq_compat::TransferStatementParameters(*stmt, transformer.named_parameter_map,
	                                           transformer.has_anonymous_parameters);
	if (!transformer.pivot_entries.empty()) {
		stmt = transformer.CreatePivotStatement(std::move(stmt));
	}
	transformer.Clear();

	// Calculate location and length cleanly
	if (stmt_pr.offset.IsValid()) {
		stmt->stmt_location = stmt_pr.offset.GetIndex();

		idx_t end_index =
		    terminator_offset.IsValid() ? terminator_offset.GetIndex() : (tokens.back().offset + tokens.back().length);

		stmt->stmt_length = end_index - stmt->stmt_location;
	}

	return stmt;
}

unique_ptr<SQLStatement> PEGTransformerFactory::TransformTopLevelStatement(vector<MatcherToken> &tokens,
                                                                           ParserOptions &options,
                                                                           Matcher &root_matcher, idx_t &token_cursor) {
	if (token_cursor >= tokens.size()) {
		return nullptr;
	}
	vector<MatcherSuggestion> suggestions;
	ParseResultAllocator parse_result_allocator;
	idx_t max_token_index = token_cursor;
	MatchState state(tokens, suggestions, parse_result_allocator, max_token_index, options.preserve_identifier_case,
	                 token_cursor);
	auto match_result = root_matcher.MatchParseResult(state);
	if (match_result == nullptr) {
		// syntax error — surface as a parser exception in the same shape as Transform()
		string token_stream;
		for (auto &token : tokens) {
			token_stream += token.text + " ";
		}
		idx_t error_token_idx = state.GetMaxTokenIndex();
		if (error_token_idx >= tokens.size()) {
			error_token_idx = tokens.size() - 1;
		}
		// Walk back past the EOI sentinel so the error message names a real token.
		if (error_token_idx > 0 && (tokens[error_token_idx].type == TokenType::END_OF_INPUT ||
		                            tokens[error_token_idx].type == TokenType::END_OF_INPUT_AUTOCOMPLETE)) {
			error_token_idx--;
		}
		auto &error_token = tokens[error_token_idx];
		auto error_message = "syntax error at or near \"" + error_token.text + "\"";
		throw ParserException::SyntaxError(token_stream, error_message, error_token.offset);
	}

	// Advance the caller's cursor past the consumed tokens.
	token_cursor = state.token_index;

	// TopLevelStatement <- Statement? (';'+ / EndOfInput)
	//   child 0: Optional<Statement>
	//   child 1: bracket-wrapper list around Choice<';'+ | EndOfInput>
	auto &tls = match_result->Cast<ListParseResult>();
	auto &stmt_opt = tls.Child<OptionalParseResult>(0);
	if (!stmt_opt.HasResult()) {
		// separator-only or EOI-only TopLevelStatement — no statement to yield
		return nullptr;
	}
	auto &term_wrapper = tls.Child<ListParseResult>(1);
	auto &term_inner = term_wrapper.Child<ChoiceParseResult>(0).GetResult();
	optional_idx terminator_offset;
	if (term_inner.type != ParseResultType::END_OF_INPUT) {
		auto semi_children = term_inner.Cast<RepeatParseResult>().GetChildren();
		if (!semi_children.empty()) {
			terminator_offset = semi_children[0].get().offset;
		}
	}

	ArenaAllocator transformer_allocator(Allocator::DefaultAllocator());
	PEGTransformerState transformer_state(tokens);
	auto &transform_functions = GetTransformFunctions(options);
	PEGTransformer transformer(transformer_allocator, transformer_state, transform_functions, parser.rules, options);

	return ExtractAndTransformStatement(transformer, tokens, stmt_opt.GetResult(), terminator_offset);
}

#define REGISTER_TRANSFORM(FUNCTION) Register(string(#FUNCTION).substr(9), &FUNCTION)

void PEGTransformerFactory::RegisterComment() {
	// comment.gram
	REGISTER_TRANSFORM(TransformCommentValue);
}

void PEGTransformerFactory::RegisterCommon() {
	// common.gram
	REGISTER_TRANSFORM(TransformNumberLiteral);
	REGISTER_TRANSFORM(TransformStringLiteral);
	REGISTER_TRANSFORM(TransformIntervalToIntervalAsType);
}

void PEGTransformerFactory::RegisterCreateTable() {
	// create_table.gram
	REGISTER_TRANSFORM(TransformColLabelOrString);
	REGISTER_TRANSFORM(TransformIdentifier);
}

void PEGTransformerFactory::RegisterExpression() {
	// expression.gram
	REGISTER_TRANSFORM(TransformExpression);
	REGISTER_TRANSFORM(TransformPrefixExpression);
	REGISTER_TRANSFORM(TransformOverClause);
}

void PEGTransformerFactory::RegisterPivot() {
	// PivotStatement and UnpivotStatement measure parameter usage while transforming
	// the source table, so their top-level wrappers remain manual.
	REGISTER_TRANSFORM(TransformPivotStatement);
	REGISTER_TRANSFORM(TransformUnpivotStatement);
}

void PEGTransformerFactory::RegisterSelect() {
	// select.gram rules that remain manual after generated wrappers are registered.
	Register("SelectStatementInternal", &TransformSelectStatementInternalRule);
	REGISTER_TRANSFORM(TransformSimpleSelect);
	REGISTER_TRANSFORM(TransformTableRef);
	REGISTER_TRANSFORM(TransformWithClause);
	REGISTER_TRANSFORM(TransformWindowDefinition);
}

void PEGTransformerFactory::RegisterKeywordsAndIdentifiers() {
	Register("PragmaName", &TransformIdentifierOrKeyword);
	Register("TypeName", &TransformIdentifierOrKeyword);
	Register("ColLabel", &TransformIdentifierOrKeyword);
	Register("PlainIdentifier", &TransformIdentifierOrKeyword);
	Register("QuotedIdentifier", &TransformIdentifierOrKeyword);
	Register("ReservedKeyword", &TransformIdentifierOrKeyword);
	Register("UnreservedKeyword", &TransformIdentifierOrKeyword);
	Register("ColumnNameKeyword", &TransformIdentifierOrKeyword);
	Register("FuncNameKeyword", &TransformIdentifierOrKeyword);
	Register("TypeNameKeyword", &TransformIdentifierOrKeyword);
	Register("SettingName", &TransformIdentifierOrKeyword);
	Register("ExplainOptionName", &TransformIdentifierOrKeyword);
}

PEGTransformerFactory::PEGTransformerFactory() {
	RegisterGenerated();
	RegisterGeneratedTrampoline();
	REGISTER_TRANSFORM(TransformStatement);
	RegisterComment();
	RegisterCommon();
	RegisterCreateTable();
	RegisterExpression();
	RegisterPivot();
	RegisterSelect();
	RegisterKeywordsAndIdentifiers();
}

const case_insensitive_map_t<PEGTransformer::AnyTransformFunction> &
PEGTransformerFactory::GetTransformFunctions(ParserOptions &options) {
#if __has_include("duckdb/common/identifier.hpp")
	if (options.debug_transformer_trampoline_style) {
		return trampoline_transform_functions;
	}
#endif
	return sql_transform_functions;
}






} // namespace duckpgq_peg
} // namespace duckdb
