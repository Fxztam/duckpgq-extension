#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/statement/delete_statement.hpp"
#if __has_include("duckdb/parser/query_node/delete_query_node.hpp")
#include "duckdb/parser/query_node/delete_query_node.hpp"
#endif

namespace duckdb {
namespace duckpgq_peg {

unique_ptr<SQLStatement> PEGTransformerFactory::TransformDeleteStatement(
    PEGTransformer &transformer, optional<CommonTableExpressionMap> with_clause,
    unique_ptr<BaseTableRef> target_opt_alias, optional<vector<unique_ptr<TableRef>>> delete_using_clause,
    optional<unique_ptr<ParsedExpression>> where_clause,
    optional<vector<unique_ptr<ParsedExpression>>> returning_clause) {
	auto result = make_uniq<DeleteStatement>();
	#if __has_include("duckdb/common/identifier.hpp")
	auto &node = *result->node;
#else
	auto &node = *result;
#endif
	if (with_clause && !with_clause->map.empty()) {
		node.cte_map = std::move(*with_clause);
	}
	node.table = std::move(target_opt_alias);
	if (delete_using_clause) {
		node.using_clauses = std::move(*delete_using_clause);
	}
	if (where_clause) {
		node.condition = std::move(*where_clause);
	}
	if (returning_clause) {
		node.returning_list = std::move(*returning_clause);
	}
	return std::move(result);
}

unique_ptr<BaseTableRef> PEGTransformerFactory::TransformTargetOptAlias(PEGTransformer &transformer,
                                                                        unique_ptr<BaseTableRef> base_table_name,
                                                                        const bool &has_result,
                                                                        const optional<Identifier> &col_id) {
	if (col_id && !col_id->empty()) {
		#if __has_include("duckdb/common/identifier.hpp")
		base_table_name->alias = Identifier(*col_id);
#else
		base_table_name->alias = col_id->GetIdentifierName();
#endif
	}
	return base_table_name;
}

vector<unique_ptr<TableRef>> PEGTransformerFactory::TransformDeleteUsingClause(PEGTransformer &transformer,
                                                                               vector<unique_ptr<TableRef>> table_ref) {
	return table_ref;
}

unique_ptr<SQLStatement> PEGTransformerFactory::TransformTruncateStatement(PEGTransformer &transformer,
                                                                           const bool &has_result,
                                                                           unique_ptr<BaseTableRef> base_table_name) {
	auto result = make_uniq<DeleteStatement>();
	#if __has_include("duckdb/common/identifier.hpp")
	result->node->table = std::move(base_table_name);
#else
	result->table = std::move(base_table_name);
#endif
	return std::move(result);
}

} // namespace duckpgq_peg
} // namespace duckdb
