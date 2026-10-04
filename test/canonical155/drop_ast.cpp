#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/compat/name_metadata.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;using namespace duckdb::duckpgq_peg;using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
void TestDrop(PEGTransformer&t){
 DuckDB db(nullptr);Connection c(db);
 Check(!c.Query("CREATE SCHEMA \"s.dot\"; CREATE TABLE \"s.dot\".\"Mixed\"(x INTEGER); CREATE INDEX idx ON \"s.dot\".\"Mixed\"(x)")->HasError(),"DROP fixture");
 auto idx=F::TransformDropIndex(t,{}, {duckpgq_compat::MakeQualifiedName({Identifier("s.dot")},Identifier("idx"))});
 Check(!c.Query(std::move(idx))->HasError(),"DROP INDEX runtime");
 for(bool ignore:{false,true}){
   vector<unique_ptr<BaseTableRef>> names;auto base=make_uniq<BaseTableRef>();base->schema_name="s.dot";base->table_name="Mixed";names.push_back(std::move(base));
   optional<bool> exists;if(ignore)exists=true;
   auto stmt=F::TransformDropTable(t,CatalogType::TABLE_ENTRY,exists,std::move(names));
   Parser p;p.ParseQuery(string("DROP TABLE ")+(ignore?"IF EXISTS ":"")+"\"s.dot\".\"Mixed\"");
   Check(stmt->ToString()==p.statements[0]->ToString(),"DROP table canonical AST");
   Check(!c.Query(std::move(stmt))->HasError(),"DROP TABLE / IF EXISTS runtime");
 }
 Check(c.Query("SELECT * FROM \"s.dot\".\"Mixed\"")->HasError(),"table really dropped");
 Check(!c.Query("ATTACH ':memory:' AS scratch; CREATE SCHEMA scratch.test; CREATE TABLE scratch.test.a(x INTEGER)")->HasError(),"cascade fixture");
 auto q=duckpgq_compat::MakeQualifiedName({Identifier("scratch")},Identifier("test"));
 auto stmt=F::TransformDropSchema(t,{}, {q});
 Check(stmt->info->catalog=="scratch" && stmt->info->schema.empty() && stmt->info->name=="test","DROP SCHEMA catalog mapping");
 auto restrict_stmt=stmt->Copy();
 Check(c.Query(std::move(restrict_stmt))->HasError(),"schema RESTRICT");
 Check(!c.Query(F::TransformDropStatement(t,std::move(stmt),true))->HasError(),"schema CASCADE");
 int bad=0;
 try{F::TransformDropIndex(t,{},{});}catch(const NotImplementedException&){bad++;}
 try{F::TransformDropIndex(t,{}, {q,q});}catch(const NotImplementedException&){bad++;}
 try{F::TransformDropTrigger(t,{},Identifier("tr"),make_uniq<BaseTableRef>());}catch(const ParserException&){bad++;}
 try{F::TransformDropSchema(t,{}, {duckpgq_compat::MakeQualifiedName({Identifier("cat"),Identifier("parent")},Identifier("child"))});}catch(const ParserException&){bad++;}
 Check(bad==4,"DROP invalid targets and unsupported trigger");
 std::cout<<"PASS DROP: index/table/schema live operations, canonical AST, IF EXISTS/CASCADE and four rejections (in-memory only)\n";
}
#endif
