#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/parsed_data/create_view_info.hpp"
#include "duckdb/parser/query_node/recursive_cte_node.hpp"
#include "duckdb/parser/query_node/set_operation_node.hpp"
#include "duckpgq/compat/alter_access.hpp"

namespace duckdb {
namespace duckpgq_peg {

void PEGTransformerFactory::WrapRecursiveView(unique_ptr<CreateViewInfo> &info, unique_ptr<QueryNode> inner_node) {
	auto outer_select = make_uniq<SelectNode>();

	auto cte_info = make_uniq<CommonTableExpressionInfo>();
	cte_info->aliases = info->aliases;

#if __has_include("duckdb/common/identifier.hpp")
	cte_info->query_node = std::move(inner_node);
	outer_select->cte_map.map.insert(info->GetViewName(), std::move(cte_info));
#else
	cte_info->query = make_uniq<SelectStatement>();
	cte_info->query->node = std::move(inner_node);
	outer_select->cte_map.map.insert(info->view_name, std::move(cte_info));
#endif

	for (const auto &column : info->aliases) {
		outer_select->select_list.push_back(make_uniq<ColumnRefExpression>(column));
	}

#if __has_include("duckdb/common/identifier.hpp")
	auto table_description = TableDescription(
	    QualifiedName(info->GetQualifiedName().Catalog(), info->GetQualifiedName().Schema(), info->GetViewName()));
	outer_select->from_table = make_uniq<BaseTableRef>(table_description);
#else
	// Refer to the local CTE, not to a catalog-qualified stored view.
	auto table = make_uniq<BaseTableRef>();
	table->table_name = info->view_name;
	outer_select->from_table = std::move(table);
#endif

	auto outer_select_statement = make_uniq<SelectStatement>();
	outer_select_statement->node = std::move(outer_select);
	info->query = std::move(outer_select_statement);
}

void PEGTransformerFactory::ConvertToRecursiveView(unique_ptr<CreateViewInfo> &info, unique_ptr<QueryNode> &node) {
	vector<unique_ptr<ParsedExpression>> empty_key_targets;
#if __has_include("duckdb/common/identifier.hpp")
	auto result_node = ToRecursiveCTE(std::move(node), info->GetViewName(), info->aliases, empty_key_targets);
#else
	auto result_node = ToRecursiveCTE(std::move(node), Identifier(info->view_name), StringsToIdentifiers(info->aliases), empty_key_targets);
#endif
	WrapRecursiveView(info, std::move(result_node));
}

unique_ptr<CreateStatement>
PEGTransformerFactory::TransformCreateViewStmt(PEGTransformer &transformer, const optional<bool> &create_recursive,
                                               const optional<bool> &if_not_exists, const QualifiedName &qualified_name,
                                               const optional<vector<string>> &insert_column_list,
                                               optional<case_insensitive_map_t<unique_ptr<ParsedExpression>>> with_list,
                                               unique_ptr<SelectStatement> select_statement_internal) {
	auto result = make_uniq<CreateStatement>();
	auto info = make_uniq<CreateViewInfo>();
	info->on_conflict = if_not_exists ? OnCreateConflict::IGNORE_ON_CONFLICT : OnCreateConflict::ERROR_ON_CONFLICT;
#if __has_include("duckdb/common/identifier.hpp")
	info->SetQualifiedName(qualified_name);
#else
	info->catalog = qualified_name.catalog;
	info->schema = qualified_name.schema;
	info->view_name = qualified_name.name;
#endif
	if (insert_column_list) {
		info->aliases = duckpgq_compat::HostNames(StringsToIdentifiers(*insert_column_list));
	}
	if (with_list) {
#if !__has_include("duckdb/common/identifier.hpp")
		if (!with_list->empty()) {
			throw NotImplementedException("VIEW options are not supported by canonical DuckDB 1.5.5");
		}
#else
		for (auto &option_entry : *with_list) {
			if (!StringUtil::CIEquals(option_entry.first, "defer_binding")) {
				throw ParserException("Only DEFER_BINDING is currently supported as option for CREATE VIEW");
			}
			if (option_entry.second->GetExpressionClass() != ExpressionClass::CONSTANT) {
				throw InvalidInputException("Defer binding option must be a constant value");
			}
			auto &val = option_entry.second->Cast<ConstantExpression>().GetValue();
			if (val.IsNull()) {
				info->binding_mode = CreateViewBindingMode::SKIP_BINDING;
			} else if (val.type().id() != LogicalTypeId::BOOLEAN) {
				throw InvalidInputException("Defer binding option must be a boolean");
			} else if (BooleanValue::Get(val)) {
				info->binding_mode = CreateViewBindingMode::SKIP_BINDING;
			}
		}
#endif
	}
	if (create_recursive) {
		ConvertToRecursiveView(info, select_statement_internal->node);
	} else {
		info->query = std::move(select_statement_internal);
	}
	transformer.PivotEntryCheck("view");
	result->info = std::move(info);
	return result;
}

bool PEGTransformerFactory::TransformCreateRecursive(PEGTransformer &transformer) {
	return true;
}

} // namespace duckpgq_peg
} // namespace duckdb
