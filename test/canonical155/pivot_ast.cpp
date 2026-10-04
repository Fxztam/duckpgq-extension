#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include <stdexcept>
#include <iostream>
using namespace duckdb;
using namespace duckdb::duckpgq_peg;
void TestPivotAST(PEGTransformer &t) {
    for (const char *sql : {
        "SELECT * FROM t PIVOT (sum(v) FOR x IN ('a', 'b')) AS p",
        "SELECT * FROM t UNPIVOT INCLUDE NULLS (v FOR n IN (a, b)) AS u",
        "SELECT * FROM t UNPIVOT EXCLUDE NULLS (v FOR n IN (a, b)) AS u"}) {
        Parser parser; parser.ParseQuery(sql);
        auto &expected = parser.statements[0]->Cast<SelectStatement>().node->Cast<SelectNode>().from_table->Cast<PivotRef>();
        vector<PivotColumn> pivots;
        for (auto &p : expected.pivots) pivots.push_back(p.Copy());
        TableAlias alias;
        alias.name = Identifier(expected.alias);
        alias.column_name_alias = StringsToIdentifiers(expected.column_name_alias);
        unique_ptr<TableRef> actual;
        if (!expected.unpivot_names.empty()) {
            actual = PEGTransformerFactory::TransformTableUnpivotClauseBody(t, expected.unpivot_names, std::move(pivots));
            actual = PEGTransformerFactory::TransformTableUnpivotClause(t, expected.include_nulls, std::move(actual), alias);
        } else {
            vector<unique_ptr<ParsedExpression>> aggregates;
            for (auto &a : expected.aggregates) aggregates.push_back(a->Copy());
            actual = PEGTransformerFactory::TransformTablePivotClauseBody(t, std::move(aggregates), std::move(pivots), expected.groups);
            actual = PEGTransformerFactory::TransformTablePivotClause(t, std::move(actual), alias);
        }
        actual->Cast<PivotRef>().source = expected.source->Copy();
        if (!actual->Equals(expected)) throw std::runtime_error("PIVOT/UNPIVOT AST differs from canonical parser");
    }
    auto row = []() -> unique_ptr<ParsedExpression> {
        vector<unique_ptr<ParsedExpression>> args;
        args.push_back(make_uniq<ColumnRefExpression>("x"));
        args.push_back(make_uniq<ColumnRefExpression>("y"));
        return make_uniq<FunctionExpression>("row", std::move(args));
    };
    PivotColumn tuple_target;
    PivotColumnEntry tuple_entry;
    tuple_entry.values = {Value("a"), Value("b")};
    tuple_target.entries.push_back(std::move(tuple_entry));
    auto header = row();
    auto *first = header->Cast<FunctionExpression>().children[0].get();
    auto unpacked = PEGTransformerFactory::TransformPivotValueList(t, std::move(header), std::move(tuple_target));
    if (unpacked.pivot_expressions.size() != 2 || unpacked.pivot_expressions[0].get() != first) throw std::runtime_error("tuple header move failed");
    PivotColumn scalar_target; PivotColumnEntry scalar_entry;
    scalar_entry.values = {Value("ab")}; scalar_target.entries.push_back(std::move(scalar_entry));
    auto scalar = row(); auto *whole = scalar.get();
    auto kept = PEGTransformerFactory::TransformPivotValueList(t, std::move(scalar), std::move(scalar_target));
    if (kept.pivot_expressions.size() != 1 || kept.pivot_expressions[0].get() != whole) throw std::runtime_error("scalar IN must keep compound row");
    auto values = row(); PivotColumnEntry flat;
    if (!PEGTransformerFactory::TransformPivotInList(values, flat) || flat.values != vector<Value>{Value("x"), Value("y")}) throw std::runtime_error("tuple IN values/order");
    vector<unique_ptr<ParsedExpression>> partial;
    partial.push_back(make_uniq<ConstantExpression>(Value(1)));
    partial.push_back(make_uniq<FunctionExpression>("random", vector<unique_ptr<ParsedExpression>>{}));
    unique_ptr<ParsedExpression> nonconstant = make_uniq<FunctionExpression>("row", std::move(partial));
    PivotColumnEntry rollback; rollback.values.push_back(Value(99));
    if (PEGTransformerFactory::TransformPivotInList(nonconstant, rollback) || rollback.values != vector<Value>{Value(99)}) throw std::runtime_error("partial tuple not rolled back");
    int negatives = 0;
    unique_ptr<ParsedExpression> qualified = make_uniq<ColumnRefExpression>(vector<string>{"t", "x"});
    try { PEGTransformerFactory::TransformPivotInList(qualified, flat); } catch (const ParserException &) { ++negatives; }
    try { PEGTransformerFactory::TransformUnpivotValueList(t, {}, {}); } catch (const ParserException &) { ++negatives; }
    try { PEGTransformerFactory::TransformUnpivotValueList(t, {"a", "b"}, {}); } catch (const ParserException &) { ++negatives; }
    vector<PivotColumn> too_many; too_many.emplace_back(); too_many.emplace_back();
    try { PEGTransformerFactory::TransformTableUnpivotClauseBody(t, {"v"}, std::move(too_many)); } catch (const ParserException &) { ++negatives; }
    if (negatives != 4) throw std::runtime_error("PIVOT/UNPIVOT negative checks missing");
    auto groups = PEGTransformerFactory::TransformPivotGroupByList(t, {Identifier("Group Ö"), Identifier("Second")});
    if (groups != vector<string>{"Group Ö", "Second"}) throw std::runtime_error("PIVOT group order/type lost");
    auto grouped = PEGTransformerFactory::TransformTablePivotClauseBody(t, {}, {}, groups);
    if (grouped->Cast<PivotRef>().groups != groups) throw std::runtime_error("PIVOT group AST lost");
    Parser subparser; subparser.ParseQuery("SELECT (SELECT x FROM t)");
    auto &sublist = subparser.statements[0]->Cast<SelectStatement>().node->Cast<SelectNode>().select_list;
    vector<PivotColumn> subinput; PivotColumn subcol;
    subcol.pivot_expressions.push_back(std::move(sublist[0])); subinput.push_back(std::move(subcol));
    bool sub_rejected = false;
    try { PEGTransformerFactory::TransformPivotColumnList(t, std::move(subinput)); }
    catch (const ParserException &) { sub_rejected = true; }
    if (!sub_rejected) throw std::runtime_error("PIVOT subquery accepted");
    auto named = PEGTransformerFactory::TransformIntoNameValues(t, Identifier("Name Case"), {Identifier("Value Ö")});
    if (named.column.unpivot_names[0] != "Name Case" || named.unpivot_names[0].GetIdentifierName() != "Value Ö") throw std::runtime_error("UNPIVOT naming lost");
    vector<PivotColumn> input;
    PivotColumn column;
    column.pivot_expressions.push_back(make_uniq<ColumnRefExpression>("x"));
    auto *identity = column.pivot_expressions[0].get();
    input.push_back(std::move(column));
    auto result = PEGTransformerFactory::TransformPivotColumnList(t, std::move(input));
    if (result[0].pivot_expressions[0].get() != identity) throw std::runtime_error("PIVOT ownership lost");
    vector<PivotColumn> invalid;
    PivotColumn constant;
    constant.pivot_expressions.push_back(make_uniq<ConstantExpression>(Value(73)));
    invalid.push_back(std::move(constant));
    bool rejected = false;
    try { PEGTransformerFactory::TransformPivotColumnList(t, std::move(invalid)); }
    catch (const ParserException &) { rejected = true; }
    if (!rejected) throw std::runtime_error("PIVOT constant accepted");
    std::cout << "PASS PIVOT/UNPIVOT: canonical AST, NULL modes, tuples, rollback, ownership, groups, names, negative cases\n";
}
#endif
