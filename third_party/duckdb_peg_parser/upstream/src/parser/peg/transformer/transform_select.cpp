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

namespace duckdb {
namespace duckpgq_peg {

unique_ptr<SQLStatement>
PEGTransformerFactory::TransformSelectStatement(PEGTransformer &transformer,
                                                unique_ptr<SelectStatement> select_statement_internal) {
	return std::move(select_statement_internal);
}

unique_ptr<SelectStatement> PEGTransformerFactory::TransformSelectStatementInternalRule(PEGTransformer &transformer,
                                                                                        ParseResult &parse_result) {
	auto &list_pr = parse_result.Cast<ListParseResult>();
	CommonTableExpressionMap cte_map;
	transformer.TransformOptional<CommonTableExpressionMap>(list_pr, 0, cte_map);
	if (!cte_map.map.empty()) {
		transformer.stored_cte_map.push_back(cte_map);
	}
	auto select_statement = transformer.Transform<unique_ptr<SelectStatement>>(list_pr.Child<ListParseResult>(1));

	if (!cte_map.map.empty()) {
		select_statement->node->cte_map = std::move(cte_map);
	}
	vector<unique_ptr<ResultModifier>> result_modifiers;
	transformer.TransformOptional<vector<unique_ptr<ResultModifier>>>(list_pr, 2, result_modifiers);
	for (auto &result_modifier : result_modifiers) {
		select_statement->node->modifiers.push_back(std::move(result_modifier));
	}
	if (select_statement->node->type != QueryNodeType::SELECT_NODE) {
		return select_statement;
	}
	auto &select_node = select_statement->node->Cast<SelectNode>();
	if (select_node.from_table->type != TableReferenceType::SHOW_REF) {
		return select_statement;
	}
	auto &show_ref = select_node.from_table->Cast<ShowRef>();
	if (!select_statement->node->cte_map.map.empty()) {
		throw ParserException("%s with CTE not allowed - wrap the statement in a subquery instead",
		                      EnumUtil::ToString(show_ref.show_type));
	}
	if (!select_statement->node->modifiers.empty()) {
		throw ParserException("%s with ORDER BY not allowed - wrap the statement in a subquery instead",
		                      EnumUtil::ToString(show_ref.show_type));
	}
	return select_statement;
}

unique_ptr<SelectStatement> PEGTransformerFactory::TransformSelectSetOpChain(
    PEGTransformer &transformer, unique_ptr<SelectStatement> intersect_chain,
    optional<vector<pair<unique_ptr<SetOperationNode>, unique_ptr<SelectStatement>>>> select_set_op_chain_tail) {
	auto select = std::move(intersect_chain);
	if (!select_set_op_chain_tail) {
		return select;
	}
	for (auto &tail : *select_set_op_chain_tail) {
		auto setop_result = std::move(tail.first);
		auto right_select = std::move(tail.second);
		if (select->node->type == QueryNodeType::SET_OPERATION_NODE && select->node->modifiers.empty() &&
		    select->node->cte_map.map.empty()) {
			auto &existing = select->node->Cast<SetOperationNode>();
			if (existing.setop_type == setop_result->setop_type && existing.setop_all == setop_result->setop_all &&
			    (existing.setop_type == SetOperationType::UNION ||
			     existing.setop_type == SetOperationType::UNION_BY_NAME)) {
				existing.children.push_back(std::move(right_select->node));
				continue;
			}
		}
		setop_result->children.push_back(std::move(select->node));
		setop_result->children.push_back(std::move(right_select->node));
		select->node = std::move(setop_result);
	}
	return select;
}

pair<unique_ptr<SetOperationNode>, unique_ptr<SelectStatement>>
PEGTransformerFactory::TransformSelectSetOpChainTail(PEGTransformer &transformer,
                                                     unique_ptr<SetOperationNode> setop_clause,
                                                     unique_ptr<SelectStatement> intersect_chain) {
	return make_pair(std::move(setop_clause), std::move(intersect_chain));
}

unique_ptr<SelectStatement> PEGTransformerFactory::TransformIntersectChain(
    PEGTransformer &transformer, unique_ptr<SelectStatement> select_atom,
    optional<vector<pair<unique_ptr<SetOperationNode>, unique_ptr<SelectStatement>>>> intersect_chain_tail) {
	auto select = std::move(select_atom);
	if (!intersect_chain_tail) {
		return select;
	}
	for (auto &tail : *intersect_chain_tail) {
		auto intersect_node = std::move(tail.first);
		auto right_select = std::move(tail.second);
		intersect_node->children.push_back(std::move(select->node));
		intersect_node->children.push_back(std::move(right_select->node));
		select->node = std::move(intersect_node);
	}
	return select;
}

pair<unique_ptr<SetOperationNode>, unique_ptr<SelectStatement>>
PEGTransformerFactory::TransformIntersectChainTail(PEGTransformer &transformer,
                                                   unique_ptr<SetOperationNode> set_intersect_clause,
                                                   unique_ptr<SelectStatement> select_atom) {
	return make_pair(std::move(set_intersect_clause), std::move(select_atom));
}

unique_ptr<SetOperationNode> PEGTransformerFactory::TransformSetIntersectClause(PEGTransformer &transformer,
                                                                                const optional<bool> &distinct_or_all) {
	auto result = make_uniq<SetOperationNode>();
	result->setop_type = SetOperationType::INTERSECT;
	if (distinct_or_all) {
		result->setop_all = !*distinct_or_all;
	}
	return result;
}

unique_ptr<SetOperationNode> PEGTransformerFactory::TransformSetopClause(PEGTransformer &transformer,
                                                                         const SetOperationType &setop_type,
                                                                         const optional<bool> &distinct_or_all,
                                                                         const bool &has_result) {
	auto result = make_uniq<SetOperationNode>();
	result->setop_type = setop_type;
	if (distinct_or_all) {
		result->setop_all = !*distinct_or_all;
	}
	if (has_result) {
		if (result->setop_type == SetOperationType::UNION) {
			result->setop_type = SetOperationType::UNION_BY_NAME;
		} else {
			throw ParserException("Invalid combination of %s and BY NAME", EnumUtil::ToString(result->setop_type));
		}
	}
	return result;
}

SetOperationType PEGTransformerFactory::TransformSetopUnion(PEGTransformer &transformer) {
	return SetOperationType::UNION;
}

SetOperationType PEGTransformerFactory::TransformSetopExcept(PEGTransformer &transformer) {
	return SetOperationType::EXCEPT;
}

bool PEGTransformerFactory::TransformLateral(PEGTransformer &transformer) {
	return true;
}

bool PEGTransformerFactory::TransformWithOrdinality(PEGTransformer &transformer) {
	return true;
}

unique_ptr<SelectStatement> PEGTransformerFactory::TransformSimpleSelect(PEGTransformer &transformer,
                                                                         ParseResult &parse_result) {
	auto &list_pr = parse_result.Cast<ListParseResult>();
	auto &opt_window_clause = list_pr.Child<OptionalParseResult>(4);
	if (opt_window_clause.HasResult()) {
		auto window_functions =
		    transformer.Transform<vector<unique_ptr<ParsedExpression>>>(opt_window_clause.GetResult());
		for (auto &window_func : window_functions) {
			D_ASSERT(!window_func->GetAlias().empty());
			Identifier window_name(window_func->GetAlias());
			window_func->ClearAlias();
			auto it = transformer.window_clauses.find(window_name);
			if (it != transformer.window_clauses.end()) {
				throw ParserException("window \"%s\" is already defined", window_name.GetIdentifierName());
			}
			auto window_function = unique_ptr_cast<ParsedExpression, WindowExpression>(std::move(window_func));
			transformer.window_clauses[window_name] = std::move(window_function);
		}
	}
	auto select_node = transformer.Transform<unique_ptr<SelectNode>>(list_pr.Child<ListParseResult>(0));
	transformer.TransformOptional<unique_ptr<ParsedExpression>>(list_pr, 1, select_node->where_clause);
	auto &group_opt = list_pr.Child<OptionalParseResult>(2);
	if (group_opt.HasResult()) {
		auto group_by_node = transformer.Transform<GroupByNode>(group_opt.GetResult());
		if (group_by_node.group_expressions.size() == 1 && ExpressionIsEmptyStar(*group_by_node.group_expressions[0])) {
			select_node->aggregate_handling = AggregateHandling::FORCE_AGGREGATES;
			group_by_node.group_expressions.clear();
			group_by_node.grouping_sets.clear();
		}
		select_node->groups = std::move(group_by_node);
	}
	transformer.TransformOptional<unique_ptr<ParsedExpression>>(list_pr, 3, select_node->having);
	transformer.TransformOptional<unique_ptr<ParsedExpression>>(list_pr, 5, select_node->qualify);
	transformer.TransformOptional<unique_ptr<SampleOptions>>(list_pr, 6, select_node->sample);
	auto select_statement = make_uniq<SelectStatement>();
	select_statement->node = std::move(select_node);
	transformer.window_clauses.clear();
	return select_statement;
}

FunctionArgument PEGTransformerFactory::TransformNamedFunctionArgument(PEGTransformer &transformer,
                                                                       MacroParameter named_parameter) {
	named_parameter.expression->SetAlias(duckpgq_compat::HostName(named_parameter.name));
	return FunctionArgument(named_parameter.name, std::move(named_parameter.expression));
}

FunctionArgument PEGTransformerFactory::TransformPositionalFunctionArgument(PEGTransformer &transformer,
                                                                            unique_ptr<ParsedExpression> expression) {
	return FunctionArgument(std::move(expression));
}

MacroParameter PEGTransformerFactory::TransformNamedParameter(PEGTransformer &transformer,
                                                              const Identifier &type_func_name,
                                                              const optional<LogicalType> &type,
                                                              unique_ptr<ParsedExpression> expression) {
	MacroParameter parameter;
	parameter.expression = std::move(expression);
	parameter.name = type_func_name;
	parameter.is_default = true;
	if (type) {
		parameter.type = *type;
	}
	return parameter;
}

unique_ptr<BaseTableRef> PEGTransformerFactory::TransformUnqualifiedBaseTableName(PEGTransformer &transformer,
                                                                                  const Identifier &table_name) {
	return duckpgq_compat::TableFromNames({table_name});
}

Identifier PEGTransformerFactory::TransformSchemaQualification(PEGTransformer &transformer,
                                                               const Identifier &schema_name) {
	return schema_name;
}

Identifier PEGTransformerFactory::TransformCatalogQualification(PEGTransformer &transformer,
                                                                const Identifier &catalog_name) {
	return catalog_name;
}

QualifiedName PEGTransformerFactory::TransformCatalogReservedSchemaIdentifier(
    PEGTransformer &transformer, const Identifier &catalog_qualification,
    const Identifier &reserved_schema_qualification, const Identifier &reserved_identifier_or_string_literal) {
	return duckpgq_compat::MakeQualifiedName({catalog_qualification, reserved_schema_qualification},
	                                       reserved_identifier_or_string_literal);
}

QualifiedName PEGTransformerFactory::TransformSchemaReservedIdentifierOrStringLiteral(
    PEGTransformer &transformer, const Identifier &schema_qualification,
    const Identifier &reserved_identifier_or_string_literal) {
	return duckpgq_compat::MakeQualifiedName({schema_qualification}, reserved_identifier_or_string_literal);
}

static bool IsConditionlessJoin(const JoinRef &join) {
	if (join.condition || !join.using_columns.empty()) {
		return false;
	}
	if (join.ref_type != JoinRefType::CROSS && join.ref_type != JoinRefType::POSITIONAL &&
	    join.ref_type != JoinRefType::NATURAL) {
		return false;
	}
	return true;
}

static unique_ptr<TableRef> ReassociateJoins(unique_ptr<TableRef> root) {
	// Left-rotate while the current node is a conditionless join and its right child is a join.
	// This converts right-associative join trees (from PEG grammar) to left-associative.
	while (root->type == TableReferenceType::JOIN) {
		auto &current = root->Cast<JoinRef>();
		if (!IsConditionlessJoin(current) || !current.right || current.right->type != TableReferenceType::JOIN) {
			break;
		}
		// Left rotation:
		//   current(left=A, right=inner(left=B, right=C))
		//   => inner(left=current(left=A, right=B), right=C)
		auto inner = std::move(current.right);
		auto &inner_join = inner->Cast<JoinRef>();
		current.right = std::move(inner_join.left);
		inner_join.left = std::move(root);
		root = std::move(inner);
	}
	return root;
}

//! Check whether the RHS TableRef of a JoinOrPivot parse result has its own JoinOrPivot* entries.
//! This distinguishes PEG right-recursion (has entries) from parenthesized joins (no entries).
//! Navigation: JoinOrPivot → Choice → JoinClause → Choice → JoinWithoutOnClause → child(2)=TableRef → child(1)=Optional
static bool RHSTableRefHasJoinOrPivot(ParseResult &join_or_pivot_pr) {
	auto &jop_list = join_or_pivot_pr.Cast<ListParseResult>();
	auto &jop_choice = jop_list.Child<ChoiceParseResult>(0);
	auto &join_clause = jop_choice.GetResult().Cast<ListParseResult>();
	auto &jc_choice = join_clause.Child<ChoiceParseResult>(0);
	auto &join_impl = jc_choice.GetResult().Cast<ListParseResult>();
	// For JoinWithoutOnClause the TableRef is at index 2
	auto &table_ref = join_impl.Child<ListParseResult>(2);
	return table_ref.Child<OptionalParseResult>(1).HasResult();
}

unique_ptr<TableRef> PEGTransformerFactory::TransformTableRef(PEGTransformer &transformer, ParseResult &parse_result) {
	auto &list_pr = parse_result.Cast<ListParseResult>();
	auto inner_table_ref = transformer.Transform<unique_ptr<TableRef>>(list_pr.Child<ListParseResult>(0));
	auto &join_or_pivot_opt = list_pr.Child<OptionalParseResult>(1);
	if (!join_or_pivot_opt.HasResult()) {
		return inner_table_ref;
	}
	auto &repeat_join_or_pivot = join_or_pivot_opt.GetResult().Cast<RepeatParseResult>();
	for (auto join_or_pivot : repeat_join_or_pivot.GetChildren()) {
		auto transform_join_or_pivot = transformer.Transform<unique_ptr<TableRef>>(join_or_pivot);
		if (transform_join_or_pivot->type == TableReferenceType::JOIN) {
			auto &join_ref = transform_join_or_pivot->Cast<JoinRef>();
			join_ref.left = std::move(inner_table_ref);
			if (IsConditionlessJoin(join_ref) && RHSTableRefHasJoinOrPivot(join_or_pivot)) {
				inner_table_ref = ReassociateJoins(std::move(transform_join_or_pivot));
			} else {
				inner_table_ref = std::move(transform_join_or_pivot);
			}
		} else if (transform_join_or_pivot->type == TableReferenceType::PIVOT) {
			auto &pivot_ref = transform_join_or_pivot->Cast<PivotRef>();
			pivot_ref.source = std::move(inner_table_ref);
			inner_table_ref = std::move(transform_join_or_pivot);
		} else {
			throw NotImplementedException("Unsupported TableRef type encountered: %s",
			                              EnumUtil::ToString(transform_join_or_pivot->type));
		}
	}
	return inner_table_ref;
}





unique_ptr<ParsedExpression> PEGTransformerFactory::TransformWindowDefinition(PEGTransformer &transformer,
                                                                              ParseResult &parse_result) {
	auto &list_pr = parse_result.Cast<ListParseResult>();
	transformer.in_window_definition = true;
	auto window_function = transformer.Transform<unique_ptr<WindowExpression>>(list_pr.Child<ListParseResult>(2));
	transformer.in_window_definition = false;
	window_function->SetAlias(duckpgq_compat::HostName(list_pr.Child<IdentifierParseResult>(0).identifier));
	return std::move(window_function);
}

unique_ptr<SampleOptions> PEGTransformerFactory::TransformSampleEntryFunction(
    PEGTransformer &transformer, const optional<SampleMethod> &sample_function, unique_ptr<SampleOptions> sample_count,
    const optional<optional_idx> &repeatable_sample) {
	if (sample_function) {
		sample_count->method = *sample_function;
	}
	if (repeatable_sample) {
		sample_count->seed = *repeatable_sample;
		sample_count->repeatable = true;
	}
	return sample_count;
}

unique_ptr<SampleOptions>
PEGTransformerFactory::TransformSampleEntryCount(PEGTransformer &transformer, unique_ptr<SampleOptions> sample_count,
                                                 const optional<pair<SampleMethod, optional_idx>> &sample_properties) {
	if (sample_properties) {
		sample_count->method = sample_properties->first;
		sample_count->seed = sample_properties->second;
		if (sample_count->seed.IsValid()) {
			sample_count->repeatable = true;
		}
	}
	return sample_count;
}

bool PEGTransformerFactory::TransformSamplePercentage(PEGTransformer &transformer) {
	return true;
}

bool PEGTransformerFactory::TransformSampleRows(PEGTransformer &transformer) {
	return false;
}

unique_ptr<SelectStatement>
PEGTransformerFactory::TransformSelectParens(PEGTransformer &transformer,
                                             unique_ptr<SelectStatement> select_statement_internal) {
	return select_statement_internal;
}

vector<unique_ptr<ResultModifier>>
PEGTransformerFactory::TransformResultModifiers(PEGTransformer &transformer,
                                                optional<vector<OrderByNode>> order_by_clause,
                                                optional<unique_ptr<ResultModifier>> limit_offset) {
	vector<unique_ptr<ResultModifier>> result;
	if (order_by_clause) {
		auto order_modifier = make_uniq<OrderModifier>();
		order_modifier->orders = std::move(*order_by_clause);
		result.push_back(std::move(order_modifier));
	}
	if (limit_offset) {
		result.push_back(std::move(*limit_offset));
	}
	return result;
}

unique_ptr<ResultModifier>
PEGTransformerFactory::TransformLimitOffsetClause(PEGTransformer &transformer, LimitPercentResult limit_clause,
                                                  optional<LimitPercentResult> offset_clause) {
	LimitPercentResult empty_offset;
	return VerifyLimitOffset(limit_clause, offset_clause ? *offset_clause : empty_offset);
}

unique_ptr<ResultModifier>
PEGTransformerFactory::TransformOffsetLimitClause(PEGTransformer &transformer, LimitPercentResult offset_clause,
                                                  optional<LimitPercentResult> limit_clause) {
	LimitPercentResult empty_limit;
	return VerifyLimitOffset(limit_clause ? *limit_clause : empty_limit, offset_clause);
}

unique_ptr<ResultModifier> PEGTransformerFactory::TransformOffsetFetchClause(PEGTransformer &transformer,
                                                                             LimitPercentResult offset_clause,
                                                                             LimitPercentResult fetch_clause) {
	return VerifyLimitOffset(fetch_clause, offset_clause);
}

unique_ptr<ResultModifier> PEGTransformerFactory::TransformFetchOnlyClause(PEGTransformer &transformer,
                                                                           LimitPercentResult fetch_clause) {
	LimitPercentResult empty_offset;
	return VerifyLimitOffset(fetch_clause, empty_offset);
}

unique_ptr<SelectStatement> PEGTransformerFactory::TransformTableStatement(PEGTransformer &transformer,
                                                                           unique_ptr<BaseTableRef> base_table_name) {
	auto result = make_uniq<SelectStatement>();
	auto node = make_uniq<SelectNode>();
	node->select_list.push_back(make_uniq<StarExpression>());
	node->from_table = std::move(base_table_name);
	result->node = std::move(node);
	return result;
}

unique_ptr<SelectStatement>
PEGTransformerFactory::TransformSimpleSelectParens(PEGTransformer &transformer,
                                                   unique_ptr<SelectStatement> simple_select) {
	return simple_select;
}

unique_ptr<SelectNode> PEGTransformerFactory::TransformSelectFromClause(PEGTransformer &transformer,
                                                                        unique_ptr<SelectNode> select_clause,
                                                                        optional<unique_ptr<TableRef>> from_clause) {
	if (from_clause) {
		select_clause->from_table = std::move(*from_clause);
	} else {
		select_clause->from_table = make_uniq<EmptyTableRef>();
	}
	return select_clause;
}

unique_ptr<SelectNode>
PEGTransformerFactory::TransformFromSelectClause(PEGTransformer &transformer, unique_ptr<TableRef> from_clause,
                                                 optional<unique_ptr<SelectNode>> select_clause) {
	unique_ptr<SelectNode> result;
	if (!select_clause) {
		result = make_uniq<SelectNode>();
		result->select_list.push_back(make_uniq<StarExpression>());
	} else {
		result = std::move(*select_clause);
	}
	result->from_table = std::move(from_clause);
	return result;
}

vector<unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformUsingKey(PEGTransformer &transformer,
                                         vector<unique_ptr<ParsedExpression>> target_list) {
	return target_list;
}

unique_ptr<SelectNode>
PEGTransformerFactory::TransformSelectClause(PEGTransformer &transformer, optional<DistinctClause> distinct_clause,
                                             optional<vector<unique_ptr<ParsedExpression>>> target_list) {
	auto result = make_uniq<SelectNode>();
	if (distinct_clause && distinct_clause->is_distinct) {
		auto distinct_modifier = make_uniq<DistinctModifier>();
		for (auto &distinct_on : distinct_clause->distinct_targets) {
			distinct_modifier->distinct_on_targets.push_back(distinct_on->Copy());
		}
		result->modifiers.push_back(std::move(distinct_modifier));
	}
	if (!target_list) {
		throw ParserException("SELECT clause without selection list");
	}
	for (auto &expr_ptr : *target_list) {
		result->select_list.push_back(std::move(expr_ptr));
	}
	return result;
}

vector<unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformTargetList(PEGTransformer &transformer,
                                           vector<unique_ptr<ParsedExpression>> aliased_expression) {
	return aliased_expression;
}

vector<string> PEGTransformerFactory::TransformColumnAliases(PEGTransformer &transformer,
                                                             const vector<Identifier> &col_id_or_string) {
	return IdentifiersToStrings(col_id_or_string);
}

DistinctClause PEGTransformerFactory::TransformDistinctAll(PEGTransformer &transformer) {
	DistinctClause result;
	result.is_distinct = false;
	return result;
}

DistinctClause
PEGTransformerFactory::TransformDistinctOn(PEGTransformer &transformer,
                                           optional<vector<unique_ptr<ParsedExpression>>> distinct_on_targets) {
	DistinctClause result;
	result.is_distinct = true;
	if (distinct_on_targets) {
		result.distinct_targets = std::move(*distinct_on_targets);
	}
	return result;
}

vector<unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformDistinctOnTargets(PEGTransformer &transformer,
                                                  vector<unique_ptr<ParsedExpression>> expression) {
	return expression;
}

unique_ptr<TableRef> PEGTransformerFactory::TransformTableSubquery(PEGTransformer &transformer,
                                                                   const optional<bool> &lateral,
                                                                   unique_ptr<TableRef> subquery_reference,
                                                                   const optional<TableAlias> &table_alias) {
	if (table_alias) {
		subquery_reference->alias = duckpgq_compat::HostName(table_alias->name);
		subquery_reference->column_name_alias = duckpgq_compat::HostNames(table_alias->column_name_alias);
	}
	return subquery_reference;
}

unique_ptr<TableRef> PEGTransformerFactory::TransformBaseTableRef(PEGTransformer &transformer,
                                                                  const optional<Identifier> &table_alias_colon,
                                                                  unique_ptr<BaseTableRef> base_table_name,
                                                                  const optional<TableAlias> &table_alias,
                                                                  optional<unique_ptr<AtClause>> at_clause,
                                                                  optional<unique_ptr<SampleOptions>> sample_clause) {
	if (table_alias_colon) {
		base_table_name->alias = duckpgq_compat::HostName(*table_alias_colon);
	}
	if (table_alias && table_alias_colon) {
		throw ParserException("Table reference %s cannot have two aliases", base_table_name->ToString());
	}
	if (table_alias) {
		base_table_name->alias = duckpgq_compat::HostName(table_alias->name);
		base_table_name->column_name_alias = duckpgq_compat::HostNames(table_alias->column_name_alias);
	}
	if (at_clause) {
		base_table_name->at_clause = std::move(*at_clause);
	}
	if (sample_clause) {
		base_table_name->sample = std::move(*sample_clause);
	}
	return std::move(base_table_name);
}

Identifier PEGTransformerFactory::TransformTableAliasColon(PEGTransformer &transformer,
                                                           const Identifier &col_id_or_string) {
	return col_id_or_string;
}

unique_ptr<TableRef> PEGTransformerFactory::TransformValuesRef(PEGTransformer &transformer,
                                                               unique_ptr<SelectStatement> values_clause,
                                                               const optional<TableAlias> &table_alias) {
	auto subquery_ref = make_uniq<SubqueryRef>(std::move(values_clause));
	if (table_alias) {
		subquery_ref->alias = duckpgq_compat::HostName(table_alias->name);
		subquery_ref->column_name_alias = duckpgq_compat::HostNames(table_alias->column_name_alias);
	}
	return std::move(subquery_ref);
}

unique_ptr<TableRef> PEGTransformerFactory::TransformParensTableRef(PEGTransformer &transformer,
                                                                    const optional<Identifier> &table_alias_colon,
                                                                    unique_ptr<TableRef> table_ref,
                                                                    const optional<TableAlias> &table_alias,
                                                                    optional<unique_ptr<SampleOptions>> sample_clause) {
	if (table_alias_colon && table_alias) {
		throw ParserException("Table reference %s cannot have two aliases", table_ref->ToString());
	}
	if (!table_alias_colon && !table_alias && !sample_clause) {
		return table_ref;
	}
	auto select_statement = make_uniq<SelectStatement>();
	auto select_node = make_uniq<SelectNode>();
	select_node->select_list.push_back(make_uniq<StarExpression>());
	select_node->from_table = std::move(table_ref);
	select_statement->node = std::move(select_node);
	auto subquery = make_uniq<SubqueryRef>(std::move(select_statement));
	if (table_alias_colon) {
		subquery->alias = duckpgq_compat::HostName(*table_alias_colon);
	}
	if (table_alias) {
		subquery->alias = duckpgq_compat::HostName(table_alias->name);
		subquery->column_name_alias = duckpgq_compat::HostNames(table_alias->column_name_alias);
	}
	if (sample_clause) {
		subquery->sample = std::move(*sample_clause);
	}
	return std::move(subquery);
}


unique_ptr<BaseTableRef> PEGTransformerFactory::TransformSchemaReservedTable(PEGTransformer &transformer,
                                                                             const Identifier &schema_qualification,
                                                                             const Identifier &reserved_table_name) {
	return duckpgq_compat::TableFromNames({schema_qualification, reserved_table_name});
}

unique_ptr<BaseTableRef> PEGTransformerFactory::TransformCatalogReservedSchemaTable(
    PEGTransformer &transformer, const Identifier &catalog_qualification,
    const Identifier &reserved_schema_qualification, const Identifier &reserved_table_name) {
	return duckpgq_compat::TableFromNames({catalog_qualification, reserved_schema_qualification, reserved_table_name});
}

QualifiedName PEGTransformerFactory::TransformQualifiedTableFunction(PEGTransformer &transformer,
                                                                     const optional<Identifier> &catalog_qualification,
                                                                     const optional<Identifier> &schema_qualification,
                                                                     const Identifier &table_function_name) {
	Identifier catalog = catalog_qualification ? *catalog_qualification : Identifier();
	Identifier schema = schema_qualification ? *schema_qualification : Identifier();
	if (!catalog.empty() && schema.empty()) {
		schema = std::move(catalog);
		catalog = Identifier();
	}
	vector<Identifier> path;
	if (!catalog.empty()) path.push_back(catalog);
	if (!schema.empty()) path.push_back(schema);
	return duckpgq_compat::MakeQualifiedName(std::move(path), table_function_name);
}

vector<FunctionArgument>
PEGTransformerFactory::TransformTableFunctionArguments(PEGTransformer &transformer,
                                                       optional<vector<FunctionArgument>> function_argument) {
	if (function_argument) {
		return std::move(*function_argument);
	}
	return {};
}

TableAlias PEGTransformerFactory::TransformTableAliasAs(PEGTransformer &transformer,
                                                        const QualifiedName &identifier_or_string_literal,
                                                        const optional<vector<string>> &column_aliases) {
	TableAlias result;
	#if __has_include("duckdb/common/identifier.hpp")
	result.name = identifier_or_string_literal.Name();
#else
	result.name = Identifier(identifier_or_string_literal.name);
#endif
	if (column_aliases) {
		result.column_name_alias = StringsToIdentifiers(*column_aliases);
	}
	return result;
}

TableAlias PEGTransformerFactory::TransformTableAliasWithoutAs(PEGTransformer &transformer,
                                                               const Identifier &identifier,
                                                               const optional<vector<string>> &column_aliases) {
	TableAlias result;
	result.name = identifier;
	if (column_aliases) {
		result.column_name_alias = StringsToIdentifiers(*column_aliases);
	}
	return result;
}

unique_ptr<AtClause> PEGTransformerFactory::TransformAtClause(PEGTransformer &transformer,
                                                              unique_ptr<AtClause> at_specifier) {
	return at_specifier;
}

unique_ptr<AtClause> PEGTransformerFactory::TransformAtSpecifier(PEGTransformer &transformer, const string &at_unit,
                                                                 unique_ptr<ParsedExpression> expression) {
	return make_uniq<AtClause>(at_unit, std::move(expression));
}


unique_ptr<ParsedExpression> PEGTransformerFactory::TransformWhereClause(PEGTransformer &transformer,
                                                                         unique_ptr<ParsedExpression> expression) {
	return expression;
}

GroupByNode PEGTransformerFactory::TransformGroupByClause(PEGTransformer &transformer,
                                                          GroupByNode group_by_expressions) {
	return group_by_expressions;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformHavingClause(PEGTransformer &transformer,
                                                                          unique_ptr<ParsedExpression> expression) {
	return expression;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformQualifyClause(PEGTransformer &transformer,
                                                                           unique_ptr<ParsedExpression> expression) {
	return expression;
}

unique_ptr<SampleOptions> PEGTransformerFactory::TransformSampleClause(PEGTransformer &transformer,
                                                                       unique_ptr<SampleOptions> sample_entry) {
	return sample_entry;
}

vector<unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformWindowClause(PEGTransformer &transformer,
                                             vector<unique_ptr<ParsedExpression>> window_definition) {
	return window_definition;
}

SampleMethod PEGTransformerFactory::TransformSampleFunction(PEGTransformer &transformer, const Identifier &col_id) {
	return EnumUtil::FromString<SampleMethod>(col_id.GetIdentifierName());
}

pair<SampleMethod, optional_idx>
PEGTransformerFactory::TransformSampleProperties(PEGTransformer &transformer, const Identifier &col_id,
                                                 const optional<optional_idx> &sample_seed) {
	return make_pair(EnumUtil::FromString<SampleMethod>(col_id.GetIdentifierName()),
	                 sample_seed ? *sample_seed : optional_idx());
}

optional_idx PEGTransformerFactory::TransformRepeatableSample(PEGTransformer &transformer,
                                                              const optional_idx &sample_seed) {
	return sample_seed;
}

optional_idx PEGTransformerFactory::TransformSampleSeed(PEGTransformer &transformer,
                                                        unique_ptr<ParsedExpression> number_literal) {
	auto const_expr = number_literal->Cast<ConstantExpression>();
	return optional_idx(duckpgq_compat::ConstantValue(const_expr).GetValue<idx_t>());
}

unique_ptr<SampleOptions> PEGTransformerFactory::TransformSampleCount(PEGTransformer &transformer,
                                                                      unique_ptr<ParsedExpression> sample_value,
                                                                      const optional<bool> &sample_unit) {
	auto result = make_uniq<SampleOptions>();
	if (sample_value->GetExpressionClass() != ExpressionClass::CONSTANT) {
		throw ParserException(sample_value->GetQueryLocation(),
		                      "Only constants are supported in sample clause currently");
	}
	auto &const_expr = sample_value->Cast<ConstantExpression>();
	auto &sample_value_const = duckpgq_compat::ConstantValue(const_expr);
	result->is_percentage = sample_unit.value_or(false);
	if (result->is_percentage) {
		auto percentage = sample_value_const.GetValue<double>();
		if (percentage < 0 || percentage > 100) {
			throw ParserException("Sample sample_size %llf out of range, must be between 0 and 100", percentage);
		}
		result->sample_size = Value::DOUBLE(percentage);
		result->method = SampleMethod::SYSTEM_SAMPLE;
	} else {
		auto rows = sample_value_const.GetValue<int64_t>();
		if (rows < 0 || sample_value_const.GetValue<uint64_t>() > SampleOptions::MAX_SAMPLE_ROWS) {
			throw ParserException("Sample rows %lld out of range, must be between 0 and %lld", rows,
			                      SampleOptions::MAX_SAMPLE_ROWS);
		}
		result->sample_size = Value::BIGINT(rows);
		result->method = SampleMethod::RESERVOIR_SAMPLE;
	}
	return result;
}

GroupByNode PEGTransformerFactory::TransformGroupByAll(PEGTransformer &transformer) {
	GroupByNode result;
	result.group_expressions.push_back(make_uniq<StarExpression>());
	return result;
}

unique_ptr<TableRef>
PEGTransformerFactory::TransformSubqueryReference(PEGTransformer &transformer,
                                                  unique_ptr<SelectStatement> select_statement_internal) {
	return make_uniq<SubqueryRef>(std::move(select_statement_internal));
}

OrderByNode PEGTransformerFactory::TransformOrderByExpression(PEGTransformer &transformer,
                                                              unique_ptr<ParsedExpression> expression,
                                                              const optional<OrderType> &desc_or_asc,
                                                              const optional<OrderByNullType> &nulls_first_or_last) {
	return OrderByNode(desc_or_asc.value_or(OrderType::ORDER_DEFAULT),
	                   nulls_first_or_last.value_or(OrderByNullType::ORDER_DEFAULT), std::move(expression));
}

vector<OrderByNode> PEGTransformerFactory::TransformOrderByClause(PEGTransformer &transformer,
                                                                  vector<OrderByNode> order_by_expressions) {
	return order_by_expressions;
}

vector<OrderByNode> PEGTransformerFactory::TransformOrderByExpressionList(PEGTransformer &transformer,
                                                                          vector<OrderByNode> order_by_expression) {
	return order_by_expression;
}

vector<OrderByNode> PEGTransformerFactory::TransformOrderByAll(PEGTransformer &transformer,
                                                               const optional<OrderType> &desc_or_asc,
                                                               const optional<OrderByNullType> &nulls_first_or_last) {
	vector<OrderByNode> result;
	auto star_expr = make_uniq<StarExpression>();
	#if __has_include("duckdb/common/identifier.hpp")
	star_expr->IsColumnsMutable() = true;
#else
	star_expr->columns = true;
#endif
	result.push_back(OrderByNode(desc_or_asc.value_or(OrderType::ORDER_DEFAULT),
	                             nulls_first_or_last.value_or(OrderByNullType::ORDER_DEFAULT), std::move(star_expr)));
	return result;
}

LimitPercentResult PEGTransformerFactory::TransformLimitClause(PEGTransformer &transformer,
                                                               LimitPercentResult limit_value) {
	return limit_value;
}

LimitPercentResult PEGTransformerFactory::TransformOffsetClause(PEGTransformer &transformer,
                                                                LimitPercentResult offset_value) {
	return offset_value;
}

LimitPercentResult PEGTransformerFactory::TransformOffsetValue(PEGTransformer &transformer,
                                                               unique_ptr<ParsedExpression> expression,
                                                               const bool &has_result) {
	LimitPercentResult result;
	result.expression = std::move(expression);
	return result;
}

LimitPercentResult PEGTransformerFactory::TransformLimitAll(PEGTransformer &transformer) {
	LimitPercentResult result;
	result.expression = make_uniq<ConstantExpression>(Value());
	result.is_percent = false;
	return result;
}

LimitPercentResult PEGTransformerFactory::TransformLimitLiteralPercent(PEGTransformer &transformer,
                                                                       unique_ptr<ParsedExpression> number_literal) {
	LimitPercentResult result;
	result.expression = std::move(number_literal);
	result.is_percent = true;
	return result;
}

LimitPercentResult PEGTransformerFactory::TransformFetchValue(PEGTransformer &transformer,
                                                              unique_ptr<ParsedExpression> expression) {
	LimitPercentResult result;
	result.expression = std::move(expression);
	return result;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformColIdExpression(PEGTransformer &transformer,
                                                                             const Identifier &col_id,
                                                                             unique_ptr<ParsedExpression> expression) {
	expression->SetAlias(duckpgq_compat::HostName(col_id));
	return expression;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformExpressionAsCollabel(
    PEGTransformer &transformer, unique_ptr<ParsedExpression> expression, const Identifier &col_label_or_string) {
	expression->SetAlias(duckpgq_compat::HostName(col_label_or_string));
	return expression;
}

unique_ptr<ParsedExpression> PEGTransformerFactory::TransformExpressionOptIdentifier(
    PEGTransformer &transformer, unique_ptr<ParsedExpression> expression, const optional<Identifier> &identifier) {
	if (identifier) {
		expression->SetAlias(duckpgq_compat::HostName(*identifier));
	}
	return expression;
}

unique_ptr<SelectStatement>
PEGTransformerFactory::TransformValuesClause(PEGTransformer &transformer,
                                             vector<vector<unique_ptr<ParsedExpression>>> values_expressions) {
	if (values_expressions.size() > 1) {
		const auto expected_size = values_expressions[0].size();
		for (idx_t i = 1; i < values_expressions.size(); i++) {
			if (values_expressions[i].size() != expected_size) {
				throw ParserException(
				    *values_expressions[i][0],
				    "VALUES lists must all be the same length, expected %d %s but found a list with %d %s",
				    expected_size, expected_size == 1 ? "entry" : "entries", values_expressions[i].size(),
				    values_expressions[i].size() == 1 ? "entry" : "entries");
			}
		}
	}
	auto result = make_uniq<ExpressionListRef>();
	result->alias = "valueslist";
	result->values = std::move(values_expressions);

	auto select_node = make_uniq<SelectNode>();
	select_node->from_table = std::move(result);
	select_node->select_list.push_back(make_uniq<StarExpression>());
	auto select_statement = make_uniq<SelectStatement>();
	select_statement->node = std::move(select_node);
	return select_statement;
}

vector<unique_ptr<ParsedExpression>>
PEGTransformerFactory::TransformValuesExpressions(PEGTransformer &transformer,
                                                  vector<unique_ptr<ParsedExpression>> expression) {
	return expression;
}

} // namespace duckpgq_peg
} // namespace duckdb
