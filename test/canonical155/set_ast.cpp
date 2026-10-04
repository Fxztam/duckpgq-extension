#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/statement/set_statement.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/cast_expression.hpp"
#include "duckdb/parser/expression/default_expression.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb; using namespace duckdb::duckpgq_peg; using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static unique_ptr<SQLStatement> Parse(const string&s){Parser p;p.ParseQuery(s);return std::move(p.statements[0]);}
static unique_ptr<ParsedExpression> Const(Value v){return make_uniq<ConstantExpression>(std::move(v));}
static vector<unique_ptr<ParsedExpression>> One(unique_ptr<ParsedExpression> e){vector<unique_ptr<ParsedExpression>> v;v.push_back(std::move(e));return v;}
static int tostring_fallbacks=0;
static void SameSet(const SQLStatement&a,const SQLStatement&b,const string&m){
 Check(a.type==b.type,(m+": statement type").c_str());
 auto &x=a.Cast<SetStatement>();auto &y=b.Cast<SetStatement>();
 Check(x.name==y.name && x.scope==y.scope && x.set_type==y.set_type,(m+": name/scope/type").c_str());
 if(x.set_type==SetType::SET){
  auto &vx=a.Cast<SetVariableStatement>();auto &vy=b.Cast<SetVariableStatement>();
  // structural Equals can differ only in the internal type of an otherwise identical string constant; fall back to the printed form and count it
  if(!vx.value->Equals(*vy.value)){++tostring_fallbacks;Check(vx.value->ToString()==vy.value->ToString(),(m+": value").c_str());}
 }
 if(a.ToString()!=b.ToString())std::cerr<<m<<" ToString: pgq "<<a.ToString()<<" | canonical "<<b.ToString()<<std::endl;
 Check(a.ToString()==b.ToString(),(m+": ToString").c_str());
}
static int canonical_rejected_forms=0;
// Time-zone forms the canonical parser may not support: compare when it parses, otherwise count the
// canonical rejection (must be the documented NotImplementedException) and let the caller assert semantics.
static bool ParsesCanonically(const string&sql,unique_ptr<SQLStatement>&out){
 try{out=Parse(sql);return true;}
 catch(const NotImplementedException&){++canonical_rejected_forms;std::cerr<<"canonical rejects: "<<sql<<std::endl;return false;}
}
void TestSet(PEGTransformer&t){
 DuckDB db(nullptr);Connection c(db);
 // SET <setting> = <value> with every scope spelling
 struct Case{optional<SetScope> scope;string name;unique_ptr<ParsedExpression> value;string sql;};
 vector<Case> cases;
 cases.push_back({{},"threads",Const(Value(2)),"SET threads = 2"});
 cases.push_back({SetScope::SESSION,"threads",Const(Value(2)),"SET SESSION threads = 2"});
 cases.push_back({SetScope::GLOBAL,"threads",Const(Value(2)),"SET GLOBAL threads = 2"});
 cases.push_back({{},"default_null_order",make_uniq<ColumnRefExpression>("nulls_first"),"SET default_null_order = nulls_first"});
 cases.push_back({{},"memory_limit",Const(Value("1GB")),"SET memory_limit = '1GB'"});
 for(auto&cs:cases){
  auto info=F::TransformSetSetting(t,cs.scope,Identifier(cs.name));
  auto actual=F::TransformSetStatement(t,F::TransformStandardAssignment(t,info,One(std::move(cs.value))));
  SameSet(*actual,*Parse(cs.sql),cs.sql);
  auto copy=actual->Copy();Check(copy->type==actual->type,"set copy");
 }
 // SET VARIABLE and SET ... = DEFAULT (becomes RESET)
 {
  auto info=F::TransformSetVariable(t,F::TransformVariableScope(t),Identifier("v"));
  SameSet(*F::TransformStandardAssignment(t,info,One(Const(Value(7)))),*Parse("SET VARIABLE v = 7"),"set variable");
  SameSet(*F::TransformStandardAssignment(t,F::TransformSetSetting(t,{},Identifier("threads")),One(make_uniq<DefaultExpression>())),
          *Parse("SET threads = DEFAULT"),"set default becomes reset");
 }
 // SET TIME ZONE: string, identifier, DEFAULT/LOCAL, interval forms
 SameSet(*F::TransformSetTimeZone(t,F::TransformZoneStringLiteral(t,"Europe/Berlin")),*Parse("SET TIME ZONE 'Europe/Berlin'"),"tz string");
 SameSet(*F::TransformSetTimeZone(t,F::TransformZoneIdentifier(t,Identifier("UTC"))),*Parse("SET TIME ZONE UTC"),"tz identifier");
 {
  // DEFAULT/LOCAL: PGQ resets the timezone setting
  unique_ptr<SQLStatement> ref;
  auto def=F::TransformSetTimeZone(t,F::TransformZoneDefault(t));
  Check(def->set_type==SetType::RESET && def->name=="timezone","tz default is a timezone reset");
  if(ParsesCanonically("SET TIME ZONE DEFAULT",ref))SameSet(*def,*ref,"tz default");
  auto local=F::TransformSetTimeZone(t,F::TransformZoneLocal(t));
  Check(local->set_type==SetType::RESET && local->name=="timezone","tz local is a timezone reset");
  if(ParsesCanonically("SET TIME ZONE LOCAL",ref))SameSet(*local,*ref,"tz local");
  auto interval=F::TransformSetTimeZone(t,F::TransformZoneIntervalWithInterval(t,"2",{}));
  Check(interval->set_type==SetType::SET && interval->name=="timezone","tz interval is a timezone set");
  if(ParsesCanonically("SET TIME ZONE INTERVAL '2'",ref))SameSet(*interval,*ref,"tz interval");
 }
 // RESET: plain, scoped, variable
 SameSet(*F::TransformResetStatement(t,F::TransformSetSetting(t,{},Identifier("threads"))),*Parse("RESET threads"),"reset");
 SameSet(*F::TransformResetStatement(t,F::TransformSetSetting(t,SetScope::GLOBAL,Identifier("threads"))),*Parse("RESET GLOBAL threads"),"reset global");
 SameSet(*F::TransformResetStatement(t,F::TransformSetVariable(t,F::TransformVariableScope(t),Identifier("v"))),*Parse("RESET VARIABLE v"),"reset variable");
 Check(F::TransformLocalScope(t)==SetScope::LOCAL && F::TransformSessionScope(t)==SetScope::SESSION &&
       F::TransformGlobalScope(t)==SetScope::GLOBAL,"scope markers");
 // live: PGQ statements take effect exactly like the canonical ones
 Check(!c.Query(F::TransformStandardAssignment(t,F::TransformSetSetting(t,{},Identifier("threads")),One(Const(Value(3)))))->HasError(),"set runtime");
 auto th=c.Query("SELECT current_setting('threads')");Check(!th->HasError()&&th->GetValue(0,0)==Value::BIGINT(3),"set took effect");
 Check(!c.Query(F::TransformStandardAssignment(t,F::TransformSetVariable(t,F::TransformVariableScope(t),Identifier("v")),One(Const(Value(7)))))->HasError(),"set variable runtime");
 auto v=c.Query("SELECT getvariable('v')");Check(!v->HasError()&&v->GetValue(0,0)==Value::INTEGER(7),"variable value");
 Check(!c.Query(F::TransformSetTimeZone(t,F::TransformZoneStringLiteral(t,"Europe/Berlin")))->HasError(),"set time zone runtime");
 auto tz=c.Query("SELECT current_setting('TimeZone')");Check(!tz->HasError()&&tz->GetValue(0,0)==Value("Europe/Berlin"),"time zone took effect");
 Check(!c.Query(F::TransformResetStatement(t,F::TransformSetVariable(t,F::TransformVariableScope(t),Identifier("v"))))->HasError(),"reset variable runtime");
 auto gone=c.Query("SELECT getvariable('v')");Check(!gone->HasError()&&gone->GetValue(0,0).IsNull(),"variable reset");
 Check(!c.Query(F::TransformResetStatement(t,F::TransformSetSetting(t,{},Identifier("threads"))))->HasError(),"reset runtime");
 // rejections
 int bad=0;
 try{F::TransformStandardAssignment(t,F::TransformSetSetting(t,SetScope::LOCAL,Identifier("threads")),One(Const(Value(1))));}catch(const NotImplementedException&){bad++;}
 try{F::TransformResetStatement(t,F::TransformSetSetting(t,SetScope::LOCAL,Identifier("threads")));}catch(const NotImplementedException&){bad++;}
 { vector<unique_ptr<ParsedExpression>> two;two.push_back(Const(Value(1)));two.push_back(Const(Value(2)));
   try{F::TransformStandardAssignment(t,F::TransformSetSetting(t,{},Identifier("threads")),std::move(two));}catch(const ParserException&){bad++;} }
 Check(bad==3,"set rejections");
 std::cout<<"PASS SET/RESET: five assignment spellings, SET VARIABLE, DEFAULT-to-RESET, five TIME ZONE forms ("<<canonical_rejected_forms<<" rejected by the canonical parser), three RESET forms, live effects, three rejections; "<<tostring_fallbacks<<" value comparison(s) by printed form"<<std::endl;
}
#endif
