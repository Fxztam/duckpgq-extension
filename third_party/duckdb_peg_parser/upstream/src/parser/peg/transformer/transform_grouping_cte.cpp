#include "duckdb/common/enum_util.hpp"
#include "duckpgq/compat/expression_access.hpp"
#include "duckpgq/compat/alter_access.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/parser/expression_map.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/ast/distinct_clause.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/ast/join_prefix.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/ast/join_qualifier.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/ast/limit_percent_result.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/ast/table_alias.hpp"
#include "duckdb/parser/tableref/showref.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/tableref/emptytableref.hpp"
#include "duckdb/parser/query_node/select_node.hpp"
#include "duckdb/parser/tableref/joinref.hpp"
#include "duckdb/parser/tableref/expressionlistref.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "duckdb/parser/tableref/at_clause.hpp"
#include "duckdb/parser/query_node/set_operation_node.hpp"
#include "duckdb/parser/tableref/pivotref.hpp"
#include "duckdb/parser/statement/insert_statement.hpp"
#include "duckdb/parser/statement/update_statement.hpp"
#include "duckdb/parser/statement/delete_statement.hpp"
#if __has_include("duckdb/parser/query_node/insert_query_node.hpp")
#include "duckdb/parser/query_node/insert_query_node.hpp"
#include "duckdb/parser/query_node/update_query_node.hpp"
#include "duckdb/parser/query_node/delete_query_node.hpp"
#endif

