#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/ast/analyze_target.hpp"
#include "duckpgq/compat/expression_access.hpp"
#include "duckdb/parser/statement/vacuum_statement.hpp"
#include "duckdb/parser/parsed_data/vacuum_info.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb; using namespace duckdb::duckpgq_peg; using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static unique_ptr<SQLStatement> Parse(const string&s){Parser p;p.ParseQuery(s);return std::move(p.statements[0]);}
static void SameVacuum(const SQLStatement&a,const SQLStatement&b,const string&m){
 Check(a.type==StatementType::VACUUM_STATEMENT&&b.type==a.type,(m+": statement type").c_str());
 auto &x=*a.Cast<VacuumStatement>().info;auto &y=*b.Cast<VacuumStatement>().info;
 if(!(x.options.vacuum==y.options.vacuum && x.options.analyze==y.options.analyze && x.has_table==y.has_table && x.columns==y.columns && (x.ref!=nullptr)==(y.ref!=nullptr)))std::cerr<<m<<" vacuum "<<x.options.vacuum<<"/"<<y.options.vacuum<<" analyze "<<x.options.analyze<<"/"<<y.options.analyze<<" has_table "<<x.has_table<<"/"<<y.has_table<<" columns "<<x.columns.size()<<"/"<<y.columns.size()<<std::endl;
 Check(x.options.vacuum==y.options.vacuum && x.options.analyze==y.options.analyze && x.has_table==y.has_table &&
       x.columns==y.columns && (x.ref!=nullptr)==(y.ref!=nullptr),(m+": fields").c_str());
 if(x.ref)Check(x.ref->ToString()==y.ref->ToString(),(m+": table ref").c_str());
 if(a.ToString()!=b.ToString())std::cerr<<m<<" ToString: pgq "<<a.ToString()<<" | canonical "<<b.ToString()<<std::endl;
 Check(a.ToString()==b.ToString(),(m+": ToString").c_str());
}
static optional<AnalyzeTarget> Target(PEGTransformer&t,const string&table,optional<vector<string>> columns={}){
 return F::TransformAnalyzeTarget(t,duckpgq_compat::TableFromNames({Identifier(table)}),columns);
}
void TestVacuum(PEGTransformer&t){
 DuckDB db(nullptr);Connection c(db);
 Check(F::TransformOptAnalyze(t)=="analyze"&&F::TransformOptFull(t)=="full"&&F::TransformOptFreeze(t)=="freeze"&&F::TransformOptVerbose(t)=="verbose","option markers");
 Check(F::TransformNameList(t,{Identifier("a"),Identifier("B")})==vector<string>({"a","B"}),"name list");
 // legacy options and table targets
 SameVacuum(*F::TransformVacuumStatement(t,{},{}),*Parse("VACUUM"),"vacuum");
 SameVacuum(*F::TransformVacuumStatement(t,{},Target(t,"tbl")),*Parse("VACUUM tbl"),"vacuum table");
 SameVacuum(*F::TransformVacuumStatement(t,F::TransformVacuumLegacyOptions(t,{},{},{},string("analyze")),{}),*Parse("VACUUM ANALYZE"),"vacuum analyze");
 SameVacuum(*F::TransformVacuumStatement(t,F::TransformVacuumLegacyOptions(t,{},{},{},string("analyze")),Target(t,"tbl")),*Parse("VACUUM ANALYZE tbl"),"vacuum analyze table");
 SameVacuum(*F::TransformVacuumStatement(t,F::TransformVacuumLegacyOptions(t,{},{},{},string("analyze")),Target(t,"tbl",vector<string>{"a","b"})),
            *Parse("VACUUM ANALYZE tbl(a, b)"),"vacuum analyze columns");
 SameVacuum(*F::TransformVacuumStatement(t,F::TransformVacuumParensOptions(t,{"analyze"}),Target(t,"tbl")),*Parse("VACUUM (ANALYZE) tbl"),"vacuum parens analyze");
 SameVacuum(*F::TransformVacuumStatement(t,F::TransformVacuumParensOptions(t,{"ANALYZE"}),{}),*Parse("VACUUM (analyze)"),"vacuum parens case");
 auto st=F::TransformVacuumStatement(t,{},Target(t,"tbl",vector<string>{"a"}));
 auto copy=st->Copy();
 Check(copy->ToString()==st->ToString()&&copy->Cast<VacuumStatement>().info.get()!=st->Cast<VacuumStatement>().info.get(),"vacuum independent copy");
 // live: both statements execute identically on a table
 Check(!c.Query("CREATE TABLE tbl(a INT, b INT)")->HasError()&&!c.Query("INSERT INTO tbl VALUES (1,2),(3,4)")->HasError(),"vacuum fixture");
 for(int mode=0;mode<3;++mode){
  unique_ptr<SQLStatement> pgq;string sql;
  if(mode==0){pgq=F::TransformVacuumStatement(t,{},Target(t,"tbl"));sql="VACUUM tbl";}
  if(mode==1){pgq=F::TransformVacuumStatement(t,F::TransformVacuumLegacyOptions(t,{},{},{},string("analyze")),Target(t,"tbl",vector<string>{"a"}));sql="VACUUM ANALYZE tbl(a)";}
  if(mode==2){pgq=F::TransformVacuumStatement(t,F::TransformVacuumParensOptions(t,{"analyze"}),{});sql="VACUUM (ANALYZE)";}
  auto r1=c.Query(std::move(pgq));auto r2=c.Query(sql);
  Check(r1->HasError()==r2->HasError(),"vacuum execution outcome");
  if(r1->HasError())Check(r1->GetError()==r2->GetError(),"vacuum execution error");
  Check(!r1->HasError(),"vacuum executes");
 }
 auto data=c.Query("SELECT sum(a),sum(b) FROM tbl");Check(!data->HasError()&&data->GetValue(0,0)==Value::HUGEINT(4)&&data->GetValue(1,0)==Value::HUGEINT(6),"vacuum keeps data");
 // unsupported options are rejected explicitly, like the canonical parser
 int rejected=0,canonical=0;
 for(const char*opt:{"full","freeze","verbose"}){
  try{F::TransformVacuumLegacyOptions(t,opt==string("full")?optional<string>("full"):optional<string>(),
      opt==string("freeze")?optional<string>("freeze"):optional<string>(),
      opt==string("verbose")?optional<string>("verbose"):optional<string>(),{});}catch(const NotImplementedException&){rejected++;}
  try{F::TransformVacuumParensOptions(t,{opt});}catch(const NotImplementedException&){rejected++;}
  try{Parse(string("VACUUM ")+opt);}catch(const Exception&){canonical++;}
 }
 try{F::TransformVacuumParensOptions(t,{"disable_page_skipping"});}catch(const NotImplementedException&){rejected++;}
 Check(rejected==7,"vacuum rejections");
 std::cout<<"PASS VACUUM: seven statement shapes against the canonical parser, name list/markers, copy, three live executions, seven rejections ("<<canonical<<" of 3 legacy spellings also rejected by the canonical parser)"<<std::endl;
}
#endif
