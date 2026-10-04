#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/statement/prepare_statement.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;using namespace duckdb::duckpgq_peg;using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static unique_ptr<SQLStatement> Parse(const string &sql){Parser p;p.ParseQuery(sql);return std::move(p.statements[0]);}
static void Run(Connection &c,unique_ptr<SQLStatement> s){auto r=c.Query(std::move(s));if(r->HasError())throw std::runtime_error(r->GetError());}
void TestPrepare(PEGTransformer &t){
 DuckDB db(nullptr);Connection c(db);Check(!c.Query("CREATE TABLE items(x INTEGER)")->HasError(),"prepare fixture");
 for(bool named:{false,true}){
   string query=named?"SELECT $value::INTEGER+3":"SELECT $1::INTEGER+3";
   string name=named?"Named_ö":"Positional";
   auto child=Parse(query);auto *identity=child.get();
   auto actual=F::TransformPrepareStatement(t,Identifier(name),{},std::move(child));
   auto &a=actual->Cast<PrepareStatement>();Check(a.statement.get()==identity && a.name==name,"prepare name/move");
   auto ref=Parse("PREPARE \""+name+"\" AS "+query);
   Check(actual->ToString()==ref->ToString(),"prepare canonical AST");
   auto copy=actual->Copy();Check(copy->Cast<PrepareStatement>().statement.get()!=identity,"prepare independent copy");
   Run(c,std::move(actual));
   vector<FunctionArgument> args;
   if(named)args.emplace_back(Identifier("value"),make_uniq<ConstantExpression>(Value(70)));
   else args.emplace_back(make_uniq<ConstantExpression>(Value(70)));
   auto result=c.Query(F::TransformExecuteStatement(t,Identifier(name),std::move(args)));
   Check(!result->HasError() && result->GetValue(0,0)==Value(73),"PGQ PREPARE to EXECUTE");
 }
 for(const string sql:{"INSERT INTO items VALUES(7)","UPDATE items SET x=x+1","DELETE FROM items WHERE x=8"}){
   Run(c,F::TransformPrepareStatement(t,Identifier("modify"),{},Parse(sql)));
   Run(c,F::TransformExecuteStatement(t,Identifier("modify"),{}));
 }
 auto rows=c.Query("SELECT count(*) FROM items");Check(!rows->HasError() && rows->GetValue(0,0).GetValue<int64_t>()==0,"prepared DML end state");
 auto copy=F::TransformPrepareStatement(t,Identifier("copy_only"),{},Parse("COPY items TO 'not-executed.csv'"));
 Check(copy->Cast<PrepareStatement>().statement->type==StatementType::COPY_STATEMENT,"COPY accepted without execution");
 int bad=0;
 try{F::TransformPrepareStatement(t,Identifier("invalid"),{},Parse("CREATE TABLE other(x INTEGER)"));}catch(const ParserException&){bad++;}
 try{F::TransformTypeList(t,{LogicalType::INTEGER});}catch(const NotImplementedException&){bad++;}
 Check(bad==2,"prepare invalid statement/type list");
 std::cout<<"PASS PREPARE: canonical AST/name/move/copy, PGQ EXECUTE chain, DML state and negative cases (COPY AST only)\n";
}
#endif
