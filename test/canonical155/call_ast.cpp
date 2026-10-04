#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/compat/name_metadata.hpp"
#include "duckdb/parser/statement/call_statement.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb; using namespace duckdb::duckpgq_peg; using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static unique_ptr<SQLStatement> Parse(const string&s){Parser p;p.ParseQuery(s);return std::move(p.statements[0]);}
static string Rows(Connection&c,unique_ptr<SQLStatement> st){
 auto r=c.Query(std::move(st));
 if(r->HasError())return "ERROR: "+r->GetError();
 return r->ToString();
}
void TestCall(PEGTransformer&t){
 DuckDB db(nullptr); Connection c(db);
 // positional arguments: AST, ownership and live execution against the canonical statement
 for(int n:{0,3,5}){
  vector<FunctionArgument> args;
  auto expr=make_uniq<ConstantExpression>(Value(n));auto *identity=expr.get();
  args.emplace_back(std::move(expr));
  auto actual=F::TransformCallStatement(t,duckpgq_compat::MakeQualifiedName(Identifier("range")),std::move(args));
  auto expected=Parse("CALL range("+std::to_string(n)+")");
  auto &a=actual->Cast<CallStatement>();auto &b=expected->Cast<CallStatement>();
  Check(a.function.get()!=nullptr && a.function->Cast<FunctionExpression>().children[0].get()==identity,"call positional move identity");
  Check(a.function->Equals(*b.function) && a.ToString()==b.ToString(),"call canonical AST");
  auto copy=actual->Copy();
  Check(copy->Cast<CallStatement>().function.get()!=a.function.get(),"call independent copy");
  auto got=Rows(c,std::move(actual));auto ref=Rows(c,std::move(expected));
  Check(got.rfind("ERROR",0)!=0 && got==ref,"call live result equals canonical");
 }
 // named argument must equal the canonical parser's named-argument AST
 {
  vector<FunctionArgument> args;
  args.emplace_back(make_uniq<ConstantExpression>(Value(7)));
  args.emplace_back(Identifier("num_rows"),make_uniq<ConstantExpression>(Value(2)));
  auto actual=F::TransformCallStatement(t,duckpgq_compat::MakeQualifiedName(Identifier("repeat_row")),std::move(args));
  auto expected=Parse("CALL repeat_row(7, num_rows := 2)");
  Check(actual->Cast<CallStatement>().function->Equals(*expected->Cast<CallStatement>().function),"call named canonical AST");
  auto got=Rows(c,std::move(actual));auto ref=Rows(c,std::move(expected));
  Check(got.rfind("ERROR",0)!=0 && got==ref,"call named live result equals canonical");
 }
 // no arguments and a qualified name
 {
  auto actual=F::TransformCallStatement(t,duckpgq_compat::MakeQualifiedName(Identifier("pragma_version")),{});
  auto expected=Parse("CALL pragma_version()");
  Check(actual->Cast<CallStatement>().function->Equals(*expected->Cast<CallStatement>().function),"call no-argument AST");
  auto got=Rows(c,std::move(actual));
  Check(got.rfind("ERROR",0)!=0 && got==Rows(c,std::move(expected)),"call no-argument live result");
  auto q=F::TransformCallStatement(t,duckpgq_compat::MakeQualifiedName(Identifier("MixedCase")),{});
  auto qref=Parse("CALL \"MixedCase\"()");
  Check(q->ToString()==qref->ToString(),"call quoted name spelling");
 }
 // unknown function and missing argument expression are rejected like the canonical path
 int bad=0;
 {
  auto actual=F::TransformCallStatement(t,duckpgq_compat::MakeQualifiedName(Identifier("no_such_table_function")),{});
  if(Rows(c,std::move(actual)).rfind("ERROR",0)==0)bad++;
  vector<FunctionArgument> args;args.emplace_back(unique_ptr<ParsedExpression>());
  try{F::TransformCallStatement(t,duckpgq_compat::MakeQualifiedName(Identifier("range")),std::move(args));}
  catch(const Exception&){bad++;}
 }
 Check(bad==2,"call rejections");
 std::cout<<"PASS CALL: positional/named/no-argument AST, ownership, live canonical result equality and two rejections\n";
}
#endif