#include "duckdb/parser/query_node/recursive_cte_node.hpp"
namespace duckdb {
namespace duckpgq_peg {
unique_ptr<QueryNode> PEGTransformerFactory::ToRecursiveCTE(unique_ptr<QueryNode> node, const Identifier &name,
                                                            vector<Identifier> &aliases,
                                                            vector<unique_ptr<ParsedExpression>> &key_targets) {
	if (node->type != QueryNodeType::SET_OPERATION_NODE) {
		return node;
	}

	auto &set_node = node->Cast<SetOperationNode>();

	if (set_node.setop_type != SetOperationType::UNION) {
		return node;
	}

	if (set_node.children.size() < 2) {
		throw ParserException("Expected at least two children to set operation node in recursive CTE");
	}

	auto recursive_node = make_uniq<RecursiveCTENode>();
	recursive_node->cte_map = std::move(set_node.cte_map);
	recursive_node->ctename = duckpgq_compat::HostName(name);
	recursive_node->aliases = duckpgq_compat::HostNames(aliases);

	auto owned_set_node = unique_ptr_cast<QueryNode, SetOperationNode>(std::move(node));
	recursive_node->union_all = owned_set_node->setop_all;

	for (auto &modifier : owned_set_node->modifiers) {
		if (modifier->type == ResultModifierType::LIMIT_MODIFIER
#if !__has_include("duckdb/common/identifier.hpp")
		    || modifier->type == ResultModifierType::LIMIT_PERCENT_MODIFIER
#endif
		) {
			throw ParserException("LIMIT or OFFSET in a recursive query is not allowed");
		}
		if (modifier->type == ResultModifierType::ORDER_MODIFIER) {
			throw ParserException("ORDER BY in a recursive query is not allowed");
		}
	}
	if (owned_set_node->children.size() == 2) {
		recursive_node->left = std::move(owned_set_node->children[0]);
		recursive_node->right = std::move(owned_set_node->children[1]);
	} else {
		// N-ary flattened node: split into binary (left = all but last, right = last)
		// This matches the left-recursive binary tree structure from the grammar
		recursive_node->right = std::move(owned_set_node->children.back());
		owned_set_node->children.pop_back();
		if (owned_set_node->children.size() == 1) {
			recursive_node->left = std::move(owned_set_node->children[0]);
		} else {
			recursive_node->left = std::move(owned_set_node);
		}
	}
	for (auto &key : key_targets) {
		recursive_node->key_targets.emplace_back(key->Copy());
	}

	return std::move(recursive_node);
}

struct GroupingExpressionMap {
	parsed_expression_map_t<ProjectionIndex> map;
};

static void CheckGroupingSetMax(idx_t count) {
	static constexpr const idx_t MAX_GROUPING_SETS = 65535;
	if (count > MAX_GROUPING_SETS) {
		throw ParserException("Maximum grouping set count of %d exceeded", MAX_GROUPING_SETS);
	}
}

static void CheckGroupingSetCubes(idx_t current_count, idx_t cube_count) {
	idx_t combinations = 1;
	for (idx_t i = 0; i < cube_count; i++) {
		combinations *= 2;
		CheckGroupingSetMax(current_count + combinations);
	}
}

static GroupingSet VectorToGroupingSet(vector<ProjectionIndex> &indexes) {
	GroupingSet result;
	for (idx_t i = 0; i < indexes.size(); i++) {
		result.insert(indexes[i]);
	}
	return result;
}

void PEGTransformerFactory::AddGroupByExpression(unique_ptr<ParsedExpression> expression, GroupingExpressionMap &map,
                                                 GroupByNode &result, vector<ProjectionIndex> &result_set) {
	if (expression->GetExpressionType() == ExpressionType::FUNCTION) {
		auto &func = expression->Cast<FunctionExpression>();
		if (duckpgq_compat::FunctionName(func) == "row") {
			for (auto &child : duckpgq_compat::Arguments(func)) {
				AddGroupByExpression(std::move(duckpgq_compat::Expression(child)), map, result, result_set);
			}
			return;
		}
	}
	auto entry = map.map.find(*expression);
	ProjectionIndex result_idx;
	if (entry == map.map.end()) {
		result_idx = ProjectionIndex(result.group_expressions.size());
		map.map[*expression] = result_idx;
		result.group_expressions.push_back(std::move(expression));
	} else {
		result_idx = entry->second;
	}
	result_set.push_back(result_idx);
}

static void AddCubeSets(const GroupingSet &current_set, vector<GroupingSet> &cube_sets,
                        vector<GroupingSet> &result_sets, idx_t start_idx = 0) {
	CheckGroupingSetMax(result_sets.size());
	result_sets.push_back(current_set);
	for (idx_t k = start_idx; k < cube_sets.size(); k++) {
		auto child_set = current_set;
		child_set.insert(cube_sets[k].begin(), cube_sets[k].end());
		AddCubeSets(child_set, cube_sets, result_sets, k + 1);
	}
}

vector<GroupingSet> PEGTransformerFactory::GroupByExpressionUnfolding(GroupByExpressionInfo &group_by_expr,
                                                                      GroupingExpressionMap &map, GroupByNode &result) {
	vector<GroupingSet> result_sets;
	if (group_by_expr.type == GroupByExpressionInfoType::EMPTY) {
		result_sets.emplace_back();

	} else if (group_by_expr.type == GroupByExpressionInfoType::EXPRESSION) {
		vector<ProjectionIndex> indexes;
		AddGroupByExpression(std::move(group_by_expr.expression), map, result, indexes);
		result_sets.push_back(VectorToGroupingSet(indexes));
	} else if (group_by_expr.type == GroupByExpressionInfoType::GROUPING_SETS) {
		for (auto &child_expr : group_by_expr.children) {
			auto child_sets = GroupByExpressionUnfolding(child_expr, map, result);
			result_sets.insert(result_sets.end(), child_sets.begin(), child_sets.end());
		}
	} else if (group_by_expr.type == GroupByExpressionInfoType::CUBE ||
	           group_by_expr.type == GroupByExpressionInfoType::ROLLUP) {
		if (group_by_expr.expressions.empty()) {
			throw ParserException("CUBE or ROLLUP column list cannot be empty");
		}

		vector<GroupingSet> unfolding_sets;
		for (auto &expr : group_by_expr.expressions) {
			vector<ProjectionIndex> indexes;
			AddGroupByExpression(std::move(expr), map, result, indexes);

			GroupingSet s;
			for (auto idx : indexes) {
				s.insert(idx);
			}
			unfolding_sets.push_back(std::move(s));
		}

		if (group_by_expr.type == GroupByExpressionInfoType::CUBE) {
			CheckGroupingSetCubes(result_sets.size(), unfolding_sets.size());
			GroupingSet current_set;
			AddCubeSets(current_set, unfolding_sets, result_sets, 0);
		} else {
			GroupingSet current_set;
			result_sets.push_back(current_set);
			for (idx_t i = 0; i < unfolding_sets.size(); i++) {
				current_set.insert(unfolding_sets[i].begin(), unfolding_sets[i].end());
				result_sets.push_back(current_set);
			}
		}
	}
	return result_sets;
}

string PEGTransformerFactory::TransformCubeKeyword(PEGTransformer &transformer) {
	return "CUBE";
}

string PEGTransformerFactory::TransformRollupKeyword(PEGTransformer &transformer) {
	return "ROLLUP";
}

GroupByNode PEGTransformerFactory::TransformGroupByList(PEGTransformer &transformer,
                                                        vector<GroupByExpressionInfo> group_by_expression) {
	GroupByNode result;
	GroupingExpressionMap map;

	for (auto &group_by_expr : group_by_expression) {
		vector<GroupingSet> next_sets = GroupByExpressionUnfolding(group_by_expr, map, result);

		if (result.grouping_sets.empty()) {
			result.grouping_sets = std::move(next_sets);
		} else {
			vector<GroupingSet> new_sets;
			idx_t grouping_set_count = result.grouping_sets.size() * next_sets.size();
			CheckGroupingSetMax(grouping_set_count);
			new_sets.reserve(grouping_set_count);

			for (auto &current_set : result.grouping_sets) {
				for (auto &next_set : next_sets) {
					GroupingSet combined_set;
					combined_set.insert(current_set.begin(), current_set.end());
					combined_set.insert(next_set.begin(), next_set.end());
					new_sets.push_back(std::move(combined_set));
				}
			}
			result.grouping_sets = std::move(new_sets);
		}
	}
	return result;
}

GroupByExpressionInfo PEGTransformerFactory::TransformGroupByBaseExpression(PEGTransformer &transformer,
                                                                            unique_ptr<ParsedExpression> expression) {
	GroupByExpressionInfo result;
	result.type = GroupByExpressionInfoType::EXPRESSION;
	result.expression = std::move(expression);
	return result;
}

GroupByExpressionInfo PEGTransformerFactory::TransformEmptyGroupingItem(PEGTransformer &transformer) {
	GroupByExpressionInfo result;
	result.type = GroupByExpressionInfoType::EMPTY;
	return result;
}

GroupByExpressionInfo
PEGTransformerFactory::TransformCubeOrRollupClause(PEGTransformer &transformer, const string &cube_or_rollup,
                                                   optional<vector<unique_ptr<ParsedExpression>>> expression) {
	GroupByExpressionInfo result;
	result.type = StringUtil::CIEquals(cube_or_rollup, "CUBE") ? GroupByExpressionInfoType::CUBE
	                                                           : GroupByExpressionInfoType::ROLLUP;
	if (expression) {
		result.expressions = std::move(*expression);
	}
	return result;
}

GroupByExpressionInfo
PEGTransformerFactory::TransformGroupingSetsClause(PEGTransformer &transformer,
                                                   vector<GroupByExpressionInfo> group_by_expression) {
	GroupByExpressionInfo result;
	result.type = GroupByExpressionInfoType::GROUPING_SETS;
	result.children = std::move(group_by_expression);
	return result;
}

CommonTableExpressionMap PEGTransformerFactory::TransformWithClause(PEGTransformer &transformer,
                                                                    ParseResult &parse_result) {
	auto &list_pr = parse_result.Cast<ListParseResult>();
	bool is_recursive = list_pr.Child<OptionalParseResult>(1).HasResult();
	auto with_statement_list = ExtractParseResultsFromList(list_pr.Child<ListParseResult>(2));
	CommonTableExpressionMap result;

	for (idx_t entry_idx = 0; entry_idx < with_statement_list.size(); entry_idx++) {
		auto with_entry = transformer.Transform<pair<Identifier, unique_ptr<CommonTableExpressionInfo>>>(
		    with_statement_list[entry_idx]);

		if (is_recursive) {
#if __has_include("duckdb/common/identifier.hpp")
			auto &query_node = with_entry.second->query_node;
#else
			auto &query_node = with_entry.second->query->node;
#endif
			if (!query_node) {
				throw ParserException("Recursive CTEs with DML statements are not supported");
			}
#if __has_include("duckdb/common/identifier.hpp")
			if (query_node->type == QueryNodeType::INSERT_QUERY_NODE ||
			    query_node->type == QueryNodeType::UPDATE_QUERY_NODE ||
			    query_node->type == QueryNodeType::DELETE_QUERY_NODE) {
				throw ParserException("Recursive CTEs with DML statements are not supported");
			}
#endif
			// Now safe to call on SELECT, VALUES, etc.
#if __has_include("duckdb/common/identifier.hpp")
			auto aliases = with_entry.second->aliases;
#else
			auto aliases = StringsToIdentifiers(with_entry.second->aliases);
#endif
			query_node = ToRecursiveCTE(std::move(query_node), with_entry.first, aliases,
			                            with_entry.second->key_targets);
		}
		auto &cte_name = with_entry.first;

		auto it = result.map.find(duckpgq_compat::HostName(cte_name));
		if (it != result.map.end()) {
			// can't have two CTEs with same name
			throw ParserException("Duplicate CTE name \"%s\"", cte_name.GetIdentifierName());
		}
		result.map.insert(duckpgq_compat::HostName(with_entry.first), std::move(with_entry.second));
	}
	return result;
}

pair<Identifier, unique_ptr<CommonTableExpressionInfo>>
PEGTransformerFactory::TransformWithStatement(PEGTransformer &transformer, const Identifier &col_id_or_string,
                                              const optional<vector<string>> &insert_column_list,
                                              optional<vector<unique_ptr<ParsedExpression>>> using_key,
                                              const optional<bool> &materialized, unique_ptr<TableRef> cte_body) {
	auto result = make_uniq<CommonTableExpressionInfo>();
	auto cte_name = col_id_or_string;
	if (insert_column_list) {
		result->aliases = duckpgq_compat::HostNames(StringsToIdentifiers(*insert_column_list));
	}
	if (using_key) {
		result->key_targets = std::move(*using_key);
	}
	if (materialized) {
		// If this has a result, we know it is either NEVER or ALWAYS
		if (*materialized) {
			result->materialized = CTEMaterialize::CTE_MATERIALIZE_NEVER;
		} else {
			result->materialized = CTEMaterialize::CTE_MATERIALIZE_ALWAYS;
		}
	}
	D_ASSERT(cte_body->type == TableReferenceType::SUBQUERY);
	auto subquery_ref = unique_ptr_cast<TableRef, SubqueryRef>(std::move(cte_body));
#if __has_include("duckdb/common/identifier.hpp")
	result->query_node = std::move(subquery_ref->subquery->node);
#else
	result->query = std::move(subquery_ref->subquery);
#endif
	return make_pair(cte_name, std::move(result));
}

unique_ptr<TableRef>
PEGTransformerFactory::TransformCTESelectBody(PEGTransformer &transformer,
                                              unique_ptr<SelectStatement> select_statement_internal) {
	return make_uniq<SubqueryRef>(std::move(select_statement_internal));
}

unique_ptr<TableRef> PEGTransformerFactory::TransformCTEDMLBody(PEGTransformer &transformer,
                                                                unique_ptr<SQLStatement> statement) {
#if !__has_include("duckdb/common/identifier.hpp")
	throw ParserException("DuckPGQ: DML CTE bodies are not supported by canonical DuckDB 1.5.5");
#else
	unique_ptr<QueryNode> query_node;
	switch (statement->type) {
	case StatementType::INSERT_STATEMENT:
		query_node = unique_ptr_cast<InsertQueryNode, QueryNode>(std::move(statement->Cast<InsertStatement>().node));
		break;
	case StatementType::UPDATE_STATEMENT:
		query_node = unique_ptr_cast<UpdateQueryNode, QueryNode>(std::move(statement->Cast<UpdateStatement>().node));
		break;
	case StatementType::DELETE_STATEMENT:
		query_node = unique_ptr_cast<DeleteQueryNode, QueryNode>(std::move(statement->Cast<DeleteStatement>().node));
		break;
	default:
		throw ParserException("A CTE body must be a SELECT, INSERT, UPDATE, or DELETE statement");
	}
	auto select_statement = make_uniq<SelectStatement>();
	select_statement->node = std::move(query_node);
	return make_uniq<SubqueryRef>(std::move(select_statement));
#endif
}

bool PEGTransformerFactory::TransformMaterialized(PEGTransformer &transformer, const bool &has_result) {
	return has_result;
}
} // namespace duckpgq_peg
} // namespace duckdb
