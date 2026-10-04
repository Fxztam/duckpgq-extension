#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/compat/name_metadata.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/tableref/showref.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;using namespace duckdb::duckpgq_peg;using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
void TestDescribe(PEGTransformer&t){
 DuckDB db(nullptr);Connection c(db);
 Check(!c.Query("CREATE SCHEMA \"s.dot\"; CREATE TABLE \"s.dot\".\"Mixed\"(x INTEGER); INSERT INTO \"s.dot\".\"Mixed\" VALUES (1),(2)")->HasError(),"describe fixture");
 for(bool summary:{false,true}){
   auto base=make_uniq<BaseTableRef>();base->schema_name="s.dot";base->table_name="Mixed";
   auto node=F::TransformShowQualifiedName(t,summary?ShowType::SUMMARY:ShowType::DESCRIBE,F::TransformDescribeBaseTableName(t,std::move(base)));
   auto r=c.Query(F::TransformDescribeStatement(t,std::move(node)));
   auto ref=c.Query(string(summary?"SUMMARIZE ":"DESCRIBE ")+"\"s.dot\".\"Mixed\"");
   Check(!r->HasError() && !ref->HasError() && r->RowCount()==ref->RowCount() && r->ColumnCount()==ref->ColumnCount(),"describe dimensions");
   for(idx_t row=0;row<r->RowCount();row++)for(idx_t col=0;col<r->ColumnCount();col++){
     auto a=r->GetValue(col,row),b=ref->GetValue(col,row);
     Check(a.IsNull()||b.IsNull()?a.IsNull()&&b.IsNull():a==b,"describe result differential");
   }
 }
 Check(!c.Query("CREATE TABLE simple(x INTEGER)")->HasError(),"simple fixture");
 auto r=c.Query(F::TransformDescribeStatement(t,F::TransformShowQualifiedName(t,ShowType::DESCRIBE,F::TransformDescribeStringLiteral(t,"simple"))));
 Check(!r->HasError() && r->GetValue(0,0)==Value("x"),"string target execution");
 r=c.Query(F::TransformDescribeStatement(t,F::TransformShowTables(t,ShowType::DESCRIBE,duckpgq_compat::MakeQualifiedName(Identifier("s.dot")))));
 Check(!r->HasError() && r->RowCount()==1 && r->GetValue(0,0)==Value("Mixed"),"SHOW TABLES schema");
 Parser p;p.ParseQuery("SELECT 73 AS answer");auto select=unique_ptr_cast<SQLStatement,SelectStatement>(std::move(p.statements[0]));
 auto *identity=select->node.get();auto node=F::TransformShowSelect(t,ShowType::DESCRIBE,std::move(select));
 Check(node->Cast<SelectNode>().from_table->Cast<ShowRef>().query.get()==identity,"describe query move");
 r=c.Query(F::TransformDescribeStatement(t,std::move(node)));
 Check(!r->HasError() && r->GetValue(0,0)==Value("answer"),"DESCRIBE SELECT");
 r=c.Query(F::TransformDescribeStatement(t,F::TransformShowAllTables(t,ShowType::DESCRIBE)));
 Check(!r->HasError() && r->RowCount()==2,"SHOW ALL TABLES");
 int bad=0;try{F::TransformShowQualifiedName(t,ShowType::SUMMARY,{});}catch(const ParserException&){bad++;}
 try{F::TransformShowTables(t,ShowType::DESCRIBE,duckpgq_compat::MakeQualifiedName({Identifier("cat"),Identifier("schema")},Identifier("table")));}catch(const ParserException&){bad++;}
 Check(bad==2,"describe negatives");
 std::cout<<"PASS DESCRIBE: live qualified/string/query targets, summary differential, ownership, table lists and two rejections\n";
}
#endif
