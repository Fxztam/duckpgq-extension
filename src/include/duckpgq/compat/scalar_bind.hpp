#pragma once
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/planner/expression.hpp"

namespace duckdb {
namespace duckpgq_compat {
// Qualified on purpose: function_access.hpp declares overloads named Expression() in this namespace.
#if __has_include("duckdb/common/identifier.hpp")
using ScalarBindInput = BindScalarFunctionInput;
template <unique_ptr<FunctionData> (*Callback)(ScalarBindInput &)>
constexpr bind_scalar_function_t AdaptBind() { return Callback; }
inline const LogicalType &ReturnType(const duckdb::Expression &expression) { return expression.GetReturnType(); }
#else
// Borrowed PGQ-local view. Neither this type nor its layout is passed to DuckDB.
class ScalarBindInput {
public:
    ScalarBindInput(ClientContext &context, ScalarFunction &function, vector<unique_ptr<duckdb::Expression>> &arguments)
        : context(context), function(function), arguments(arguments) {}
    ClientContext &GetClientContext() const { return context; }
    ScalarFunction &GetBoundFunction() const { return function; }
    vector<unique_ptr<duckdb::Expression>> &GetArguments() const { return arguments; }
private:
    ClientContext &context;
    ScalarFunction &function;
    vector<unique_ptr<duckdb::Expression>> &arguments;
};
template <unique_ptr<FunctionData> (*Callback)(ScalarBindInput &)>
unique_ptr<FunctionData> BindThunk(ClientContext &context, ScalarFunction &function,
                                 vector<unique_ptr<duckdb::Expression>> &arguments) {
    ScalarBindInput input(context, function, arguments);
    return Callback(input);
}
template <unique_ptr<FunctionData> (*Callback)(ScalarBindInput &)>
constexpr bind_scalar_function_t AdaptBind() { return &BindThunk<Callback>; }
inline const LogicalType &ReturnType(const duckdb::Expression &expression) { return expression.return_type; }
#endif
} // namespace duckpgq_compat
} // namespace duckdb
