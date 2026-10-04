#include "duckpgq/compat/function_access.hpp"
#include "duckpgq/compat/function_argument.hpp"
#include "duckpgq/compat/unsupported_statement.hpp"
#include "duckdb.h"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/query_node/select_node.hpp"
#include <iostream>
#include <stdexcept>
#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/matcher.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/storage/arena_allocator.hpp"
#endif

using namespace duckdb;
#ifdef PGQ_TRANSFORMER_TEST
void TestCreateAST(duckpgq_peg::PEGTransformer &transformer);
void TestDMLAST(duckpgq_peg::PEGTransformer &transformer);
void TestAlterAST(duckpgq_peg::PEGTransformer &transformer);
void TestPivotAST(duckpgq_peg::PEGTransformer &transformer);
void TestJoinAST(duckpgq_peg::PEGTransformer &transformer);
void TestGroupingCTE(duckpgq_peg::PEGTransformer &transformer);
void TestWindowCase(duckpgq_peg::PEGTransformer &transformer);
void TestWindowFunction(duckpgq_peg::PEGTransformer &transformer);
void TestArraySubquery(duckpgq_peg::PEGTransformer &transformer);
void TestStructComparison(duckpgq_peg::PEGTransformer &transformer);
void TestRegexBetween(duckpgq_peg::PEGTransformer &transformer);
void TestArithmetic(duckpgq_peg::PEGTransformer &transformer);
void TestCollateParameters(duckpgq_peg::PEGTransformer &transformer);
void TestUnaryNumbers(duckpgq_peg::PEGTransformer &transformer);
void TestAggregateStar(duckpgq_peg::PEGTransformer &transformer);
void TestChildOperators(duckpgq_peg::PEGTransformer &transformer);
void TestSubqueries(duckpgq_peg::PEGTransformer &transformer);
void TestReplaceEntry(duckpgq_peg::PEGTransformer &transformer);
void TestInterval(duckpgq_peg::PEGTransformer &transformer);
void TestComprehension(duckpgq_peg::PEGTransformer &transformer);
void TestComments(duckpgq_peg::PEGTransformer &transformer);
void TestCommon(duckpgq_peg::PEGTransformer &transformer);
void TestCopy(duckpgq_peg::PEGTransformer &transformer);
void TestIndex(duckpgq_peg::PEGTransformer &transformer);
void TestMacro(duckpgq_peg::PEGTransformer &transformer);
void TestSecret(duckpgq_peg::PEGTransformer &transformer);
void TestTable(duckpgq_peg::PEGTransformer &transformer);
void TestView(duckpgq_peg::PEGTransformer &transformer);
void TestDescribe(duckpgq_peg::PEGTransformer &transformer);
void TestDrop(duckpgq_peg::PEGTransformer &transformer);
void TestExecute(duckpgq_peg::PEGTransformer &transformer);
void TestPrepare(duckpgq_peg::PEGTransformer &transformer);
void TestDeallocate(duckpgq_peg::PEGTransformer &transformer);
void TestExplain(duckpgq_peg::PEGTransformer &transformer);
void TestCall(duckpgq_peg::PEGTransformer &transformer);
void TestDetach(duckpgq_peg::PEGTransformer &transformer);
void TestExport(duckpgq_peg::PEGTransformer &transformer);
void TestLoad(duckpgq_peg::PEGTransformer &transformer);
void TestPragma(duckpgq_peg::PEGTransformer &transformer);
void TestSet(duckpgq_peg::PEGTransformer &transformer);
void TestTransaction(duckpgq_peg::PEGTransformer &transformer);
void TestVacuum(duckpgq_peg::PEGTransformer &transformer);
void TestPgqGraph(duckpgq_peg::PEGTransformer &transformer);
void TestPegTextEndToEnd();
#endif
void TestBindingAdapter();
void TestNameMetadata();
void TestSerialization();
void TestPathSerialization();
static void Require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
void TestExpressionAccess();
int main() {
    try {
    TestExpressionAccess();
    TestPathSerialization();
    TestSerialization();
    TestBindingAdapter();
    TestNameMetadata();
    Require(std::string(duckdb_library_version()) == "v1.5.5", "canonical library version");
    Parser parser;
    parser.ParseQuery("SELECT pgq_test(73), 91");
    auto &select = parser.statements[0]->Cast<SelectStatement>().node->Cast<SelectNode>();
    auto &function = select.select_list[0]->Cast<FunctionExpression>();
    Require(duckpgq_compat::FunctionName(function) == "pgq_test", "function name");
    auto &arguments = duckpgq_compat::Arguments(function);
    Require(arguments.size() == 1, "argument count");
    auto &expression = duckpgq_compat::Expression(arguments[0]);
    auto original = expression.get();
    auto copied = expression->Copy();
    Require(copied.get() != original && copied->Equals(*original), "explicit copy independence");
    auto moved = std::move(expression);
    Require(!expression && moved.get() == original, "move must transfer, not copy ownership");
    Require(moved->Cast<ConstantExpression>().value.GetValue<int64_t>() == 73, "argument value");
    expression = std::move(moved);
    const FunctionExpression &read_only = function;
    const auto &read_arguments = duckpgq_compat::Arguments(read_only);
    Require(duckpgq_compat::Expression(read_arguments[0]) == original, "const identity");
    expression = std::move(select.select_list[1]);
    Require(duckpgq_compat::Expression(arguments[0])->Cast<ConstantExpression>().value.GetValue<int64_t>() == 91, "replacement");
    Parser named_parser;
    named_parser.ParseQuery("SELECT main.pgq_test(item := 73, other := 91)");
    auto &expected = named_parser.statements[0]->Cast<SelectStatement>().node->Cast<SelectNode>().select_list[0]->Cast<FunctionExpression>();
    vector<duckpgq_peg::FunctionArgument> pgq_args;
    pgq_args.emplace_back(Identifier("item"), expected.children[0]->Copy());
    pgq_args.emplace_back(Identifier("other"), expected.children[1]->Copy());
    QualifiedName qualified;
    qualified.catalog = expected.catalog;
    qualified.schema = expected.schema;
    qualified.name = expected.function_name;
    auto rebuilt = duckpgq_peg::BuildFunctionExpression(qualified, std::move(pgq_args));
    Require(rebuilt->Equals(expected), "named arguments must match canonical parser AST");
    vector<duckpgq_peg::FunctionArgument> invalid;
    invalid.emplace_back(unique_ptr<ParsedExpression>());
    bool rejected = false;
    try { duckpgq_peg::BuildFunctionExpression(qualified, std::move(invalid)); }
    catch (const InvalidInputException &) { rejected = true; }
    Require(rejected, "null argument must be diagnosed");
    for (const char *statement : {"CONNECT", "DISCONNECT", "CREATE TRIGGER"}) {
        bool diagnosed = false;
        try { duckpgq_peg::RejectUnsupportedHostStatement(statement); }
        catch (const ParserException &ex) {
            auto message = std::string(ex.what());
            diagnosed = message.find(statement) != std::string::npos && message.find("1.5.5") != std::string::npos;
        }
        Require(diagnosed, "unsupported statement diagnostic");
    }
#ifdef PGQ_TRANSFORMER_TEST
    using namespace duckpgq_peg;
    ArenaAllocator arena(Allocator::DefaultAllocator());
    vector<MatcherToken> tokens;
    PEGTransformerState state(tokens);
    case_insensitive_map_t<PEGTransformer::AnyTransformFunction> functions;
    case_insensitive_map_t<PEGRule> rules;
    ParserOptions options;
    PEGTransformer transformer(arena, state, functions, rules, options);
    TestCreateAST(transformer);
    TestDMLAST(transformer);
    TestAlterAST(transformer);
    TestPivotAST(transformer);
    TestJoinAST(transformer);
    TestGroupingCTE(transformer);
    TestWindowCase(transformer);
    TestWindowFunction(transformer);
    TestArraySubquery(transformer);
    TestStructComparison(transformer);
    TestRegexBetween(transformer);
    TestArithmetic(transformer);
    TestCollateParameters(transformer);
    TestUnaryNumbers(transformer);
    TestAggregateStar(transformer);
    TestChildOperators(transformer);
    TestSubqueries(transformer);
    TestReplaceEntry(transformer);
    TestInterval(transformer);
    TestComprehension(transformer);
    TestComments(transformer);
    TestCommon(transformer);
    TestCopy(transformer);
    TestIndex(transformer);
    TestMacro(transformer);
    TestSecret(transformer);
    TestTable(transformer);
    TestView(transformer);
    TestDescribe(transformer);
    TestDrop(transformer);
    TestExecute(transformer);
    TestPrepare(transformer);
    TestDeallocate(transformer);
    TestExplain(transformer);
    TestCall(transformer);
    TestDetach(transformer);
    TestExport(transformer);
    TestLoad(transformer);
    TestPragma(transformer);
    TestSet(transformer);
    TestTransaction(transformer);
    TestVacuum(transformer);
    TestPgqGraph(transformer);
    TestPegTextEndToEnd();
    auto use_target = PEGTransformerFactory::TransformUseTargetCatalogSchema(
        transformer, Identifier("db space"), Identifier("s\"x"), {});
    auto use_statement = PEGTransformerFactory::TransformUseStatement(transformer, use_target);
    Parser use_parser;
    use_parser.ParseQuery("USE \"db space\".\"s\"\"x\"");
    Require(use_statement->ToString() == use_parser.statements[0]->ToString(), "real USE transformer differs from canonical parser");
    int rejections = 0;
    try { PEGTransformerFactory::TransformLocalSessionTarget(transformer); }
    catch (const ParserException &) { rejections++; }
    try { PEGTransformerFactory::TransformConnectStatement(transformer, {}); }
    catch (const ParserException &) { rejections++; }
    try { PEGTransformerFactory::TransformDisconnectStatement(transformer); }
    catch (const ParserException &) { rejections++; }
    try { PEGTransformerFactory::TransformCreateTriggerStmt(transformer, {}, Identifier("tr"),
        TriggerTiming::BEFORE, TriggerEventInfo{}, nullptr, {}, {}, nullptr); }
    catch (const ParserException &) { rejections++; }
    Require(rejections == 4, "real Connect/Trigger transformers must reject absent host features");
#endif
    auto ids = StringsToIdentifiers(vector<string>{"MixedCase", "unicode_ö"});
    Require(IdentifiersToStrings(ids) == vector<string>({"MixedCase", "unicode_ö"}), "identifier round trip");
    identifier_map_t<int> lookup;
    lookup[Identifier("MixedCase")] = 73;
    Require(lookup.at(Identifier("mixedcase")) == 73, "identifier map case-insensitive lookup");
    std::cout << "PASS canonical 1.5.5 PGQ: argument AST parity, ownership, bind ABI, names, metadata, USE, Connect/Trigger rejection\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL PGQ regression: " << error.what() << std::endl;
        return 1;
    }
}
