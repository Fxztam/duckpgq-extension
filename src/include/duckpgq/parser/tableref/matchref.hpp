#pragma once

#include "duckdb/common/vector.hpp"
#include "duckpgq/parser/path_pattern.hpp"
#include "duckdb/parser/tableref.hpp"
#include "duckdb/common/enums/expression_type.hpp"

namespace duckdb {

class MatchExpression : public ParsedExpression {
public:
	// An extension-only placeholder, not a built-in parsed or bound expression.
	// GRAPH_TABLE grammar is not registered in the create-only migration slice.
	static constexpr const ExpressionClass TYPE = ExpressionClass::INVALID;

public:
	MatchExpression() : ParsedExpression(ExpressionType::FUNCTION_REF, TYPE) {
	}

	string pg_name;
	string alias;
	vector<unique_ptr<PathPattern>> path_patterns;

	vector<unique_ptr<ParsedExpression>> column_list;

	unique_ptr<ParsedExpression> where_clause;

public:
	string ToString() const override;
	bool Equals(const BaseExpression &other_p) const override;

	unique_ptr<ParsedExpression> Copy() const override;

	//! Serializes a blob into a MatchRef
	void Serialize(Serializer &serializer) const override;
	//! Deserializes a blob back into a MatchRef
	static unique_ptr<ParsedExpression> Deserialize(Deserializer &deserializer);
};

} // namespace duckdb
