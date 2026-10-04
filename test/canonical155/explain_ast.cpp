#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/statement/explain_statement.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb; using namespace duckdb::duckpgq_peg; using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static unique_ptr<SQLStatement> Parse(const string&s){Parser p;p.ParseQuery(s);return std::move(p.statements[0]);}
static GenericCopyOption Option(PEGTransformer&t,const string&name,const string&value){
 optional<unique_ptr<ParsedExpression>> e;
 e=make_uniq<ColumnRefExpression>(value);
 return F::TransformExplainOption(t,Identifier(name),std::move(e));
}
void TestExplain(PEGTransformer&t){
 DuckDB db(nullptr); Connection c(db);
 Check(F::TransformExplainAnalyze(t),"analyze marker");
 int executed=0,canonical_failures=0;
 for(auto format:{"default","text","JSON","html","graphviz","yaml","mermaid"}){
  for(bool analyze:{false,true}){
   vector<GenericCopyOption> opts;opts.push_back(Option(t,"FORMAT",format));
   optional<bool> flag;if(analyze)flag=true;
   auto actual=F::TransformExplainStatement(t,flag,F::TransformExplainOptionList(t,opts),Parse("SELECT 73"));
   auto expected=Parse("EXPLAIN (FORMAT '"+string(format)+"'"+(analyze?", ANALYZE":"")+") SELECT 73");
   auto &a=actual->Cast<ExplainStatement>();auto &b=expected->Cast<ExplainStatement>();
   Check(a.explain_type==b.explain_type && a.explain_format==b.explain_format &&
     a.stmt->ToString()==b.stmt->ToString(),"explain canonical fields");
   auto copy=actual->Copy();
   Check(copy->Cast<ExplainStatement>().stmt.get()!=a.stmt.get(),"explain independent child copy");
   auto result=c.Query(std::move(actual));
   // Execution is a differential against the canonical parser's statement: some renderer/mode
   // combinations (e.g. ANALYZE with YAML) are unimplemented in 1.5.5 and must fail identically.
   auto reference=c.Query(std::move(expected));
   Check(result->HasError()==reference->HasError(),"explain execution outcome matches canonical");
   if(reference->HasError()){
    Check(result->GetError()==reference->GetError(),"explain execution error matches canonical");
    ++canonical_failures;
   }else{
    Check(result->RowCount()>0 && !result->GetValue(1,0).IsNull(),"explain renderer execution");
    ++executed;
   }
  }
 }
 // FORMAT without an argument is a canonical no-op, even following a valued FORMAT.
 vector<GenericCopyOption> opts;opts.push_back(Option(t,"format","json"));
 opts.push_back(F::TransformExplainOption(t,Identifier("format"),{}));
 auto actual=F::TransformExplainStatement(t,{},opts,Parse("SELECT 1"));
 auto expected=Parse("EXPLAIN (FORMAT json, FORMAT) SELECT 1");
 Check(actual->Cast<ExplainStatement>().explain_format==expected->Cast<ExplainStatement>().explain_format,"bare format no-op");
 auto plain=F::TransformExplainStatement(t,{},{},Parse("SELECT 1"));
 Check(plain->Cast<ExplainStatement>().explain_format==ExplainFormat::DEFAULT,"default explain format");
 for(int mode=0;mode<4;++mode){
  vector<GenericCopyOption> invalid;string sql;
  if(mode==0){invalid.push_back(Option(t,"format","nonsense"));sql="EXPLAIN (FORMAT nonsense) SELECT 1";}
  if(mode==1){invalid.push_back(Option(t,"format","json"));invalid.push_back(Option(t,"format","text"));sql="EXPLAIN (FORMAT json, FORMAT text) SELECT 1";}
  if(mode==2){invalid.push_back(Option(t,"unknown","text"));sql="EXPLAIN (unknown text) SELECT 1";}
  if(mode==3){
   optional<unique_ptr<ParsedExpression>> e;e=make_uniq<ConstantExpression>(Value(42));
   invalid.push_back(F::TransformExplainOption(t,Identifier("format"),std::move(e)));
   sql="EXPLAIN (FORMAT 42) SELECT 1";
  }
  bool rejected=false,reference=false;ExceptionType code=ExceptionType::INVALID,ref=ExceptionType::INVALID;
  try{F::TransformExplainStatement(t,{},invalid,Parse("SELECT 1"));}catch(const Exception&e){rejected=true;code=ErrorData(e).Type();}
  try{Parse(sql);}catch(const Exception&e){reference=true;ref=ErrorData(e).Type();}
  Check(rejected && reference && code==ref,"explain canonical error class");
 }
 Check(executed>0,"explain at least one live execution");
 std::cout<<"PASS EXPLAIN: seven formats, ANALYZE, canonical fields/copy, "<<executed<<" live renderer executions plus "<<canonical_failures<<" identical canonical failures, bare FORMAT, four rejections\n";
}
#endif
