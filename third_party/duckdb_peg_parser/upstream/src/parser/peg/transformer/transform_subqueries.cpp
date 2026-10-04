#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/expression/subquery_expression.hpp"
#include "duckdb/parser/expression/operator_expression.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
namespace duckdb {
namespace duckpgq_peg {
#if __has_include("duckdb/common/identifier.hpp")
unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformSubqueryExpression(PEGTransformer &transformer, const optional<bool> &subquery_not,
                                                   const optional<bool> &subquery_exists,
                                                   unique_ptr<TableRef> subquery_reference) {
	bool is_not = subquery_not ? *subquery_not : false;
	bool is_exists = subquery_exists ? *subquery_exists : false;
	auto result = make_uniq<SubqueryExpression>();
	if (is_exists) {
		result->GetSubqueryTypeMutable() = SubqueryType::EXISTS;
	} else {
		result->GetSubqueryTypeMutable() = SubqueryType::SCALAR;
	}
	if (subquery_reference->type == TableReferenceType::SUBQUERY) {
		auto &subquery_ref = subquery_reference->Cast<SubqueryRef>();
		result->SubqueryMutable() = std::move(subquery_ref.subquery);
	} else {
		auto select_statement = make_uniq<SelectStatement>();
		auto select_node = make_uniq<SelectNode>();
		select_node->select_list.push_back(make_uniq<StarExpression>());
		select_node->from_table = std::move(subquery_reference);
		select_statement->node = std::move(select_node);
		result->SubqueryMutable() = std::move(select_statement);
	}
	if (is_not) {
		vector<unique_ptr<ParsedExpression>> children;
		children.push_back(std::move(result));
		auto not_operator = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(children));
		return std::move(not_operator);
	}
	return std::move(result);
}

bool PEGTransformerFactory::TransformSubqueryNot(PEGTransformer &transformer) {
	return true;
}

bool PEGTransformerFactory::TransformSubqueryExists(PEGTransformer &transformer) {
	return true;
}
#else
unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformSubqueryExpression(PEGTransformer &transformer, const optional<bool> &subquery_not,
                                                   const optional<bool> &subquery_exists,
                                                   unique_ptr<TableRef> subquery_reference) {
	bool is_not = subquery_not ? *subquery_not : false;
	bool is_exists = subquery_exists ? *subquery_exists : false;
	auto result = make_uniq<SubqueryExpression>();
	if (is_exists) {
		result->subquery_type = SubqueryType::EXISTS;
	} else {
		result->subquery_type = SubqueryType::SCALAR;
	}
	if (subquery_reference->type == TableReferenceType::SUBQUERY) {
		auto &subquery_ref = subquery_reference->Cast<SubqueryRef>();
		result->subquery = std::move(subquery_ref.subquery);
	} else {
		auto select_statement = make_uniq<SelectStatement>();
		auto select_node = make_uniq<SelectNode>();
		select_node->select_list.push_back(make_uniq<StarExpression>());
		select_node->from_table = std::move(subquery_reference);
		select_statement->node = std::move(select_node);
		result->subquery = std::move(select_statement);
	}
	if (is_not) {
		vector<unique_ptr<ParsedExpression>> children;
		children.push_back(std::move(result));
		auto not_operator = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(children));
		return std::move(not_operator);
	}
	return std::move(result);
}

bool PEGTransformerFactory::TransformSubqueryNot(PEGTransformer &transformer) {
	return true;
}

bool PEGTransformerFactory::TransformSubqueryExists(PEGTransformer &transformer) {
	return true;
}
#endif
} // namespace duckpgq_peg
} // namespace duckdb
