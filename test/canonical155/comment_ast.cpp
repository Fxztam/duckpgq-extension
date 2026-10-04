#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/alter_statement.hpp"
#include "duckdb/parser/parsed_data/comment_on_column_info.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;using namespace duckdb::duckpgq_peg;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static string Quote(const string&s){string out="\"";for(char c:s){out+=c;if(c=='"')out+=c;}return out+"\"";}
void TestComments(PEGTransformer&t){
 DuckDB db(nullptr);Connection c(db);
 Check(!c.Query("CREATE TABLE target(x INTEGER)")->HasError(),"comment fixture");
 for(bool column:{false,true})for(bool clear:{false,true}){
   vector<string> names={"target"};if(column)names.push_back("x");
   Value value=clear?Value():Value("owner's note");
   auto actual=PEGTransformerFactory::TransformCommentStatement(t,column?CatalogType::INVALID:CatalogType::TABLE_ENTRY,names,value);
   string ref=string("COMMENT ON ")+(column?"COLUMN target.x":"TABLE target")+" IS "+(clear?"NULL":"'owner''s note'");
   Parser p;p.ParseQuery(ref);Check(actual->ToString()==p.statements[0]->ToString(),"comment canonical AST text");
   if(column){
     auto &info=actual->Cast<AlterStatement>().info->Cast<SetColumnCommentInfo>();
     Check(info.name=="target" && info.column_name=="x","column COMMENT target identity");
   }
   // Canonical 1.5.5 column-comment ToString omits column_name; execute the real AST.
   auto executed=c.Query(std::move(actual));
   if(executed->HasError())throw std::runtime_error("COMMENT AST execution: "+executed->GetError());
   auto result=c.Query(column?"SELECT comment FROM duckdb_columns() WHERE table_name='target' AND column_name='x'":"SELECT comment FROM duckdb_tables() WHERE table_name='target'");
   Check(!result->HasError(),"comment metadata query");
   auto stored=result->GetValue(0,0);Check(clear?stored.IsNull():stored==value,"comment metadata value");
 }
 for(const vector<string> names:{vector<string>{"Mixed Name"},vector<string>{"s.dot","unicode_ö"},vector<string>{"cat","s","quote\"name"}}){
   auto actual=PEGTransformerFactory::TransformCommentStatement(t,CatalogType::TABLE_ENTRY,names,Value("text"));
   string ref="COMMENT ON TABLE ";for(idx_t i=0;i<names.size();i++){if(i)ref+=".";ref+=Quote(names[i]);}ref+=" IS 'text'";
   Parser p;p.ParseQuery(ref);Check(actual->ToString()==p.statements[0]->ToString(),"qualified COMMENT spelling");
 }
 for(CatalogType type:{CatalogType::SCHEMA_ENTRY,CatalogType::DATABASE_ENTRY}){
   bool bad=false;try{PEGTransformerFactory::TransformCommentStatement(t,type,{"x"},Value("x"));}catch(const NotImplementedException&){bad=true;}
   Check(bad,"unsupported comment target accepted");
 }
 bool bad=false;try{PEGTransformerFactory::TransformCommentStatement(t,CatalogType::INVALID,{"x"},Value("x"));}catch(const ParserException&){bad=true;}
 Check(bad,"unqualified column comment accepted");
 std::cout<<"PASS COMMENT: table/column set-clear live metadata, qualified spelling and unsupported/invalid targets\n";
}
#endif
