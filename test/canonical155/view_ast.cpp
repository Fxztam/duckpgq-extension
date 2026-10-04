#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/compat/name_metadata.hpp"
#include "duckdb/parser/parsed_data/create_view_info.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;using namespace duckdb::duckpgq_peg;using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static unique_ptr<SelectStatement> Query(const string&s){Parser p;p.ParseQuery(s);return unique_ptr_cast<SQLStatement,SelectStatement>(std::move(p.statements[0]));}
void TestView(PEGTransformer&t){
 DuckDB db(nullptr);Connection c(db);Check(!c.Query("CREATE SCHEMA \"s.dot\"")->HasError(),"view schema");
 auto name=duckpgq_compat::MakeQualifiedName({Identifier("s.dot")},Identifier("View_ö"));
 auto query=Query("SELECT 73 AS original");auto *identity=query.get();
 auto view=F::TransformCreateViewStmt(t,{},{},name,vector<string>{"Value Name"},{},std::move(query));
 auto &info=view->info->Cast<CreateViewInfo>();
 Check(info.query.get()==identity && info.schema=="s.dot" && info.view_name=="View_ö","view names/query move");
 Parser ref;ref.ParseQuery("CREATE VIEW \"s.dot\".\"View_ö\"(\"Value Name\") AS SELECT 73 AS original");
 Check(view->ToString()==ref.statements[0]->ToString(),"view canonical AST");
 auto r=c.Query(std::move(view));if(r->HasError())throw std::runtime_error(r->GetError());
 r=c.Query("SELECT \"Value Name\" FROM \"s.dot\".\"View_ö\"");
 Check(!r->HasError() && r->GetValue(0,0)==Value(73),"view runtime alias");
 auto repeat=F::TransformCreateViewStmt(t,{},true,name,vector<string>{"Value Name"},{},Query("SELECT 99"));
 Check(!c.Query(std::move(repeat))->HasError(),"view IF NOT EXISTS");
 r=c.Query("SELECT * FROM \"s.dot\".\"View_ö\"");Check(!r->HasError() && r->GetValue(0,0)==Value(73),"view conflict preserves query");
 auto recursive=F::TransformCreateViewStmt(t,true,{},duckpgq_compat::MakeQualifiedName(Identifier("counter_view")),
   vector<string>{"n"},{},Query("SELECT 1 UNION ALL SELECT n+1 FROM counter_view WHERE n<3"));
 r=c.Query(std::move(recursive));if(r->HasError())throw std::runtime_error(r->GetError());
 r=c.Query("SELECT sum(n) FROM counter_view");Check(!r->HasError() && r->GetValue(0,0).GetValue<int64_t>()==6,"recursive view execution");
 for(bool enabled:{false,true}) {
   case_insensitive_map_t<unique_ptr<ParsedExpression>> options;options["defer_binding"]=make_uniq<ConstantExpression>(Value(enabled));
   bool rejected=false;
   try{F::TransformCreateViewStmt(t,{},{},name,{},std::move(options),Query("SELECT 1"));}catch(const NotImplementedException&){rejected=true;}
   Check(rejected,"canonical host cannot defer view binding");
 }
 bool rejected=false;try{Parser p;p.ParseQuery("CREATE VIEW v WITH (defer_binding=true) AS SELECT 1");}catch(const NotImplementedException&){rejected=true;}
 Check(rejected,"canonical VIEW options oracle");
 std::cout<<"PASS CREATE VIEW: canonical AST/names/aliases, query move, live normal/recursive/conflict and unsupported options\n";
}
#endif
