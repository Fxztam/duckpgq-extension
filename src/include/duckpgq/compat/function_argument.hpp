#pragma once
#include "duckpgq/parser/identifier.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/qualified_name.hpp"
#include "duckdb/common/exception.hpp"

namespace duckdb {
namespace duckpgq_peg {
#if __has_include("duckdb/common/identifier.hpp")
using FunctionArgument = duckdb::FunctionArgument;
inline unique_ptr<FunctionExpression> BuildFunctionExpression(const QualifiedName &name,
                                                             vector<FunctionArgument> arguments) {
    return make_uniq<FunctionExpression>(name, std::move(arguments));
}
#else
// Parser-local value, never passed across the DuckDB ABI.
class FunctionArgument {
public:
    FunctionArgument(unique_ptr<ParsedExpression> expression) : expression(std::move(expression)) {}
    FunctionArgument(Identifier name, unique_ptr<ParsedExpression> expression)
        : name(std::move(name)), expression(std::move(expression)) {}
    const Identifier &GetName() const { return name; }
    bool HasName() const { return !name.empty(); }
    unique_ptr<ParsedExpression> &GetExpressionMutable() { return expression; }
    const ParsedExpression &GetExpression() const {
        if (!expression) throw InvalidInputException("DuckPGQ: missing function argument expression");
        return *expression;
    }
    FunctionArgument Copy() const { return FunctionArgument(name, expression ? expression->Copy() : nullptr); }
private:
    Identifier name;
    unique_ptr<ParsedExpression> expression;
};

inline vector<unique_ptr<ParsedExpression>> LowerFunctionArguments(vector<FunctionArgument> arguments) {
    // Validate all elements before moving any ownership.
    for (auto &argument : arguments) {
        if (!argument.GetExpressionMutable()) throw InvalidInputException("DuckPGQ: missing function argument expression");
    }
    vector<unique_ptr<ParsedExpression>> result;
    result.reserve(arguments.size());
    for (auto &argument : arguments) {
        auto expression = std::move(argument.GetExpressionMutable());
        // Canonical Transformer::TransformNamedArg represents the name as alias.
        if (argument.HasName()) expression->SetAlias(argument.GetName().GetIdentifierName());
        result.push_back(std::move(expression));
    }
    return result;
}
inline unique_ptr<FunctionExpression> BuildFunctionExpression(const QualifiedName &name,
                                                             vector<FunctionArgument> arguments) {
    return make_uniq<FunctionExpression>(name.catalog, name.schema, name.name,
                                         LowerFunctionArguments(std::move(arguments)));
}
#endif
} // namespace duckpgq_peg
} // namespace duckdb
