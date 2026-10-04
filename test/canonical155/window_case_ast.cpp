#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/expression/comparison_expression.hpp"
#include <stdexcept>
#include <iostream>
using namespace duckdb;
using namespace duckdb::duckpgq_peg;
void TestWindowCase(PEGTransformer &t) {
    for (bool simple : {false,true}) {
        vector<CaseCheck> checks;
        unique_ptr<ParsedExpression> condition;
        if(simple) condition=make_uniq<ConstantExpression>(Value(1));
        else condition=make_uniq<ComparisonExpression>(ExpressionType::COMPARE_EQUAL,make_uniq<ColumnRefExpression>("x"),make_uniq<ConstantExpression>(Value(1)));
        checks.push_back(PEGTransformerFactory::TransformCaseWhenThen(t,std::move(condition),make_uniq<ConstantExpression>(Value("yes"))));
        optional<unique_ptr<ParsedExpression>> operand;
        if(simple) operand=make_uniq<ColumnRefExpression>("x");
        auto actual=PEGTransformerFactory::TransformCaseExpression(t,std::move(operand),std::move(checks),{});
        Parser p; p.ParseQuery(simple?"SELECT CASE x WHEN 1 THEN 'yes' END":"SELECT CASE WHEN x=1 THEN 'yes' END");
        auto &expected=p.statements[0]->Cast<SelectStatement>().node->Cast<SelectNode>().select_list[0];
        if(!actual->Equals(*expected)) throw std::runtime_error("CASE AST mismatch");
    }
    auto window=PEGTransformerFactory::TransformWindowFrameContents(t,{},{},{});
    if(window->start!=WindowBoundary::UNBOUNDED_PRECEDING || window->end!=WindowBoundary::CURRENT_ROW_RANGE) throw std::runtime_error("window default frame mismatch");
    WindowFrame frame; frame.start=WindowBoundary::EXPR_PRECEDING_ROWS; frame.end=WindowBoundary::CURRENT_ROW_ROWS;
    frame.start_expr=make_uniq<ConstantExpression>(Value(2)); auto *identity=frame.start_expr.get();
    frame.exclude_clause=WindowExcludeMode::TIES;
    auto framed=PEGTransformerFactory::TransformWindowFrameContents(t,{},{},std::move(frame));
    if(framed->start_expr.get()!=identity || framed->exclude_clause!=WindowExcludeMode::TIES || framed->end!=WindowBoundary::CURRENT_ROW_ROWS) throw std::runtime_error("window frame ownership mismatch");
    vector<OrderByNode> orders; orders.emplace_back(OrderType::ASCENDING,OrderByNullType::ORDER_DEFAULT,make_uniq<StarExpression>());
    bool rejected=false;
    try { PEGTransformerFactory::TransformWindowFrameContents(t,{},std::move(orders),{}); } catch(const ParserException &) { rejected=true; }
    if(!rejected) throw std::runtime_error("ORDER BY ALL accepted in window");
    std::cout<<"PASS CASE/window frame: canonical CASE ASTs, default NULL/frame, frame move, exclusion, ORDER BY ALL rejection\n";
}
#endif
