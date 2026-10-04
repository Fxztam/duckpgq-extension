#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/statement/execute_statement.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;using namespace duckdb::duckpgq_peg;using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
void TestExecute(PEGTransformer&t){
 DuckDB db(nullptr);Connection c(db);
 for(const char *sql:{"PREPARE plain AS SELECT 73","PREPARE \"positional\" AS SELECT $1::INTEGER + $2::INTEGER","PREPARE named AS SELECT $val::INTEGER"}) {
   auto fixture=c.Query(sql);if(fixture->HasError())throw std::runtime_error(fixture->GetError());
 }
 auto r=c.Query(F::TransformExecuteStatement(t,Identifier("plain"),{}));
 Check(!r->HasError() && r->GetValue(0,0)==Value(73),"execute no arguments");
 for(bool named:{false,true})for(bool null_value:{false,true}){
   vector<FunctionArgument> args;auto expr=make_uniq<ConstantExpression>(null_value?Value():Value(70));auto *identity=expr.get();
   expr->SetAlias("discard_me");
   if(named)args.emplace_back(Identifier("val"),std::move(expr));
   else {args.emplace_back(std::move(expr));args.emplace_back(make_uniq<ConstantExpression>(Value(3)));}
   auto actual=F::TransformExecuteStatement(t,Identifier(named?"named":"positional"),std::move(args));
   auto &a=actual->Cast<ExecuteStatement>();
   Check(a.named_values.at(named?"val":"1").get()==identity && identity->GetAlias().empty(),"execute move and alias clearing");
   Parser p;p.ParseQuery(named?(null_value?"EXECUTE named(val := NULL)":"EXECUTE named(val := 70)"):(null_value?"EXECUTE \"positional\"(NULL,3)":"EXECUTE \"positional\"(70,3)"));
   auto &b=p.statements[0]->Cast<ExecuteStatement>();
   Check(a.name==b.name && a.named_values.size()==b.named_values.size(),"execute AST shape");
   for(auto &arg:a.named_values)Check(arg.second->Equals(*b.named_values.at(arg.first)),"execute canonical argument AST");
   r=c.Query(std::move(actual));
   Check(!r->HasError(),"execute runtime");auto value=r->GetValue(0,0);
   Check(null_value?value.IsNull():value==Value(named?70:73),"execute result");
 }
 int bad=0;
 for(bool named_first:{false,true}){
   vector<FunctionArgument> args;
   if(named_first)args.emplace_back(Identifier("val"),make_uniq<ConstantExpression>(Value(1)));
   args.emplace_back(make_uniq<ConstantExpression>(Value(2)));
   if(!named_first)args.emplace_back(Identifier("val"),make_uniq<ConstantExpression>(Value(1)));
   try{F::TransformExecuteStatement(t,Identifier("named"),std::move(args));}catch(const NotImplementedException&){bad++;}
 }
 vector<FunctionArgument> args;args.emplace_back(make_uniq<ColumnRefExpression>("x"));
 try{F::TransformExecuteStatement(t,Identifier("named"),std::move(args));}catch(const InvalidInputException&){bad++;}
 Check(bad==3,"execute mixed/non-scalar rejection");
 std::cout<<"PASS EXECUTE: canonical arguments, alias/move, live positional/named/NULL/no-args and three rejections\n";
}
#endif
