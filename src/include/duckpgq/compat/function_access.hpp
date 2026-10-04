#pragma once

#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/cast_expression.hpp"
#include "duckdb/parser/expression/operator_expression.hpp"

// PGQ-owned accessors; never extend or shadow a DuckDB host class.
// The pinned PGQ fork introduced Identifier and FunctionArgument together.
namespace duckdb {
namespace duckpgq_compat {
inline unique_ptr<ParsedExpression> &CastChildMutable(CastExpression &expression) {
#if __has_include("duckdb/common/identifier.hpp")
    return expression.ChildMutable();
#else
    return expression.child;
#endif
}
inline auto &OperatorChildren(OperatorExpression &expression) {
#if __has_include("duckdb/common/identifier.hpp")
    return expression.GetChildrenMutable();
#else
    return expression.children;
#endif
}
inline const ParsedExpression &CastChild(const CastExpression &expression) {
#if __has_include("duckdb/common/identifier.hpp")
    return expression.Child();
#else
    return *expression.child;
#endif
}
inline const auto &OperatorChildren(const OperatorExpression &expression) {
#if __has_include("duckdb/common/identifier.hpp")
    return expression.GetChildren();
#else
    return expression.children;
#endif
}
inline const Value &ConstantValue(const ConstantExpression &expression) {
#if __has_include("duckdb/common/identifier.hpp")
    return expression.GetValue();
#else
    return expression.value;
#endif
}
#if __has_include("duckdb/common/identifier.hpp")
inline const string &FunctionName(const FunctionExpression &function) {
    return function.FunctionName().GetIdentifierName();
}
inline auto &Arguments(FunctionExpression &function) { return function.GetArgumentsMutable(); }
inline const auto &Arguments(const FunctionExpression &function) { return function.GetArguments(); }
inline unique_ptr<ParsedExpression> &Expression(FunctionArgument &argument) {
    return argument.GetExpressionMutable();
}
inline const ParsedExpression *Expression(const FunctionArgument &argument) {
    return &argument.GetExpression();
}
#else
inline const string &FunctionName(const FunctionExpression &function) { return function.function_name; }
inline auto &Arguments(FunctionExpression &function) { return function.children; }
inline const auto &Arguments(const FunctionExpression &function) { return function.children; }
inline unique_ptr<ParsedExpression> &Expression(unique_ptr<ParsedExpression> &argument) { return argument; }
inline const ParsedExpression *Expression(const unique_ptr<ParsedExpression> &argument) { return argument.get(); }
#endif
} // namespace duckpgq_compat
} // namespace duckdb
