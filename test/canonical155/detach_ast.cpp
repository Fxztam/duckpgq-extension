#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/statement/detach_statement.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb; using namespace duckdb::duckpgq_peg; using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static unique_ptr<SQLStatement> Parse(const string&s){Parser p;p.ParseQuery(s);return std::move(p.statements[0]);}
static string Q(const string&n){string q="\"";for(char ch:n){q+=ch;if(ch=='"')q+='"';}return q+"\"";}
static bool Attach(Connection&c,const string&name){return !c.Query("ATTACH ':memory:' AS "+Q(name))->HasError();}
static bool Attached(Connection&c,const string&name){
 auto r=c.Query("SELECT count(*) FROM duckdb_databases() WHERE database_name = '"+name+"'");
 return !r->HasError() && r->GetValue(0,0)==Value::BIGINT(1);
}
void TestDetach(PEGTransformer&t){
 DuckDB db(nullptr),db_reference(nullptr); Connection c(db),reference(db_reference);
 int executed=0;
 for(bool database_keyword:{false,true})for(bool if_exists:{false,true}){
  const string name=database_keyword?"Mixed ö\"x":"plain";
  optional<bool> flag;if(if_exists)flag=true;
  auto actual=F::TransformDetachStatement(t,database_keyword,flag,Identifier(name));
  // canonical grammar accepts IF EXISTS only together with DATABASE
  string sql="DETACH ";if(database_keyword||if_exists)sql+="DATABASE ";if(if_exists)sql+="IF EXISTS ";
  string quoted="\"";for(char ch:name){quoted+=ch;if(ch=='"')quoted+='"';}quoted+="\"";
  auto expected=Parse(sql+quoted);
  auto &a=actual->Cast<DetachStatement>();auto &b=expected->Cast<DetachStatement>();
  Check(a.info && b.info && a.info->name==b.info->name && a.info->name==name &&
        a.info->if_not_found==b.info->if_not_found &&
        a.info->if_not_found==(if_exists?OnEntryNotFound::RETURN_NULL:OnEntryNotFound::THROW_EXCEPTION),"detach canonical AST fields");
  Check(actual->ToString()==expected->ToString(),"detach canonical ToString");
  auto copy=actual->Copy();
  Check(copy->Cast<DetachStatement>().info.get()!=a.info.get() && copy->Cast<DetachStatement>().info->name==name,"detach independent info copy");
  // live: attached database is removed by both paths, state afterwards identical
  Check(Attach(c,name)&&Attach(reference,name)&&Attached(c,name),"detach attach setup");
  Check(!c.Query(std::move(actual))->HasError() && !reference.Query(std::move(expected))->HasError(),"detach runtime");
  Check(!Attached(c,name),"detach removed database");
  ++executed;
  // second detach of the now-missing database: error vs. no-op must match the canonical statement
  auto again=F::TransformDetachStatement(t,database_keyword,flag,Identifier(name));
  auto again_ref=Parse(sql+quoted);
  auto r1=c.Query(std::move(again));auto r2=reference.Query(std::move(again_ref));
  Check(r1->HasError()==r2->HasError() && r1->HasError()==!if_exists,"detach missing database behavior");
  if(r1->HasError())Check(r1->GetError()==r2->GetError(),"detach missing database error");
 }
 // the system catalog cannot be detached; both paths must fail identically
 auto sys=c.Query(F::TransformDetachStatement(t,true,{},Identifier("system")));
 auto sys_ref=reference.Query("DETACH DATABASE \"system\"");
 Check(sys->HasError() && sys_ref->HasError() && sys->GetError()==sys_ref->GetError(),"detach system rejection");
 std::cout<<"PASS DETACH: DATABASE/IF EXISTS forms, quoted names, canonical AST/copy, "<<executed<<" live detaches, missing-database and system rejections\n";
}
#endif
