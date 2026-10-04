#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/compat/window_function.hpp"
#include "duckpgq/compat/name_metadata.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include <stdexcept>
#include <iostream>
using namespace duckdb;
using namespace duckdb::duckpgq_peg;
void TestWindowFunction(PEGTransformer &t) {
    Parser ordered;ordered.ParseQuery("SELECT abs(t.x + s.y)");
    auto &order_expr=ordered.statements[0]->Cast<SelectStatement>().node->Cast<SelectNode>().select_list[0];
    auto *order_identity=order_expr.get();
    PEGTransformerFactory::RemoveOrderQualificationRecursive(order_expr);
    Parser unqualified;unqualified.ParseQuery("SELECT abs(x + y)");
    if(order_expr.get()!=order_identity || !order_expr->Equals(*unqualified.statements[0]->Cast<SelectStatement>().node->Cast<SelectNode>().select_list[0]))throw std::runtime_error("recursive order qualification/move mismatch");
    auto qualified=PEGTransformerFactory::TransformCatalogReservedSchemaFunctionName(t,Identifier("Cat X"),Identifier("Sch Ö"),Identifier("Fn.Mixed"));
    if(qualified.catalog!="Cat X" || qualified.schema!="Sch Ö" || qualified.name!="Fn.Mixed")throw std::runtime_error("qualified function spelling lost");
    auto schema=PEGTransformerFactory::TransformSchemaReservedFunctionName(t,Identifier("Sch"),Identifier("Fn"));
    if(!schema.catalog.empty() || schema.schema!="Sch" || schema.name!="Fn")throw std::runtime_error("schema function path lost");
    vector<unique_ptr<ParsedExpression>> tables;
    tables.push_back(make_uniq<ColumnRefExpression>(vector<string>{"Cat X","Sch Ö","Table.Mixed"}));
    auto scan=PEGTransformerFactory::TransformExpressionStatement(t,std::move(tables));
    Parser scan_parser;scan_parser.ParseQuery("SELECT * FROM \"Cat X\".\"Sch Ö\".\"Table.Mixed\"");
    if(scan->ToString()!=scan_parser.statements[0]->ToString())throw std::runtime_error("expression table scan mismatch");
    vector<unique_ptr<ParsedExpression>> invalid_tables;
    invalid_tables.push_back(make_uniq<ColumnRefExpression>("t"));invalid_tables.push_back(make_uniq<ConstantExpression>(Value(1)));
    bool mixed_rejected=false;
    try { PEGTransformerFactory::TransformExpressionStatement(t,std::move(invalid_tables)); }catch(const ParserException &){mixed_rejected=true;}
    if(!mixed_rejected)throw std::runtime_error("mixed table/expression statement accepted");
    for(const string name : {"if", "ifnull", "abs"}) {
        MethodArguments args;
        Parser arg_parser; arg_parser.ParseQuery(name=="if"?"SELECT true":"SELECT 1");
        args.arguments.emplace_back(std::move(arg_parser.statements[0]->Cast<SelectStatement>().node->Cast<SelectNode>().select_list[0]));
        if(name!="abs")args.arguments.emplace_back(make_uniq<ConstantExpression>(Value(2)));
        if(name=="if")args.arguments.emplace_back(make_uniq<ConstantExpression>(Value(3)));
        auto actual=PEGTransformerFactory::TransformFunctionExpression(t,duckpgq_compat::MakeQualifiedName(Identifier(name)),std::move(args),{},{},false,{});
        Parser p;p.ParseQuery(name=="if"?"SELECT if(true,2,3)":(name=="ifnull"?"SELECT ifnull(1,2)":"SELECT abs(1)"));
        if(!actual->Equals(*p.statements[0]->Cast<SelectStatement>().node->Cast<SelectNode>().select_list[0]))throw std::runtime_error("ordinary function/CASE mismatch");
    }
    int rejected=0;
    for(int i=0;i<7;++i) {
        MethodArguments args; string name="row_number"; unique_ptr<ParsedExpression> filter;
        if(i==0) args.distinct=true;
        if(i==1) filter=make_uniq<ConstantExpression>(Value(true));
        if(i==2) {name="sum";args.has_ignore_nulls=true;}
        if(i==3) {name="lead";for(int j=0;j<4;++j)args.arguments.emplace_back(make_uniq<ConstantExpression>(Value(j)));}
        if(i==4) {name="dense_rank";args.order_bys.emplace_back(OrderType::ASCENDING,OrderByNullType::ORDER_DEFAULT,make_uniq<ColumnRefExpression>("x"));}
        if(i==6)t.in_window_definition=true;
        try { BuildWindowFunction(t,duckpgq_compat::MakeQualifiedName(Identifier(name)),std::move(args),{},std::move(filter),i==5,PEGTransformerFactory::TransformWindowFrameContents(t,{},{},{})); }
        catch(const ParserException &) { ++rejected; }
        t.in_window_definition=false;
    }
    if(rejected!=7)throw std::runtime_error("missing window semantic diagnostic");
    auto base=PEGTransformerFactory::TransformWindowFrameContents(t,{},{},{});
    base->partitions.push_back(make_uniq<ColumnRefExpression>("g"));
    t.window_clauses[Identifier("Base")]=std::move(base);
    auto inherited=PEGTransformerFactory::TransformWindowFrameNameContents(t,Identifier("base"),PEGTransformerFactory::TransformWindowFrameContents(t,{},{},{}));
    if(inherited->partitions.size()!=1 || inherited->partitions[0].get()==t.window_clauses[Identifier("Base")]->partitions[0].get()) throw std::runtime_error("named window must copy partition ownership");
    bool override_rejected=false;
    vector<unique_ptr<ParsedExpression>> partitions;partitions.push_back(make_uniq<ColumnRefExpression>("other"));
    try { PEGTransformerFactory::TransformWindowFrameNameContents(t,Identifier("Base"),PEGTransformerFactory::TransformWindowFrameContents(t,std::move(partitions),{},{})); }catch(const ParserException &){override_rejected=true;}
    if(!override_rejected)throw std::runtime_error("named partition override accepted");
    t.window_clauses.clear();
    for(const string fn : {"sum(x)","count(*)","row_number()","rank()","dense_rank()","percent_rank()","cume_dist()","ntile(4)","lead(x,2,0)","lag(x,2,0)","first_value(x IGNORE NULLS)","last_value(x)","nth_value(x,2)","sum(DISTINCT x) FILTER (WHERE x>0)","first_value(x ORDER BY y)"}) {
        Parser p; p.ParseQuery("SELECT "+fn+" OVER (PARTITION BY g ORDER BY y ROWS BETWEEN 2 PRECEDING AND CURRENT ROW) FROM t");
        auto &expected=p.statements[0]->Cast<SelectStatement>().node->Cast<SelectNode>().select_list[0]->Cast<WindowExpression>();
        MethodArguments args; args.distinct=expected.distinct; args.ignore_nulls=expected.ignore_nulls; args.has_ignore_nulls=expected.ignore_nulls;
        for(auto &a:expected.children) args.arguments.emplace_back(a->Copy());
        if(expected.offset_expr) args.arguments.emplace_back(expected.offset_expr->Copy());
        if(expected.default_expr) args.arguments.emplace_back(expected.default_expr->Copy());
        for(auto &o:expected.arg_orders) args.order_bys.emplace_back(o.type,o.null_order,o.expression->Copy());
        auto frame=unique_ptr_cast<ParsedExpression,WindowExpression>(expected.Copy());
        frame->children.clear(); frame->filter_expr.reset(); frame->arg_orders.clear(); frame->offset_expr.reset(); frame->default_expr.reset();
        auto *boundary=frame->start_expr.get();
        auto name=duckpgq_compat::MakeQualifiedName(Identifier(expected.function_name));
        optional<unique_ptr<ParsedExpression>> filter;
        if(expected.filter_expr)filter=expected.filter_expr->Copy();
        auto actual=PEGTransformerFactory::TransformFunctionExpression(t,name,std::move(args),{},std::move(filter),false,std::move(frame));
        if(!actual->Equals(expected) || actual->Cast<WindowExpression>().start_expr.get()!=boundary) throw std::runtime_error("window AST/ownership differs: "+fn);
    }
    std::cout<<"PASS window-function AST parity: 15 aggregate/ranking/value/offset/FILTER/DISTINCT/order/null cases\n";
}
#endif
