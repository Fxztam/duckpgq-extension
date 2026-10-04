#include "duckpgq/compat/scalar_bind.hpp"
#include "duckdb.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include <stdexcept>
#include <type_traits>

using namespace duckdb;
static ClientContext *expected_context;
static ScalarFunction *expected_function;
static vector<unique_ptr<Expression>> *expected_arguments;
static Expression *expected_expression;
static unique_ptr<FunctionData> ProbeBind(duckpgq_compat::ScalarBindInput &input) {
    if (&input.GetClientContext() != expected_context || &input.GetBoundFunction() != expected_function ||
        &input.GetArguments() != expected_arguments || input.GetArguments()[0].get() != expected_expression) {
        throw std::runtime_error("bind adapter changed reference identity");
    }
    if (duckpgq_compat::ReturnType(*input.GetArguments()[0]) != LogicalType::BIGINT) {
        throw std::runtime_error("bind argument return type");
    }
    input.GetBoundFunction().SetReturnType(LogicalType::DOUBLE);
    return nullptr;
}
static unique_ptr<FunctionData> ThrowBind(duckpgq_compat::ScalarBindInput &) {
    throw InvalidInputException("bind rejection sentinel");
}
void TestBindingAdapter() {
    static_assert(std::is_same<decltype(duckpgq_compat::AdaptBind<ProbeBind>()), bind_scalar_function_t>::value,
                  "must use the real host ABI callback signature");
    DuckDB database(nullptr);
    Connection connection(database);
    ScalarFunction function("pgq_probe", {LogicalType::BIGINT}, LogicalType::BIGINT, nullptr);
    vector<unique_ptr<Expression>> arguments;
    arguments.push_back(make_uniq<BoundConstantExpression>(Value::BIGINT(73)));
    expected_context = connection.context.get();
    expected_function = &function;
    expected_arguments = &arguments;
    expected_expression = arguments[0].get();
    auto result = duckpgq_compat::AdaptBind<ProbeBind>()(*connection.context, function, arguments);
    if (result || arguments[0].get() != expected_expression || function.GetReturnType() != LogicalType::DOUBLE) {
        throw std::runtime_error("bind mutation/ownership propagation");
    }
    bool caught = false;
    try { duckpgq_compat::AdaptBind<ThrowBind>()(*connection.context, function, arguments); }
    catch (const InvalidInputException &) { caught = true; }
    if (!caught) throw std::runtime_error("bind exception was swallowed");
}
