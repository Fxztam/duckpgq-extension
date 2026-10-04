#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/ast/extension_repository_info.hpp"
#include "duckpgq/compat/name_metadata.hpp"
#include "duckdb/parser/statement/load_statement.hpp"
#include "duckdb/parser/statement/update_extensions_statement.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb; using namespace duckdb::duckpgq_peg; using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static unique_ptr<SQLStatement> Parse(const string&s){Parser p;p.ParseQuery(s);return std::move(p.statements[0]);}
static void SameLoad(const SQLStatement&a,const SQLStatement&b,const char*m){
 auto &x=*a.Cast<LoadStatement>().info;auto &y=*b.Cast<LoadStatement>().info;
 Check(x.filename==y.filename && x.repository==y.repository && x.repo_is_alias==y.repo_is_alias &&
       x.version==y.version && x.load_type==y.load_type && a.ToString()==b.ToString(),m);
}
void TestLoad(PEGTransformer&t){
 // LOAD: plain, quoted/path, with the unsupported AS alias
 SameLoad(*F::TransformLoadStatement(t,Identifier("httpfs"),{}),*Parse("LOAD httpfs"),"load identifier");
 SameLoad(*F::TransformLoadStatement(t,Identifier("some/dir/my ext.duckdb_extension"),{}),
          *Parse("LOAD 'some/dir/my ext.duckdb_extension'"),"load path");
 SameLoad(*F::TransformLoadStatement(t,Identifier("Mixed"),{}),*Parse("LOAD \"Mixed\""),"load quoted identifier");
 // INSTALL / FORCE INSTALL, repository alias or URL, version; against the canonical parser
 struct Case{bool force;string name;optional<ExtensionRepositoryInfo> repo;optional<string> version;string sql;};
 ExtensionRepositoryInfo alias=F::TransformFromSourceIdentifier(t,Identifier("community"));
 ExtensionRepositoryInfo url=F::TransformFromSourceString(t,"https://example.invalid/repo");
 Check(alias.repository_is_alias && !url.repository_is_alias && url.name.GetIdentifierName()=="https://example.invalid/repo","repository info");
 vector<Case> cases={
  {false,"spatial",{},{},"INSTALL spatial"},
  {true,"spatial",{},{},"FORCE INSTALL spatial"},
  {false,"spatial",alias,{},"INSTALL spatial FROM community"},
  {false,"spatial",url,{},"INSTALL spatial FROM 'https://example.invalid/repo'"},
  {true,"spatial",alias,string("v1.2.3"),"FORCE INSTALL spatial FROM community VERSION 'v1.2.3'"},
  {false,"spatial",{},string("v1.2.3"),"INSTALL spatial VERSION 'v1.2.3'"}};
 for(auto&c:cases){
  auto actual=F::TransformInstallStatement(t,c.force,duckpgq_compat::MakeQualifiedName(Identifier(c.name)),c.repo,c.version);
  SameLoad(*actual,*Parse(c.sql),c.sql.c_str());
  auto copy=actual->Copy();
  Check(copy->Cast<LoadStatement>().info.get()!=actual->Cast<LoadStatement>().info.get(),"install independent copy");
 }
 Check(F::TransformVersionNumber(t,duckpgq_compat::MakeQualifiedName(Identifier("v9")))=="v9","version number");
 // UPDATE EXTENSIONS
 for(int mode=0;mode<3;++mode){
  optional<vector<Identifier>> names;string sql="UPDATE EXTENSIONS";
  if(mode==1){names=vector<Identifier>{Identifier("a")};sql+=" (a)";}
  if(mode==2){names=vector<Identifier>{Identifier("a"),Identifier("b")};sql+=" (a, b)";}
  auto actual=F::TransformUpdateExtensionsStatement(t,names);auto expected=Parse(sql);
  Check(actual->Cast<UpdateExtensionsStatement>().info->extensions_to_update==
        expected->Cast<UpdateExtensionsStatement>().info->extensions_to_update,"update extensions names");
 }
 // live (offline): loading a missing extension file fails identically; INSTALL/UPDATE are never executed
 DuckDB db(nullptr);Connection c(db);
 const string missing=".b/pgqa/does-not-exist/none.duckdb_extension";
 auto r1=c.Query(F::TransformLoadStatement(t,Identifier(missing),{}));
 auto r2=c.Query("LOAD '"+missing+"'");
 Check(r1->HasError()&&r2->HasError()&&r1->GetError()==r2->GetError(),"load missing extension error identical");
 // AS alias does not exist in canonical 1.5.5: explicit rejection, never a silent drop
 int rejected=0;
 try{F::TransformLoadStatement(t,Identifier("httpfs"),Identifier("alias"));}catch(const ParserException&){rejected++;}
 Check(rejected==1,"load AS alias rejected");
 bool canonical_rejects=false;
 try{Parse("LOAD httpfs AS alias");}catch(const Exception&){canonical_rejects=true;}
 Check(canonical_rejects,"canonical parser also rejects LOAD AS");
 std::cout<<"PASS LOAD/INSTALL: three LOAD spellings, six INSTALL forms, UPDATE EXTENSIONS lists, repository info, live missing-extension error, AS alias rejected\n";
}
#endif
