#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/parsed_data/create_secret_info.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;using namespace duckdb::duckpgq_peg;using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static void TestSecretSemanticParity(PEGTransformer &t) {
 int failures=0;
 auto expect=[&](bool ok,const char *message) {
   if(!ok){std::cerr<<"SECRET PARITY: "<<message<<"\n";failures++;}
 };
 for(bool named:{false,true}) {
   bool canonical=false,pgq=false;
   try {Parser p;p.ParseQuery(named?"CREATE SECRET named (PROVIDER config)":"CREATE SECRET (PROVIDER config)");}
   catch(const ParserException&){canonical=true;}
   vector<GenericCopyOption> options;options.emplace_back("provider",Value("config"));
   optional<Identifier> name;if(named)name=Identifier("named");
   try {F::TransformCreateSecretStmt(t,{},name,{},options);}catch(const ParserException&){pgq=true;}
   expect(canonical && pgq,"missing TYPE must be rejected for named and unnamed secrets");
 }
 for(bool identifiers:{false,true}) for(const string name:{"MiXeD","UPPER","lower"}) {
   vector<GenericCopyOption> options;
   auto add=[&](const char *key,const char *value) {
     GenericCopyOption o;o.name=Identifier(key);
     if(identifiers)o.expression=make_uniq<ColumnRefExpression>(value);
     else o.children.push_back(Value(value));
     options.push_back(std::move(o));
   };
   add("TYPE","http");add("PROVIDER","config");add("BEARER_TOKEN","synthetic_marker");
   Parser parser;
   string args=identifiers?"TYPE http, PROVIDER config, BEARER_TOKEN synthetic_marker":
                           "TYPE 'http', PROVIDER 'config', BEARER_TOKEN 'synthetic_marker'";
   parser.ParseQuery("CREATE SECRET \""+name+"\" IN memory ("+args+")");
   auto actual=F::TransformCreateSecretStmt(t,{},Identifier(name),Identifier("MEMORY"),options);
   auto &a=actual->info->Cast<CreateSecretInfo>();
   auto &b=parser.statements[0]->Cast<CreateStatement>().info->Cast<CreateSecretInfo>();
   expect(a.name==b.name && a.name==StringUtil::Lower(name),"explicit name case normalization");
   expect(a.type->Equals(*b.type) && a.provider->Equals(*b.provider) &&
          a.options.at("bearer_token")->Equals(*b.options.at("bearer_token")),
          "TYPE/PROVIDER/general option expression normalization");
   if(identifiers)expect(options[0].expression->GetExpressionType()==ExpressionType::COLUMN_REF,
                         "normalization must not mutate input");
   a.persist_type=SecretPersistType::TEMPORARY;b.persist_type=SecretPersistType::TEMPORARY;
   // Independent databases, explicit CONFIG provider; no env lookup/network/persistence.
   DuckDB db_a(nullptr),db_b(nullptr);Connection ca(db_a),cb(db_b);
   auto ra=ca.Query(std::move(actual));auto rb=cb.Query(std::move(parser.statements[0]));
   expect(!ra->HasError() && !rb->HasError(),"canonical and PGQ temporary secret execution");
   string query="SELECT name, type, provider, persistent, storage FROM duckdb_secrets()";
   auto ma=ca.Query(query),mb=cb.Query(query);
   expect(!ma->HasError() && !mb->HasError() && ma->RowCount()==1 && mb->RowCount()==1,
          "temporary catalog cardinality");
   if(!ma->HasError() && !mb->HasError() && ma->RowCount()==1 && mb->RowCount()==1)
     for(idx_t col=0;col<5;col++)expect(ma->GetValue(col,0)==mb->GetValue(col,0),"catalog differential");
 }
 Check(failures==0,"SECRET semantic differential failures");
 std::cout<<"PASS SECRET semantic parity: named/unnamed TYPE rejection, six name/option AST and live catalog comparisons\n";
}
void TestSecret(PEGTransformer&t){
 TestSecretSemanticParity(t);
 vector<GenericCopyOption> opts;
 opts.emplace_back("TYPE",Value("http"));opts.emplace_back("PROVIDER",Value("config"));
 opts.emplace_back("SCOPE",Value("https://example.invalid/"));
 opts.emplace_back("BEARER_TOKEN",Value("synthetic-test-only"));
 auto actual=F::TransformCreateSecretStmt(t,{},Identifier("pgq_test"),Identifier("MEMORY"),opts);
 auto &info=actual->info->Cast<CreateSecretInfo>();
 Check(info.name=="pgq_test" && info.storage_type=="memory" && info.options.count("bearer_token")==1,"secret name/storage/options");
 Parser parser;parser.ParseQuery("CREATE SECRET pgq_test IN memory (TYPE 'http', PROVIDER 'config', SCOPE 'https://example.invalid/', BEARER_TOKEN 'synthetic-test-only')");
 auto &ref=parser.statements[0]->Cast<CreateStatement>().info->Cast<CreateSecretInfo>();
 Check(info.type->Equals(*ref.type) && info.provider->Equals(*ref.provider) &&
       info.scope->Equals(*ref.scope) && info.options.at("bearer_token")->Equals(*ref.options.at("bearer_token")),
       "secret canonical expression parity");
 auto copied=info.Copy();auto &ci=copied->Cast<CreateSecretInfo>();
 Check(ci.type.get()!=info.type.get() && ci.type->Equals(*info.type),"secret independent copy");
 // Force temporary persistence and memory storage; never access environment credentials or network.
 info.persist_type=SecretPersistType::TEMPORARY;
 DuckDB db(nullptr);Connection c(db);
 auto r=c.Query(std::move(actual));if(r->HasError())throw std::runtime_error("synthetic temporary secret creation failed");
 r=c.Query("SELECT count(*) FROM duckdb_secrets() WHERE name='pgq_test' AND persistent=false AND type='http' AND provider='config'");
 Check(!r->HasError() && r->GetValue(0,0).GetValue<int64_t>()==1,"temporary secret catalog");
 auto repeat=F::TransformCreateSecretStmt(t,true,Identifier("pgq_test"),Identifier("memory"),opts);
 repeat->info->Cast<CreateSecretInfo>().persist_type=SecretPersistType::TEMPORARY;
 Check(!c.Query(std::move(repeat))->HasError(),"secret IF NOT EXISTS");
 auto def=F::TransformCreateSecretStmt(t,{},{},{},opts);
 Check(def->info->Cast<CreateSecretInfo>().name=="__default_http","secret default name");
 GenericCopyOption expression;expression.name=Identifier("type");expression.expression=make_uniq<ColumnRefExpression>("HTTP");
 opts.clear();opts.push_back(expression);
 def=F::TransformCreateSecretStmt(t,{},{},{},opts);
 Check(def->info->Cast<CreateSecretInfo>().name=="__default_http" &&
       def->info->Cast<CreateSecretInfo>().type.get()!=opts[0].expression.get(),"identifier default and option copy");
 int bad=0;
 try{F::TransformCreateSecretStmt(t,{},{},{},{});}catch(const ParserException&){bad++;}
 opts.emplace_back("token",Value("fake"));opts.emplace_back("TOKEN",Value("fake"));
 try{F::TransformCreateSecretStmt(t,{},{},{},opts);}catch(const BinderException&){bad++;}
 opts.clear();expression.expression=make_uniq<FunctionExpression>("lower",vector<unique_ptr<ParsedExpression>>{});
 opts.push_back(expression);
 try{F::TransformCreateSecretStmt(t,{},{},{},opts);}catch(const InvalidInputException&){bad++;}
 Check(bad==3,"secret missing type/duplicate/nonconstant rejection");
 std::cout<<"PASS CREATE SECRET: synthetic memory-only execution, canonical expressions, naming/copies and three rejections\n";
}
#endif
