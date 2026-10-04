#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/statement/pragma_statement.hpp"
#include "duckdb/parser/statement/set_statement.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/comparison_expression.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb; using namespace duckdb::duckpgq_peg; using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static unique_ptr<SQLStatement> Parse(const string&s){Parser p;p.ParseQuery(s);return std::move(p.statements[0]);}
static unique_ptr<ParsedExpression> Const(Value v){return make_uniq<ConstantExpression>(std::move(v));}
static unique_ptr<ParsedExpression> Col(const string&n){return make_uniq<ColumnRefExpression>(n);}
static unique_ptr<ParsedExpression> Col2(const string&a,const string&b){return make_uniq<ColumnRefExpression>(vector<string>{a,b});}
static vector<unique_ptr<ParsedExpression>> One(unique_ptr<ParsedExpression> e){vector<unique_ptr<ParsedExpression>> v;v.push_back(std::move(e));return v;}
static void SamePragma(const SQLStatement&a,const SQLStatement&b,const char*m){
 Check(a.type==b.type,m);
 if(a.type==StatementType::PRAGMA_STATEMENT){
  auto &x=*a.Cast<PragmaStatement>().info;auto &y=*b.Cast<PragmaStatement>().info;
  if(!(x.name==y.name && x.parameters.size()==y.parameters.size() && x.named_parameters.size()==y.named_parameters.size()))std::cerr<<m<<": pgq "<<a.ToString()<<" | canonical "<<b.ToString()<<" params "<<x.parameters.size()<<"/"<<y.parameters.size()<<" named "<<x.named_parameters.size()<<"/"<<y.named_parameters.size()<<std::endl;
  Check(x.name==y.name && x.parameters.size()==y.parameters.size() && x.named_parameters.size()==y.named_parameters.size(),m);
  for(idx_t i=0;i<x.parameters.size();++i)Check(x.parameters[i]->Equals(*y.parameters[i]),m);
  for(auto &n:x.named_parameters){auto it=y.named_parameters.find(n.first);if(it==y.named_parameters.end()||!n.second->Equals(*it->second))std::cerr<<m<<" named value: "<<n.first<<"="<<n.second->ToString()<<" | "<<(it==y.named_parameters.end()?string("<missing>"):it->second->ToString())<<std::endl;Check(it!=y.named_parameters.end()&&n.second->Equals(*it->second),m);}
 }
 if(a.ToString()!=b.ToString())std::cerr<<m<<" ToString: pgq "<<a.ToString()<<" | canonical "<<b.ToString()<<std::endl;
 Check(a.ToString()==b.ToString(),m);
}
void TestPragma(PEGTransformer&t){
 DuckDB db(nullptr);Connection c(db);
 // calls: no parameters, scalar/column-ref/qualified parameters, named parameter
 SamePragma(*F::TransformPragmaFunction(t,Identifier("database_size"),{}),*Parse("PRAGMA database_size"),"pragma no parameters");
 SamePragma(*F::TransformPragmaFunction(t,Identifier("table_info"),One(Const(Value("tbl")))),*Parse("PRAGMA table_info('tbl')"),"pragma string parameter");
 SamePragma(*F::TransformPragmaFunction(t,Identifier("table_info"),One(Col("tbl"))),*Parse("PRAGMA table_info(tbl)"),"pragma column-ref parameter");
 SamePragma(*F::TransformPragmaFunction(t,Identifier("table_info"),One(Col2("s","tbl"))),*Parse("PRAGMA table_info(s.tbl)"),"pragma qualified parameter");
 {
  vector<unique_ptr<ParsedExpression>> params;
  params.push_back(Const(Value(1)));
  params.push_back(make_uniq<ComparisonExpression>(ExpressionType::COMPARE_EQUAL,Col("flag"),Const(Value(5))));
  SamePragma(*F::TransformPragmaFunction(t,Identifier("some_pragma"),std::move(params)),*Parse("PRAGMA some_pragma(1, flag = 5)"),"pragma named parameter");
 }
 // assignment: SQLite-compat pragma stays a call, everything else becomes a SET
 SamePragma(*F::TransformPragmaAssign(t,Identifier("table_info"),One(Const(Value("tbl")))),*Parse("PRAGMA table_info = 'tbl'"),"pragma compat assign");
 SamePragma(*F::TransformPragmaAssign(t,Identifier("threads"),One(Const(Value(2)))),*Parse("PRAGMA threads = 2"),"pragma assign constant");
 SamePragma(*F::TransformPragmaAssign(t,Identifier("default_null_order"),One(Col("nulls_first"))),*Parse("PRAGMA default_null_order = nulls_first"),"pragma assign column-ref");
 SamePragma(*F::TransformPragmaAssign(t,Identifier("memory_limit"),One(Col2("a","b"))),*Parse("PRAGMA memory_limit = a.b"),"pragma assign qualified");
 auto st=F::TransformPragmaStatement(t,F::TransformPragmaFunction(t,Identifier("database_size"),{}));
 Check(st->type==StatementType::PRAGMA_STATEMENT,"pragma statement passthrough");
 // live: assignment takes effect exactly like the canonical statement; a call returns the same result
 Check(!c.Query(F::TransformPragmaAssign(t,Identifier("threads"),One(Const(Value(2)))))->HasError(),"pragma assign runtime");
 auto threads=c.Query("SELECT current_setting('threads')");
 Check(!threads->HasError()&&threads->GetValue(0,0)==Value::BIGINT(2),"pragma assign took effect");
 Check(!c.Query("CREATE TABLE tbl(a INT, b VARCHAR)")->HasError(),"pragma fixture");
 // table_info is a query-pragma: the PragmaHandler expands it only on the string path, so the statement
 // object fails identically for the canonical parser's statement (asserted); the live result uses ToString().
 auto object_pgq=c.Query(F::TransformPragmaFunction(t,Identifier("table_info"),One(Const(Value("tbl")))));
 auto object_ref=c.Query(Parse("PRAGMA table_info('tbl')"));
 Check(object_pgq->HasError()&&object_ref->HasError()&&object_pgq->GetError()==object_ref->GetError(),"pragma object path matches canonical");
 auto call=c.Query(F::TransformPragmaFunction(t,Identifier("table_info"),One(Const(Value("tbl"))))->ToString());
 auto ref=c.Query("PRAGMA table_info('tbl')");
 if(call->HasError()||ref->HasError()||call->ToString()!=ref->ToString())std::cerr<<"pgq: "<<(call->HasError()?call->GetError():call->ToString())<<std::endl<<"ref: "<<(ref->HasError()?ref->GetError():ref->ToString())<<std::endl;
 Check(!call->HasError()&&!ref->HasError()&&call->ToString()==ref->ToString(),"pragma call result");
 // rejections
 int bad=0;
 try{F::TransformPragmaAssign(t,Identifier("threads"),{});}catch(const ParserException&){bad++;}
 { vector<unique_ptr<ParsedExpression>> two;two.push_back(Const(Value(1)));two.push_back(Const(Value(2)));
   try{F::TransformPragmaAssign(t,Identifier("threads"),std::move(two));}catch(const ParserException&){bad++;} }
 { vector<unique_ptr<ParsedExpression>> p;
   p.push_back(make_uniq<ComparisonExpression>(ExpressionType::COMPARE_EQUAL,Const(Value(1)),Const(Value(2))));
   try{F::TransformPragmaFunction(t,Identifier("x"),std::move(p));}catch(const ParserException&){bad++;} }
 Check(bad==3,"pragma rejections");
 std::cout<<"PASS PRAGMA: five call shapes, four assignment shapes, compat pragma, live assignment/result equality, three rejections\n";
}
#endif
