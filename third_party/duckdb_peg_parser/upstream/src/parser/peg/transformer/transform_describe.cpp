#include "duckdb/parser/tableref/showref.hpp"
#include "duckpgq/compat/alter_access.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"

namespace duckdb {
namespace duckpgq_peg {

static string DescribeCatalog(const QualifiedName &n) {
#if __has_include("duckdb/common/identifier.hpp")
	return n.Catalog().GetIdentifierName();
#else
	return n.catalog;
#endif
}
static string DescribeSchema(const QualifiedName &n) {
#if __has_include("duckdb/common/identifier.hpp")
	return n.Schema().GetIdentifierName();
#else
	return n.schema;
#endif
}
static string DescribeName(const QualifiedName &n) {
#if __has_include("duckdb/common/identifier.hpp")
	return n.Name().GetIdentifierName();
#else
	return n.name;
#endif
}
static string DescribeSchema(const BaseTableRef &n) {
#if __has_include("duckdb/common/identifier.hpp")
	return n.GetQualifiedName().Schema().GetIdentifierName();
#else
	return n.schema_name;
#endif
}
static string DescribeName(const BaseTableRef &n) {
#if __has_include("duckdb/common/identifier.hpp")
	return n.Table().GetIdentifierName();
#else
	return n.table_name;
#endif
}

template<class T> static void DescribeSetTable(ShowRef &ref, const T &value) {
#if __has_include("duckdb/common/identifier.hpp")
	ref.SetTableName(Identifier(value));
#else
	ref.table_name = Identifier(value).GetIdentifierName();
#endif
}

template<class T> static void DescribeSetSchema(ShowRef &ref, const T &value) {
#if __has_include("duckdb/common/identifier.hpp")
	ref.SetSchemaName(Identifier(value));
#else
	ref.schema_name = Identifier(value).GetIdentifierName();
#endif
}

template<class T> static void DescribeSetCatalog(ShowRef &ref, const T &value) {
#if __has_include("duckdb/common/identifier.hpp")
	ref.SetCatalogName(Identifier(value));
#else
	ref.catalog_name = Identifier(value).GetIdentifierName();
#endif
}

static bool DescribeNameEmpty(const ShowRef &ref) {
#if __has_include("duckdb/common/identifier.hpp")
	return ref.GetTableName().empty();
#else
	return ref.table_name.empty();
#endif
}
static void DescribeSetBase(BaseTableRef &ref, const Identifier &value) {
#if __has_include("duckdb/common/identifier.hpp")
	ref.SetTable(value);
#else
	ref.table_name = value.GetIdentifierName();
#endif
}


unique_ptr<SelectStatement> PEGTransformerFactory::TransformDescribeStatement(PEGTransformer &transformer,
                                                                              unique_ptr<QueryNode> child) {
	auto select_statement = make_uniq<SelectStatement>();
	select_statement->node = std::move(child);
	return select_statement;
}

unique_ptr<QueryNode>
PEGTransformerFactory::TransformShowSelect(PEGTransformer &transformer, const ShowType &show_or_describe_or_summarize,
                                           unique_ptr<SelectStatement> select_statement_internal) {
	auto result = make_uniq<ShowRef>();
	result->show_type = show_or_describe_or_summarize;
	result->query = std::move(select_statement_internal->node);
	auto select_node = make_uniq<SelectNode>();
	select_node->select_list.push_back(make_uniq<StarExpression>());
	select_node->from_table = std::move(result);
	return std::move(select_node);
}

unique_ptr<QueryNode> PEGTransformerFactory::TransformShowTables(PEGTransformer &transformer,
                                                                 const ShowType &show_or_describe,
                                                                 const QualifiedName &qualified_name) {
	auto showref = make_uniq<ShowRef>();
	showref->show_type = ShowType::SHOW_FROM;
	if (!IsInvalidCatalog(DescribeCatalog(qualified_name))) {
		throw ParserException("Expected \"SHOW TABLES FROM database\", \"SHOW TABLES FROM schema\", or "
		                      "\"SHOW TABLES FROM database.schema\"");
	}
	if (IsInvalidSchema(DescribeSchema(qualified_name))) {
		DescribeSetSchema(*showref, DescribeName(qualified_name));
	} else {
		DescribeSetCatalog(*showref, DescribeSchema(qualified_name));
		DescribeSetSchema(*showref, DescribeName(qualified_name));
	}
	auto select_node = make_uniq<SelectNode>();
	select_node->select_list.push_back(make_uniq<StarExpression>());
	select_node->from_table = std::move(showref);
	return std::move(select_node);
}

unique_ptr<QueryNode> PEGTransformerFactory::TransformShowAllTables(PEGTransformer &transformer,
                                                                    const ShowType &show_or_describe) {
	auto result = make_uniq<ShowRef>();
	// Legacy reasons, see bind_showref.cpp
	DescribeSetTable(*result, "__show_tables_expanded");
	result->show_type = ShowType::SHOW_UNQUALIFIED;
	auto select_node = make_uniq<SelectNode>();
	select_node->select_list.push_back(make_uniq<StarExpression>());
	select_node->from_table = std::move(result);
	return std::move(select_node);
}

unique_ptr<QueryNode> PEGTransformerFactory::TransformDescribePropertyGraph(PEGTransformer &transformer,
                                                                           const ShowType &describe_rule,
                                                                           const QualifiedName &qualified_name) {
	auto showref = make_uniq<ShowRef>();
	showref->show_type = ShowType::DESCRIBE;
	DescribeSetTable(*showref, DescribeName(qualified_name));

	auto select_node = make_uniq<SelectNode>();
	select_node->select_list.push_back(make_uniq<StarExpression>());
	select_node->from_table = std::move(showref);
	return std::move(select_node);
}

unique_ptr<QueryNode> PEGTransformerFactory::TransformShowQualifiedName(PEGTransformer &transformer,
                                                                        const ShowType &show_or_describe_or_summarize,
                                                                        optional<DescribeTarget> describe_target) {
	auto showref = make_uniq<ShowRef>();
	showref->show_type = show_or_describe_or_summarize;
	DescribeTarget target;
	if (describe_target) {
		target = std::move(*describe_target);
	}

	if (target.is_table_name || target.table_ref) {
		if (target.is_table_name) {
			// Case: SHOW 'something' or DESCRIBE 'something'
			DescribeSetTable(*showref, target.table_name);
		} else {
			// Case: A relation/table reference
			auto &base_table = *target.table_ref;

			if (showref->show_type == ShowType::SHOW_FROM) {
				// Logic for SHOW TABLES FROM [database].[schema]
				if (IsInvalidSchema(DescribeSchema(base_table))) {
					DescribeSetSchema(*showref, DescribeName(base_table));
				} else {
					DescribeSetCatalog(*showref, DescribeSchema(base_table));
					DescribeSetSchema(*showref, DescribeName(base_table));
				}
			} else if (IsInvalidSchema(DescribeSchema(base_table))) {
				// Logic for unqualified relations (databases, tables, variables)
				auto table_name = StringUtil::Lower(DescribeName(base_table));
				if (table_name == "databases" || table_name == "tables" || table_name == "schemas" ||
				    table_name == "variables") {
					DescribeSetTable(*showref, Identifier("\"" + table_name + "\""));
					showref->show_type = ShowType::SHOW_UNQUALIFIED;
				}
			}
		}
		if (DescribeNameEmpty(*showref) && showref->show_type != ShowType::SHOW_FROM) {
			auto show_select_node = make_uniq<SelectNode>();
			show_select_node->select_list.push_back(make_uniq<StarExpression>());
			if (target.is_table_name) {
				// Case: SHOW 'something' or DESCRIBE 'something'
				auto table_ref = make_uniq<BaseTableRef>();
				DescribeSetBase(*table_ref, target.table_name);
				show_select_node->from_table = std::move(table_ref);
			} else {
				// Case: A relation/table reference
				show_select_node->from_table = std::move(target.table_ref);
			}
			showref->query = std::move(show_select_node);
		}
	} else {
		// Case: No relation specified (e.g., just "SHOW TABLES")
		if (showref->show_type == ShowType::SUMMARY) {
			throw ParserException("Expected table name with SUMMARIZE");
		}
		DescribeSetTable(*showref, "__show_tables_expanded");
		showref->show_type = ShowType::SHOW_UNQUALIFIED;
	}

	auto select_node = make_uniq<SelectNode>();
	select_node->select_list.push_back(make_uniq<StarExpression>());
	select_node->from_table = std::move(showref);

	return std::move(select_node);
}

DescribeTarget PEGTransformerFactory::TransformDescribeBaseTableName(PEGTransformer &transformer,
                                                                     unique_ptr<BaseTableRef> base_table_name) {
	DescribeTarget result;
	result.table_ref = std::move(base_table_name);
	return result;
}

DescribeTarget PEGTransformerFactory::TransformDescribeStringLiteral(PEGTransformer &transformer,
                                                                     const string &string_literal) {
	DescribeTarget result;
	result.is_table_name = true;
	result.table_name = Identifier(string_literal);
	return result;
}

ShowType PEGTransformerFactory::TransformSummarizeRule(PEGTransformer &transformer) {
	return ShowType::SUMMARY;
}

ShowType PEGTransformerFactory::TransformShowRule(PEGTransformer &transformer) {
	return ShowType::DESCRIBE;
}

ShowType PEGTransformerFactory::TransformDescribeLongRule(PEGTransformer &transformer) {
	return ShowType::DESCRIBE;
}

ShowType PEGTransformerFactory::TransformDescRule(PEGTransformer &transformer) {
	return ShowType::DESCRIBE;
}

} // namespace duckpgq_peg
} // namespace duckdb
