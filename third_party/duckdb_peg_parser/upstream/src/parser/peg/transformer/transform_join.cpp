#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/compat/alter_access.hpp"
#include "duckdb/parser/tableref/joinref.hpp"
#include "duckdb/common/enum_util.hpp"
namespace duckdb {
namespace duckpgq_peg {
unique_ptr<TableRef> PEGTransformerFactory::TransformRegularJoinClause(PEGTransformer &transformer,
                                                                       const optional<bool> &asof,
                                                                       const optional<JoinType> &join_type,
                                                                       unique_ptr<TableRef> table_ref,
                                                                       JoinQualifier join_qualifier) {
	auto result = make_uniq<JoinRef>();
	if (asof.value_or(false)) {
		result->ref_type = JoinRefType::ASOF;
	}
	result->type = join_type.value_or(JoinType::INNER);
	result->right = std::move(table_ref);
	if (join_qualifier.on_clause) {
		result->condition = std::move(join_qualifier.on_clause);
	} else if (!join_qualifier.using_columns.empty()) {
		result->using_columns = duckpgq_compat::HostNames(join_qualifier.using_columns);
	} else {
		throw InternalException("Invalid join qualifier found.");
	}
	return std::move(result);
}

unique_ptr<TableRef> PEGTransformerFactory::TransformJoinByClause(PEGTransformer &transformer, const string &col_label,
                                                                  unique_ptr<TableRef> table_ref,
                                                                  JoinQualifier join_qualifier) {
	auto result = make_uniq<JoinRef>();
	// resolve the join type name against the JoinType enum (case-insensitive); accept an optional `_join` suffix,
	// so e.g. `mark` and `mark_join` are equivalent. EnumUtil::FromString throws on an unknown name.
	auto type_name = col_label;
	if (StringUtil::EndsWith(StringUtil::Lower(type_name), "_join")) {
		type_name = type_name.substr(0, type_name.size() - 5);
	}
	result->type = EnumUtil::FromString<JoinType>(type_name);
	if (result->type == JoinType::INVALID) {
		throw ParserException("\"%s\" is not a valid join type for JOIN BY", col_label);
	}
	result->right = std::move(table_ref);
	if (join_qualifier.on_clause) {
		result->condition = std::move(join_qualifier.on_clause);
	} else if (!join_qualifier.using_columns.empty()) {
		result->using_columns = duckpgq_compat::HostNames(join_qualifier.using_columns);
	} else {
		throw InternalException("Invalid join qualifier found.");
	}
	return std::move(result);
}

bool PEGTransformerFactory::TransformAsof(PEGTransformer &transformer) {
	return true;
}

JoinType PEGTransformerFactory::TransformFullJoin(PEGTransformer &transformer, const bool &has_result) {
	return JoinType::OUTER;
}

JoinType PEGTransformerFactory::TransformLeftJoin(PEGTransformer &transformer, const bool &has_result) {
	return JoinType::LEFT;
}

JoinType PEGTransformerFactory::TransformRightJoin(PEGTransformer &transformer, const bool &has_result) {
	return JoinType::RIGHT;
}

JoinType PEGTransformerFactory::TransformSemiJoin(PEGTransformer &transformer) {
	return JoinType::SEMI;
}

JoinType PEGTransformerFactory::TransformAntiJoin(PEGTransformer &transformer) {
	return JoinType::ANTI;
}

JoinType PEGTransformerFactory::TransformInnerJoin(PEGTransformer &transformer) {
	return JoinType::INNER;
}

unique_ptr<TableRef> PEGTransformerFactory::TransformJoinWithoutOnClause(PEGTransformer &transformer,
                                                                         const JoinPrefix &join_prefix,
                                                                         unique_ptr<TableRef> table_ref) {
	auto result = make_uniq<JoinRef>();
	result->ref_type = join_prefix.ref_type;
	result->type = join_prefix.join_type;
	result->right = std::move(table_ref);
	return std::move(result);
}

JoinQualifier PEGTransformerFactory::TransformOnClause(PEGTransformer &transformer,
                                                       unique_ptr<ParsedExpression> expression) {
	JoinQualifier result;
	result.on_clause = std::move(expression);
	return result;
}

JoinQualifier PEGTransformerFactory::TransformUsingClause(PEGTransformer &transformer,
                                                          const vector<Identifier> &column_name) {
	JoinQualifier result;
	for (auto &col_identifier : column_name) {
		if (col_identifier.empty()) {
			throw ParserException("Column identifier cannot be empty");
		}
		result.using_columns.push_back(col_identifier);
	}
	return result;
}

JoinPrefix PEGTransformerFactory::TransformCrossJoinPrefix(PEGTransformer &transformer) {
	JoinPrefix result;
	result.ref_type = JoinRefType::CROSS;
	return result;
}

JoinPrefix PEGTransformerFactory::TransformNaturalJoinPrefix(PEGTransformer &transformer,
                                                             const optional<JoinType> &join_type) {
	JoinPrefix result;
	result.ref_type = JoinRefType::NATURAL;
	result.join_type = join_type.value_or(JoinType::INNER);
	return result;
}

JoinPrefix PEGTransformerFactory::TransformPositionalJoinPrefix(PEGTransformer &transformer) {
	JoinPrefix result;
	result.ref_type = JoinRefType::POSITIONAL;
	return result;
}

unique_ptr<TableRef> PEGTransformerFactory::TransformFromClause(PEGTransformer &transformer,
                                                                vector<unique_ptr<TableRef>> table_ref) {
	auto result_table_ref = std::move(table_ref[0]);
	if (table_ref.size() == 1) {
		return result_table_ref;
	}
	for (idx_t i = 1; i < table_ref.size(); i++) {
		auto cross_product = make_uniq<JoinRef>();
		cross_product->left = std::move(result_table_ref);
		cross_product->right = std::move(table_ref[i]);
		cross_product->ref_type = JoinRefType::CROSS;
		cross_product->is_implicit = true;
		result_table_ref = std::move(cross_product);
	}
	return result_table_ref;
}
} // namespace duckpgq_peg
} // namespace duckdb
